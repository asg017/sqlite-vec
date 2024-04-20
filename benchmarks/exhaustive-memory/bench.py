import numpy as np
import numpy.typing as npt
import time
import hnswlib
import sqlite3
import faiss
import lancedb
import pandas as pd

# import chromadb
from usearch.index import Index, search, MetricKind

from dataclasses import dataclass

from typing import List


@dataclass
class BenchResult:
    tool: str
    build_time_ms: float
    query_times_ms: List[float]


def duration(seconds: float):
    ms = seconds * 1000
    return f"{int(ms)}ms"


def cosine_similarity(
    vec: npt.NDArray[np.float32], mat: npt.NDArray[np.float32], do_norm: bool = True
) -> npt.NDArray[np.float32]:
    sim = vec @ mat.T
    if do_norm:
        sim /= np.linalg.norm(vec) * np.linalg.norm(mat, axis=1)
    return sim


def topk(
    vec: npt.NDArray[np.float32],
    mat: npt.NDArray[np.float32],
    k: int = 5,
    do_norm: bool = True,
) -> tuple[npt.NDArray[np.int32], npt.NDArray[np.float32]]:
    sim = cosine_similarity(vec, mat, do_norm=do_norm)
    # Rather than sorting all similarities and taking the top K, it's faster to
    # argpartition and then just sort the top K.
    # The difference is O(N logN) vs O(N + k logk)
    indices = np.argpartition(-sim, kth=k)[:k]
    top_indices = np.argsort(-sim[indices])
    return indices[top_indices], sim[top_indices]


def ivecs_read(fname):
    a = np.fromfile(fname, dtype="int32")
    d = a[0]
    return a.reshape(-1, d + 1)[:, 1:].copy()


def fvecs_read(fname):
    return ivecs_read(fname).view("float32")


def bench_hnsw(base, query):
    t0 = time.time()
    p = hnswlib.Index(space="ip", dim=128)  # possible options are l2, cosine or ip

    # NOTE: Use default settings from the README.
    print("buildings hnsw")
    p.init_index(max_elements=base.shape[0], ef_construction=200, M=16)
    ids = np.arange(base.shape[0])
    p.add_items(base, ids)
    p.set_ef(50)

    print("build time", time.time() - t0)

    results = []
    times = []
    t = time.time()
    for idx, q in enumerate(query):
        t0 = time.time()
        result = p.knn_query(q, k=5)
        if idx < 5:
            print(result[0])
        results.append(result)
        times.append(time.time() - t0)
    print(time.time() - t)
    print("hnsw avg", np.mean(times))
    return results


def bench_hnsw_bf(base, query, k) -> BenchResult:
    print("hnswlib-bf")
    dimensions = base.shape[1]
    t0 = time.time()
    p = hnswlib.BFIndex(space="l2", dim=dimensions)

    p.init_index(max_elements=base.shape[0])
    ids = np.arange(base.shape[0])
    p.add_items(base, ids)

    build_time = time.time() - t0

    results = []
    times = []
    t = time.time()
    for idx, q in enumerate(query):
        t0 = time.time()
        result = p.knn_query(q, k=k)
        results.append(result)
        times.append(time.time() - t0)
    return BenchResult("hnswlib-bf", build_time, times)


def bench_numpy(base, query, k) -> BenchResult:
    print("numpy")
    times = []
    results = []
    for idx, q in enumerate(query):
        t0 = time.time()
        result = topk(q, base, k=k)
        results.append(result)
        times.append(time.time() - t0)
    return BenchResult("numpy", 0, times)


