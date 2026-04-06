import sqlite3
import struct
import pytest
from helpers import _f32, _i64, _int8, exec


def test_insert_creates_chunks_and_vectors(db, snapshot):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    vecs = [
        [1.0, 2.0, 3.0, 4.0],
        [5.0, 6.0, 7.0, 8.0],
        [0.1, 0.2, 0.3, 0.4],
        [10.0, 20.0, 30.0, 40.0],
        [0.5, 0.5, 0.5, 0.5],
    ]
    for i, v in enumerate(vecs, start=1):
        db.execute("insert into v(rowid, emb) values (?, ?)", [i, _f32(v)])

    assert exec(db, "select count(*) as cnt from v_rowids") == snapshot(
        name="rowids_count"
    )
    assert exec(db, "select count(*) as cnt from v_vector_chunks00") == snapshot(
        name="vector_chunks_count"
    )

    # Verify round-trip: each inserted vector comes back identical
    for i, v in enumerate(vecs, start=1):
        rows = db.execute("select emb from v where rowid = ?", [i]).fetchall()
        assert len(rows) == 1
        assert rows[0][0] == _f32(v)


def test_insert_auto_rowid(db):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    vecs = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]
    for v in vecs:
        db.execute("insert into v(emb) values (?)", [_f32(v)])

    rows = db.execute("select rowid from v order by rowid").fetchall()
    rowids = [r[0] for r in rows]
    assert rowids == [1, 2, 3]

    for i, v in enumerate(vecs, start=1):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32(v)


def test_insert_text_primary_key(db, snapshot):
    db.execute(
        "create virtual table v using vec0(id text primary key, emb float[4], chunk_size=8)"
    )

    db.execute(
        "insert into v(id, emb) values ('doc_a', ?)", [_f32([1.0, 2.0, 3.0, 4.0])]
    )
    db.execute(
        "insert into v(id, emb) values ('doc_b', ?)", [_f32([5.0, 6.0, 7.0, 8.0])]
    )

    assert exec(db, "select rowid, id, chunk_id, chunk_offset from v_rowids order by rowid") == snapshot(
        name="rowids"
    )

    row = db.execute("select emb from v where id = 'doc_a'").fetchone()
    assert row[0] == _f32([1.0, 2.0, 3.0, 4.0])

    row = db.execute("select emb from v where id = 'doc_b'").fetchone()
    assert row[0] == _f32([5.0, 6.0, 7.0, 8.0])


def test_delete_clears_validity(db):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i, v in enumerate(
        [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]],
        start=1,
    ):
        db.execute("insert into v(rowid, emb) values (?, ?)", [i, _f32(v)])

    db.execute("delete from v where rowid = 2")

    rows = db.execute("select rowid from v order by rowid").fetchall()
    assert [r[0] for r in rows] == [1, 3]

    rowid_rows = db.execute("select rowid from v_rowids order by rowid").fetchall()
    assert 2 not in [r[0] for r in rowid_rows]

    # Inserting a new vector after deletion still works
    db.execute("insert into v(rowid, emb) values (4, ?)", [_f32([0.0, 0.0, 0.0, 1.0])])
    row = db.execute("select emb from v where rowid = 4").fetchone()
    assert row[0] == _f32([0.0, 0.0, 0.0, 1.0])


def test_insert_delete_reinsert(db):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    db.execute("insert into v(rowid, emb) values (1, ?)", [_f32([1.0, 1.0, 1.0, 1.0])])
    db.execute("delete from v where rowid = 1")
    db.execute("insert into v(rowid, emb) values (2, ?)", [_f32([2.0, 2.0, 2.0, 2.0])])

    rows = db.execute("select rowid from v order by rowid").fetchall()
    assert [r[0] for r in rows] == [2]

    # KNN query works and returns rowid 2
    knn = db.execute(
        "select rowid, distance from v where emb match ? and k = 1",
        [_f32([2.0, 2.0, 2.0, 2.0])],
    ).fetchall()
    assert len(knn) == 1
    assert knn[0][0] == 2


