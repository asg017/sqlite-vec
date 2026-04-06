#!/usr/bin/env python3
"""Benchmark: measure recall degradation after random row deletion.

Given a dataset and index config, this script:
  1. Builds the index (flat + ANN)
  2. Measures recall at k=10 (pre-delete baseline)
  3. Deletes a random % of rows
  4. Measures recall again (post-delete)
  5. Records DB size before/after deletion, recall delta, timings

Usage:
  python bench_delete.py --subset-size 10000 --delete-pct 25 \
    "diskann-R48:type=diskann,R=48,L=128,quantizer=binary"

  # Multiple delete percentages in one run:
  python bench_delete.py --subset-size 10000 --delete-pct 10,25,50,75 \
    "diskann-R48:type=diskann,R=48,L=128,quantizer=binary"
"""
import argparse
import json
import os
import random
import shutil
import sqlite3
import statistics
import struct
import time

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_BENCH_DIR = os.path.join(_SCRIPT_DIR, "..")
_ROOT_DIR = os.path.join(_BENCH_DIR, "..")

EXT_PATH = os.path.join(_ROOT_DIR, "dist", "vec0")
DATASETS_DIR = os.path.join(_BENCH_DIR, "datasets")

DATASETS = {
    "cohere1m": {"base_db": os.path.join(DATASETS_DIR, "cohere1m", "base.db"), "dimensions": 768},
    "cohere10m": {"base_db": os.path.join(DATASETS_DIR, "cohere10m", "base.db"), "dimensions": 768},
    "nyt": {"base_db": os.path.join(DATASETS_DIR, "nyt", "base.db"), "dimensions": 256},
    "nyt-768": {"base_db": os.path.join(DATASETS_DIR, "nyt-768", "base.db"), "dimensions": 768},
    "nyt-1024": {"base_db": os.path.join(DATASETS_DIR, "nyt-1024", "base.db"), "dimensions": 1024},
    "nyt-384": {"base_db": os.path.join(DATASETS_DIR, "nyt-384", "base.db"), "dimensions": 384},
}

INSERT_BATCH_SIZE = 1000


# ============================================================================
# Timing helpers
# ============================================================================

def now_ns():
    return time.time_ns()

def ns_to_s(ns):
    return ns / 1_000_000_000

def ns_to_ms(ns):
    return ns / 1_000_000


# ============================================================================
# Index registry (subset of bench.py — only types relevant to deletion)
# ============================================================================

def _vec0_flat_create(p):
    dims = p["dimensions"]
    variant = p.get("variant", "float")
    col = f"embedding float[{dims}]"
    if variant == "int8":
        col = f"embedding int8[{dims}]"
    elif variant == "bit":
        col = f"embedding bit[{dims}]"
    return f"CREATE VIRTUAL TABLE vec_items USING vec0(id INTEGER PRIMARY KEY, {col})"

def _rescore_create(p):
    dims = p["dimensions"]
    q = p.get("quantizer", "bit")
    os_val = p.get("oversample", 8)
    return (
        f"CREATE VIRTUAL TABLE vec_items USING vec0("
        f"id INTEGER PRIMARY KEY, "
        f"embedding float[{dims}] indexed by rescore(quantizer={q}, oversample={os_val}))"
    )

def _diskann_create(p):
    dims = p["dimensions"]
    R = p.get("R", 72)
    L = p.get("L", 128)
    q = p.get("quantizer", "binary")
    bt = p.get("buffer_threshold", 0)
    sl_insert = p.get("search_list_size_insert", 0)
    sl_search = p.get("search_list_size_search", 0)
    parts = [
        f"neighbor_quantizer={q}",
        f"n_neighbors={R}",
        f"buffer_threshold={bt}",
    ]
    if sl_insert or sl_search:
        # Per-path overrides — don't also set search_list_size
        if sl_insert:
            parts.append(f"search_list_size_insert={sl_insert}")
        if sl_search:
            parts.append(f"search_list_size_search={sl_search}")
    else:
        parts.append(f"search_list_size={L}")
    opts = ", ".join(parts)
    return (
        f"CREATE VIRTUAL TABLE vec_items USING vec0("
        f"id INTEGER PRIMARY KEY, "
        f"embedding float[{dims}] indexed by diskann({opts}))"
    )

