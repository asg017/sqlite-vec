import os


def test_delete_clears_rowid_and_vectors():
    try:
        import pysqlite3 as sqlite3  # uses bundled modern SQLite with extension loading
    except ImportError:  # fallback if not available
        import sqlite3

    db = sqlite3.connect(":memory:")
    db.row_factory = sqlite3.Row
    if hasattr(db, "enable_load_extension"):
        db.enable_load_extension(True)
    ext = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "dist", "vec0"))
    try:
        # Explicit entrypoint to avoid relying on default name
        db.load_extension(ext, "sqlite3_vec_init")
    except Exception:
        # Some loaders accept missing suffix path without explicit entrypoint
        db.load_extension(ext)

    # One vector column with 1 dimension (4 bytes per vector), chunk_size=8
    db.execute("create virtual table v using vec0(vector float[1], chunk_size=8)")

    # Insert two rows with distinct raw vector bytes
    db.execute(
        "insert into v(rowid, vector) values (?, ?)",
        [1, b"\x11\x11\x11\x11"],
    )
    db.execute(
        "insert into v(rowid, vector) values (?, ?)",
        [2, b"\x22\x22\x22\x22"],
    )

    # Sanity check pre-delete: validity has first two bits set (0b00000011)
    row = db.execute("select validity, rowids from v_chunks").fetchone()
    assert row is not None
    assert row[0] == b"\x03"

    # Delete rowid=1
    db.execute("delete from v where rowid = 1")

    # After delete, validity should only have bit 1 set (0b00000010)
    row = db.execute("select validity, rowids from v_chunks").fetchone()
    assert row[0] == b"\x02"

    # Rowids BLOB: first 8 bytes (slot 0) must be zero; second (slot 1) must be rowid=2
    rowids = row[1]
    assert isinstance(rowids, (bytes, bytearray))
    assert len(rowids) == 8 * 8  # chunk_size * sizeof(i64)
    assert rowids[0:8] == b"\x00" * 8
    assert rowids[8:16] == b"\x02\x00\x00\x00\x00\x00\x00\x00"

    # Vectors BLOB for the first (and only) vector column
    vectors_row = db.execute("select vectors from v_vector_chunks00").fetchone()
    vectors = vectors_row[0]
    # chunk_size (8) * 4 bytes per float32 = 32 bytes
    assert len(vectors) == 32
    # Slot 0 cleared to zeros, slot 1 left as inserted (0x22 0x22 0x22 0x22)
    assert vectors[0:4] == b"\x00\x00\x00\x00"
    assert vectors[4:8] == b"\x22\x22\x22\x22"


def test_vacuum_shrinks_file(tmp_path):
    try:
        import pysqlite3 as sqlite3
    except ImportError:
        import sqlite3

    db_path = tmp_path / "vacuum_vec.db"

    con = sqlite3.connect(str(db_path))
    con.row_factory = sqlite3.Row
    if hasattr(con, "enable_load_extension"):
        con.enable_load_extension(True)
        ext = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "dist", "vec0"))
        try:
            con.load_extension(ext)
        except Exception:
            # Some platforms require the full filename or default entrypoint; fallback already tried
            con.load_extension(ext)

    # Use a larger chunk_size to inflate file size more clearly
    con.execute("create virtual table v using vec0(vector float[1], chunk_size=4096)")

    # Insert a decent number of rows to grow the DB
    N = 10000
    con.executemany(
        "insert into v(rowid, vector) values(?, ?)",
        ((i, b"\x11\x11\x11\x11") for i in range(1, N + 1)),
    )
    con.commit()

    size_after_insert = os.stat(db_path).st_size
    assert size_after_insert > 0

    # Drop the table to free its pages, then VACUUM to rewrite/shrink the file
    con.execute("drop table v")
    con.commit()
    con.execute("VACUUM")
    con.close()

    size_after_vacuum = os.stat(db_path).st_size

    # File should shrink after dropping the table and VACUUM
    assert size_after_vacuum < size_after_insert