def test_insert_validates_dimensions(db):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    result = exec(db, "insert into v(rowid, emb) values (1, ?)", [_f32([1.0, 2.0, 3.0])])
    assert result["error"] == "OperationalError"
    assert "Dimension mismatch" in result["message"]
    assert "Expected 4" in result["message"]
    assert "3" in result["message"]

    result = exec(
        db, "insert into v(rowid, emb) values (1, ?)", [_f32([1.0, 2.0, 3.0, 4.0, 5.0])]
    )
    assert result["error"] == "OperationalError"
    assert "Dimension mismatch" in result["message"]
    assert "Expected 4" in result["message"]
    assert "5" in result["message"]


def test_insert_validates_type(db):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    int8_vec = struct.pack("4b", 1, 2, 3, 4)
    result = exec(
        db,
        "insert into v(rowid, emb) values (1, vec_int8(?))",
        [int8_vec],
    )
    assert "error" in result
    assert "float32" in result["message"]
    assert "int8" in result["message"]


def test_info_table_contents(db, snapshot):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")
    assert exec(db, "select key, value from v_info where key not like 'CREATE_VERSION%' order by key") == snapshot()
    # Smoke-check that version keys exist without pinning exact values
    version_rows = exec(db, "select key from v_info where key like 'CREATE_VERSION%' order by key")
    keys = [r["key"] for r in version_rows["rows"]]
    assert keys == ["CREATE_VERSION", "CREATE_VERSION_MAJOR", "CREATE_VERSION_MINOR", "CREATE_VERSION_PATCH"]


def test_delete_zeroes_rowid_blob(db):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i in range(1, 4):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    db.execute("delete from v where rowid = 2")

    blob = db.execute("select rowids from v_chunks where rowid = 1").fetchone()[0]
    rowids = struct.unpack("<8q", blob)
    assert rowids[0] == 1  # slot 0 intact
    assert rowids[1] == 0  # slot 1 zeroed (was rowid 2)
    assert rowids[2] == 3  # slot 2 intact


def test_delete_zeroes_vector_blob(db):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    db.execute(
        "insert into v(rowid, emb) values (1, ?)", [_f32([1.0, 2.0, 3.0, 4.0])]
    )
    db.execute(
        "insert into v(rowid, emb) values (2, ?)", [_f32([5.0, 6.0, 7.0, 8.0])]
    )

    db.execute("delete from v where rowid = 1")

    blob = db.execute(
        "select vectors from v_vector_chunks00 where rowid = 1"
    ).fetchone()[0]
    # First slot (4 floats = 16 bytes) should be zeroed
    first_slot = struct.unpack("<4f", blob[:16])
    assert first_slot == (0.0, 0.0, 0.0, 0.0)
    # Second slot should be unchanged
    second_slot = struct.unpack("<4f", blob[16:32])
    assert second_slot == (5.0, 6.0, 7.0, 8.0)


def test_delete_all_rows_deletes_chunk(db):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i in range(1, 9):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    for i in range(1, 9):
        db.execute("delete from v where rowid = ?", [i])

    assert (
        db.execute("select count(*) from v_chunks").fetchone()[0] == 0
    )
    assert (
        db.execute("select count(*) from v_vector_chunks00").fetchone()[0] == 0
    )

    # Inserting after full deletion still works
    db.execute(
        "insert into v(rowid, emb) values (100, ?)", [_f32([9.0, 9.0, 9.0, 9.0])]
    )
    row = db.execute("select emb from v where rowid = 100").fetchone()
    assert row[0] == _f32([9.0, 9.0, 9.0, 9.0])


def test_delete_chunk_multiple_chunks(db):
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    for i in range(1, 17):
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * 4)],
        )

    # Delete all rows from the first chunk (rows 1-8)
    for i in range(1, 9):
        db.execute("delete from v where rowid = ?", [i])

    # Only 1 chunk should remain
    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 1

    # Rows 9-16 still queryable
    for i in range(9, 17):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)


def test_delete_with_metadata_columns(db):
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

    for i in range(1, 9):
        db.execute(
            "insert into v(rowid, emb, m_bool, m_int, m_float, m_text) "
            "values (?, ?, ?, ?, ?, ?)",
            [i, _f32([float(i)] * 4), i % 2 == 0, i * 10, float(i) / 2.0, f"text_{i}"],
        )

    for i in range(1, 9):
        db.execute("delete from v where rowid = ?", [i])

    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 0
    assert db.execute("select count(*) from v_vector_chunks00").fetchone()[0] == 0
    assert db.execute("select count(*) from v_metadatachunks00").fetchone()[0] == 0
    assert db.execute("select count(*) from v_metadatachunks01").fetchone()[0] == 0
    assert db.execute("select count(*) from v_metadatachunks02").fetchone()[0] == 0
    assert db.execute("select count(*) from v_metadatachunks03").fetchone()[0] == 0


