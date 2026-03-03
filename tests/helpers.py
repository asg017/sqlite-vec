import sqlite3
import struct
from collections import OrderedDict


def _f32(list):
    return struct.pack("%sf" % len(list), *list)


def _i64(list):
    return struct.pack("%sq" % len(list), *list)


def _int8(list):
    return struct.pack("%sb" % len(list), *list)


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


def vec0_shadow_table_contents(db, v, skip_info=True):
    shadow_tables = [
        row[0]
        for row in db.execute(
            "select name from sqlite_master where name like ? order by 1", [f"{v}_%"]
        ).fetchall()
    ]
    o = {}
    for shadow_table in shadow_tables:
        if skip_info and shadow_table.endswith("_info"):
            continue
        o[shadow_table] = exec(db, f"select * from {shadow_table}")
    return o
