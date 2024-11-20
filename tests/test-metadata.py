import pytest
import sqlite3
from collections import OrderedDict
import json


def test_constructor_limit(db, snapshot):
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
        "create virtual table v using vec0(vector float[1], b boolean, n int, f float, t text, chunk_size=8)"
    )
    assert exec(
        db, "select * from sqlite_master where type = 'table' order by name"
    ) == snapshot(name="sqlite_master")

    assert vec0_shadow_table_contents(db, "v") == snapshot()

    INSERT = "insert into v(vector, b, n, f, t) values (?, ?, ?, ?, ?)"
    assert exec(db, INSERT, [b"\x11\x11\x11\x11", 1, 1, 1.1, "one"]) == snapshot()
    assert exec(db, INSERT, [b"\x22\x22\x22\x22", 1, 2, 2.2, "two"]) == snapshot()
    assert exec(db, INSERT, [b"\x33\x33\x33\x33", 1, 3, 3.3, "three"]) == snapshot()

    assert exec(db, "select * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()

    assert exec(db, "drop table v") == snapshot()
    assert exec(db, "select * from sqlite_master") == snapshot()


#
# assert exec(db, "select * from v") == snapshot()
# assert vec0_shadow_table_contents(db, "v") == snapshot()
#
# db.execute("drop table v;")
# assert exec(db, "select * from sqlite_master order by name") == snapshot(
#    name="sqlite_master post drop"
# )


def test_text_knn(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], name text, chunk_size=8)"
    )
    assert vec0_shadow_table_contents(db, "v") == snapshot()
    INSERT = "insert into v(vector, name) values (?, ?)"
    db.execute(
        """
      INSERT INTO v(vector, name) VALUES
        ('[.11]', 'aaa'),
        ('[.22]', 'bbb'),
        ('[.33]', 'ccc'),
        ('[.44]', 'ddd'),
        ('[.55]', 'eee'),
        ('[.66]', 'fff'),
        ('[.77]', 'ggg'),
        ('[.88]', 'hhh'),
        ('[.99]', 'iii');
    """
    )
    assert exec(db, "select * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()

    assert (
        exec(
            db,
            "select rowid, name, distance from v where vector match '[1]' and k = 5",
        )
        == snapshot()
    )

    assert (
        exec(
            db,
            "select rowid, name, distance from v where vector match '[1]' and k = 5 and name < 'ddd'",
        )
        == snapshot()
    )
    assert (
        exec(
            db,
            "select rowid, name, distance from v where vector match '[1]' and k = 5 and name <= 'ddd'",
        )
        == snapshot()
    )
    assert (
        exec(
            db,
            "select rowid, name, distance from v where vector match '[1]' and k = 5 and name > 'fff'",
        )
        == snapshot()
    )
    assert (
        exec(
            db,
            "select rowid, name, distance from v where vector match '[1]' and k = 5 and name >= 'fff'",
        )
        == snapshot()
    )
    assert (
        exec(
            db,
            "select rowid, name, distance from v where vector match '[1]' and k = 5 and name = 'aaa'",
        )
        == snapshot()
    )
    assert (
        exec(
            db,
            "select rowid, name, distance from v where vector match '[.01]' and k = 5 and name != 'aaa'",
        )
        == snapshot()
    )


def test_long_text_updates(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], name text, chunk_size=8)"
    )
    assert vec0_shadow_table_contents(db, "v") == snapshot()
    INSERT = "insert into v(vector, name) values (?, ?)"
    exec(db, INSERT, [b"\x11\x11\x11\x11", "123456789a12"])
    exec(db, INSERT, [b"\x11\x11\x11\x11", "123456789a123"])
    assert exec(db, "select * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()


def test_long_text_knn(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], name text, chunk_size=8)"
    )
    INSERT = "insert into v(vector, name) values (?, ?)"
    exec(db, INSERT, ["[1]", "aaaa"])
    exec(db, INSERT, ["[2]", "aaaaaaaaaaaa_aaa"])
    exec(db, INSERT, ["[3]", "bbbb"])
    exec(db, INSERT, ["[4]", "bbbbbbbbbbbb_bbb"])
    exec(db, INSERT, ["[5]", "cccc"])
    exec(db, INSERT, ["[6]", "cccccccccccc_ccc"])

    tests = [
        "bbbb",
        "bb",
        "bbbbbb",
        "bbbbbbbbbbbb_bbb",
        "bbbbbbbbbbbb_aaa",
        "bbbbbbbbbbbb_ccc",
        "longlonglonglonglonglonglong",
    ]
    ops = ["=", "!=", "<", "<=", ">", ">="]
    op_names = ["eq", "ne", "lt", "le", "gt", "ge"]

    for test in tests:
        for op, op_name in zip(ops, op_names):
            assert exec(
                db,
                f"select rowid, name, distance from v where vector match '[100]' and k = 5 and name {op} ?",
                [test],
            ) == snapshot(name=f"{op_name}-{test}")


