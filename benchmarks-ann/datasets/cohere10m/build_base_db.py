#!/usr/bin/env python3
"""Build base.db from downloaded parquet files (10M dataset, 10 train shards).

Reads train-00-of-10.parquet .. train-09-of-10.parquet, test.parquet,
neighbors.parquet and creates a SQLite database with tables:
  train, query_vectors, neighbors.

Usage:
  uv run --with pandas --with pyarrow python build_base_db.py
"""
import json
import os
import sqlite3
import struct
import sys
import time

import pandas as pd

TRAIN_SHARDS = 10


def float_list_to_blob(floats):
    """Pack a list of floats into a little-endian f32 blob."""
    return struct.pack(f"<{len(floats)}f", *floats)


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    db_path = os.path.join(script_dir, "base.db")

    train_paths = [
        os.path.join(script_dir, f"train-{i:02d}-of-{TRAIN_SHARDS}.parquet")
        for i in range(TRAIN_SHARDS)
    ]
    test_path = os.path.join(script_dir, "test.parquet")
    neighbors_path = os.path.join(script_dir, "neighbors.parquet")

    for p in train_paths + [test_path, neighbors_path]:
        if not os.path.exists(p):
            print(f"ERROR: {p} not found. Run 'make download' first.")
            sys.exit(1)

    if os.path.exists(db_path):
        os.remove(db_path)

    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA page_size=4096")

    # --- query_vectors (from test.parquet) ---
    print("Loading test.parquet (query vectors)...")
    t0 = time.perf_counter()
    df_test = pd.read_parquet(test_path)
    conn.execute(
        "CREATE TABLE query_vectors (id INTEGER PRIMARY KEY, vector BLOB)"
    )
    rows = []
    for _, row in df_test.iterrows():
        rows.append((int(row["id"]), float_list_to_blob(row["emb"])))
    conn.executemany("INSERT INTO query_vectors (id, vector) VALUES (?, ?)", rows)
    conn.commit()
    print(f"  {len(rows)} query vectors in {time.perf_counter() - t0:.1f}s")

    # --- neighbors (from neighbors.parquet) ---
    print("Loading neighbors.parquet...")
    t0 = time.perf_counter()
    df_neighbors = pd.read_parquet(neighbors_path)
    conn.execute(
        "CREATE TABLE neighbors ("
        "  query_vector_id INTEGER, rank INTEGER, neighbors_id TEXT,"
        "  UNIQUE(query_vector_id, rank))"
    )
    rows = []
    for _, row in df_neighbors.iterrows():
        qid = int(row["id"])
        nids = row["neighbors_id"]
        if isinstance(nids, str):
            nids = json.loads(nids)
        for rank, nid in enumerate(nids):
            rows.append((qid, rank, str(int(nid))))
    conn.executemany(
        "INSERT INTO neighbors (query_vector_id, rank, neighbors_id) VALUES (?, ?, ?)",
        rows,
    )
    conn.commit()
    print(f"  {len(rows)} neighbor rows in {time.perf_counter() - t0:.1f}s")

    # --- train (from 10 shard parquets) ---
    print(f"Loading {TRAIN_SHARDS} train shards (10M vectors, this will take a while)...")
    conn.execute(
        "CREATE TABLE train (id INTEGER PRIMARY KEY, vector BLOB)"
    )

    global_t0 = time.perf_counter()
    total_inserted = 0
    batch_size = 10000

    for shard_idx, train_path in enumerate(train_paths):
        print(f"  Shard {shard_idx + 1}/{TRAIN_SHARDS}: {os.path.basename(train_path)}")
        t0 = time.perf_counter()
        df = pd.read_parquet(train_path)
        shard_len = len(df)

        for start in range(0, shard_len, batch_size):
            chunk = df.iloc[start : start + batch_size]
            rows = []
            for _, row in chunk.iterrows():
                rows.append((int(row["id"]), float_list_to_blob(row["emb"])))
            conn.executemany("INSERT INTO train (id, vector) VALUES (?, ?)", rows)
            conn.commit()

            total_inserted += len(rows)
            if total_inserted % 100000 < batch_size:
                elapsed = time.perf_counter() - global_t0
                rate = total_inserted / elapsed if elapsed > 0 else 0
                print(
                    f"    {total_inserted:>10}  {elapsed:.0f}s  {rate:.0f} rows/s",
                    flush=True,
                )

        shard_elapsed = time.perf_counter() - t0
        print(f"    shard done: {shard_len} rows in {shard_elapsed:.1f}s")

    elapsed = time.perf_counter() - global_t0
    print(f"  {total_inserted} train vectors in {elapsed:.1f}s")

    conn.close()
    size_mb = os.path.getsize(db_path) / (1024 * 1024)
    print(f"\nDone: {db_path} ({size_mb:.0f} MB)")


if __name__ == "__main__":
    main()