def _ivf_create(p):
    dims = p["dimensions"]
    nlist = p.get("nlist", 128)
    nprobe = p.get("nprobe", 16)
    q = p.get("quantizer", "none")
    os_val = p.get("oversample", 1)
    parts = [f"nlist={nlist}", f"nprobe={nprobe}"]
    if q != "none":
        parts.append(f"quantizer={q}")
    if os_val > 1:
        parts.append(f"oversample={os_val}")
    opts = ", ".join(parts)
    return (
        f"CREATE VIRTUAL TABLE vec_items USING vec0("
        f"id INTEGER PRIMARY KEY, "
        f"embedding float[{dims}] indexed by ivf({opts}))"
    )


INDEX_REGISTRY = {
    "vec0-flat": {
        "defaults": {"variant": "float"},
        "create_table_sql": _vec0_flat_create,
        "post_insert_hook": None,
    },
    "rescore": {
        "defaults": {"quantizer": "bit", "oversample": 8},
        "create_table_sql": _rescore_create,
        "post_insert_hook": None,
    },
    "ivf": {
        "defaults": {"nlist": 128, "nprobe": 16, "quantizer": "none",
                      "oversample": 1},
        "create_table_sql": _ivf_create,
        "post_insert_hook": lambda conn, params: _ivf_train(conn),
    },
    "diskann": {
        "defaults": {"R": 72, "L": 128, "quantizer": "binary",
                      "buffer_threshold": 0},
        "create_table_sql": _diskann_create,
        "post_insert_hook": None,
    },
}


def _ivf_train(conn):
    """Trigger built-in k-means training for IVF."""
    t0 = now_ns()
    conn.execute("INSERT INTO vec_items(vec_items) VALUES ('compute-centroids')")
    conn.commit()
    return ns_to_s(now_ns() - t0)


# ============================================================================
# Config parsing (same format as bench.py)
# ============================================================================

INT_KEYS = {"R", "L", "oversample", "nlist", "nprobe", "buffer_threshold",
            "search_list_size_insert", "search_list_size_search"}

def parse_config(spec):
    if ":" not in spec:
        raise ValueError(f"Config must be 'name:key=val,...': {spec}")
    name, rest = spec.split(":", 1)
    params = {}
    for kv in rest.split(","):
        k, v = kv.split("=", 1)
        k = k.strip()
        v = v.strip()
        if k in INT_KEYS:
            v = int(v)
        params[k] = v
    index_type = params.pop("type", None)
    if not index_type or index_type not in INDEX_REGISTRY:
        raise ValueError(f"Unknown index type: {index_type}")
    params["index_type"] = index_type
    merged = dict(INDEX_REGISTRY[index_type]["defaults"])
    merged.update(params)
    return name, merged


# ============================================================================
# DB helpers
# ============================================================================

def create_bench_db(db_path, ext_path, base_db, page_size=4096):
    if os.path.exists(db_path):
        os.remove(db_path)
    conn = sqlite3.connect(db_path)
    conn.execute(f"PRAGMA page_size={page_size}")
    conn.execute("PRAGMA journal_mode=WAL")
    conn.enable_load_extension(True)
    conn.load_extension(ext_path)
    conn.execute(f"ATTACH DATABASE '{base_db}' AS base")
    return conn


def load_query_vectors(base_db, n):
    conn = sqlite3.connect(base_db)
    rows = conn.execute(
        "SELECT id, vector FROM query_vectors LIMIT ?", (n,)
    ).fetchall()
    conn.close()
    return rows


def insert_loop(conn, subset_size, label, start_from=0):
    insert_sql = (
        "INSERT INTO vec_items(id, embedding) "
        "SELECT id, vector FROM base.train "
        "WHERE id >= :lo AND id < :hi"
    )
    total = 0
    for lo in range(start_from, subset_size, INSERT_BATCH_SIZE):
        hi = min(lo + INSERT_BATCH_SIZE, subset_size)
        conn.execute(insert_sql, {"lo": lo, "hi": hi})
        conn.commit()
        total += hi - lo
        if total % 5000 == 0 or total == subset_size - start_from:
            print(f"    [{label}] inserted {total + start_from}/{subset_size}", flush=True)


# ============================================================================
# Recall measurement
# ============================================================================

