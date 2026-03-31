#!/usr/bin/env python3
"""Benchmark runner for sqlite-vec KNN configurations.

Measures insert time, build/train time, DB size, KNN latency, and recall
across different vec0 configurations.

Config format: name:type=<index_type>,key=val,key=val

  Baseline (brute-force) keys:
    type=baseline, variant=float|int8|bit, oversample=8

  Index-specific types can be registered via INDEX_REGISTRY (see below).

Usage:
  python bench.py --subset-size 10000 \
    "brute-float:type=baseline,variant=float" \
    "brute-int8:type=baseline,variant=int8" \
    "brute-bit:type=baseline,variant=bit"
"""
import argparse
import os
import sqlite3
import statistics
import time

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EXT_PATH = os.path.join(_SCRIPT_DIR, "..", "dist", "vec0")
BASE_DB = os.path.join(_SCRIPT_DIR, "seed", "base.db")
INSERT_BATCH_SIZE = 1000


# ============================================================================
# Index registry — extension point for ANN index branches
# ============================================================================
#
# Each index type provides a dict with:
#   "defaults":          dict of default params
#   "create_table_sql":  fn(params) -> SQL string
#   "insert_sql":        fn(params) -> SQL string  (or None for default)
#   "post_insert_hook":  fn(conn, params) -> train_time_s  (or None)
#   "run_query":         fn(conn, params, query, k) -> [(id, distance), ...]  (or None for default MATCH)
#   "describe":          fn(params) -> str  (one-line description)
#
# To add a new index type, add an entry here. Example (in your branch):
#
#   INDEX_REGISTRY["diskann"] = {
#       "defaults": {"R": 72, "L": 128, "quantizer": "binary", "buffer_threshold": 0},
#       "create_table_sql": lambda p: f"CREATE VIRTUAL TABLE vec_items USING vec0(...)",
#       "insert_sql": None,
#       "post_insert_hook": None,
#       "run_query": None,
#       "describe": lambda p: f"diskann  q={p['quantizer']}  R={p['R']}  L={p['L']}",
#   }

INDEX_REGISTRY = {}


# ============================================================================
# Baseline implementation
# ============================================================================


def _baseline_create_table_sql(params):
    variant = params["variant"]
    extra = ""
    if variant == "int8":
        extra = ", embedding_int8 int8[768]"
    elif variant == "bit":
        extra = ", embedding_bq bit[768]"
    return (
        f"CREATE VIRTUAL TABLE vec_items USING vec0("
        f"  chunk_size=256,"
        f"  id integer primary key,"
        f"  embedding float[768] distance_metric=cosine"
        f"  {extra})"
    )


def _baseline_insert_sql(params):
    variant = params["variant"]
    if variant == "int8":
        return (
            "INSERT INTO vec_items(id, embedding, embedding_int8) "
            "SELECT id, vector, vec_quantize_int8(vector, 'unit') "
            "FROM base.train WHERE id >= :lo AND id < :hi"
        )
    elif variant == "bit":
        return (
            "INSERT INTO vec_items(id, embedding, embedding_bq) "
            "SELECT id, vector, vec_quantize_binary(vector) "
            "FROM base.train WHERE id >= :lo AND id < :hi"
        )
    return None  # use default


def _baseline_run_query(conn, params, query, k):
    variant = params["variant"]
    oversample = params.get("oversample", 8)

    if variant == "int8":
        return conn.execute(
            "WITH coarse AS ("
            "  SELECT id, embedding FROM vec_items"
            "  WHERE embedding_int8 MATCH vec_quantize_int8(:query, 'unit')"
            "  LIMIT :oversample_k"
            ") "
            "SELECT id, vec_distance_cosine(embedding, :query) as distance "
            "FROM coarse ORDER BY 2 LIMIT :k",
            {"query": query, "k": k, "oversample_k": k * oversample},
        ).fetchall()
    elif variant == "bit":
        return conn.execute(
            "WITH coarse AS ("
            "  SELECT id, embedding FROM vec_items"
            "  WHERE embedding_bq MATCH vec_quantize_binary(:query)"
            "  LIMIT :oversample_k"
            ") "
            "SELECT id, vec_distance_cosine(embedding, :query) as distance "
            "FROM coarse ORDER BY 2 LIMIT :k",
            {"query": query, "k": k, "oversample_k": k * oversample},
        ).fetchall()

    return None  # use default MATCH


def _baseline_describe(params):
    v = params["variant"]
    if v in ("int8", "bit"):
        return f"baseline  {v} (os={params['oversample']})"
    return f"baseline  {v}"