def bench_sqlite_vec(base, query, page_size, chunk_size, k) -> BenchResult:
    dimensions = base.shape[1]
    print(f"sqlite-vec {page_size} {chunk_size}")

    db = sqlite3.connect(":memory:")
    db.execute(f"PRAGMA page_size = {page_size}")
    db.enable_load_extension(True)
    db.load_extension("./dist/vec0")
    db.execute(
        f"""
          create virtual table vec_sift1m using vec0(
            chunk_size={chunk_size},
            vector float[{dimensions}]
          )
        """
    )

    t = time.time()
    with db:
        db.executemany(
            "insert into vec_sift1m(vector) values (?)",
            list(map(lambda x: [x.tobytes()], base)),
        )
    build_time = time.time() - t
    times = []
    results = []
    for (
        idx,
        q,
    ) in enumerate(query):
        t0 = time.time()
        result = db.execute(
            """
              select
                rowid,
                distance
              from vec_sift1m
              where vector match ?
                and k = ?
              order by distance
            """,
            [q.tobytes(), k],
        ).fetchall()
        times.append(time.time() - t0)
    return BenchResult(f"sqlite-vec vec0 ({page_size}|{chunk_size})", build_time, times)


def bench_sqlite_normal(base, query, page_size, k) -> BenchResult:
    print(f"sqlite-normal")

    db = sqlite3.connect(":memory:")
    db.enable_load_extension(True)
    db.load_extension("./dist/vec0")
    db.execute(f"PRAGMA page_size={page_size}")
    db.execute(f"create table sift1m(vector);")

    t = time.time()
    with db:
        db.executemany(
            "insert into sift1m(vector) values (?)",
            list(map(lambda x: [x.tobytes()], base)),
        )
    build_time = time.time() - t
    times = []
    results = []
    t = time.time()
    for (
        idx,
        q,
    ) in enumerate(query):
        t0 = time.time()
        result = db.execute(
            """
              select
                rowid,
                vec_distance_l2(?, vector) as distance
              from sift1m
              order by distance
              limit ?
            """,
            [q.tobytes(), k],
        ).fetchall()
        times.append(time.time() - t0)
    return BenchResult(f"sqlite-vec normal ({page_size})", build_time, times)


def bench_faiss(base, query, k) -> BenchResult:
    dimensions = base.shape[1]
    print("faiss")
    t = time.time()
    index = faiss.IndexFlatL2(dimensions)
    index.add(base)
    build_time = time.time() - t
    times = []
    results = []
    t = time.time()
    for idx, q in enumerate(query):
        t0 = time.time()
        distances, rowids = index.search(x=np.array([q]), k=k)
        results.append(rowids)
        times.append(time.time() - t0)
    print("faiss avg", duration(np.mean(times)))
    return BenchResult("faiss", build_time, times)


def bench_lancedb(base, query, k) -> BenchResult:
    dimensions = base.shape[1]
    db = lancedb.connect("a")
    data = [{"vector": row.reshape(1, -1)[0]} for row in base]
    # Create a DataFrame where each row is a 1D array
    df = pd.DataFrame(data=data, columns=["vector"])
    t = time.time()
    db.create_table("t", data=df)
    build_time = time.time() - t
    tbl = db.open_table("t")
    times = []
    for q in query:
        t0 = time.time()
        result = tbl.search(q).limit(k).to_arrow()
        times.append(time.time() - t0)
    return BenchResult("lancedb", build_time, times)


# def bench_chroma(base, query, k):
#    chroma_client = chromadb.Client()
#    collection = chroma_client.create_collection(name="my_collection")
#
#    t = time.time()
#    # chroma doesn't allow for more than 41666 vectors to be inserted at once (???)
#    i = 0
#    collection.add(embeddings=base, ids=[str(x) for x in range(len(base))])
#    print("chroma build time: ", duration(time.time() - t))
#    times = []
#    for q in query:
#        t0 = time.time()
#        result = collection.query(
#            query_embeddings=[q.tolist()],
#            n_results=k,
#        )
#        print(result)
#        times.append(time.time() - t0)
#    print("chroma avg", duration(np.mean(times)))


def bench_usearch_npy(base, query, k) -> BenchResult:
    times = []
    for q in query:
        t0 = time.time()
        # result = index.search(q, exact=True)
        result = search(base, q, k, MetricKind.L2sq, exact=True)
        times.append(time.time() - t0)
    return BenchResult("usearch numpy exact=True", 0, times)