def test_types(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], b boolean, n int, f float, t text, chunk_size=8)"
    )
    INSERT = "insert into v(vector, b, n, f, t) values (?, ?, ?, ?, ?)"

    assert exec(db, INSERT, [b"\x11\x11\x11\x11", 1, 1, 1.1, "test"]) == snapshot(
        name="legal"
    )

    # fmt: off
    assert exec(db, INSERT, [b"\x11\x11\x11\x11", 'illegal', 1, 1.1, 'test']) == snapshot(name="illegal-type-boolean")
    assert exec(db, INSERT, [b"\x11\x11\x11\x11", 1, 'illegal', 1.1, 'test']) == snapshot(name="illegal-type-int")
    assert exec(db, INSERT, [b"\x11\x11\x11\x11", 1, 1, 'illegal', 'test']) == snapshot(name="illegal-type-float")
    assert exec(db, INSERT, [b"\x11\x11\x11\x11", 1, 1, 1.1, 420]) == snapshot(name="illegal-type-text")
    # fmt: on

    assert exec(db, INSERT, [b"\x11\x11\x11\x11", 44, 1, 1.1, "test"]) == snapshot(
        name="illegal-boolean"
    )


def test_updates(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], b boolean, n int, f float, t text, chunk_size=8)"
    )
    INSERT = "insert into v(rowid, vector, b, n, f, t) values (?, ?, ?, ?, ?, ?)"

    exec(db, INSERT, [1, b"\x11\x11\x11\x11", 1, 1, 1.1, "test1"])
    exec(db, INSERT, [2, b"\x22\x22\x22\x22", 1, 2, 2.2, "test2"])
    exec(db, INSERT, [3, b"\x33\x33\x33\x33", 1, 3, 3.3, "1234567890123"])
    assert exec(db, "select * from v") == snapshot(name="1-init-contents")
    assert vec0_shadow_table_contents(db, "v") == snapshot(name="1-init-shadow")

    assert exec(
        db, "UPDATE v SET b = 0, n = 11, f = 11.11, t = 'newtest1' where rowid = 1"
    )
    assert exec(db, "select * from v") == snapshot(name="general-update-contents")
    assert vec0_shadow_table_contents(db, "v") == snapshot(
        name="general-update-shaodnw"
    )

    # string update #1: long string updated to long string
    exec(db, "UPDATE v SET t = '1234567890123-updated' where rowid = 3")
    assert exec(db, "select * from v") == snapshot(name="string-update-1-contents")
    assert vec0_shadow_table_contents(db, "v") == snapshot(
        name="string-update-1-shadow"
    )

    # string update #2: short string updated to short string
    exec(db, "UPDATE v SET t = 'test2-short' where rowid = 2")
    assert exec(db, "select * from v") == snapshot(name="string-update-2-contents")
    assert vec0_shadow_table_contents(db, "v") == snapshot(
        name="string-update-2-shadow"
    )

    # string update #3: short string updated to long string
    exec(db, "UPDATE v SET t = 'test2-long-long-long' where rowid = 2")
    assert exec(db, "select * from v") == snapshot(name="string-update-3-contents")
    assert vec0_shadow_table_contents(db, "v") == snapshot(
        name="string-update-3-shadow"
    )

    # string update #4: long string updated to short string
    exec(db, "UPDATE v SET t = 'test2-shortx' where rowid = 2")
    assert exec(db, "select * from v") == snapshot(name="string-update-4-contents")
    assert vec0_shadow_table_contents(db, "v") == snapshot(
        name="string-update-4-shadow"
    )


