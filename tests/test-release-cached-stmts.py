import sqlite3
import pytest
from helpers import _f32


def test_release_cached_stmts_basic(db):
    """The release-cached-stmts command should succeed and not affect data."""
    db.execute("create virtual table v using vec0(a float[2], chunk_size=8)")
    db.execute("insert into v(rowid, a) values (1, ?)", [_f32([0.1, 0.2])])
    db.execute("insert into v(rowid, a) values (2, ?)", [_f32([0.3, 0.4])])

    db.execute("insert into v(v) values ('release-cached-stmts')")

    # Data should still be there and queryable; vec0's cached statements
    # are re-prepared on demand.
    rows = db.execute(
        "select rowid from v where a match ? and k=10",
        [_f32([0.1, 0.2])],
    ).fetchall()
    assert sorted(r[0] for r in rows) == [1, 2]


def test_release_cached_stmts_before_any_use(db):
    """Issuing the command before any inserts should be a no-op."""
    db.execute("create virtual table v using vec0(a float[2], chunk_size=8)")
    db.execute("insert into v(v) values ('release-cached-stmts')")
    # Inserts and queries still work after.
    db.execute("insert into v(rowid, a) values (1, ?)", [_f32([0.1, 0.2])])
    rows = db.execute(
        "select rowid from v where a match ? and k=10",
        [_f32([0.1, 0.2])],
    ).fetchall()
    assert rows[0][0] == 1


def test_release_cached_stmts_diskann(db):
    """Works on DiskANN-indexed tables (the original motivating case)."""
    db.execute("""
        create virtual table v using vec0(
            a float[8] indexed by diskann(neighbor_quantizer=binary)
        )
    """)
    db.execute("insert into v(rowid, a) values (1, ?)", [_f32([0.1] * 8)])
    db.execute("insert into v(v) values ('release-cached-stmts')")
    rows = db.execute(
        "select rowid from v where a match ? and k=10",
        [_f32([0.1] * 8)],
    ).fetchall()
    assert rows[0][0] == 1


def test_release_cached_stmts_unknown_subcommand(db):
    """Unknown vec0 commands should still error as before."""
    db.execute("create virtual table v using vec0(a float[2], chunk_size=8)")
    with pytest.raises(sqlite3.OperationalError, match="unknown vec0 command"):
        db.execute("insert into v(v) values ('not-a-real-command')")
