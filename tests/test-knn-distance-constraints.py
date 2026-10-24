import sqlite3
from helpers import exec


def test_distance_constraint_values(db):
    # vec0 declares distance REAL and checks distance constraints itself, so it
    # has to keep the rows SQLite keeps for a REAL column: text that reads as a
    # number compares as that number, no distance compares with NULL, and every
    # distance is less than any other TEXT or BLOB. A distance is an f32, so a
    # value a hair either side of one rounds to it in f32 but not in SQLite.
    db.execute("create virtual table v using vec0(embedding float[1])")
    db.execute("insert into v(rowid, embedding) values (1, '[1]'), (2, '[2]'), (3, '[3]'), (4, '[0.3]')")
    db.execute("create table plain(id integer primary key, distance real)")
    distances = db.execute("select rowid, distance from v where embedding match '[0]' and k = 10").fetchall()
    db.executemany("insert into plain values (?, ?)", distances)
    hairs = [d + s * 1e-12 for _, d in distances for s in (-1, 1)]

    mismatches = []
    for op in ["<", "<=", ">", ">="]:
        for value in [None, 2, 2.0, 2.5, "2", "abc", b"\x02", 2**62, -(2**62), *hairs]:
            knn = sorted(
                row[0]
                for row in db.execute(
                    f"select rowid from v where embedding match '[0]' and k = 10 and distance {op} ?",
                    [value],
                )
            )
            want = sorted(
                row[0] for row in db.execute(f"select id from plain where distance {op} ?", [value])
            )
            if knn != want:
                mismatches.append((op, value, knn, want))
    assert mismatches == []


def test_normal(db, snapshot):
    db.execute("create virtual table v using vec0(embedding float[1], is_odd boolean, chunk_size=8)")
    db.executemany(
        "insert into v(rowid, is_odd, embedding) values (?1, ?1 % 2, ?2)",
        [
            [1, "[1]"],
            [2, "[2]"],
            [3, "[3]"],
            [4, "[4]"],
            [5, "[5]"],
            [6, "[6]"],
            [7, "[7]"],
            [8, "[8]"],
            [9, "[9]"],
            [10, "[10]"],
            [11, "[11]"],
            [12, "[12]"],
            [13, "[13]"],
            [14, "[14]"],
            [15, "[15]"],
            [16, "[16]"],
            [17, "[17]"],
        ],
    )
    assert exec(db,"SELECT * FROM v") == snapshot()

    BASE_KNN = "select rowid, distance from v where embedding match ? and k = ? "
    assert exec(db, BASE_KNN, ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance > 5", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance >= 5", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance < 3", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance <= 3", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance > 7 AND distance <= 10", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance BETWEEN 7 AND 10", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND is_odd == TRUE AND distance BETWEEN 7 AND 10", ["[1]", 5]) == snapshot()


class Row:
    def __init__(self):
        pass

    def __repr__(self) -> str:
        return repr()


