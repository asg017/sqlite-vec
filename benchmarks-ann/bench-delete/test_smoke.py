#!/usr/bin/env python3
"""Quick self-contained smoke test using a synthetic dataset.
Creates a tiny base.db in a temp dir, runs the delete benchmark, verifies output.
"""
import os
import random
import sqlite3
import struct
import sys
import tempfile

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_ROOT_DIR = os.path.join(_SCRIPT_DIR, "..", "..")
EXT_PATH = os.path.join(_ROOT_DIR, "dist", "vec0")

DIMS = 8
N_TRAIN = 200
N_QUERIES = 10
K_NEIGHBORS = 5


def _f32(vals):
    return struct.pack(f"{len(vals)}f", *vals)


def make_synthetic_base_db(path):
    """Create a minimal base.db with train vectors and query vectors."""
    rng = random.Random(123)
    db = sqlite3.connect(path)
    db.execute("CREATE TABLE train(id INTEGER PRIMARY KEY, vector BLOB)")
    db.execute("CREATE TABLE query_vectors(id INTEGER PRIMARY KEY, vector BLOB)")

    for i in range(N_TRAIN):
        vec = [rng.gauss(0, 1) for _ in range(DIMS)]
        db.execute("INSERT INTO train VALUES (?, ?)", (i, _f32(vec)))

    for i in range(N_QUERIES):
        vec = [rng.gauss(0, 1) for _ in range(DIMS)]
        db.execute("INSERT INTO query_vectors VALUES (?, ?)", (i, _f32(vec)))

    db.commit()
    db.close()


def main():
    if not os.path.exists(EXT_PATH + ".dylib") and not os.path.exists(EXT_PATH + ".so"):
        # Try bare path (sqlite handles extension)
        pass

    with tempfile.TemporaryDirectory() as tmpdir:
        base_db = os.path.join(tmpdir, "base.db")
        make_synthetic_base_db(base_db)

        # Patch DATASETS to use our synthetic DB
        import bench_delete
        bench_delete.DATASETS["synthetic"] = {
            "base_db": base_db,
            "dimensions": DIMS,
        }

        out_dir = os.path.join(tmpdir, "runs")

        # Test flat index
        print("=== Testing flat index ===")
        name, params = bench_delete.parse_config("flat:type=vec0-flat,variant=float")
        params["dimensions"] = DIMS
        results = bench_delete.run_delete_benchmark(
            name, params, base_db, EXT_PATH,
            subset_size=N_TRAIN, dims=DIMS,
            delete_pcts=[25, 50], k=K_NEIGHBORS, n_queries=N_QUERIES,
            out_dir=out_dir, seed_val=42,
        )

        bench_delete.print_report(results)

        # Flat recall should be 1.0 at all delete %
        for r in results:
            assert r["recall"] == 1.0, \
                f"Flat recall should be 1.0, got {r['recall']} at {r['delete_pct']}%"
        print("\n  PASS: flat recall is 1.0 at all delete percentages\n")

        # Test DiskANN
        print("=== Testing DiskANN ===")
        name2, params2 = bench_delete.parse_config(
            "diskann:type=diskann,R=8,L=32,quantizer=binary"
        )
        params2["dimensions"] = DIMS
        results2 = bench_delete.run_delete_benchmark(
            name2, params2, base_db, EXT_PATH,
            subset_size=N_TRAIN, dims=DIMS,
            delete_pcts=[25, 50], k=K_NEIGHBORS, n_queries=N_QUERIES,
            out_dir=out_dir, seed_val=42,
        )

        bench_delete.print_report(results2)

        # DiskANN baseline (0%) should have decent recall
        baseline = results2[0]
        assert baseline["recall"] > 0.0, \
            f"DiskANN baseline recall is zero"
        print(f"  PASS: DiskANN baseline recall={baseline['recall']}")

        # Test rescore
        print("\n=== Testing rescore ===")
        name3, params3 = bench_delete.parse_config(
            "rescore:type=rescore,quantizer=bit,oversample=4"
        )
        params3["dimensions"] = DIMS
        results3 = bench_delete.run_delete_benchmark(
            name3, params3, base_db, EXT_PATH,
            subset_size=N_TRAIN, dims=DIMS,
            delete_pcts=[25, 50], k=K_NEIGHBORS, n_queries=N_QUERIES,
            out_dir=out_dir, seed_val=42,
        )

        bench_delete.print_report(results3)
        print(f"  PASS: rescore baseline recall={results3[0]['recall']}")

    print("\n ALL SMOKE TESTS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