def measure_recall(conn, base_db, query_vectors, subset_size, k, alive_ids=None):
    """Measure KNN recall. If alive_ids is provided, ground truth is computed
    only over those IDs (to match post-delete state)."""
    recalls = []
    times_ms = []

    for qid, query in query_vectors:
        t0 = now_ns()
        results = conn.execute(
            "SELECT id, distance FROM vec_items "
            "WHERE embedding MATCH :query AND k = :k",
            {"query": query, "k": k},
        ).fetchall()
        t1 = now_ns()
        times_ms.append(ns_to_ms(t1 - t0))

        result_ids = set(r[0] for r in results)

        # Ground truth: brute-force cosine over surviving rows
        if alive_ids is not None:
            # After deletion — compute GT only over alive IDs
            # Use a temp table for the alive set for efficiency
            gt_rows = conn.execute(
                "SELECT id FROM ("
                "  SELECT id, vec_distance_l2(vector, :query) as dist "
                "  FROM base.train WHERE id < :n ORDER BY dist LIMIT :k2"
                ")",
                {"query": query, "k2": k * 5, "n": subset_size},
            ).fetchall()
            # Filter to only alive IDs, take top k
            gt_alive = [r[0] for r in gt_rows if r[0] in alive_ids][:k]
            gt_ids = set(gt_alive)
        else:
            gt_rows = conn.execute(
                "SELECT id FROM ("
                "  SELECT id, vec_distance_l2(vector, :query) as dist "
                "  FROM base.train WHERE id < :n ORDER BY dist LIMIT :k"
                ")",
                {"query": query, "k": k, "n": subset_size},
            ).fetchall()
            gt_ids = set(r[0] for r in gt_rows)

        if gt_ids:
            recalls.append(len(result_ids & gt_ids) / len(gt_ids))
        else:
            recalls.append(0.0)

    return {
        "recall": round(statistics.mean(recalls), 4) if recalls else 0.0,
        "mean_ms": round(statistics.mean(times_ms), 2) if times_ms else 0.0,
        "median_ms": round(statistics.median(times_ms), 2) if times_ms else 0.0,
    }


# ============================================================================
# Delete benchmark core
# ============================================================================