def test_delete_with_auxiliary_columns(db):
    db.execute(
        "create virtual table v using vec0("
        "emb float[4], "
        "+aux_text text, "
        "chunk_size=8"
        ")"
    )

    for i in range(1, 9):
        db.execute(
            "insert into v(rowid, emb, aux_text) values (?, ?, ?)",
            [i, _f32([float(i)] * 4), f"aux_{i}"],
        )

    for i in range(1, 9):
        db.execute("delete from v where rowid = ?", [i])

    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 0
    assert db.execute("select count(*) from v_auxiliary").fetchone()[0] == 0


def test_delete_with_text_primary_key(db):
    db.execute(
        "create virtual table v using vec0("
        "id text primary key, emb float[4], chunk_size=8"
        ")"
    )

    db.execute(
        "insert into v(id, emb) values ('a', ?)", [_f32([1.0, 2.0, 3.0, 4.0])]
    )
    db.execute(
        "insert into v(id, emb) values ('b', ?)", [_f32([5.0, 6.0, 7.0, 8.0])]
    )

    db.execute("delete from v where id = 'a'")

    # Vector blob slot 0 should be zeroed
    blob = db.execute(
        "select vectors from v_vector_chunks00 where rowid = 1"
    ).fetchone()[0]
    first_slot = struct.unpack("<4f", blob[:16])
    assert first_slot == (0.0, 0.0, 0.0, 0.0)

    # Remaining row still queryable
    row = db.execute("select emb from v where id = 'b'").fetchone()
    assert row[0] == _f32([5.0, 6.0, 7.0, 8.0])


def test_delete_with_partition_keys(db):
    db.execute(
        "create virtual table v using vec0("
        "part text partition key, emb float[4], chunk_size=8"
        ")"
    )

    for i in range(1, 9):
        db.execute(
            "insert into v(rowid, part, emb) values (?, 'A', ?)",
            [i, _f32([float(i)] * 4)],
        )
    for i in range(9, 17):
        db.execute(
            "insert into v(rowid, part, emb) values (?, 'B', ?)",
            [i, _f32([float(i)] * 4)],
        )

    # Delete all from partition A
    for i in range(1, 9):
        db.execute("delete from v where rowid = ?", [i])

    # 1 chunk should remain (partition B's)
    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 1

    # Partition B rows intact
    for i in range(9, 17):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * 4)

    # Re-insert into partition A works
    db.execute(
        "insert into v(rowid, part, emb) values (100, 'A', ?)",
        [_f32([99.0, 99.0, 99.0, 99.0])],
    )
    row = db.execute("select emb from v where rowid = 100").fetchone()
    assert row[0] == _f32([99.0, 99.0, 99.0, 99.0])


def test_delete_int8_vectors(db):
    db.execute("create virtual table v using vec0(emb int8[4], chunk_size=8)")

    db.execute(
        "insert into v(rowid, emb) values (1, vec_int8(?))",
        [_int8([1, 2, 3, 4])],
    )
    db.execute(
        "insert into v(rowid, emb) values (2, vec_int8(?))",
        [_int8([5, 6, 7, 8])],
    )

    db.execute("delete from v where rowid = 1")

    blob = db.execute(
        "select vectors from v_vector_chunks00 where rowid = 1"
    ).fetchone()[0]
    # int8[4] = 4 bytes per slot
    first_slot = struct.unpack("<4b", blob[:4])
    assert first_slot == (0, 0, 0, 0)
    second_slot = struct.unpack("<4b", blob[4:8])
    assert second_slot == (5, 6, 7, 8)


def test_delete_bit_vectors(db):
    db.execute("create virtual table v using vec0(emb bit[8], chunk_size=8)")

    db.execute(
        "insert into v(rowid, emb) values (1, vec_bit(?))",
        [bytes([0xFF])],
    )
    db.execute(
        "insert into v(rowid, emb) values (2, vec_bit(?))",
        [bytes([0xAA])],
    )

    db.execute("delete from v where rowid = 1")

    blob = db.execute(
        "select vectors from v_vector_chunks00 where rowid = 1"
    ).fetchone()[0]
    # bit[8] = 1 byte per slot
    assert blob[0:1] == bytes([0x00])
    assert blob[1:2] == bytes([0xAA])


