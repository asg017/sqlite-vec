import sqlite3
import sqlite_vec

from typing import List
import struct


def serialize_f32(vector: List[float]) -> bytes:
    """serializes a list of floats into a compact "raw bytes" format"""
    return struct.pack("%sf" % len(vector), *vector)


def serialize_int8(vector: List[int]) -> bytes:
    """serializes a list of int8 into a compact "raw bytes" format"""
    return struct.pack("%sb" % len(vector), *vector)


db = sqlite3.connect(":memory:")
db.enable_load_extension(True)
sqlite_vec.load(db)
db.enable_load_extension(False)


sqlite_version, vec_version = db.execute(
    "select sqlite_version(), vec_version()"
).fetchone()
print(f"sqlite_version={sqlite_version}, vec_version={vec_version}")


db.execute("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[8])")

items = [
    (1, [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]),
    (2, [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]),
]
with db:
    for item in items:
        db.execute(
            "INSERT INTO vec_items(rowid, embedding) VALUES (?, ?)",
            [item[0], serialize_f32(item[1])],
        )

query = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]
rows = db.execute(
    """
      SELECT
        rowid,
        distance
      FROM vec_items
      WHERE embedding MATCH ?
      ORDER BY distance
      LIMIT 5
    """,
    [serialize_f32(query)],
).fetchall()

print(rows)
