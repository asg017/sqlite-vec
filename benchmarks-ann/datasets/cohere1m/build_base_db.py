#!/usr/bin/env python3
"""Build base.db from downloaded parquet files.

Reads train.parquet, test.parquet, neighbors.parquet and creates a SQLite
database with tables: train, query_vectors, neighbors.

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


def float_list_to_blob(floats):
    """Pack a list of floats into a little-endian f32 blob."""
    return struct.pack(f"<{len(floats)}f", *floats)


def main():
    seed_dir = os.path.dirname(os.path.abspath(__file__))
    db_path = os.path.join(seed_dir, "base.db")

    train_path = os.path.join(seed_dir, "train.parquet")
    test_path = os.path.join(seed_dir, "test.parquet")
    neighbors_path = os.path.join(seed_dir, "neighbors.parquet")

    for p in (train_path, test_path, neighbors_path):
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
        # neighbors_id may be a numpy array or JSON string
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

    # --- train (from train.parquet) ---
    print("Loading train.parquet (1M vectors, this takes a few minutes)...")
    t0 = time.perf_counter()
    conn.execute(
        "CREATE TABLE train (id INTEGER PRIMARY KEY, vector BLOB)"
    )

    batch_size = 10000
    df_iter = pd.read_parquet(train_path)
    total = len(df_iter)

    for start in range(0, total, batch_size):
        chunk = df_iter.iloc[start : start + batch_size]
        rows = []
        for _, row in chunk.iterrows():
            rows.append((int(row["id"]), float_list_to_blob(row["emb"])))
        conn.executemany("INSERT INTO train (id, vector) VALUES (?, ?)", rows)
        conn.commit()

        done = min(start + batch_size, total)
        elapsed = time.perf_counter() - t0
        rate = done / elapsed if elapsed > 0 else 0
        eta = (total - done) / rate if rate > 0 else 0
        print(
            f"    {done:>8}/{total}  {elapsed:.0f}s  {rate:.0f} rows/s  eta {eta:.0f}s",
            flush=True,
        )

    elapsed = time.perf_counter() - t0
    print(f"  {total} train vectors in {elapsed:.1f}s")

    conn.close()
    size_mb = os.path.getsize(db_path) / (1024 * 1024)
    print(f"\nDone: {db_path} ({size_mb:.0f} MB)")


if __name__ == "__main__":
    main()
