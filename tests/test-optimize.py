import sqlite3
import struct
import pytest
from helpers import _f32, _i64, _int8, exec


def test_optimize_basic(db):
    """Insert 16 rows (2 chunks of 8), delete 6 from chunk 1, optimize → 1 chunk."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i in range(1, 17):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 2

    # Delete 6 from chunk 1 (rows 1-6), leaving 2 live in chunk 1
    for i in range(1, 7):
        db.execute("delete from v where rowid = ?", [i])

    # 10 live rows: 2 in chunk 1, 8 in chunk 2
    assert db.execute("select count(*) from v").fetchone()[0] == 10

    db.execute("insert into v(v) values ('optimize')")

    # After optimize: 10 entries should fit in 2 chunks (8+2)
    # but the 8 from chunk 2 can't all be moved into 6 free slots of chunk 1,
    # so we should still have at least 2 chunks.
    # Actually: left=chunk1(6 free), right=chunk2(8 live)
    # Move 6 entries from chunk2 → chunk1, chunk2 still has 2 live → 2 chunks remain
    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 2

    # All 10 rows still queryable
    rows = db.execute("select rowid from v order by rowid").fetchall()
    assert [r[0] for r in rows] == list(range(7, 17))

    for i in range(7, 17):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)


def test_optimize_full_compaction(db):
    """Insert 24 rows (3 chunks of 8), delete all but 4, optimize → 1 chunk."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i in range(1, 25):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 3

    # Keep rows 1,2,3,4 in chunk 1, delete everything else
    for i in range(5, 25):
        db.execute("delete from v where rowid = ?", [i])

    assert db.execute("select count(*) from v").fetchone()[0] == 4

    db.execute("insert into v(v) values ('optimize')")

    # Only 1 chunk should remain
    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 1
    assert db.execute("select count(*) from v_vector_chunks00").fetchone()[0] == 1

    # All 4 rows still queryable
    for i in range(1, 5):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)


def test_optimize_noop_clean_table(db):
    """Insert exactly 8 rows (1 full chunk), optimize is a no-op."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i in range(1, 9):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    db.execute("insert into v(v) values ('optimize')")

    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 1
    for i in range(1, 9):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)


def test_optimize_empty_table(db):
    """Optimize on empty table is a no-op."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")
    db.execute("insert into v(v) values ('optimize')")
    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 0


def test_optimize_knn_still_works(db):
    """After optimize, KNN queries return correct results."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i in range(1, 17):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    # Delete first 6 rows
    for i in range(1, 7):
        db.execute("delete from v where rowid = ?", [i])

    db.execute("insert into v(v) values ('optimize')")

    # KNN query for vector closest to [7,7,7,7]
    knn = db.execute(
        "select rowid, distance from v where emb match ? and k = 1",
        [_f32([7.0, 7.0, 7.0, 7.0])],
    ).fetchall()
    assert len(knn) == 1
    assert knn[0][0] == 7


def test_optimize_fullscan_still_works(db):
    """After optimize, SELECT * returns all rows."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i in range(1, 17):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    for i in range(1, 7):
        db.execute("delete from v where rowid = ?", [i])

    db.execute("insert into v(v) values ('optimize')")

    rows = db.execute("select rowid, emb from v order by rowid").fetchall()
    assert len(rows) == 10
    for row in rows:
        assert row[1] == _f32([float(row[0])] * 4)


def test_optimize_partitioned(db):
    """Two partitions each fragmented → optimized independently."""
    db.execute(
        "create virtual table v using vec0("
        "part text partition key, emb float[4], chunk_size=8"
        ")"
    )

    # Partition A: 16 rows (2 chunks)
    for i in range(1, 17):
        db.execute(
            "insert into v(rowid, part, emb) values (?, 'A', ?)",
            [i, _f32([float(i)] * 4)],
        )

    # Partition B: 16 rows (2 chunks)
    for i in range(17, 33):
        db.execute(
            "insert into v(rowid, part, emb) values (?, 'B', ?)",
            [i, _f32([float(i)] * 4)],
        )

    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 4

    # Delete 7 from each partition's first chunk
    for i in range(1, 8):
        db.execute("delete from v where rowid = ?", [i])
    for i in range(17, 24):
        db.execute("delete from v where rowid = ?", [i])

    db.execute("insert into v(v) values ('optimize')")

    # Each partition had 9 live entries: fits in 2 chunks each → 4 total
    # (7 free in chunk1 + 8 live in chunk2 → move 7 → chunk2 has 1 live → still 2 chunks)
    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 4

    # All remaining rows still accessible
    for i in range(8, 17):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)
    for i in range(24, 33):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)


