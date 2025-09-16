import sqlite3
from collections import OrderedDict


def test_constructor_limit(db, snapshot):
    assert exec(
        db,
        """
        create virtual table v using vec0(
          p1 int partition key,
          p2 int partition key,
          p3 int partition key,
          p4 int partition key,
          p5 int partition key,
          v float[1]
        )
      """,
    ) == snapshot(name="max 4 partition keys")


def test_normal(db, snapshot):
    db.execute(
        "create virtual table v using vec0(p1 int partition key, a float[1], chunk_size=8)"
    )

    db.execute("insert into v(rowid, p1, a) values (1, 100, X'11223344')")
    assert vec0_shadow_table_contents(db, "v") == snapshot(name="1 row")
    db.execute("insert into v(rowid, p1, a) values (2, 100, X'44556677')")
    assert vec0_shadow_table_contents(db, "v") == snapshot(name="2 rows, same parition")
    db.execute("insert into v(rowid, p1, a) values (3, 200, X'8899aabb')")
    assert vec0_shadow_table_contents(db, "v") == snapshot(name="3 rows, 2 partitions")


def test_types(db, snapshot):
    db.execute(
        "create virtual table v using vec0(p1 int partition key, a float[1], chunk_size=8)"
    )

    # EVIDENCE-OF: V11454_28292
    assert exec(
        db, "insert into v(p1, a) values(?, ?)", ["not int", b"\x11\x22\x33\x44"]
    ) == snapshot(name="1. raises type error")

    assert vec0_shadow_table_contents(db, "v") == snapshot(name="2. empty DB")

    # but allow NULLs
    assert exec(
        db, "insert into v(p1, a) values(?, ?)", [None, b"\x11\x22\x33\x44"]
    ) == snapshot(name="3. allow nulls")

    assert vec0_shadow_table_contents(db, "v") == snapshot(
        name="4. show NULL partition key"
    )


def test_updates(db, snapshot):
    db.execute(
        "create virtual table v using vec0(p text partition key, a float[1], chunk_size=8)"
    )

    db.execute(
        "insert into v(rowid, p, a) values (?, ?, ?)", [1, "a", b"\x11\x11\x11\x11"]
    )
    db.execute(
        "insert into v(rowid, p, a) values (?, ?, ?)", [2, "a", b"\x22\x22\x22\x22"]
    )
    db.execute(
        "insert into v(rowid, p, a) values (?, ?, ?)", [3, "a", b"\x33\x33\x33\x33"]
    )

    assert exec(db, "select * from v") == snapshot(name="1. Initial dataset")
    assert exec(db, "update v set p = ? where rowid = ?", ["new", 1]) == snapshot(
        name="2. update #1"
    )


class Row:
    def __init__(self):
        pass

    def __repr__(self) -> str:
        return repr()


def exec(db, sql, parameters=[]):
    try:
        rows = db.execute(sql, parameters).fetchall()
    except (sqlite3.OperationalError, sqlite3.DatabaseError) as e:
        return {
            "error": e.__class__.__name__,
            "message": str(e),
        }
    a = []
    for row in rows:
        o = OrderedDict()
        for k in row.keys():
            o[k] = row[k]
        a.append(o)
    result = OrderedDict()
    result["sql"] = sql
    result["rows"] = a
    return result


def vec0_shadow_table_contents(db, v):
    shadow_tables = [
        row[0]
        for row in db.execute(
            "select name from sqlite_master where name like ? order by 1", [f"{v}_%"]
        ).fetchall()
    ]
    o = {}
    for shadow_table in shadow_tables:
        if shadow_table.endswith("_info"):
            continue
        o[shadow_table] = exec(db, f"select * from {shadow_table}")
    return o
