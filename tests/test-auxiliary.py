import sqlite3
import struct
import pytest
from helpers import exec, vec0_shadow_table_contents, _f32


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


# ======================================================================
# Auxiliary columns with non-flat indexes
# ======================================================================


def test_rescore_aux_shadow_tables(db, snapshot):
    """Rescore + aux column: verify shadow tables are created correctly."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  emb float[128] indexed by rescore(quantizer=bit),"
        "  +label text,"
        "  +score float"
        ")"
    )
    assert exec(db, "SELECT name, sql FROM sqlite_master WHERE type='table' AND name LIKE 't_%' ORDER BY name") == snapshot(
        name="rescore aux shadow tables"
    )


def test_rescore_aux_insert_knn(db, snapshot):
    """Insert with aux data, KNN should return aux column values."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  emb float[128] indexed by rescore(quantizer=bit),"
        "  +label text"
        ")"
    )
    import random
    random.seed(77)
    data = [
        ("alpha", [random.gauss(0, 1) for _ in range(128)]),
        ("beta", [random.gauss(0, 1) for _ in range(128)]),
        ("gamma", [random.gauss(0, 1) for _ in range(128)]),
    ]
    for label, vec in data:
        db.execute(
            "INSERT INTO t(emb, label) VALUES (?, ?)",
            [_f32(vec), label],
        )

    assert exec(db, "SELECT rowid, label FROM t ORDER BY rowid") == snapshot(
        name="rescore aux select all"
    )
    assert vec0_shadow_table_contents(db, "t", skip_info=True) == snapshot(
        name="rescore aux shadow contents"
    )

    # KNN should include aux column, "alpha" closest to its own vector
    rows = db.execute(
        "SELECT label, distance FROM t WHERE emb MATCH ? ORDER BY distance LIMIT 3",
        [_f32(data[0][1])],
    ).fetchall()
    assert len(rows) == 3
    assert rows[0][0] == "alpha"


def test_rescore_aux_update(db):
    """UPDATE aux column on rescore table should work without affecting vectors."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  emb float[128] indexed by rescore(quantizer=bit),"
        "  +label text"
        ")"
    )
    import random
    random.seed(88)
    vec = [random.gauss(0, 1) for _ in range(128)]
    db.execute("INSERT INTO t(rowid, emb, label) VALUES (1, ?, 'original')", [_f32(vec)])
    db.execute("UPDATE t SET label = 'updated' WHERE rowid = 1")

    assert db.execute("SELECT label FROM t WHERE rowid = 1").fetchone()[0] == "updated"

    # KNN still works with updated aux
    rows = db.execute(
        "SELECT rowid, label FROM t WHERE emb MATCH ? ORDER BY distance LIMIT 1",
        [_f32(vec)],
    ).fetchall()
    assert rows[0][0] == 1
    assert rows[0][1] == "updated"


def test_rescore_aux_delete(db, snapshot):
    """DELETE should remove aux data from shadow table."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  emb float[128] indexed by rescore(quantizer=bit),"
        "  +label text"
        ")"
    )
    import random
    random.seed(99)
    for i in range(5):
        db.execute(
            "INSERT INTO t(rowid, emb, label) VALUES (?, ?, ?)",
            [i + 1, _f32([random.gauss(0, 1) for _ in range(128)]), f"item-{i+1}"],
        )

    db.execute("DELETE FROM t WHERE rowid = 3")

    assert exec(db, "SELECT rowid, label FROM t ORDER BY rowid") == snapshot(
        name="rescore aux after delete"
    )
    assert exec(db, "SELECT rowid, value00 FROM t_auxiliary ORDER BY rowid") == snapshot(
        name="rescore aux shadow after delete"
    )


def test_diskann_aux_shadow_tables(db, snapshot):
    """DiskANN + aux column: verify shadow tables are created correctly."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8),
            +label text,
            +score float
        )
    """)
    assert exec(db, "SELECT name, sql FROM sqlite_master WHERE type='table' AND name LIKE 't_%' ORDER BY name") == snapshot(
        name="diskann aux shadow tables"
    )


def test_diskann_aux_insert_knn(db, snapshot):
    """DiskANN + aux: insert, KNN, verify aux values returned."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8),
            +label text
        )
    """)
    data = [
        ("red", [1, 0, 0, 0, 0, 0, 0, 0]),
        ("green", [0, 1, 0, 0, 0, 0, 0, 0]),
        ("blue", [0, 0, 1, 0, 0, 0, 0, 0]),
    ]
    for label, vec in data:
        db.execute("INSERT INTO t(emb, label) VALUES (?, ?)", [_f32(vec), label])

    assert exec(db, "SELECT rowid, label FROM t ORDER BY rowid") == snapshot(
        name="diskann aux select all"
    )
    assert vec0_shadow_table_contents(db, "t", skip_info=True) == snapshot(
        name="diskann aux shadow contents"
    )

    rows = db.execute(
        "SELECT label, distance FROM t WHERE emb MATCH ? AND k = 3",
        [_f32([1, 0, 0, 0, 0, 0, 0, 0])],
    ).fetchall()
    assert len(rows) >= 1
    assert rows[0][0] == "red"


def test_diskann_aux_update_and_delete(db, snapshot):
    """DiskANN + aux: update aux column, delete row, verify cleanup."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8),
            +label text
        )
    """)
    for i in range(5):
        vec = [0.0] * 8
        vec[i % 8] = 1.0
        db.execute(
            "INSERT INTO t(rowid, emb, label) VALUES (?, ?, ?)",
            [i + 1, _f32(vec), f"item-{i+1}"],
        )

    db.execute("UPDATE t SET label = 'UPDATED' WHERE rowid = 2")
    db.execute("DELETE FROM t WHERE rowid = 3")

    assert exec(db, "SELECT rowid, label FROM t ORDER BY rowid") == snapshot(
        name="diskann aux after update+delete"
    )
    assert exec(db, "SELECT rowid, value00 FROM t_auxiliary ORDER BY rowid") == snapshot(
        name="diskann aux shadow after update+delete"
    )


def test_diskann_aux_drop_cleans_all(db):
    """DROP TABLE should remove aux shadow table too."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8),
            +label text
        )
    """)
    db.execute("INSERT INTO t(emb, label) VALUES (?, 'test')", [_f32([1]*8)])
    db.execute("DROP TABLE t")

    tables = [r[0] for r in db.execute(
        "SELECT name FROM sqlite_master WHERE name LIKE 't_%'"
    ).fetchall()]
    assert "t_auxiliary" not in tables