def test_deletes(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], b boolean, n int, f float, t text, chunk_size=8)"
    )
    INSERT = "insert into v(rowid, vector, b, n, f, t) values (?, ?, ?, ?, ?, ?)"

    assert exec(db, INSERT, [1, b"\x11\x11\x11\x11", 1, 1, 1.1, "test1"]) == snapshot()
    assert exec(db, INSERT, [2, b"\x22\x22\x22\x22", 1, 2, 2.2, "test2"]) == snapshot()
    assert (
        exec(db, INSERT, [3, b"\x33\x33\x33\x33", 1, 3, 3.3, "1234567890123"])
        == snapshot()
    )

    assert exec(db, "select * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()

    assert exec(db, "DELETE FROM v where rowid = 1") == snapshot()
    assert exec(db, "select * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()

    assert exec(db, "DELETE FROM v where rowid = 3") == snapshot()
    assert exec(db, "select * from v") == snapshot()
    assert vec0_shadow_table_contents(db, "v") == snapshot()


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


SUPPORTS_VTAB_IN = sqlite3.sqlite_version_info[1] >= 38


@pytest.mark.skipif(
    not SUPPORTS_VTAB_IN, reason="requires vtab `x in (...)` support in SQLite >=3.38"
)
def test_vtab_in(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], n int, t text, b boolean, f float, chunk_size=8)"
    )
    db.executemany(
        "insert into v(rowid, vector, n, t, b, f) values (?, ?, ?, ?, ?, ?)",
        [
            (1, "[1]", 999, "aaaa", 0, 1.1),
            (2, "[2]", 555, "aaaa", 0, 1.1),
            (3, "[3]", 999, "aaaa", 0, 1.1),
            (4, "[4]", 555, "aaaa", 0, 1.1),
            (5, "[5]", 999, "zzzz", 0, 1.1),
            (6, "[6]", 555, "zzzz", 0, 1.1),
            (7, "[7]", 999, "zzzz", 0, 1.1),
            (8, "[8]", 555, "zzzz", 0, 1.1),
        ],
    )

    # EVIDENCE-OF: V15248_32086
    assert exec(
        db, "select *  from v where vector match '[0]' and k = 8 and b in (1, 0)"
    ) == snapshot(name="block-bool")

    assert exec(
        db, "select *  from v where vector match '[0]' and k = 8 and f in (1.1, 0.0)"
    ) == snapshot(name="block-float")

    assert exec(
        db,
        "select rowid, n, distance  from v where vector match '[0]' and k = 8 and n in (555, 999)",
    ) == snapshot(name="allow-int-all")
    assert exec(
        db,
        "select rowid, n, distance from v where vector match '[0]' and k = 8 and n in (555, -1, -2)",
    ) == snapshot(name="allow-int-superfluous")

    assert exec(
        db,
        "select rowid, t, distance  from v where vector match '[0]' and k = 8 and t in ('aaaa', 'zzzz')",
    ) == snapshot(name="allow-text-all")
    assert exec(
        db,
        "select rowid, t, distance from v where vector match '[0]' and k = 8 and t in ('aaaa', 'foo', 'bar')",
    ) == snapshot(name="allow-text-superfluous")


def test_vtab_in_long_text(db, snapshot):
    db.execute(
        "create virtual table v using vec0(vector float[1], t text, chunk_size=8)"
    )
    data = [
        (1, "aaaa"),
        (2, "aaaaaaaaaaaa_aaa"),
        (3, "bbbb"),
        (4, "bbbbbbbbbbbb_bbb"),
        (5, "cccc"),
        (6, "cccccccccccc_ccc"),
    ]
    db.executemany(
        "insert into v(rowid, vector, t) values (:rowid, printf('[%d]', :rowid), :vector)",
        [{"rowid": row[0], "vector": row[1]} for row in data],
    )

    for _, lookup in data:
        assert exec(
            db,
            "select rowid, t from v where vector match '[0]' and k = 10 and t in (?, 'nonsense')",
            [lookup],
        ) == snapshot(name=f"individual-{lookup}")

    assert exec(
        db,
        "select rowid, t from v where vector match '[0]' and k = 10 and t in (select value from json_each(?))",
        [json.dumps([row[1] for row in data])],
    ) == snapshot(name="all")


