#!/usr/bin/env python3
"""Compute k-means centroids using FAISS and save to a centroids DB.

Reads the first N vectors from a base.db, runs FAISS k-means, and writes
the centroids to an output SQLite DB as float32 blobs.

Usage:
  python faiss_kmeans.py --base-db datasets/cohere10m/base.db --ntrain 100000 \
    --nclusters 8192 -o centroids.db

Output schema:
  CREATE TABLE centroids (
    centroid_id INTEGER PRIMARY KEY,
    centroid BLOB NOT NULL          -- float32[D]
  );
  CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);
    -- ntrain, nclusters, dimensions, elapsed_s
"""
import argparse
import os
import sqlite3
import struct
import time

import faiss
import numpy as np


def main():
    parser = argparse.ArgumentParser(description="FAISS k-means centroid computation")
    parser.add_argument("--base-db", required=True, help="path to base.db with train table")
    parser.add_argument("--ntrain", type=int, required=True, help="number of vectors to train on")
    parser.add_argument("--nclusters", type=int, required=True, help="number of clusters (nlist)")
    parser.add_argument("--niter", type=int, default=20, help="k-means iterations (default 20)")
    parser.add_argument("--seed", type=int, default=42, help="random seed")
    parser.add_argument("-o", "--output", required=True, help="output centroids DB path")
    args = parser.parse_args()

    # Load vectors
    print(f"Loading {args.ntrain} vectors from {args.base_db}...")
    conn = sqlite3.connect(args.base_db)
    rows = conn.execute(
        "SELECT vector FROM train ORDER BY id LIMIT ?", (args.ntrain,)
    ).fetchall()
    conn.close()

    # Parse float32 blobs to numpy
    first_blob = rows[0][0]
    D = len(first_blob) // 4  # float32
    print(f"  Dimensions: {D}, loaded {len(rows)} vectors")

    vectors = np.zeros((len(rows), D), dtype=np.float32)
    for i, (blob,) in enumerate(rows):
        vectors[i] = np.frombuffer(blob, dtype=np.float32)

    # Normalize for cosine distance (FAISS k-means on L2 of unit vectors ≈ cosine)
    norms = np.linalg.norm(vectors, axis=1, keepdims=True)
    norms[norms == 0] = 1
    vectors /= norms

    # Run FAISS k-means
    print(f"Running k-means: {args.nclusters} clusters, {args.niter} iterations...")
    t0 = time.perf_counter()
    kmeans = faiss.Kmeans(
        D, args.nclusters,
        niter=args.niter,
        seed=args.seed,
        verbose=True,
        gpu=False,
    )
    kmeans.train(vectors)
    elapsed = time.perf_counter() - t0
    print(f"  Done in {elapsed:.1f}s")

    centroids = kmeans.centroids  # (nclusters, D) float32

    # Write output DB
    if os.path.exists(args.output):
        os.remove(args.output)
    out = sqlite3.connect(args.output)
    out.execute("CREATE TABLE centroids (centroid_id INTEGER PRIMARY KEY, centroid BLOB NOT NULL)")
    out.execute("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)")

    for i in range(args.nclusters):
        blob = centroids[i].tobytes()
        out.execute("INSERT INTO centroids (centroid_id, centroid) VALUES (?, ?)", (i, blob))

    out.execute("INSERT INTO meta VALUES ('ntrain', ?)", (str(args.ntrain),))
    out.execute("INSERT INTO meta VALUES ('nclusters', ?)", (str(args.nclusters),))
    out.execute("INSERT INTO meta VALUES ('dimensions', ?)", (str(D),))
    out.execute("INSERT INTO meta VALUES ('niter', ?)", (str(args.niter),))
    out.execute("INSERT INTO meta VALUES ('elapsed_s', ?)", (str(round(elapsed, 3)),))
    out.execute("INSERT INTO meta VALUES ('seed', ?)", (str(args.seed),))
    out.commit()
    out.close()

    print(f"Wrote {args.nclusters} centroids to {args.output}")


if __name__ == "__main__":
    main()
