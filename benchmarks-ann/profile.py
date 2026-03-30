#!/usr/bin/env python3
"""CPU profiling for sqlite-vec KNN configurations using macOS `sample` tool.

Builds dist/sqlite3 (with -g3), generates a SQL workload (inserts + repeated
KNN queries) for each config, profiles the sqlite3 process with `sample`, and
prints the top-N hottest functions by self (exclusive) CPU samples.

Usage:
  cd benchmarks-ann
  uv run profile.py --subset-size 50000 -n 50 \\
    "baseline-int8:type=baseline,variant=int8,oversample=8" \\
    "rescore-int8:type=rescore,quantizer=int8,oversample=8"
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.join(_SCRIPT_DIR, "..")

sys.path.insert(0, _SCRIPT_DIR)
from bench import (
    BASE_DB,
    DEFAULT_INSERT_SQL,
    INDEX_REGISTRY,
    INSERT_BATCH_SIZE,
    parse_config,
)

SQLITE3_PATH = os.path.join(_PROJECT_ROOT, "dist", "sqlite3")
EXT_PATH = os.path.join(_PROJECT_ROOT, "dist", "vec0")


# ============================================================================
# SQL generation
# ============================================================================


def _query_sql_for_config(params, query_id, k):
    """Return a SQL query string for a single KNN query by query_vector id."""
    index_type = params["index_type"]
    qvec = f"(SELECT vector FROM base.query_vectors WHERE id = {query_id})"

    if index_type == "baseline":
        variant = params.get("variant", "float")
        oversample = params.get("oversample", 8)
        oversample_k = k * oversample

        if variant == "int8":
            return (
                f"WITH coarse AS ("
                f"  SELECT id, embedding FROM vec_items"
                f"  WHERE embedding_int8 MATCH vec_quantize_int8({qvec}, 'unit')"
                f"  LIMIT {oversample_k}"
                f") "
                f"SELECT id, vec_distance_cosine(embedding, {qvec}) as distance "
                f"FROM coarse ORDER BY 2 LIMIT {k};"
            )
        elif variant == "bit":
            return (
                f"WITH coarse AS ("
                f"  SELECT id, embedding FROM vec_items"
                f"  WHERE embedding_bq MATCH vec_quantize_binary({qvec})"
                f"  LIMIT {oversample_k}"
                f") "
                f"SELECT id, vec_distance_cosine(embedding, {qvec}) as distance "
                f"FROM coarse ORDER BY 2 LIMIT {k};"
            )

    # Default MATCH query (baseline-float, rescore, and others)
    return (
        f"SELECT id, distance FROM vec_items"
        f" WHERE embedding MATCH {qvec} AND k = {k};"
    )


def generate_sql(db_path, params, subset_size, n_queries, k, repeats):
    """Generate a complete SQL workload: load ext, create table, insert, query."""
    lines = []
    lines.append(".bail on")
    lines.append(f".load {EXT_PATH}")
    lines.append(f"ATTACH DATABASE '{os.path.abspath(BASE_DB)}' AS base;")
    lines.append("PRAGMA page_size=8192;")

    # Create table
    reg = INDEX_REGISTRY[params["index_type"]]
    lines.append(reg["create_table_sql"](params) + ";")

    # Inserts
    sql_fn = reg.get("insert_sql")
    insert_sql = sql_fn(params) if sql_fn else None
    if insert_sql is None:
        insert_sql = DEFAULT_INSERT_SQL
    for lo in range(0, subset_size, INSERT_BATCH_SIZE):
        hi = min(lo + INSERT_BATCH_SIZE, subset_size)
        stmt = insert_sql.replace(":lo", str(lo)).replace(":hi", str(hi))
        lines.append(stmt + ";")
        if hi % 10000 == 0 or hi == subset_size:
            lines.append("-- progress: inserted %d/%d" % (hi, subset_size))

    # Queries (repeated)
    lines.append("-- BEGIN QUERIES")
    for _rep in range(repeats):
        for qid in range(n_queries):
            lines.append(_query_sql_for_config(params, qid, k))

    return "\n".join(lines)


# ============================================================================
# Profiling with macOS `sample`
# ============================================================================


def run_profile(sqlite3_path, db_path, sql_file, sample_output, duration=120):
    """Run sqlite3 under macOS `sample` profiler.

    Starts sqlite3 directly with stdin from the SQL file, then immediately
    attaches `sample` to its PID with -mayDie (tolerates process exit).
    The workload must be long enough for sample to attach and capture useful data.
    """
    sql_fd = open(sql_file, "r")
    proc = subprocess.Popen(
        [sqlite3_path, db_path],
        stdin=sql_fd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )

    pid = proc.pid
    print(f"    sqlite3 PID: {pid}")

    # Attach sample immediately (1ms interval, -mayDie tolerates process exit)
    sample_proc = subprocess.Popen(
        ["sample", str(pid), str(duration), "1", "-mayDie", "-file", sample_output],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )

    # Wait for sqlite3 to finish
    _, stderr = proc.communicate()
    sql_fd.close()
    rc = proc.returncode
    if rc != 0:
        print(f"    sqlite3 failed (rc={rc}):", file=sys.stderr)
        print(f"    {stderr.decode().strip()}", file=sys.stderr)
        sample_proc.kill()
        return False

    # Wait for sample to finish
    sample_proc.wait()
    return True


# ============================================================================
# Parse `sample` output
# ============================================================================

# Tree-drawing characters used by macOS `sample` to represent hierarchy.
# We replace them with spaces so indentation depth reflects tree depth.
_TREE_CHARS_RE = re.compile(r"[+!:|]")

# After tree chars are replaced with spaces, each call-graph line looks like:
#   "          800 rescore_knn  (in vec0.dylib) + 3808,3640,...  [0x1a,0x2b,...]  file.c:123"
# We extract just (indent, count, symbol, module) — everything after "(in ...)"
# is decoration we don't need.
_LEADING_RE = re.compile(r"^(\s+)(\d+)\s+(.+)")


def _extract_symbol_and_module(rest):
    """Given the text after 'count ', extract (symbol, module).

    Handles patterns like:
      'rescore_knn  (in vec0.dylib) + 3808,3640,...  [0x...]'
      'pread  (in libsystem_kernel.dylib) + 8  [0x...]'
      '???  (in <unknown binary>)  [0x...]'
      'start  (in dyld) + 2840  [0x198650274]'
      'Thread_26759239   DispatchQueue_1: ...'
    """
    # Try to find "(in ...)" to split symbol from module
    m = re.match(r"^(.+?)\s+\(in\s+(.+?)\)", rest)
    if m:
        return m.group(1).strip(), m.group(2).strip()
    # No module — return whole thing as symbol, strip trailing junk
    sym = re.sub(r"\s+\[0x[0-9a-f].*", "", rest).strip()
    return sym, ""


def _parse_call_graph_lines(text):
    """Parse call-graph section into list of (depth, count, symbol, module)."""
    entries = []
    for raw_line in text.split("\n"):
        # Strip tree-drawing characters, replace with spaces to preserve depth
        line = _TREE_CHARS_RE.sub(" ", raw_line)
        m = _LEADING_RE.match(line)
        if not m:
            continue
        depth = len(m.group(1))
        count = int(m.group(2))
        rest = m.group(3)
        symbol, module = _extract_symbol_and_module(rest)
        entries.append((depth, count, symbol, module))
    return entries


def parse_sample_output(filepath):
    """Parse `sample` call-graph output, compute exclusive (self) samples per function.

    Returns dict of {display_name: self_sample_count}.
    """
    with open(filepath, "r") as f:
        text = f.read()

    # Find "Call graph:" section
    cg_start = text.find("Call graph:")
    if cg_start == -1:
        print("    Warning: no 'Call graph:' section found in sample output")
        return {}

    # End at "Total number in stack" or EOF
    cg_end = text.find("\nTotal number in stack", cg_start)
    if cg_end == -1:
        cg_end = len(text)

    entries = _parse_call_graph_lines(text[cg_start:cg_end])

    if not entries:
        print("    Warning: no call graph entries parsed")
        return {}

    # Compute self (exclusive) samples per function:
    #   self = count - sum(direct_children_counts)
    self_samples = {}
    for i, (depth, count, sym, mod) in enumerate(entries):
        children_sum = 0
        child_depth = None
        for j in range(i + 1, len(entries)):
            j_depth = entries[j][0]
            if j_depth <= depth:
                break
            if child_depth is None:
                child_depth = j_depth
            if j_depth == child_depth:
                children_sum += entries[j][1]

        self_count = count - children_sum
        if self_count > 0:
            key = f"{sym}  ({mod})" if mod else sym
            self_samples[key] = self_samples.get(key, 0) + self_count

    return self_samples


# ============================================================================
# Display
# ============================================================================


def print_profile(title, self_samples, top_n=20):
    total = sum(self_samples.values())
    if total == 0:
        print(f"\n=== {title} (no samples) ===")
        return

    sorted_syms = sorted(self_samples.items(), key=lambda x: -x[1])

    print(f"\n=== {title} (top {top_n}, {total} total self-samples) ===")
    for sym, count in sorted_syms[:top_n]:
        pct = 100.0 * count / total
        print(f"  {pct:5.1f}%  {count:>6}  {sym}")


# ============================================================================
# Main
# ============================================================================


def main():
    parser = argparse.ArgumentParser(
        description="CPU profiling for sqlite-vec KNN configurations",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "configs", nargs="+", help="config specs (name:type=X,key=val,...)"
    )
    parser.add_argument("--subset-size", type=int, required=True)
    parser.add_argument("-k", type=int, default=10, help="KNN k (default 10)")
    parser.add_argument(
        "-n", type=int, default=50, help="number of distinct queries (default 50)"
    )
    parser.add_argument(
        "--repeats",
        type=int,
        default=10,
        help="repeat query set N times for more samples (default 10)",
    )
    parser.add_argument(
        "--top", type=int, default=20, help="show top N functions (default 20)"
    )
    parser.add_argument("--base-db", default=BASE_DB)
    parser.add_argument("--sqlite3", default=SQLITE3_PATH)
    parser.add_argument(
        "--keep-temp",
        action="store_true",
        help="keep temp directory with DBs, SQL, and sample output",
    )
    args = parser.parse_args()

    # Check prerequisites
    if not os.path.exists(args.base_db):
        print(f"Error: base DB not found at {args.base_db}", file=sys.stderr)
        print("Run 'make seed' in benchmarks-ann/ first.", file=sys.stderr)
        sys.exit(1)

    if not shutil.which("sample"):
        print("Error: macOS 'sample' tool not found.", file=sys.stderr)
        sys.exit(1)

    # Build CLI
    print("Building dist/sqlite3...")
    result = subprocess.run(
        ["make", "cli"], cwd=_PROJECT_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"Error: make cli failed:\n{result.stderr}", file=sys.stderr)
        sys.exit(1)
    print("  done.")

    if not os.path.exists(args.sqlite3):
        print(f"Error: sqlite3 not found at {args.sqlite3}", file=sys.stderr)
        sys.exit(1)

    configs = [parse_config(c) for c in args.configs]

    tmpdir = tempfile.mkdtemp(prefix="sqlite-vec-profile-")
    print(f"Working directory: {tmpdir}")

    all_profiles = []

    for i, (name, params) in enumerate(configs, 1):
        reg = INDEX_REGISTRY[params["index_type"]]
        desc = reg["describe"](params)
        print(f"\n[{i}/{len(configs)}] {name}  ({desc})")

        # Generate SQL workload
        db_path = os.path.join(tmpdir, f"{name}.db")
        sql_text = generate_sql(
            db_path, params, args.subset_size, args.n, args.k, args.repeats
        )
        sql_file = os.path.join(tmpdir, f"{name}.sql")
        with open(sql_file, "w") as f:
            f.write(sql_text)

        total_queries = args.n * args.repeats
        print(
            f"  SQL workload: {args.subset_size} inserts + "
            f"{total_queries} queries ({args.n} x {args.repeats} repeats)"
        )

        # Profile
        sample_file = os.path.join(tmpdir, f"{name}.sample.txt")
        print(f"  Profiling...")
        ok = run_profile(args.sqlite3, db_path, sql_file, sample_file)
        if not ok:
            print(f"  FAILED — skipping {name}")
            all_profiles.append((name, desc, {}))
            continue

        if not os.path.exists(sample_file):
            print(f"  Warning: sample output not created")
            all_profiles.append((name, desc, {}))
            continue

        # Parse
        self_samples = parse_sample_output(sample_file)
        all_profiles.append((name, desc, self_samples))

        # Show individual profile
        print_profile(f"{name} ({desc})", self_samples, args.top)

    # Side-by-side comparison if multiple configs
    if len(all_profiles) > 1:
        print("\n" + "=" * 80)
        print("COMPARISON")
        print("=" * 80)

        # Collect all symbols that appear in top-N of any config
        all_syms = set()
        for _name, _desc, prof in all_profiles:
            sorted_syms = sorted(prof.items(), key=lambda x: -x[1])
            for sym, _count in sorted_syms[: args.top]:
                all_syms.add(sym)

        # Build comparison table
        rows = []
        for sym in all_syms:
            row = [sym]
            for _name, _desc, prof in all_profiles:
                total = sum(prof.values())
                count = prof.get(sym, 0)
                pct = 100.0 * count / total if total > 0 else 0.0
                row.append((pct, count))
            max_pct = max(r[0] for r in row[1:])
            rows.append((max_pct, row))

        rows.sort(key=lambda x: -x[0])

        # Header
        header = f"{'function':>40}"
        for name, desc, _ in all_profiles:
            header += f"  {name:>14}"
        print(header)
        print("-" * len(header))

        for _sort_key, row in rows[: args.top * 2]:
            sym = row[0]
            display_sym = sym if len(sym) <= 40 else sym[:37] + "..."
            line = f"{display_sym:>40}"
            for pct, count in row[1:]:
                if count > 0:
                    line += f"  {pct:>13.1f}%"
                else:
                    line += f"  {'-':>14}"
            print(line)

    if args.keep_temp:
        print(f"\nTemp files kept at: {tmpdir}")
    else:
        shutil.rmtree(tmpdir)
        print(f"\nTemp files cleaned up. Use --keep-temp to preserve.")


if __name__ == "__main__":
    main()