INDEX_REGISTRY["baseline"] = {
    "defaults": {"variant": "float", "oversample": 8},
    "create_table_sql": _baseline_create_table_sql,
    "insert_sql": _baseline_insert_sql,
    "post_insert_hook": None,
    "run_query": _baseline_run_query,
    "describe": _baseline_describe,
}


# ============================================================================
# Rescore implementation
# ============================================================================


def _rescore_create_table_sql(params):
    quantizer = params.get("quantizer", "bit")
    oversample = params.get("oversample", 8)
    return (
        f"CREATE VIRTUAL TABLE vec_items USING vec0("
        f"  chunk_size=256,"
        f"  id integer primary key,"
        f"  embedding float[768] distance_metric=cosine"
        f"  indexed by rescore(quantizer={quantizer}, oversample={oversample}))"
    )


def _rescore_describe(params):
    q = params.get("quantizer", "bit")
    os = params.get("oversample", 8)
    return f"rescore  {q} (os={os})"


INDEX_REGISTRY["rescore"] = {
    "defaults": {"quantizer": "bit", "oversample": 8},
    "create_table_sql": _rescore_create_table_sql,
    "insert_sql": None,
    "post_insert_hook": None,
    "run_query": None,  # default MATCH query works — rescore is automatic
    "describe": _rescore_describe,
}


# ============================================================================
# IVF implementation
# ============================================================================


def _ivf_create_table_sql(params):
    return (
        f"CREATE VIRTUAL TABLE vec_items USING vec0("
        f"  id integer primary key,"
        f"  embedding float[768] distance_metric=cosine"
        f"    indexed by ivf("
        f"      nlist={params['nlist']},"
        f"      nprobe={params['nprobe']}"
        f"    )"
        f")"
    )


def _ivf_post_insert_hook(conn, params):
    print("  Training k-means centroids...", flush=True)
    t0 = time.perf_counter()
    conn.execute("INSERT INTO vec_items(id) VALUES ('compute-centroids')")
    conn.commit()
    elapsed = time.perf_counter() - t0
    print(f"  Training done in {elapsed:.1f}s", flush=True)
    return elapsed


def _ivf_describe(params):
    return f"ivf  nlist={params['nlist']:<4} nprobe={params['nprobe']}"


INDEX_REGISTRY["ivf"] = {
    "defaults": {"nlist": 128, "nprobe": 16},
    "create_table_sql": _ivf_create_table_sql,
    "insert_sql": None,
    "post_insert_hook": _ivf_post_insert_hook,
    "run_query": None,
    "describe": _ivf_describe,
}


# ============================================================================
# Config parsing
# ============================================================================

INT_KEYS = {
    "R", "L", "buffer_threshold", "nlist", "nprobe", "oversample",
    "n_trees", "search_k",
}


def parse_config(spec):
    """Parse 'name:type=baseline,key=val,...' into (name, params_dict)."""
    if ":" in spec:
        name, opts_str = spec.split(":", 1)
    else:
        name, opts_str = spec, ""

    raw = {}
    if opts_str:
        for kv in opts_str.split(","):
            k, v = kv.split("=", 1)
            raw[k.strip()] = v.strip()

    index_type = raw.pop("type", "baseline")
    if index_type not in INDEX_REGISTRY:
        raise ValueError(
            f"Unknown index type: {index_type}. "
            f"Available: {', '.join(sorted(INDEX_REGISTRY.keys()))}"
        )

    reg = INDEX_REGISTRY[index_type]
    params = dict(reg["defaults"])
    for k, v in raw.items():
        if k in INT_KEYS:
            params[k] = int(v)
        else:
            params[k] = v
    params["index_type"] = index_type

    return name, params


# ============================================================================
# Shared helpers
# ============================================================================


def load_query_vectors(base_db_path, n):
    conn = sqlite3.connect(base_db_path)
    rows = conn.execute(
        "SELECT id, vector FROM query_vectors ORDER BY id LIMIT :n", {"n": n}
    ).fetchall()
    conn.close()
    return [(r[0], r[1]) for r in rows]


def insert_loop(conn, sql, subset_size, label=""):
    t0 = time.perf_counter()
    for lo in range(0, subset_size, INSERT_BATCH_SIZE):
        hi = min(lo + INSERT_BATCH_SIZE, subset_size)
        conn.execute(sql, {"lo": lo, "hi": hi})
        conn.commit()
        done = hi
        if done % 5000 == 0 or done == subset_size:
            elapsed = time.perf_counter() - t0
            rate = done / elapsed if elapsed > 0 else 0
            print(
                f"    [{label}] {done:>8}/{subset_size}  "
                f"{elapsed:.1f}s  {rate:.0f} rows/s",
                flush=True,
            )
    return time.perf_counter() - t0


