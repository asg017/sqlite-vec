import sqlite3
import pytest
from helpers import exec


@pytest.mark.skipif(
    sqlite3.sqlite_version_info[1] < 37,
    reason="pragma_table_list was added in SQLite 3.37",
)
def test_shadow(db, snapshot):
    db.execute(
        "create virtual table v using vec0(a float[1], partition text partition key, metadata text, +name text, chunk_size=8)"
    )
    assert exec(db, "select * from sqlite_master order by name") == snapshot()
    assert (
        exec(db, "select * from pragma_table_list where type = 'shadow' order by name") == snapshot()
    )

    db.execute("drop table v;")
    assert (
        exec(db, "select * from pragma_table_list where type = 'shadow' order by name") == snapshot()
    )


def test_info(db, snapshot):
    db.execute("create virtual table v using vec0(a float[1])")
    assert exec(db, "select key, typeof(value) from v_info order by 1") == snapshot()


def test_command_column_name_conflict(db):
    """Table name matching a column name should error (command column conflict)."""
    # This would conflict: hidden command column 'embeddings' vs vector column 'embeddings'
    with pytest.raises(sqlite3.OperationalError, match="conflicts with table name"):
        db.execute(
            "create virtual table embeddings using vec0(embeddings float[4])"
        )

    # Different names should work fine
    db.execute("create virtual table t using vec0(embeddings float[4])")


