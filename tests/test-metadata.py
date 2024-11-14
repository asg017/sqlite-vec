import sqlite3
from collections import OrderedDict


def test_constructor_limit(db, snapshot):
    pass
    assert exec(
        db,
        f"""
        create virtual table v using vec0(
          {",".join([f"metadata{x} integer" for x in range(17)])}
          v float[1]
        )
      """,
    ) == snapshot(name="max 16 metadata columns")


def test_normal(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], n1 int, n2 int64, f float, d double, t text, chunk_size=8)"
    )
    assert exec(
        db, "select * from sqlite_master where type = 'table' order by name"
    ) == snapshot(name="sqlite_master")

    assert vec0_shadow_table_contents(db, "v") == snapshot()

    INSERT = "insert into v(vector, n1, n2, f, d, t) values (?, ?, ?, ?, ?, ?)"
    assert exec(db, INSERT, [b"\x11\x11\x11\x11", 1, 1, 1.1, 1.1, "one"]) == snapshot()
    assert exec(db, INSERT, [b"\x22\x22\x22\x22", 2, 2, 2.2, 2.2, "two"]) == snapshot()
    assert (
        exec(db, INSERT, [b"\x33\x33\x33\x33", 3, 3, 3.3, 3.3, "three"]) == snapshot()
    )

    assert exec(db, "select * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()


#
# assert exec(db, "select * from v") == snapshot()
# assert vec0_shadow_table_contents(db, "v") == snapshot()
#
# db.execute("drop table v;")
# assert exec(db, "select * from sqlite_master order by name") == snapshot(
#    name="sqlite_master post drop"
# )


def test_types(db, snapshot):
    pass


def test_updates(db, snapshot):
    pass


def test_deletes(db, snapshot):
    pass


def test_knn(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], name text, chunk_size=8)"
    )
    assert exec(
        db, "select * from sqlite_master where type = 'table' order by name"
    ) == snapshot(name="sqlite_master")
    db.executemany(
        "insert into v(vector, name) values (?, ?)",
        [("[1]", "alex"), ("[2]", "brian"), ("[3]", "craig")],
    )

    # EVIDENCE-OF: V16511_00582 catches "illegal" constraints on metadata columns
    assert (
        exec(
            db,
            "select *, distance from v where vector match '[5]' and k = 3 and name like 'illegal'",
        )
        == snapshot()
    )


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
        o[shadow_table] = exec(db, f"select * from {shadow_table}")
    return o