def test_optimize_with_metadata(db):
    """Optimize with integer, float, boolean, and short text metadata."""
    db.execute(
        "create virtual table v using vec0("
        "emb float[4], "
        "m_bool boolean, "
        "m_int integer, "
        "m_float float, "
        "m_text text, "
        "chunk_size=8"
        ")"
    )

    for i in range(1, 17):
        db.execute(
            "insert into v(rowid, emb, m_bool, m_int, m_float, m_text) "
            "values (?, ?, ?, ?, ?, ?)",
            [i, _f32([float(i)] * 4), i % 2 == 0, i * 10, float(i) / 2.0, f"t{i}"],
        )

    for i in range(1, 7):
        db.execute("delete from v where rowid = ?", [i])

    db.execute("insert into v(v) values ('optimize')")

    # Verify metadata preserved
    for i in range(7, 17):
        row = db.execute(
            "select m_bool, m_int, m_float, m_text from v where rowid = ?", [i]
        ).fetchone()
        assert row[0] == (1 if i % 2 == 0 else 0), f"bool mismatch at rowid {i}"
        assert row[1] == i * 10, f"int mismatch at rowid {i}"
        assert abs(row[2] - float(i) / 2.0) < 1e-6, f"float mismatch at rowid {i}"
        assert row[3] == f"t{i}", f"text mismatch at rowid {i}"


def test_optimize_with_auxiliary(db):
    """Aux data still accessible after optimize (keyed by rowid, no move needed)."""
    db.execute(
        "create virtual table v using vec0("
        "emb float[4], +aux_text text, chunk_size=8"
        ")"
    )

    for i in range(1, 17):
        db.execute(
            "insert into v(rowid, emb, aux_text) values (?, ?, ?)",
            [i, _f32([float(i)] * 4), f"aux_{i}"],
        )

    for i in range(1, 7):
        db.execute("delete from v where rowid = ?", [i])

    db.execute("insert into v(v) values ('optimize')")

    for i in range(7, 17):
        row = db.execute(
            "select aux_text from v where rowid = ?", [i]
        ).fetchone()
        assert row[0] == f"aux_{i}"


def test_optimize_text_pk(db):
    """Rowids correctly updated, text PKs still work after optimize."""
    db.execute(
        "create virtual table v using vec0("
        "id text primary key, emb float[4], chunk_size=8"
        ")"
    )

    for i in range(1, 17):
        db.execute(
            "insert into v(id, emb) values (?, ?)",
            [f"doc_{i}", _f32([float(i)] * 4)],
        )

    for i in range(1, 7):
        db.execute("delete from v where id = ?", [f"doc_{i}"])

    db.execute("insert into v(v) values ('optimize')")

    for i in range(7, 17):
        row = db.execute(
            "select emb from v where id = ?", [f"doc_{i}"]
        ).fetchone()
        assert row[0] == _f32([float(i)] * 4)


def _file_db(tmp_path):
    """Open a file-backed DB (required for page_count to shrink after VACUUM)."""
    db = sqlite3.connect(str(tmp_path / "test.db"))
    db.row_factory = sqlite3.Row
    db.enable_load_extension(True)
    db.load_extension("dist/vec0")
    db.enable_load_extension(False)
    return db


