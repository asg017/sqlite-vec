import sqlite3
import time
from random import randrange
from statistics import mean


def connect(path):
    print(path)
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

types.reverse()

for t in types:
    for page_size in page_sizes:
        for chunk_size in chunk_sizes:
            print(f"page_size={page_size}, chunk_size={chunk_size}")

            func = "embedding"
            if t == "int8":
                func = "vec_quantize_i8(embedding, 'unit')"
            if t == "bit":
                func = "vec_quantize_binary(embedding)"

            times = []
            trials = 20
            db = connect(f"dbs/test.{page_size}.{chunk_size}.{t}.db")

            for trial in range(trials):
                t0 = time.time()
                results = db.execute(
                    f"""
                      select rowid
                      from vec_items
                      where embedding match (select {func} from vec_items where rowid = ?)
                        and k = 10
                      order by distance
                    """,
                    [randrange(100000)],
                ).fetchall()

                times.append(time.time() - t0)
            print(mean(times))

"""

page_size=4096, chunk_size=256
0.2635102152824402
page_size=4096, chunk_size=1024
0.2609449863433838
page_size=4096, chunk_size=2048
0.275589919090271
page_size=8192, chunk_size=256
0.18621582984924318
page_size=8192, chunk_size=1024
0.20939643383026124
page_size=8192, chunk_size=2048
0.22376316785812378
page_size=16384, chunk_size=256
0.16012665033340454
page_size=16384, chunk_size=1024
0.18346318006515502
page_size=16384, chunk_size=2048
0.18224761486053467
page_size=32768, chunk_size=256
0.14202518463134767
page_size=32768, chunk_size=1024
0.15340715646743774
page_size=32768, chunk_size=2048
0.18018823862075806
"""
