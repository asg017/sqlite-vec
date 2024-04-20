import sqlite3
import time


def connect(path):
    db = sqlite3.connect(path)
    db.enable_load_extension(True)
    db.load_extension("../dist/vec0")
    db.execute("select load_extension('../dist/vec0', 'sqlite3_vec_fs_read_init')")
    db.enable_load_extension(False)
    return db


page_sizes = [  # 4096, 8192,
    16384,
    32768,
]
chunk_sizes = [128, 256, 1024, 2048]
types = ["f32", "int8", "bit"]

SRC = "../examples/dbpedia-openai/data/vectors.npy"

for page_size in page_sizes:
    for chunk_size in chunk_sizes:
        for t in types:
            print(f"{t} page_size={page_size}, chunk_size={chunk_size}")

            t0 = time.time()
            db = connect(f"dbs/test.{page_size}.{chunk_size}.{t}.db")
            db.execute(f"pragma page_size = {page_size}")
            with db:
                db.execute(
                    f"""
                      create virtual table vec_items using vec0(
                        embedding {t}[1536],
                        chunk_size={chunk_size}
                      )
                    """
                )
                func = "vector"
                if t == "int8":
                    func = "vec_quantize_i8(vector, 'unit')"
                if t == "bit":
                    func = "vec_quantize_binary(vector)"
                db.execute(
                    f"""
                      insert into vec_items
                      select rowid, {func}
                      from vec_npy_each(vec_npy_file(?))
                      limit 100000
                    """,
                    [SRC],
                )
            elapsed = time.time() - t0
            print(elapsed)

"""

# for 100_000

page_size=4096, chunk_size=256
3.5894200801849365
page_size=4096, chunk_size=1024
60.70046401023865
page_size=4096, chunk_size=2048
201.04426288604736
page_size=8192, chunk_size=256
7.034514904022217
page_size=8192, chunk_size=1024
9.983598947525024
page_size=8192, chunk_size=2048
12.318921089172363
page_size=16384, chunk_size=256
4.97080397605896
page_size=16384, chunk_size=1024
6.051195859909058
page_size=16384, chunk_size=2048
8.492683172225952
page_size=32768, chunk_size=256
5.906642198562622
page_size=32768, chunk_size=1024
5.876632213592529
page_size=32768, chunk_size=2048
5.420510292053223
"""