def test_idxstr(db, snapshot):
    db.execute(
        """
          create virtual table vec_movies using vec0(
            movie_id integer primary key,
            synopsis_embedding float[1],
            +title text,
            is_favorited boolean,
            genre text,
            num_reviews int,
            mean_rating float,
            chunk_size=8
          );
        """
    )

    assert (
        eqp(
            db,
            "select * from vec_movies where synopsis_embedding match '' and k = 0 and is_favorited = true",
        )
        == snapshot()
    )

    ops = ["<", ">", "<=", ">=", "!="]

    for op in ops:
        assert eqp(
            db,
            f"select * from vec_movies where synopsis_embedding match '' and k = 0 and genre {op} NULL",
        ) == snapshot(name=f"knn-constraint-text {op}")

    for op in ops:
        assert eqp(
            db,
            f"select * from vec_movies where synopsis_embedding match '' and k = 0 and num_reviews {op} NULL",
        ) == snapshot(name=f"knn-constraint-int {op}")

    for op in ops:
        assert eqp(
            db,
            f"select * from vec_movies where synopsis_embedding match '' and k = 0 and mean_rating {op} NULL",
        ) == snapshot(name=f"knn-constraint-float {op}")

    # for op in ops:
    #    assert eqp(
    #        db,
    #        f"select * from vec_movies where synopsis_embedding match '' and k = 0 and is_favorited {op} NULL",
    #    ) == snapshot(name=f"knn-constraint-boolean {op}")


def eqp(db, sql):
    o = OrderedDict()
    o["sql"] = sql
    o["plan"] = [
        dict(row) for row in db.execute(f"explain query plan {sql}").fetchall()
    ]
    for p in o["plan"]:
        # value is different on macos-aarch64 in github actions, not sure why
        del p["notused"]
    return o