def bench_usearch_special(base, query, k) -> BenchResult:
    dimensions = base.shape[1]
    index = Index(ndim=dimensions)
    t = time.time()
    index.add(np.arange(len(base)), base)
    build_time = time.time() - t

    times = []
    for q in query:
        t0 = time.time()
        result = index.search(q, exact=True)
        times.append(time.time() - t0)
    return BenchResult("usuearch index exact=True", build_time, times)


from rich.console import Console
from rich.table import Table


def suite(name, base, query, k):
    print(f"Starting benchmark suite: {name} {base.shape}, k={k}")
    results = []
    # n = bench_chroma(base[:40000], query, k=k)
    # n = bench_usearch_npy(base, query, k=k)
    # n = bench_usearch_special(base, query, k=k)
    results.append(bench_faiss(base, query, k=k))
    results.append(bench_hnsw_bf(base, query, k=k))
    # n = bench_sqlite_vec(base, query, 4096, 1024, k=k)
    # n = bench_sqlite_vec(base, query, 32768, 1024, k=k)
    results.append(bench_sqlite_vec(base, query, 32768, 256, k=k))
    # n = bench_sqlite_vec(base, query, 16384, 64, k=k)
    # n = bench_sqlite_vec(base, query, 16384, 32, k=k)
    results.append(bench_sqlite_normal(base, query, 8192, k=k))
    results.append(bench_lancedb(base, query, k=k))
    results.append(bench_numpy(base, query, k=k))
    # h = bench_hnsw(base, query)

    table = Table(
        title=f"{name}: {base.shape[0]:,} {base.shape[1]}-dimension vectors, k={k}"
    )

    table.add_column("Tool")
    table.add_column("Build Time (ms)", justify="right")
    table.add_column("Query time (ms)", justify="right")
    for res in results:
        table.add_row(
            res.tool, duration(res.build_time_ms), duration(np.mean(res.query_times_ms))
        )

    console = Console()
    console.print(table)


import argparse


def parse_args():
    parser = argparse.ArgumentParser(description="Benchmark processing script.")
    # Required arguments
    parser.add_argument("-n", "--name", required=True, help="Name of the benchmark.")
    parser.add_argument(
        "-i", "--input", required=True, help="Path to input file (.npy)."
    )
    parser.add_argument(
        "-k", type=int, required=True, help="Parameter k to use in benchmark."
    )

    # Optional arguments
    parser.add_argument(
        "-q", "--query", required=False, help="Path to query file (.npy)."
    )
    parser.add_argument(
        "--sample",
        type=int,
        required=False,
        help="Number of entries in base to use. Defaults all",
    )
    parser.add_argument(
        "--qsample",
        type=int,
        required=False,
        help="Number of queries to use. Defaults all",
    )

    args = parser.parse_args()
    return args


from pathlib import Path


def cli_read_input(input):
    input_path = Path(input)
    if input_path.suffix == ".fvecs":
        return fvecs_read(input_path)
    if input_path.suffx == ".npy":
        return np.fromfile(input_path, dtype="float32")
    raise Exception("unknown filetype", input)


def cli_read_query(query, base):
    if query is None:
        return base[np.random.choice(base.shape[0], 100, replace=False), :]
    return cli_read_input(query)


def main():
    args = parse_args()
    base = cli_read_input(args.input)[: args.sample]
    queries = cli_read_query(args.query, base)[: args.qsample]
    suite(args.name, base, queries, args.k)

    from sys import argv

    # base = fvecs_read("sift/sift_base.fvecs")  # [:100000]
    # query = fvecs_read("sift/sift_query.fvecs")[:100]
    # print(base.shape)
    # k = int(argv[1]) if len(argv) > 1 else 5
    # suite("sift1m", base, query, k)


if __name__ == "__main__":
    main()
