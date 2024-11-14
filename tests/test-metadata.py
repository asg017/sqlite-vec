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


def test_stress(db, snapshot):
    db.execute(
        """
          create virtual table vec_movies using vec0(
            movie_id integer primary key,
            synopsis_embedding float[1],
            +title text,
            genre text,
            num_reviews int,
            mean_rating float,
            chunk_size=8
          );
        """
    )

    db.execute(
        """
          INSERT INTO vec_movies(movie_id, synopsis_embedding, genre, title, num_reviews, mean_rating)
          VALUES
            (1, '[1]', 'horror', 'The Conjuring', 153, 4.6),
            (2, '[2]', 'comedy', 'Dumb and Dumber', 382, 2.6),
            (3, '[3]', 'scifi', 'Interstellar', 53, 5.0),
            (4, '[4]', 'fantasy', 'The Lord of the Rings: The Fellowship of the Ring', 210, 4.2),
            (5, '[5]', 'documentary', 'An Inconvenient Truth', 93, 3.4),
            (6, '[6]', 'horror', 'Hereditary', 167, 4.7),
            (7, '[7]', 'comedy', 'Anchorman: The Legend of Ron Burgundy', 482, 2.9),
            (8, '[8]', 'scifi', 'Blade Runner 2049', 301, 5.0),
            (9, '[9]', 'fantasy', 'Harry Potter and the Sorcerer''s Stone', 134, 4.1),
            (10, '[10]', 'documentary', 'Free Solo', 66, 3.2),
            (11, '[11]', 'horror', 'Get Out', 88, 4.9),
            (12, '[12]', 'comedy', 'The Hangover', 59, 2.8),
            (13, '[13]', 'scifi', 'The Matrix', 423, 4.5),
            (14, '[14]', 'fantasy', 'Pan''s Labyrinth', 275, 3.6),
            (15, '[15]', 'documentary', '13th', 191, 4.4),
            (16, '[16]', 'horror', 'It Follows', 314, 4.3),
            (17, '[17]', 'comedy', 'Step Brothers', 74, 3.0),
            (18, '[18]', 'scifi', 'Inception', 201, 5.0),
            (19, '[19]', 'fantasy', 'The Shape of Water', 399, 2.7),
            (20, '[20]', 'documentary', 'Won''t You Be My Neighbor?', 186, 4.8),
            (21, '[21]', 'scifi', 'Gravity', 342, 4.0),
            (22, '[22]', 'scifi', 'Dune', 451, 4.4),
            (23, '[23]', 'scifi', 'The Martian', 522, 4.6),
            (24, '[24]', 'horror', 'A Quiet Place', 271, 4.3),
            (25, '[25]', 'fantasy', 'The Chronicles of Narnia: The Lion, the Witch and the Wardrobe', 310, 3.9);

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
