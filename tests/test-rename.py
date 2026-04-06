import sqlite3
import pytest
from helpers import _f32


def _shadow_tables(db, prefix):
    """Return sorted list of shadow table names for a given prefix."""
    return sorted([
        row[0] for row in db.execute(
            r"select name from sqlite_master where name like ? escape '\' and type='table' order by 1",
            [f"{prefix}\\__%"],
        ).fetchall()
    ])


def test_rename_basic(db):
    """ALTER TABLE RENAME should rename vec0 table and all shadow tables."""
    db.execute("create virtual table v using vec0(a float[2], chunk_size=8)")
    db.execute("insert into v(rowid, a) values (1, ?)", [_f32([0.1, 0.2])])
    db.execute("insert into v(rowid, a) values (2, ?)", [_f32([0.3, 0.4])])

    assert _shadow_tables(db, "v") == [
        "v_chunks",
        "v_info",
        "v_rowids",
        "v_vector_chunks00",
    ]

    db.execute("ALTER TABLE v RENAME TO v2")

    # Old name should no longer work
    with pytest.raises(sqlite3.OperationalError):
        db.execute("select * from v")

    # New name should work and return the same data
    rows = db.execute(
        "select rowid, distance from v2 where a match ? and k=10",
        [_f32([0.1, 0.2])],
    ).fetchall()
    assert len(rows) == 2
    assert rows[0][0] == 1  # closest match

    # Shadow tables should all be renamed
    assert _shadow_tables(db, "v2") == [
        "v2_chunks",
        "v2_info",
        "v2_rowids",
        "v2_vector_chunks00",
    ]

    # No old shadow tables should remain
    assert _shadow_tables(db, "v") == []


def test_rename_insert_after(db):
    """Inserts and queries should work after rename."""
    db.execute("create virtual table v using vec0(a float[2], chunk_size=8)")
    db.execute("insert into v(rowid, a) values (1, ?)", [_f32([0.1, 0.2])])
    db.execute("ALTER TABLE v RENAME TO v2")

    # Insert into renamed table
    db.execute("insert into v2(rowid, a) values (2, ?)", [_f32([0.3, 0.4])])

    rows = db.execute(
        "select rowid from v2 where a match ? and k=10",
        [_f32([0.3, 0.4])],
    ).fetchall()
    assert len(rows) == 2
    assert rows[0][0] == 2


def test_rename_delete_after(db):
    """Deletes should work after rename."""
    db.execute("create virtual table v using vec0(a float[2], chunk_size=8)")
    db.execute("insert into v(rowid, a) values (1, ?)", [_f32([0.1, 0.2])])
    db.execute("insert into v(rowid, a) values (2, ?)", [_f32([0.3, 0.4])])
    db.execute("ALTER TABLE v RENAME TO v2")

    db.execute("delete from v2 where rowid = 1")
    rows = db.execute(
        "select rowid from v2 where a match ? and k=10",
        [_f32([0.3, 0.4])],
    ).fetchall()
    assert len(rows) == 1
    assert rows[0][0] == 2


def test_rename_with_auxiliary(db):
    """Rename should also rename the _auxiliary shadow table."""
    db.execute(
        "create virtual table v using vec0(a float[2], +name text, chunk_size=8)"
    )
    db.execute(
        "insert into v(rowid, a, name) values (1, ?, 'hello')",
        [_f32([0.1, 0.2])],
    )

    assert _shadow_tables(db, "v") == [
        "v_auxiliary",
        "v_chunks",
        "v_info",
        "v_rowids",
        "v_vector_chunks00",
    ]

    db.execute("ALTER TABLE v RENAME TO v2")

    # Auxiliary data should be accessible
    rows = db.execute(
        "select rowid, name from v2 where a match ? and k=10",
        [_f32([0.1, 0.2])],
    ).fetchall()
    assert rows[0][0] == 1
    assert rows[0][1] == "hello"

    assert _shadow_tables(db, "v2") == [
        "v2_auxiliary",
        "v2_chunks",
        "v2_info",
        "v2_rowids",
        "v2_vector_chunks00",
    ]
    assert _shadow_tables(db, "v") == []


def test_rename_with_metadata(db):
    """Rename should also rename metadata shadow tables."""
    db.execute(
        "create virtual table v using vec0(a float[2], tag text, chunk_size=8)"
    )
    db.execute(
        "insert into v(rowid, a, tag) values (1, ?, 'a')",
        [_f32([0.1, 0.2])],
    )

    assert _shadow_tables(db, "v") == [
        "v_chunks",
        "v_info",
        "v_metadatachunks00",
        "v_metadatatext00",
        "v_rowids",
        "v_vector_chunks00",
    ]

    db.execute("ALTER TABLE v RENAME TO v2")

    rows = db.execute(
        "select rowid, tag from v2 where a match ? and k=10",
        [_f32([0.1, 0.2])],
    ).fetchall()
    assert rows[0][0] == 1
    assert rows[0][1] == "a"

    assert _shadow_tables(db, "v2") == [
        "v2_chunks",
        "v2_info",
        "v2_metadatachunks00",
        "v2_metadatatext00",
        "v2_rowids",
        "v2_vector_chunks00",
    ]
    assert _shadow_tables(db, "v") == []


def test_rename_drop_after(db):
    """DROP TABLE should work on a renamed table."""
    db.execute("create virtual table v using vec0(a float[2], chunk_size=8)")
    db.execute("insert into v(rowid, a) values (1, ?)", [_f32([0.1, 0.2])])
    db.execute("ALTER TABLE v RENAME TO v2")
    db.execute("DROP TABLE v2")

    # Nothing should remain
    assert _shadow_tables(db, "v2") == []