def open_bench_db(db_path, ext_path, base_db):
    if os.path.exists(db_path):
        os.remove(db_path)
    conn = sqlite3.connect(db_path)
    conn.enable_load_extension(True)
    conn.load_extension(ext_path)
    conn.execute("PRAGMA page_size=8192")
    conn.execute(f"ATTACH DATABASE '{base_db}' AS base")
    return conn


DEFAULT_INSERT_SQL = (
    "INSERT INTO vec_items(id, embedding) "
    "SELECT id, vector FROM base.train WHERE id >= :lo AND id < :hi"
)


# ============================================================================
# Build
# ============================================================================


def build_index(base_db, ext_path, name, params, subset_size, out_dir):
    db_path = os.path.join(out_dir, f"{name}.{subset_size}.db")
    conn = open_bench_db(db_path, ext_path, base_db)

    reg = INDEX_REGISTRY[params["index_type"]]

    conn.execute(reg["create_table_sql"](params))

    label = params["index_type"]
    print(f"  Inserting {subset_size} vectors...")

    sql_fn = reg.get("insert_sql")
    sql = sql_fn(params) if sql_fn else None
    if sql is None:
        sql = DEFAULT_INSERT_SQL

    insert_time = insert_loop(conn, sql, subset_size, label)

    train_time = 0.0
    hook = reg.get("post_insert_hook")
    if hook:
        train_time = hook(conn, params)

    row_count = conn.execute("SELECT count(*) FROM vec_items").fetchone()[0]
    conn.close()
    file_size_mb = os.path.getsize(db_path) / (1024 * 1024)

    return {
        "db_path": db_path,
        "insert_time_s": round(insert_time, 3),
        "train_time_s": round(train_time, 3),
        "total_time_s": round(insert_time + train_time, 3),
        "insert_per_vec_ms": round((insert_time / row_count) * 1000, 2)
        if row_count
        else 0,
        "rows": row_count,
        "file_size_mb": round(file_size_mb, 2),
    }


# ============================================================================
# KNN measurement
# ============================================================================


def _default_match_query(conn, query, k):
    return conn.execute(
        "SELECT id, distance FROM vec_items "
        "WHERE embedding MATCH :query AND k = :k",
        {"query": query, "k": k},
    ).fetchall()


def measure_knn(db_path, ext_path, base_db, params, subset_size, k=10, n=50):
    conn = sqlite3.connect(db_path)
    conn.enable_load_extension(True)
    conn.load_extension(ext_path)
    conn.execute(f"ATTACH DATABASE '{base_db}' AS base")

    query_vectors = load_query_vectors(base_db, n)

    reg = INDEX_REGISTRY[params["index_type"]]
    query_fn = reg.get("run_query")

    times_ms = []
    recalls = []
    for qid, query in query_vectors:
        t0 = time.perf_counter()

        results = None
        if query_fn:
            results = query_fn(conn, params, query, k)
        if results is None:
            results = _default_match_query(conn, query, k)

        elapsed_ms = (time.perf_counter() - t0) * 1000
        times_ms.append(elapsed_ms)
        result_ids = set(r[0] for r in results)

        # Ground truth: use pre-computed neighbors table for full dataset,
        # otherwise brute-force over the subset
        if subset_size >= 1000000:
            gt_rows = conn.execute(
                "SELECT CAST(neighbors_id AS INTEGER) FROM base.neighbors "
                "WHERE query_vector_id = :qid AND rank < :k",
                {"qid": qid, "k": k},
            ).fetchall()
        else:
            gt_rows = conn.execute(
                "SELECT id FROM ("
                "  SELECT id, vec_distance_cosine(vector, :query) as dist "
                "  FROM base.train WHERE id < :n ORDER BY dist LIMIT :k"
                ")",
                {"query": query, "k": k, "n": subset_size},
            ).fetchall()
        gt_ids = set(r[0] for r in gt_rows)

        if gt_ids:
            recalls.append(len(result_ids & gt_ids) / len(gt_ids))
        else:
            recalls.append(0.0)

    conn.close()

    return {
        "mean_ms": round(statistics.mean(times_ms), 2),
        "median_ms": round(statistics.median(times_ms), 2),
        "p99_ms": round(sorted(times_ms)[int(len(times_ms) * 0.99)], 2)
        if len(times_ms) > 1
        else round(times_ms[0], 2),
        "total_ms": round(sum(times_ms), 2),
        "recall": round(statistics.mean(recalls), 4),
    }


# ============================================================================
# Results persistence
# ============================================================================


