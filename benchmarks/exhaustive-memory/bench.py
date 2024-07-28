import numpy as np
import numpy.typing as npt
import time
import sqlite3
import pandas as pd
from dataclasses import dataclass
from rich.console import Console
from rich.table import Table
from typing import List, Optional


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
    a = np.fromfile(fname, dtype="int32",)
    d = a[0]
    return a.reshape(-1, d + 1)[:, 1:].copy()


def fvecs_read(fname, sample):
    return ivecs_read(fname).view("float32")[:sample]


def bench_hnsw(base, query):
    import hnswlib
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
        results.append(result)
        times.append(time.time() - t0)
    print(time.time() - t)
    print("hnsw avg", np.mean(times))
    return results


def bench_hnsw_bf(base, query, k) -> BenchResult:
    import hnswlib
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
    print("numpy...")
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
    print(f"sqlite-vec {page_size} {chunk_size}...")

    db = sqlite3.connect(":memory:")
    db.execute(f"PRAGMA page_size = {page_size}")
    db.enable_load_extension(True)
    db.load_extension("../../dist/vec0")
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
        assert len(result) == k
        times.append(time.time() - t0)
    return BenchResult(f"sqlite-vec vec0 ({page_size}|{chunk_size})", build_time, times)


def bench_sqlite_vec_scalar(base, query, page_size, k) -> BenchResult:
    print(f"sqlite-vec-scalar...")

    db = sqlite3.connect(":memory:")
    db.enable_load_extension(True)
    db.load_extension("../../dist/vec0")
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
        assert len(result) == k
        times.append(time.time() - t0)
    return BenchResult(f"sqlite-vec-scalar ({page_size})", build_time, times)

def bench_libsql(base, query, page_size, k) -> BenchResult:
    print(f"libsql ...")
    dimensions = base.shape[1]

    db = sqlite3.connect(":memory:")
    db.enable_load_extension(True)
    assert db.execute("select 'vector' in (select name from pragma_function_list)").fetchone()[0] == 1
    db.execute(f"PRAGMA page_size={page_size}")
    db.execute(f"create table vectors(vector f32_blob({dimensions}));")

    # TODO: only does DiskANN?
    #db.execute("CREATE INDEX vectors_idx ON vectors (libsql_vector_idx(vector, 'metric=cosine'))")

    t = time.time()
    with db:
        db.executemany(
            "insert into vectors(vector) values (?)",
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
                vector_distance_cos(?, vector) as distance
              FROM vectors
              order by 2
              limit ?
            """,
            [q.tobytes(), k],
        ).fetchall()
        times.append(time.time() - t0)
    return BenchResult(f"libsql ({page_size})", build_time, times)


def register_np(db, array, name):
    ptr = array.__array_interface__["data"][0]
    nvectors, dimensions = array.__array_interface__["shape"]
    element_type = array.__array_interface__["typestr"]

    assert element_type == "<f4"

    name_escaped = db.execute("select printf('%w', ?)", [name]).fetchone()[0]

    db.execute(
        "insert into temp.vec_static_blobs(name, data) select ?, vec_static_blob_from_raw(?, ?, ?, ?)",
        [name, ptr, element_type, dimensions, nvectors],
    )

    db.execute(
        f'create virtual table "{name_escaped}" using vec_static_blob_entries({name_escaped})'
    )

def bench_sqlite_vec_static(base, query, k) -> BenchResult:
    print(f"sqlite-vec static...")

    db = sqlite3.connect(":memory:")
    db.enable_load_extension(True)
    db.load_extension("../../dist/vec0")



    t = time.time()
    register_np(db, base, "base")
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
                rowid
              from base
              where vector match ?
                and k = ?
              order by distance
            """,
            [q.tobytes(), k],
        ).fetchall()
        assert len(result) == k
        times.append(time.time() - t0)
    return BenchResult(f"sqlite-vec static", build_time, times)

def bench_faiss(base, query, k) -> BenchResult:
    import faiss
    dimensions = base.shape[1]
    print("faiss...")
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
    return BenchResult("faiss", build_time, times)