def test_optimize_disk_space_reclaimed(tmp_path):
    """PRAGMA page_count decreases after optimize + VACUUM."""
    dims = 256
    db = _file_db(tmp_path)
    db.execute(f"create virtual table v using vec0(emb float[{dims}], chunk_size=8)")

    for i in range(1, 25):  # 3 full chunks of 8
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * dims)],
        )
    db.commit()
    pages_before = db.execute("pragma page_count").fetchone()[0]

    # Delete 20 of 24 rows (leaving 4 live)
    for i in range(5, 25):
        db.execute("delete from v where rowid = ?", [i])

    db.execute("insert into v(v) values ('optimize')")
    db.commit()

    db.execute("vacuum")
    pages_after = db.execute("pragma page_count").fetchone()[0]
    assert pages_after < pages_before, (
        f"page_count should shrink after optimize+vacuum: "
        f"{pages_before} -> {pages_after}"
    )

    # Remaining rows still work
    for i in range(1, 5):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * dims)
    db.close()


def test_optimize_unknown_command(db):
    """Unknown command gives SQLITE_ERROR with message."""
    result = exec(db, "insert into v(v) values ('bogus')")
    # We need a table first
    db.execute("create virtual table v2 using vec0(emb float[4], chunk_size=8)")
    result = exec(db, "insert into v2(v2) values ('bogus')")
    assert "error" in result
    assert "Unknown" in result["message"] or "unknown" in result["message"]


def test_optimize_insert_after(db):
    """Inserting new rows after optimize still works correctly."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i in range(1, 17):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    for i in range(1, 7):
        db.execute("delete from v where rowid = ?", [i])

    db.execute("insert into v(v) values ('optimize')")

    # Insert new rows after optimize
    for i in range(100, 108):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    # Both old and new rows queryable
    for i in range(7, 17):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)
    for i in range(100, 108):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)


def test_optimize_multiple_moves_from_same_chunk(db):
    """Ensure multiple live entries in the same source chunk are all moved."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    # 24 rows = 3 chunks of 8
    for i in range(1, 25):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    # Delete all of chunk 1 (1-8) — leaves 8 free slots
    for i in range(1, 9):
        db.execute("delete from v where rowid = ?", [i])

    # Delete half of chunk 2 (9-12) — leaves 4 live in chunk 2, 8 live in chunk 3
    for i in range(9, 13):
        db.execute("delete from v where rowid = ?", [i])

    # 12 live rows total: 4 in chunk 2 (offsets 4-7), 8 in chunk 3 (offsets 0-7)
    assert db.execute("select count(*) from v").fetchone()[0] == 12

    db.execute("insert into v(v) values ('optimize')")

    # After optimize: all 12 should fit in 2 chunks, chunk 3 should be emptied
    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 2

    # All remaining rows still queryable with correct vectors
    for i in range(13, 25):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)


def test_optimize_scattered_deletes(db):
    """Delete every other row to create scattered free slots across chunks."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i in range(1, 25):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    # Delete even rows: 2,4,6,8,10,12,14,16,18,20,22,24
    for i in range(2, 25, 2):
        db.execute("delete from v where rowid = ?", [i])

    # 12 live rows scattered across 3 chunks
    assert db.execute("select count(*) from v").fetchone()[0] == 12

    db.execute("insert into v(v) values ('optimize')")

    # After optimize: 12 rows should fit in 2 chunks
    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 2

    # All remaining odd rows still queryable
    for i in range(1, 25, 2):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)


def test_optimize_with_long_text_metadata(db):
    """Long text metadata (overflow) preserved after optimize."""
    db.execute(
        "create virtual table v using vec0("
        "emb float[4], m_text text, chunk_size=8"
        ")"
    )

    long_text = "x" * 100  # >12 chars, stored in overflow table

    for i in range(1, 17):
        db.execute(
            "insert into v(rowid, emb, m_text) values (?, ?, ?)",
            [i, _f32([float(i)] * 4), f"{long_text}_{i}"],
        )

    for i in range(1, 7):
        db.execute("delete from v where rowid = ?", [i])

    db.execute("insert into v(v) values ('optimize')")

    for i in range(7, 17):
        row = db.execute(
            "select m_text from v where rowid = ?", [i]
        ).fetchone()
        assert row[0] == f"{long_text}_{i}"
