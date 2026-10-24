import sqlite3
from helpers import exec, vec0_shadow_table_contents


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


def test_constraint_values(db):
    # vec0 checks partition key constraints in its own query of the chunks it
    # stores, so it has to keep the rows SQLite keeps for the declared types:
    # those of `plain`, and those of SQLite's own scan of `v`.
    db.execute(
        "create virtual table v using vec0(p integer partition key, s text partition key, a float[1], chunk_size=8)"
    )
    db.execute("create table plain(id integer primary key, p integer, s text)")
    for row in [(1, 5, "a"), (2, 6, "A"), (3, 5, "b  "), (4, 7, "5")]:
        db.execute("insert into v(rowid, p, s, a) values (?, ?, ?, '[1]')", row)
        db.execute("insert into plain values (?, ?, ?)", row)

    def rowids(sql, parameters):
        return sorted(row[0] for row in db.execute(sql, parameters))

    mismatches = []
    for where in [
        "p = ?", "p != ?", "p > ?", "p <= ?",
        "s = ?", "s != ?", "s < ?",
        "s = ? collate nocase", "s != ? collate nocase", "s >= ? collate nocase", "s = ? collate rtrim",
    ]:
        for value in [None, 5, 5.0, 5.5, "5", " 5", "a", "A", "b", b"\x05"]:
            want = rowids(f"select id from plain where {where}", [value])
            for sql in [
                f"select rowid from v where a match '[1]' and k = 10 and {where}",
                f"select rowid from v where {where}",
            ]:
                if rowids(sql, [value]) != want:
                    mismatches.append((sql, value, rowids(sql, [value]), want))
    assert mismatches == []


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