def bench_lancedb(base, query, k) -> BenchResult:
    import lancedb
    print('lancedb...')
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

def bench_duckdb(base, query, k) -> BenchResult:
    import duckdb
    import pyarrow as pa
    print("duckdb...")
    dimensions = base.shape[1]
    db = duckdb.connect(":memory:")
    db.execute(f"CREATE TABLE t(vector float[{dimensions}])")

    t0 = time.time()
    pa_base = pa.Table.from_arrays([pa.array(list(base))], names=['vector'])
    pa_base
    db.execute(f"INSERT INTO t(vector) SELECT vector::float[{dimensions}] FROM pa_base")
    build_time = time.time() - t0
    times = []
    for q in query:
        t0 = time.time()
        result = db.execute(
            f"""
              SELECT
                rowid,
                array_cosine_similarity(vector, ?::float[{dimensions}])
              FROM t
              ORDER BY 2 DESC
              LIMIT ?
            """, [q, k]).fetchall()
        times.append(time.time() - t0)
    return BenchResult("duckdb", build_time, times)

def bench_sentence_transformers(base, query, k) -> BenchResult:
    from sentence_transformers.util import semantic_search
    print("sentence-transformers")
    dimensions = base.shape[1]
    t0 = time.time()
    build_time = time.time() - t0

    times = []
    for q in query:
        t0 = time.time()
        result = semantic_search(q, base, top_k=k)
        times.append(time.time() - t0)

    return BenchResult("sentence-transformers", build_time, times)


def bench_chroma(base, query, k):
   import chromadb
   from chromadb.utils.batch_utils import create_batches
   chroma_client = chromadb.EphemeralClient()
   collection = chroma_client.create_collection(name="my_collection")

   t = time.time()
   for batch in create_batches(api=chroma_client, ids=[str(x) for x in range(len(base))], embeddings=base.tolist()):
      collection.add(*batch)
   build_time = time.time() - t
   times = []
   for q in query:
       t0 = time.time()
       result = collection.query(
           query_embeddings=[q.tolist()],
           n_results=k,
       )
       times.append(time.time() - t0)
   #print("chroma avg", duration(np.mean(times)))
   return BenchResult("chroma", build_time, times)

def bench_usearch_npy(base, query, k) -> BenchResult:
    from usearch.index import Index, search, MetricKind
    times = []
    for q in query:
        t0 = time.time()
        # result = index.search(q, exact=True)
        result = search(base, q, k, MetricKind.L2sq, exact=True)
        times.append(time.time() - t0)
    return BenchResult("usearch numpy exact=True", 0, times)


def bench_usearch_special(base, query, k) -> BenchResult:
    from usearch.index import Index, search, MetricKind
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
    return BenchResult("usuearch index", build_time, times)


