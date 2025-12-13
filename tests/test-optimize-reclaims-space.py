import os
import pytest


def load_vec_extension(db):
    if not hasattr(db, "load_extension"):
        pytest.skip("SQLite build does not support loading extensions")
    if hasattr(db, "enable_load_extension"):
        db.enable_load_extension(True)
    ext = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "dist", "vec0"))
    try:
        # Explicit entrypoint to avoid relying on default name
        db.load_extension(ext, "sqlite3_vec_init")
    except Exception:
        # Some loaders accept missing suffix path without explicit entrypoint
        db.load_extension(ext)


def pragma_int(db, sql):
    return db.execute(sql).fetchone()[0]


def test_optimize_reclaims_pages_with_autovacuum_incremental(tmp_path):
    try:
        import pysqlite3 as sqlite3  # uses bundled modern SQLite with extension loading
    except ImportError:  # fallback if not available
        import sqlite3

    db_path = tmp_path / "optimize_reclaim.db"

    db = sqlite3.connect(str(db_path))
    db.row_factory = sqlite3.Row

    # Enable autovacuum before creating vec tables; VACUUM is safe here because
    # the database only has SQLite system tables at this point.
    db.execute("PRAGMA auto_vacuum = INCREMENTAL")
    db.execute("VACUUM")
    db.execute("PRAGMA journal_mode = WAL")

    load_vec_extension(db)

    # Use a modest chunk_size so we create several chunks and can reclaim them
    db.execute("create virtual table v using vec0(vector float[1], chunk_size=64)")

    # Insert 256 rows (four chunks at chunk_size=64)
    db.executemany(
        "insert into v(rowid, vector) values(?, ?)",
        ((i, b"\x11\x11\x11\x11") for i in range(1, 257)),
    )
    db.commit()
    chunk_rows_after_insert = pragma_int(db, "select count(*) from v_chunks")

    # Delete half the rows to create free space inside vec shadow tables
    db.execute("delete from v where rowid > 128")
    db.commit()
    chunk_rows_after_delete = pragma_int(db, "select count(*) from v_chunks")

    # Compact vec shadow tables and reclaim file pages with autovacuum
    db.execute("insert into v(v) values('optimize')")
    db.commit()
    db.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    db.execute("PRAGMA incremental_vacuum")
    chunk_rows_after_optimize = pragma_int(db, "select count(*) from v_chunks")

    # Initially 256 rows at chunk_size 64 -> 4 chunk rows. After deleting half,
    # optimize should compact to 2 chunk rows.
    assert chunk_rows_after_insert == 4
    assert chunk_rows_after_delete == 4
    assert chunk_rows_after_optimize == 2


def test_optimize_then_vacuum_allows_future_writes(tmp_path):
    try:
        import pysqlite3 as sqlite3  # uses bundled modern SQLite with extension loading
    except ImportError:
        import sqlite3

    db_path = tmp_path / "vacuum_safe.db"

    db = sqlite3.connect(str(db_path))
    db.row_factory = sqlite3.Row
    load_vec_extension(db)

    db.execute("PRAGMA journal_mode = WAL")
    db.execute("create virtual table v using vec0(vector float[1], chunk_size=8)")

    # 32 rows -> 4 chunks at chunk_size=8
    db.executemany(
        "insert into v(rowid, vector) values(?, ?)",
        ((i, b"\x11\x11\x11\x11") for i in range(1, 33)),
    )
    db.commit()

    # Delete half, then compact
    db.execute("delete from v where rowid > 16")
    db.commit()
    db.execute("insert into v(v) values('optimize')")
    db.commit()

    # Checkpoint before VACUUM; capture size/page count
    db.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    size_before_vacuum = db.execute("PRAGMA page_count").fetchone()[0]
    disk_bytes_before = os.stat(db_path).st_size

    # VACUUM should preserve shadow table consistency
    db.execute("VACUUM")
    db.execute("PRAGMA journal_mode = WAL")
    size_after_vacuum = db.execute("PRAGMA page_count").fetchone()[0]
    disk_bytes_after = os.stat(db_path).st_size

    # Insert more rows after VACUUM; expect no blob-open failures
    db.executemany(
        "insert into v(rowid, vector) values(?, ?)",
        ((i, b"\x22\x22\x22\x22") for i in range(17, 25)),
    )
    db.commit()

    # Row count and chunk rows should be consistent (3 chunks cover 24 rows)
    assert db.execute("select count(*) from v").fetchone()[0] == 24
    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 3
    # File/page count should not grow; should shrink when pages are freed
    assert size_after_vacuum <= size_before_vacuum
    assert disk_bytes_after <= disk_bytes_before
