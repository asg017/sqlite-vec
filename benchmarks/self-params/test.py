import sqlite3
import time


def connect(path):
    db = sqlite3.connect(path)
    db.enable_load_extension(True)
    db.load_extension("../dist/vec0")
    db.execute("select load_extension('../dist/vec0', 'sqlite3_vec_fs_read_init')")
    db.enable_load_extension(False)
    return db


page_sizes = [4096, 8192, 16384, 32768]
chunk_sizes = [256, 1024, 2048]

for page_size in page_sizes:
    for chunk_size in chunk_sizes:
        print(f"page_size={page_size}, chunk_size={chunk_size}")

        t0 = time.time()
        db = connect(f"dbs/test.{page_size}.{chunk_size}.db")
        print(db.execute("pragma page_size").fetchone()[0])
        print(db.execute("select count(*) from vec_items_rowids").fetchone()[0])