def run_delete_benchmark(name, params, base_db, ext_path, subset_size, dims,
                         delete_pcts, k, n_queries, out_dir, seed_val):
    params["dimensions"] = dims
    reg = INDEX_REGISTRY[params["index_type"]]
    create_sql = reg["create_table_sql"](params)

    results = []

    # Build once, copy for each delete %
    print(f"\n{'='*60}")
    print(f"Config: {name}  (type={params['index_type']})")
    print(f"{'='*60}")

    os.makedirs(out_dir, exist_ok=True)
    master_db_path = os.path.join(out_dir, f"{name}.{subset_size}.db")
    print(f"  Building index ({subset_size} vectors)...")
    build_t0 = now_ns()
    conn = create_bench_db(master_db_path, ext_path, base_db)
    conn.execute(create_sql)
    insert_loop(conn, subset_size, name)
    hook = reg.get("post_insert_hook")
    if hook:
        print(f"  Training...")
        hook(conn, params)
    conn.close()
    build_time_s = ns_to_s(now_ns() - build_t0)
    master_size = os.path.getsize(master_db_path)
    print(f"  Built in {build_time_s:.1f}s  ({master_size / (1024*1024):.1f} MB)")

    # Load query vectors once
    query_vectors = load_query_vectors(base_db, n_queries)

    # Measure pre-delete baseline on the master copy
    print(f"\n  --- 0% deleted (baseline) ---")
    conn = sqlite3.connect(master_db_path)
    conn.enable_load_extension(True)
    conn.load_extension(ext_path)
    conn.execute(f"ATTACH DATABASE '{base_db}' AS base")
    baseline = measure_recall(conn, base_db, query_vectors, subset_size, k)
    conn.close()
    print(f"  recall={baseline['recall']:.4f}  "
          f"query={baseline['mean_ms']:.2f}ms")

    results.append({
        "name": name,
        "index_type": params["index_type"],
        "subset_size": subset_size,
        "delete_pct": 0,
        "n_deleted": 0,
        "n_remaining": subset_size,
        "recall": baseline["recall"],
        "query_mean_ms": baseline["mean_ms"],
        "query_median_ms": baseline["median_ms"],
        "db_size_mb": round(master_size / (1024 * 1024), 2),
        "build_time_s": round(build_time_s, 1),
        "delete_time_s": 0.0,
        "vacuum_size_mb": round(master_size / (1024 * 1024), 2),
    })

    # All IDs in the dataset
    all_ids = list(range(subset_size))

    for pct in sorted(delete_pcts):
        n_delete = int(subset_size * pct / 100)
        print(f"\n  --- {pct}% deleted ({n_delete} rows) ---")

        # Copy master DB and work on the copy
        copy_path = os.path.join(out_dir, f"{name}.{subset_size}.del{pct}.db")
        shutil.copy2(master_db_path, copy_path)
        # Also copy WAL/SHM if they exist
        for suffix in ["-wal", "-shm"]:
            src = master_db_path + suffix
            if os.path.exists(src):
                shutil.copy2(src, copy_path + suffix)

        conn = sqlite3.connect(copy_path)
        conn.enable_load_extension(True)
        conn.load_extension(ext_path)
        conn.execute(f"ATTACH DATABASE '{base_db}' AS base")

        # Pick random IDs to delete (deterministic per pct)
        rng = random.Random(seed_val + pct)
        to_delete = set(rng.sample(all_ids, n_delete))
        alive_ids = set(all_ids) - to_delete

        # Delete
        delete_t0 = now_ns()
        batch = []
        for i, rid in enumerate(to_delete):
            batch.append(rid)
            if len(batch) >= 500 or i == len(to_delete) - 1:
                placeholders = ",".join("?" for _ in batch)
                conn.execute(
                    f"DELETE FROM vec_items WHERE id IN ({placeholders})",
                    batch,
                )
                conn.commit()
                batch = []
        delete_time_s = ns_to_s(now_ns() - delete_t0)

        remaining = conn.execute("SELECT count(*) FROM vec_items").fetchone()[0]
        pre_vacuum_size = os.path.getsize(copy_path)
        print(f"  deleted {n_delete} rows in {delete_time_s:.2f}s  "
              f"({remaining} remaining)")

        # Measure post-delete recall
        post = measure_recall(conn, base_db, query_vectors, subset_size, k,
                              alive_ids=alive_ids)
        print(f"  recall={post['recall']:.4f}  "
              f"(delta={post['recall'] - baseline['recall']:+.4f})  "
              f"query={post['mean_ms']:.2f}ms")

        # VACUUM and measure size savings — close fully, reopen without base
        conn.close()
        vconn = sqlite3.connect(copy_path)
        vconn.execute("VACUUM")
        vconn.close()
        post_vacuum_size = os.path.getsize(copy_path)
        saved_mb = (pre_vacuum_size - post_vacuum_size) / (1024 * 1024)
        print(f"  size: {pre_vacuum_size/(1024*1024):.1f} MB -> "
              f"{post_vacuum_size/(1024*1024):.1f} MB after VACUUM "
              f"(saved {saved_mb:.1f} MB)")

        results.append({
            "name": name,
            "index_type": params["index_type"],
            "subset_size": subset_size,
            "delete_pct": pct,
            "n_deleted": n_delete,
            "n_remaining": remaining,
            "recall": post["recall"],
            "query_mean_ms": post["mean_ms"],
            "query_median_ms": post["median_ms"],
            "db_size_mb": round(pre_vacuum_size / (1024 * 1024), 2),
            "build_time_s": round(build_time_s, 1),
            "delete_time_s": round(delete_time_s, 2),
            "vacuum_size_mb": round(post_vacuum_size / (1024 * 1024), 2),
        })

    return results


# ============================================================================
# Results DB
# ============================================================================

RESULTS_SCHEMA = """\
CREATE TABLE IF NOT EXISTS delete_runs (
    run_id INTEGER PRIMARY KEY,
    config_name TEXT NOT NULL,
    index_type TEXT NOT NULL,
    params TEXT,
    dataset TEXT NOT NULL,
    subset_size INTEGER NOT NULL,
    delete_pct INTEGER NOT NULL,
    n_deleted INTEGER NOT NULL,
    n_remaining INTEGER NOT NULL,
    k INTEGER NOT NULL,
    n_queries INTEGER NOT NULL,
    seed INTEGER NOT NULL,
    recall REAL,
    query_mean_ms REAL,
    query_median_ms REAL,
    db_size_mb REAL,
    vacuum_size_mb REAL,
    build_time_s REAL,
    delete_time_s REAL,
    created_at TEXT DEFAULT (datetime('now'))
);
"""

