import sqlite3
from helpers import exec


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


