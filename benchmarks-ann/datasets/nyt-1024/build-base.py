# /// script
# requires-python = ">=3.12"
# dependencies = [
#     "sentence-transformers",
#     "torch<=2.7",
#     "tqdm",
# ]
# ///

import argparse
import sqlite3
from array import array
from itertools import batched

from sentence_transformers import SentenceTransformer
from tqdm import tqdm


def main():
    parser = argparse.ArgumentParser(
        description="Build base.db with train vectors, query vectors, and brute-force KNN neighbors",
    )
    parser.add_argument(
        "--contents-db", "-c", default=None,
        help="Path to contents.db (source of headlines and IDs)",
    )
    parser.add_argument(
        "--model", "-m", default="mixedbread-ai/mxbai-embed-large-v1",
        help="HuggingFace model ID (default: mixedbread-ai/mxbai-embed-large-v1)",
    )
    parser.add_argument(
        "--queries-file", "-q", default="queries.txt",
        help="Path to the queries file (default: queries.txt)",
    )
    parser.add_argument(
        "--output", "-o", required=True,
        help="Path to the output base.db",
    )
    parser.add_argument(
        "--batch-size", "-b", type=int, default=256,
        help="Batch size for embedding (default: 256)",
    )
    parser.add_argument(
        "--k", "-k", type=int, default=100,
        help="Number of nearest neighbors (default: 100)",
    )
    parser.add_argument(
        "--limit", "-l", type=int, default=0,
        help="Limit number of headlines to embed (0 = all)",
    )
    parser.add_argument(
        "--vec-path", "-v", default="~/projects/sqlite-vec/dist/vec0",
        help="Path to sqlite-vec extension (default: ~/projects/sqlite-vec/dist/vec0)",
    )
    parser.add_argument(
        "--skip-neighbors", action="store_true",
        help="Skip the brute-force KNN neighbor computation",
    )
    args = parser.parse_args()

    import os
    vec_path = os.path.expanduser(args.vec_path)

    print(f"Loading model {args.model}...")
    model = SentenceTransformer(args.model)

    # Read headlines from contents.db
    src = sqlite3.connect(args.contents_db)
    limit_clause = f" LIMIT {args.limit}" if args.limit > 0 else ""
    headlines = src.execute(
        f"SELECT id, headline FROM contents ORDER BY id{limit_clause}"
    ).fetchall()
    src.close()
    print(f"Loaded {len(headlines)} headlines from {args.contents_db}")

    # Read queries
    with open(args.queries_file) as f:
        queries = [line.strip() for line in f if line.strip()]
    print(f"Loaded {len(queries)} queries from {args.queries_file}")

    # Create output database
    db = sqlite3.connect(args.output)
    db.enable_load_extension(True)
    db.load_extension(vec_path)
    db.enable_load_extension(False)

    db.execute("CREATE TABLE IF NOT EXISTS train(id INTEGER PRIMARY KEY, vector BLOB)")
    db.execute("CREATE TABLE IF NOT EXISTS query_vectors(id INTEGER PRIMARY KEY, vector BLOB)")
    db.execute(
        "CREATE TABLE IF NOT EXISTS neighbors("
        "  query_vector_id INTEGER, rank INTEGER, neighbors_id TEXT,"
        "  UNIQUE(query_vector_id, rank))"
    )

    # Step 1: Embed headlines -> train table
    print("Embedding headlines...")
    for batch in tqdm(
        batched(headlines, args.batch_size),
        total=(len(headlines) + args.batch_size - 1) // args.batch_size,
    ):
        ids = [r[0] for r in batch]
        texts = [r[1] for r in batch]
        embeddings = model.encode(texts, normalize_embeddings=True)

        params = [
            (int(rid), array("f", emb.tolist()).tobytes())
            for rid, emb in zip(ids, embeddings)
        ]
        db.executemany("INSERT INTO train VALUES (?, ?)", params)
        db.commit()

    del headlines
    n = db.execute("SELECT count(*) FROM train").fetchone()[0]
    print(f"Embedded {n} headlines")

    # Step 2: Embed queries -> query_vectors table
    print("Embedding queries...")
    query_embeddings = model.encode(queries, normalize_embeddings=True)
    query_params = []
    for i, emb in enumerate(query_embeddings, 1):
        blob = array("f", emb.tolist()).tobytes()
        query_params.append((i, blob))
    db.executemany("INSERT INTO query_vectors VALUES (?, ?)", query_params)
    db.commit()
    print(f"Embedded {len(queries)} queries")

    if args.skip_neighbors:
        db.close()
        print(f"Done (skipped neighbors). Wrote {args.output}")
        return

    # Step 3: Brute-force KNN via sqlite-vec -> neighbors table
    n_queries = db.execute("SELECT count(*) FROM query_vectors").fetchone()[0]
    print(f"Computing {args.k}-NN for {n_queries} queries via sqlite-vec...")
    for query_id, query_blob in tqdm(
        db.execute("SELECT id, vector FROM query_vectors").fetchall()
    ):
        results = db.execute(
            """
            SELECT
                train.id,
                vec_distance_cosine(train.vector, ?) AS distance
            FROM train
            WHERE distance IS NOT NULL
            ORDER BY distance ASC
            LIMIT ?
            """,
            (query_blob, args.k),
        ).fetchall()

        params = [
            (query_id, rank, str(rid))
            for rank, (rid, _dist) in enumerate(results)
        ]
        db.executemany("INSERT INTO neighbors VALUES (?, ?, ?)", params)

    db.commit()
    db.close()
    print(f"Done. Wrote {args.output}")


if __name__ == "__main__":
    main()