def _file_db(tmp_path):
    """Open a file-backed DB (required for page_count to shrink after VACUUM)."""
    db = sqlite3.connect(str(tmp_path / "test.db"))
    db.row_factory = sqlite3.Row
    db.enable_load_extension(True)
    db.load_extension("dist/vec0")
    db.enable_load_extension(False)
    return db


def test_delete_chunk_shrinks_pages(tmp_path):
    """Use large vectors (float[256]) so each chunk blob spans multiple pages,
    making the page_count difference measurable after VACUUM."""
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

    # Delete all rows
    for i in range(1, 25):
        db.execute("delete from v where rowid = ?", [i])
    db.commit()

    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 0

    db.execute("vacuum")
    pages_after = db.execute("pragma page_count").fetchone()[0]
    assert pages_after < pages_before, (
        f"page_count should shrink after deleting all chunks and vacuum: "
        f"{pages_before} -> {pages_after}"
    )
    db.close()


def test_delete_one_chunk_of_two_shrinks_pages(tmp_path):
    """Use large vectors (float[256]) so each chunk blob spans multiple pages,
    making the page_count difference measurable after VACUUM."""
    dims = 256
    db = _file_db(tmp_path)
    db.execute(f"create virtual table v using vec0(emb float[{dims}], chunk_size=8)")

    for i in range(1, 17):  # 2 full chunks of 8
        db.execute(
            "insert into v(rowid, emb) values (?, ?)",
            [i, _f32([float(i)] * dims)],
        )
    db.commit()
    pages_before = db.execute("pragma page_count").fetchone()[0]

    # Delete all rows from the first chunk (rows 1-8)
    for i in range(1, 9):
        db.execute("delete from v where rowid = ?", [i])
    db.commit()

    assert db.execute("select count(*) from v_chunks").fetchone()[0] == 1

    db.execute("vacuum")
    pages_after = db.execute("pragma page_count").fetchone()[0]
    assert pages_after < pages_before, (
        f"page_count should shrink after deleting one chunk and vacuum: "
        f"{pages_before} -> {pages_after}"
    )

    # Remaining rows still queryable after vacuum
    for i in range(9, 17):
        row = db.execute("select emb from v where rowid = ?", [i]).fetchone()
        assert row[0] == _f32([float(i)] * dims)
    db.close()


def test_wal_concurrent_reader_during_write(tmp_path):
    """In WAL mode, a reader should see a consistent snapshot while a writer inserts."""
    dims = 4
    db_path = str(tmp_path / "test.db")

    # Writer: create table, insert initial rows, enable WAL
    writer = sqlite3.connect(db_path)
    writer.enable_load_extension(True)
    writer.load_extension("dist/vec0")
    writer.execute("PRAGMA journal_mode=WAL")
    writer.execute(
        f"CREATE VIRTUAL TABLE v USING vec0(emb float[{dims}])"
    )
    for i in range(1, 11):
        writer.execute("INSERT INTO v(rowid, emb) VALUES (?, ?)", [i, _f32([float(i)] * dims)])
    writer.commit()

    # Reader: open separate connection, start read
    reader = sqlite3.connect(db_path)
    reader.enable_load_extension(True)
    reader.load_extension("dist/vec0")

    # Reader sees 10 rows
    count_before = reader.execute("SELECT count(*) FROM v").fetchone()[0]
    assert count_before == 10

    # Writer inserts more rows (not yet committed)
    writer.execute("BEGIN")
    for i in range(11, 21):
        writer.execute("INSERT INTO v(rowid, emb) VALUES (?, ?)", [i, _f32([float(i)] * dims)])

    # Reader still sees 10 (WAL snapshot isolation)
    count_during = reader.execute("SELECT count(*) FROM v").fetchone()[0]
    assert count_during == 10

    # KNN during writer's transaction should work on reader's snapshot
    rows = reader.execute(
        "SELECT rowid FROM v WHERE emb MATCH ? AND k = 5",
        [_f32([1.0] * dims)],
    ).fetchall()
    assert len(rows) == 5
    assert all(r[0] <= 10 for r in rows)  # only original rows

    # Writer commits
    writer.commit()

    # Reader sees new rows after re-query (new snapshot)
    count_after = reader.execute("SELECT count(*) FROM v").fetchone()[0]
    assert count_after == 20

    writer.close()
    reader.close()


