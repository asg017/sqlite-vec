import sqlite3
import numpy as np

db = sqlite3.connect(":memory:")

db.enable_load_extension(True)
db.load_extension("./dist/vec0")
db.execute("select load_extension('./dist/vec0', 'sqlite3_vec_raw_init')")
db.enable_load_extension(False)

x = np.array([[0.1, 0.2, 0.3, 0.4], [0.9, 0.8, 0.7, 0.6]], dtype=np.float32)
y = np.array([[0.2, 0.3], [0.9, 0.8], [0.6, 0.5]], dtype=np.float32)
z = np.array(
    [
        [0.1, 0.1, 0.1, 0.1],
        [0.2, 0.2, 0.2, 0.2],
        [0.3, 0.3, 0.3, 0.3],
        [0.4, 0.4, 0.4, 0.4],
        [0.5, 0.5, 0.5, 0.5],
    ],
    dtype=np.float32,
)


def register_np(array, name):
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


register_np(x, "x")
register_np(y, "y")
register_np(z, "z")
print(db.execute("select *, dimensions, count from temp.vec_static_blobs;").fetchall())

print(db.execute("select vec_to_json(vector) from x;").fetchall())
print(db.execute("select (vector) from y limit 2;").fetchall())
print(
    db.execute(
        "select (vector) from z where vector match ? and k = 2 order by distance;",
        [np.array([0.3, 0.3, 0.3, 0.3], dtype=np.float32)],
    ).fetchall()
)