def suite(name, base, query, k, benchmarks):
    print(f"Starting benchmark suite: {name} {base.shape}, k={k}")
    results = []

    for b in benchmarks:
        if b == "faiss":
            results.append(bench_faiss(base, query, k=k))
        elif b == "vec-static":
          results.append(bench_sqlite_vec_static(base, query, k=k))
        elif b.startswith("vec-scalar"):
            _, page_size = b.split('.')
            results.append(bench_sqlite_vec_scalar(base, query, page_size, k=k))
        elif b.startswith("libsql"):
            _, page_size = b.split('.')
            results.append(bench_libsql(base, query, page_size, k=k))
        elif b.startswith("vec-vec0"):
            _, page_size, chunk_size = b.split('.')
            results.append(bench_sqlite_vec(base, query, int(page_size), int(chunk_size), k=k))
        elif b == "usearch":
            results.append(bench_usearch_npy(base, query, k=k))
        elif b == "hnswlib":
            results.append(bench_hnsw_bf(base, query, k=k))
        elif b == "numpy":
            results.append(bench_numpy(base, query, k=k))
        elif b == "duckdb":
            results.append(bench_duckdb(base, query, k=k))
        elif b == "sentence-transformers":
            results.append(bench_sentence_transformers(base, query, k=k))
        elif b == "chroma":
            results.append(bench_chroma(base, query, k=k))
        else:
            raise Exception(f"unknown benchmark {b}")

    #results.append(bench_sqlite_vec(base, query, 32768, 512, k=k))
    #results.append(bench_sqlite_vec(base, query, 32768, 256, k=k))


    #results.append(bench_sqlite_vec_expo(base, query, k=k))

      # n = bench_chroma(base[:40000], query, k=k)

      # n = bench_usearch_special(base, query, k=k)



      # n = bench_sqlite_vec(base, query, 4096, 1024, k=k)
      # n = bench_sqlite_vec(base, query, 32768, 1024, k=k)



      # blessed

      ###   #for pgsz in [4096, 8192, 16384, 32768, 65536]:
      ###   #    for chunksz in [8, 32, 128, 512, 1024, 2048]:
      ###   #      results.append(bench_sqlite_vec(base, query, pgsz, chunksz, k=k))
      ###   # n = bench_sqlite_vec(base, query, 16384, 64, k=k)
      ###   # n = bench_sqlite_vec(base, query, 16384, 32, k=k)
      ###   results.append(bench_sqlite_normal(base, query, 8192, k=k))
      ###   results.append(bench_lancedb(base, query, k=k))

      ###   #h = bench_hnsw(base, query)

    table = Table(
        title=f"{name}: {base.shape[0]:,} {base.shape[1]}-dimension vectors, k={k}"
    )

    table.add_column("Tool")
    table.add_column("Build Time (ms)", justify="right")
    table.add_column("Query time (ms)", justify="right")
    for res in sorted(results, key=lambda x: np.mean(x.query_times_ms)):
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
        default=-1
    )
    parser.add_argument(
        "--qsample",
        type=int,
        required=False,
        help="Number of queries to use. Defaults all",
    )
    parser.add_argument(
        "-x", help="type of runs to make", default="faiss,vec-scalar.4096,vec-static,vec-vec0.4096.16,usearch,duckdb,hnswlib,numpy"
    )

    args = parser.parse_args()
    return args


from pathlib import Path


def cli_read_input(input, sample):
    input_path = Path(input)
    if input_path.suffix == ".fvecs":
        return fvecs_read(input_path, sample)
    if input_path.suffx == ".npy":
        return np.fromfile(input_path, dtype="float32", count=sample)
    raise Exception("unknown filetype", input)


def cli_read_query(query, base):
    if query is None:
        return base[np.random.choice(base.shape[0], 100, replace=False), :]
    return cli_read_input(query, -1)



@dataclass
class Config:
    name: str
    input: str
    k: int
    queries: str
    qsample: int
    tests: List[str]
    sample: Optional[int]

def parse_config_file(path:str) -> Config:
  name = None
  input = None
  k = None
  queries = None
  qsample = None
  sample = None
  tests = []

  for line in open(path, 'r'):
    line = line.strip()
    if not line or line.startswith('#'):
      continue
    elif line.startswith('@name='):
      name = line.removeprefix('@name=')
    elif line.startswith('@k='):
      k = line.removeprefix('@k=')
    elif line.startswith('@input='):
      input = line.removeprefix('@input=')
    elif line.startswith('@queries='):
      queries = line.removeprefix('@queries=')
    elif line.startswith('@qsample='):
      qsample = line.removeprefix('@qsample=')
    elif line.startswith('@sample='):
      sample = line.removeprefix('@sample=')
    elif line.startswith('@'):
        raise Exception(f"unknown config line '{line}'")
    else:
      tests.append(line)
  return Config(name, input, int(k), queries, int(qsample), tests, int(sample) if sample is not None else None)



from sys import argv
if __name__ == "__main__":
    config = parse_config_file(argv[1])
    print(config)
    #args = parse_args()
    #print(args)
    base = cli_read_input(config.input, config.sample)
    queries = cli_read_query(config.queries, base)[: config.qsample]
    suite(config.name, base, queries, config.k, config.tests)

    #main()
