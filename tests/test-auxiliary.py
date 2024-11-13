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


def test_types(db, snapshot):
    pass


def test_updates(db, snapshot):
    pass


def test_deletes(db, snapshot):
    pass


def test_knn(db, snapshot):
    pass


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