def save_results(results_path, rows):
    db = sqlite3.connect(results_path)
    db.executescript(open(os.path.join(_SCRIPT_DIR, "schema.sql")).read())
    for r in rows:
        db.execute(
            "INSERT OR REPLACE INTO build_results "
            "(config_name, index_type, subset_size, db_path, "
            " insert_time_s, train_time_s, total_time_s, rows, file_size_mb) "
            "VALUES (?,?,?,?,?,?,?,?,?)",
            (
                r["name"], r["index_type"], r["n_vectors"], r["db_path"],
                r["insert_time_s"], r["train_time_s"], r["total_time_s"],
                r["rows"], r["file_size_mb"],
            ),
        )
        db.execute(
            "INSERT OR REPLACE INTO bench_results "
            "(config_name, index_type, subset_size, k, n, "
            " mean_ms, median_ms, p99_ms, total_ms, qps, recall, db_path) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
            (
                r["name"], r["index_type"], r["n_vectors"], r["k"], r["n_queries"],
                r["mean_ms"], r["median_ms"], r["p99_ms"], r["total_ms"],
                round(r["n_queries"] / (r["total_ms"] / 1000), 1)
                if r["total_ms"] > 0 else 0,
                r["recall"], r["db_path"],
            ),
        )
    db.commit()
    db.close()


# ============================================================================
# Reporting
# ============================================================================


def print_report(all_results):
    print(
        f"\n{'name':>20} {'N':>7} {'type':>10} {'config':>28}  "
        f"{'ins(s)':>7} {'train':>6} {'MB':>7} "
        f"{'qry(ms)':>8} {'recall':>7}"
    )
    print("-" * 115)
    for r in all_results:
        train = f"{r['train_time_s']:.1f}" if r["train_time_s"] > 0 else "-"
        print(
            f"{r['name']:>20} {r['n_vectors']:>7} {r['index_type']:>10} "
            f"{r['config_desc']:>28}  "
            f"{r['insert_time_s']:>7.1f} {train:>6} {r['file_size_mb']:>7.1f} "
            f"{r['mean_ms']:>8.2f} {r['recall']:>7.4f}"
        )


# ============================================================================
# Main
# ============================================================================


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark runner for sqlite-vec KNN configurations",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("configs", nargs="+", help="config specs (name:type=X,key=val,...)")
    parser.add_argument("--subset-size", type=int, required=True)
    parser.add_argument("-k", type=int, default=10, help="KNN k (default 10)")
    parser.add_argument("-n", type=int, default=50, help="number of queries (default 50)")
    parser.add_argument("--base-db", default=BASE_DB)
    parser.add_argument("--ext", default=EXT_PATH)
    parser.add_argument("-o", "--out-dir", default="runs")
    parser.add_argument("--results-db", default=None,
                        help="path to results DB (default: <out-dir>/results.db)")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    results_db = args.results_db or os.path.join(args.out_dir, "results.db")
    configs = [parse_config(c) for c in args.configs]

    all_results = []
    for i, (name, params) in enumerate(configs, 1):
        reg = INDEX_REGISTRY[params["index_type"]]
        desc = reg["describe"](params)
        print(f"\n[{i}/{len(configs)}] {name}  ({desc.strip()})")

        build = build_index(
            args.base_db, args.ext, name, params, args.subset_size, args.out_dir
        )
        train_str = f" + {build['train_time_s']}s train" if build["train_time_s"] > 0 else ""
        print(
            f"  Build: {build['insert_time_s']}s insert{train_str}  "
            f"{build['file_size_mb']} MB"
        )

        print(f"  Measuring KNN (k={args.k}, n={args.n})...")
        knn = measure_knn(
            build["db_path"], args.ext, args.base_db,
            params, args.subset_size, k=args.k, n=args.n,
        )
        print(f"  KNN: mean={knn['mean_ms']}ms  recall@{args.k}={knn['recall']}")

        all_results.append({
            "name": name,
            "n_vectors": args.subset_size,
            "index_type": params["index_type"],
            "config_desc": desc,
            "db_path": build["db_path"],
            "insert_time_s": build["insert_time_s"],
            "train_time_s": build["train_time_s"],
            "total_time_s": build["total_time_s"],
            "insert_per_vec_ms": build["insert_per_vec_ms"],
            "rows": build["rows"],
            "file_size_mb": build["file_size_mb"],
            "k": args.k,
            "n_queries": args.n,
            "mean_ms": knn["mean_ms"],
            "median_ms": knn["median_ms"],
            "p99_ms": knn["p99_ms"],
            "total_ms": knn["total_ms"],
            "recall": knn["recall"],
        })

    print_report(all_results)
    save_results(results_db, all_results)
    print(f"\nResults saved to {results_db}")


if __name__ == "__main__":
    main()