def test_insert_or_replace_integer_pk(db):
    """INSERT OR REPLACE should update vector when rowid already exists."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    db.execute(
        "insert into v(rowid, emb) values (1, ?)", [_f32([1.0, 2.0, 3.0, 4.0])]
    )
    # Replace with new vector
    db.execute(
        "insert or replace into v(rowid, emb) values (1, ?)",
        [_f32([10.0, 20.0, 30.0, 40.0])],
    )

    # Should still have exactly 1 row
    count = db.execute("select count(*) from v").fetchone()[0]
    assert count == 1

    # Vector should be the replaced value
    row = db.execute("select emb from v where rowid = 1").fetchone()
    assert row[0] == _f32([10.0, 20.0, 30.0, 40.0])


def test_insert_or_replace_new_row(db):
    """INSERT OR REPLACE with a new rowid should just insert normally."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    db.execute(
        "insert or replace into v(rowid, emb) values (1, ?)",
        [_f32([1.0, 2.0, 3.0, 4.0])],
    )

    count = db.execute("select count(*) from v").fetchone()[0]
    assert count == 1

    row = db.execute("select emb from v where rowid = 1").fetchone()
    assert row[0] == _f32([1.0, 2.0, 3.0, 4.0])


def test_insert_or_replace_text_pk(db):
    """INSERT OR REPLACE should work with text primary keys."""
    db.execute(
        "create virtual table v using vec0("
        "id text primary key, emb float[4], chunk_size=8"
        ")"
    )

    db.execute(
        "insert into v(id, emb) values ('doc_a', ?)",
        [_f32([1.0, 2.0, 3.0, 4.0])],
    )
    db.execute(
        "insert or replace into v(id, emb) values ('doc_a', ?)",
        [_f32([10.0, 20.0, 30.0, 40.0])],
    )

    count = db.execute("select count(*) from v").fetchone()[0]
    assert count == 1

    row = db.execute("select emb from v where id = 'doc_a'").fetchone()
    assert row[0] == _f32([10.0, 20.0, 30.0, 40.0])


def test_insert_or_replace_with_auxiliary(db):
    """INSERT OR REPLACE should also replace auxiliary column values."""
    db.execute(
        "create virtual table v using vec0("
        "emb float[4], +label text, chunk_size=8"
        ")"
    )

    db.execute(
        "insert into v(rowid, emb, label) values (1, ?, 'old')",
        [_f32([1.0, 2.0, 3.0, 4.0])],
    )
    db.execute(
        "insert or replace into v(rowid, emb, label) values (1, ?, 'new')",
        [_f32([10.0, 20.0, 30.0, 40.0])],
    )

    count = db.execute("select count(*) from v").fetchone()[0]
    assert count == 1

    row = db.execute("select emb, label from v where rowid = 1").fetchone()
    assert row[0] == _f32([10.0, 20.0, 30.0, 40.0])
    assert row[1] == "new"


def test_insert_or_replace_knn_uses_new_vector(db):
    """After INSERT OR REPLACE, KNN should find the new vector, not the old one."""
    db.execute("create virtual table v using vec0(emb float[4], chunk_size=8)")

    db.execute(
        "insert into v(rowid, emb) values (1, ?)", [_f32([1.0, 0.0, 0.0, 0.0])]
    )
    db.execute(
        "insert into v(rowid, emb) values (2, ?)", [_f32([0.0, 1.0, 0.0, 0.0])]
    )

    # Replace row 1's vector to be very close to row 2
    db.execute(
        "insert or replace into v(rowid, emb) values (1, ?)",
        [_f32([0.0, 0.9, 0.0, 0.0])],
    )

    # KNN for [0, 1, 0, 0] should return row 2 first (exact), then row 1 (close)
    rows = db.execute(
        "select rowid, distance from v where emb match ? and k = 2",
        [_f32([0.0, 1.0, 0.0, 0.0])],
    ).fetchall()
    assert rows[0][0] == 2
    assert rows[1][0] == 1
    assert rows[1][1] < 0.11  # should be close (L2 distance ≈ 0.1)
