import sqlite3
from collections import OrderedDict


def test_constructor_limit(db, snapshot):
    assert exec(
        db,
        f"""
        create virtual table v using vec0(
          {",".join([f"+aux{x} integer" for x in range(17)])}
          v float[1]
        )
      """,
    ) == snapshot(name="max 16 auxiliary columns")


def test_normal(db, snapshot):
    db.execute(
        "create virtual table v using vec0(a float[1], +name text, chunk_size=8)"
    )
    assert exec(db, "select * from sqlite_master order by name") == snapshot(
        name="sqlite_master"
    )

    db.execute("insert into v(a, name) values (?, ?)", [b"\x11\x11\x11\x11", "alex"])
    db.execute("insert into v(a, name) values (?, ?)", [b"\x22\x22\x22\x22", "brian"])
    db.execute("insert into v(a, name) values (?, ?)", [b"\x33\x33\x33\x33", "craig"])

    assert exec(db, "select * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()

    db.execute("drop table v;")
    assert exec(db, "select * from sqlite_master order by name") == snapshot(
        name="sqlite_master post drop"
    )


def test_types(db, snapshot):
    db.execute(
        """
          create virtual table v using vec0(
            vector float[1],
            +aux_int integer,
            +aux_float float,
            +aux_text text,
            +aux_blob blob
          )
        """
    )
    assert exec(db, "select * from v") == snapshot()
    INSERT = "insert into v(vector, aux_int, aux_float, aux_text, aux_blob) values (?, ?, ?, ?, ?)"

    assert (
        exec(db, INSERT, [b"\x11\x11\x11\x11", 1, 1.22, "text", b"blob"]) == snapshot()
    )
    assert exec(db, "select * from v") == snapshot()

    # TODO: integrity test transaction failures in shadow tables
    db.commit()
    # bad types
    db.execute("BEGIN")
    assert (
        exec(db, INSERT, [b"\x11\x11\x11\x11", "not int", 1.2, "text", b"blob"])
        == snapshot()
    )
    assert (
        exec(db, INSERT, [b"\x11\x11\x11\x11", 1, "not float", "text", b"blob"])
        == snapshot()
    )
    assert exec(db, INSERT, [b"\x11\x11\x11\x11", 1, 1.2, 1, b"blob"]) == snapshot()
    assert exec(db, INSERT, [b"\x11\x11\x11\x11", 1, 1.2, "text", 1]) == snapshot()
    db.execute("ROLLBACK")

    # NULLs are totally chill
    assert exec(db, INSERT, [b"\x11\x11\x11\x11", None, None, None, None]) == snapshot()
    assert exec(db, "select * from v") == snapshot()


def test_updates(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], +name text, chunk_size=8)"
    )
    db.executemany(
        "insert into v(vector, name) values (?, ?)",
        [("[1]", "alex"), ("[2]", "brian"), ("[3]", "craig")],
    )
    assert exec(db, "select rowid, * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()

    assert exec(db, "update v set name = 'ALEX' where rowid = 1") == snapshot()
    assert exec(db, "select rowid, * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()


def test_deletes(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], +name text, chunk_size=8)"
    )
    db.executemany(
        "insert into v(vector, name) values (?, ?)",
        [("[1]", "alex"), ("[2]", "brian"), ("[3]", "craig")],
    )
    assert exec(db, "select rowid, * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()

    assert exec(db, "delete from v where rowid = 1") == snapshot()
    assert exec(db, "select rowid, * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()


def test_knn(db, snapshot):
    db.execute("create virtual table v using vec0(vector float[1], +name text)")
    db.executemany(
        "insert into v(vector, name) values (?, ?)",
        [("[1]", "alex"), ("[2]", "brian"), ("[3]", "craig")],
    )
    assert exec(db, "select * from v") == snapshot()
    assert exec(
        db, "select *, distance from v where vector match '[5]' and k = 10"
    ) == snapshot(name="legal KNN w/ aux")

    # EVIDENCE-OF: V25623_09693 No aux constraint allowed on KNN queries
    assert exec(
        db,
        "select *, distance from v where vector match '[5]' and k = 10 and name = 'alex'",
    ) == snapshot(name="illegal KNN w/ aux")


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
