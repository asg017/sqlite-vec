import sqlite3
import struct
import pytest
from helpers import _f32, exec


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
    assert exec(db, "select key, value from v_info order by key") == snapshot()