def test_stress(db, snapshot):
    db.execute(
        """
          create virtual table vec_movies using vec0(
            movie_id integer primary key,
            synopsis_embedding float[1],
            +title text,
            is_favorited boolean,
            genre text,
            num_reviews int,
            mean_rating float,
            chunk_size=8
          );
        """
    )

    db.execute(
        """
          INSERT INTO vec_movies(movie_id, synopsis_embedding, is_favorited, genre, title, num_reviews, mean_rating)
          VALUES
            (1, '[1]', 0, 'horror', 'The Conjuring', 153, 4.6),
            (2, '[2]', 0, 'comedy', 'Dumb and Dumber', 382, 2.6),
            (3, '[3]', 0, 'scifi', 'Interstellar', 53, 5.0),
            (4, '[4]', 0, 'fantasy', 'The Lord of the Rings: The Fellowship of the Ring', 210, 4.2),
            (5, '[5]', 1, 'documentary', 'An Inconvenient Truth', 93, 3.4),
            (6, '[6]', 1, 'horror', 'Hereditary', 167, 4.7),
            (7, '[7]', 1, 'comedy', 'Anchorman: The Legend of Ron Burgundy', 482, 2.9),
            (8, '[8]', 0, 'scifi', 'Blade Runner 2049', 301, 5.0),
            (9, '[9]', 1, 'fantasy', 'Harry Potter and the Sorcerer''s Stone', 134, 4.1),
            (10, '[10]', 0, 'documentary', 'Free Solo', 66, 3.2),
            (11, '[11]', 1, 'horror', 'Get Out', 88, 4.9),
            (12, '[12]', 0, 'comedy', 'The Hangover', 59, 2.8),
            (13, '[13]', 1, 'scifi', 'The Matrix', 423, 4.5),
            (14, '[14]', 0, 'fantasy', 'Pan''s Labyrinth', 275, 3.6),
            (15, '[15]', 1, 'documentary', '13th', 191, 4.4),
            (16, '[16]', 0, 'horror', 'It Follows', 314, 4.3),
            (17, '[17]', 1, 'comedy', 'Step Brothers', 74, 3.0),
            (18, '[18]', 1, 'scifi', 'Inception', 201, 5.0),
            (19, '[19]', 1, 'fantasy', 'The Shape of Water', 399, 2.7),
            (20, '[20]', 1, 'documentary', 'Won''t You Be My Neighbor?', 186, 4.8),
            (21, '[21]', 1, 'scifi', 'Gravity', 342, 4.0),
            (22, '[22]', 1, 'scifi', 'Dune', 451, 4.4),
            (23, '[23]', 1, 'scifi', 'The Martian', 522, 4.6),
            (24, '[24]', 1, 'horror', 'A Quiet Place', 271, 4.3),
            (25, '[25]', 1, 'fantasy', 'The Chronicles of Narnia: The Lion, the Witch and the Wardrobe', 310, 3.9);

        """
    )

    assert vec0_shadow_table_contents(db, "vec_movies") == snapshot()
    assert (
        exec(
            db,
            """
          select
            movie_id,
            title,
            genre,
            num_reviews,
            mean_rating,
            is_favorited,
            distance
          from vec_movies
          where synopsis_embedding match '[15.5]'
            and genre = 'scifi'
            and num_reviews between 100 and 500
            and mean_rating > 3.5
            and k = 5;
        """,
        )
        == snapshot()
    )

    assert (
        exec(
            db,
            "select movie_id, genre, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and genre = 'horror'",
        )
        == snapshot()
    )
    assert (
        exec(
            db,
            "select movie_id, genre, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and genre = 'comedy'",
        )
        == snapshot()
    )
    assert (
        exec(
            db,
            "select movie_id, num_reviews, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and num_reviews between 100 and 500",
        )
        == snapshot()
    )
    assert (
        exec(
            db,
            "select movie_id, num_reviews, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and num_reviews >= 500",
        )
        == snapshot()
    )
    assert (
        exec(
            db,
            "select movie_id, mean_rating, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and mean_rating < 3.0",
        )
        == snapshot()
    )
    assert (
        exec(
            db,
            "select movie_id, mean_rating, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and mean_rating between 4.0 and 5.0",
        )
        == snapshot()
    )

    assert exec(
        db,
        "select movie_id, is_favorited, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and is_favorited = TRUE",
    ) == snapshot(name="bool-eq-true")
    assert exec(
        db,
        "select movie_id, is_favorited, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and is_favorited != TRUE",
    ) == snapshot(name="bool-ne-true")
    assert exec(
        db,
        "select movie_id, is_favorited, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and is_favorited = FALSE",
    ) == snapshot(name="bool-eq-false")
    assert exec(
        db,
        "select movie_id, is_favorited, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and is_favorited != FALSE",
    ) == snapshot(name="bool-ne-false")

    # EVIDENCE-OF: V10145_26984
    assert exec(
        db,
        "select movie_id, is_favorited, distance from vec_movies where synopsis_embedding match '[100]' and k = 5 and is_favorited >= 999",
    ) == snapshot(name="bool-other-op")


def test_errors(db, snapshot):
    db.execute("create virtual table v using vec0(vector float[1], t text)")
    db.execute("insert into v(vector, t) values ('[1]', 'aaaaaaaaaaaax')")

    assert exec(db, "select * from v") == snapshot()

    # EVIDENCE-OF: V15466_32305
    db.set_authorizer(
        authorizer_deny_on(sqlite3.SQLITE_READ, "v_metadatatext00", "data")
    )
    assert exec(db, "select * from v") == snapshot()


def authorizer_deny_on(operation, x1, x2=None):
    def _auth(op, p1, p2, p3, p4):
        if op == operation and p1 == x1 and p2 == x2:
            return sqlite3.SQLITE_DENY
        return sqlite3.SQLITE_OK

    return _auth


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