def save_results(results, out_dir, dataset, subset_size, params_json, k, n_queries, seed_val):
    db_path = os.path.join(out_dir, "delete_results.db")
    db = sqlite3.connect(db_path)
    db.execute("PRAGMA journal_mode=WAL")
    db.executescript(RESULTS_SCHEMA)
    for r in results:
        db.execute(
            "INSERT INTO delete_runs "
            "(config_name, index_type, params, dataset, subset_size, "
            " delete_pct, n_deleted, n_remaining, k, n_queries, seed, "
            " recall, query_mean_ms, query_median_ms, "
            " db_size_mb, vacuum_size_mb, build_time_s, delete_time_s) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (
                r["name"], r["index_type"], params_json, dataset, r["subset_size"],
                r["delete_pct"], r["n_deleted"], r["n_remaining"], k, n_queries, seed_val,
                r["recall"], r["query_mean_ms"], r["query_median_ms"],
                r["db_size_mb"], r["vacuum_size_mb"], r["build_time_s"], r["delete_time_s"],
            ),
        )
    db.commit()
    db.close()
    return db_path


# ============================================================================
# Reporting
# ============================================================================

def print_report(all_results):
    print(f"\n{'name':>22} {'del%':>5} {'deleted':>8} {'remain':>8} "
          f"{'recall':>7} {'delta':>7} {'qry(ms)':>8} "
          f"{'size(MB)':>9} {'vacuumed':>9} {'del(s)':>7}")
    print("-" * 110)

    # Group by config name
    configs = {}
    for r in all_results:
        configs.setdefault(r["name"], []).append(r)

    for name, rows in configs.items():
        baseline_recall = rows[0]["recall"]  # 0% delete is always first
        for r in rows:
            delta = r["recall"] - baseline_recall
            delta_str = f"{delta:+.4f}" if r["delete_pct"] > 0 else "-"
            print(
                f"{r['name']:>22} {r['delete_pct']:>4}% "
                f"{r['n_deleted']:>8} {r['n_remaining']:>8} "
                f"{r['recall']:>7.4f} {delta_str:>7} {r['query_mean_ms']:>8.2f} "
                f"{r['db_size_mb']:>9.1f} {r['vacuum_size_mb']:>9.1f} "
                f"{r['delete_time_s']:>7.2f}"
            )
        print()


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Benchmark recall degradation after random row deletion",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("configs", nargs="+",
                        help="config specs (name:type=X,key=val,...)")
    parser.add_argument("--subset-size", type=int, default=10000,
                        help="number of vectors to build (default: 10000)")
    parser.add_argument("--delete-pct", type=str, default="10,25,50",
                        help="comma-separated delete percentages (default: 10,25,50)")
    parser.add_argument("-k", type=int, default=10, help="KNN k (default 10)")
    parser.add_argument("-n", type=int, default=50,
                        help="number of queries (default 50)")
    parser.add_argument("--dataset", default="cohere1m",
                        choices=list(DATASETS.keys()))
    parser.add_argument("--ext", default=EXT_PATH)
    parser.add_argument("-o", "--out-dir",
                        default=os.path.join(_SCRIPT_DIR, "runs"))
    parser.add_argument("--seed", type=int, default=42,
                        help="random seed for delete selection (default: 42)")
    args = parser.parse_args()

    ds = DATASETS[args.dataset]
    base_db = ds["base_db"]
    dims = ds["dimensions"]
    if not os.path.exists(base_db):
        print(f"Error: dataset not found at {base_db}")
        print(f"Run: make -C {os.path.dirname(base_db)}")
        return 1

    delete_pcts = [int(x.strip()) for x in args.delete_pct.split(",")]
    for p in delete_pcts:
        if not 0 < p < 100:
            print(f"Error: delete percentage must be 1-99, got {p}")
            return 1

    out_dir = os.path.join(args.out_dir, args.dataset, str(args.subset_size))
    os.makedirs(out_dir, exist_ok=True)

    all_results = []
    for spec in args.configs:
        name, params = parse_config(spec)
        params_json = json.dumps(params)
        results = run_delete_benchmark(
            name, params, base_db, args.ext, args.subset_size, dims,
            delete_pcts, args.k, args.n, out_dir, args.seed,
        )
        all_results.extend(results)

        save_results(results, out_dir, args.dataset, args.subset_size,
                     params_json, args.k, args.n, args.seed)

    print_report(all_results)

    results_path = os.path.join(out_dir, "delete_results.db")
    print(f"\nResults saved to: {results_path}")
    print(f"Query: sqlite3 {results_path} "
          f"\"SELECT config_name, delete_pct, recall, vacuum_size_mb "
          f"FROM delete_runs ORDER BY config_name, delete_pct\"")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
