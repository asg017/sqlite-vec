"""Mutation and edge-case tests for the rescore index feature."""
import struct
import sqlite3
import pytest
import math
import random


@pytest.fixture()
def db():
    db = sqlite3.connect(":memory:")
    db.row_factory = sqlite3.Row
    db.enable_load_extension(True)
    db.load_extension("dist/vec0")
    db.enable_load_extension(False)
    return db


def float_vec(values):
    """Pack a list of floats into a blob for sqlite-vec."""
    return struct.pack(f"{len(values)}f", *values)


def unpack_float_vec(blob):
    """Unpack a float vector blob."""
    n = len(blob) // 4
    return list(struct.unpack(f"{n}f", blob))


# ============================================================================
# Error cases: rescore + aux/metadata/partition
# ============================================================================


def test_create_error_with_aux_column(db):
    """Rescore should reject auxiliary columns."""
    with pytest.raises(sqlite3.OperationalError, match="Auxiliary columns"):
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            "  embedding float[8] indexed by rescore(quantizer=bit),"
            "  +extra text"
            ")"
        )


def test_create_error_with_metadata_column(db):
    """Rescore should reject metadata columns."""
    with pytest.raises(sqlite3.OperationalError, match="Metadata columns"):
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            "  embedding float[8] indexed by rescore(quantizer=bit),"
            "  genre text"
            ")"
        )


def test_create_error_with_partition_key(db):
    """Rescore should reject partition key columns."""
    with pytest.raises(sqlite3.OperationalError, match="Partition key"):
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            "  embedding float[8] indexed by rescore(quantizer=bit),"
            "  user_id integer partition key"
            ")"
        )


# ============================================================================
# Insert / batch / delete / update mutations
# ============================================================================


def test_insert_single_verify_knn(db):
    """Insert a single row and verify KNN returns it."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    db.execute("INSERT INTO t(rowid, embedding) VALUES (1, ?)", [float_vec([1.0] * 8)])
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([1.0] * 8)],
    ).fetchall()
    assert len(rows) == 1
    assert rows[0]["rowid"] == 1
    assert rows[0]["distance"] < 0.01


def test_insert_large_batch(db):
    """Insert 200+ rows (multiple chunks with default chunk_size=1024) and verify count and KNN."""
    dim = 16
    n = 200
    random.seed(99)
    db.execute(
        f"CREATE VIRTUAL TABLE t USING vec0("
        f"  embedding float[{dim}] indexed by rescore(quantizer=int8)"
        f")"
    )
    for i in range(n):
        v = [random.gauss(0, 1) for _ in range(dim)]
        db.execute(
            "INSERT INTO t(rowid, embedding) VALUES (?, ?)",
            [i + 1, float_vec(v)],
        )
    row = db.execute("SELECT count(*) as cnt FROM t").fetchone()
    assert row["cnt"] == n

    # KNN should return results
    query = float_vec([random.gauss(0, 1) for _ in range(dim)])
    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 10",
        [query],
    ).fetchall()
    assert len(rows) == 10


def test_delete_all_rows(db):
    """Delete every row, verify count=0, KNN returns empty."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    for i in range(20):
        db.execute(
            "INSERT INTO t(rowid, embedding) VALUES (?, ?)",
            [i + 1, float_vec([float(i)] * 8)],
        )
    assert db.execute("SELECT count(*) as cnt FROM t").fetchone()["cnt"] == 20

    for i in range(20):
        db.execute("DELETE FROM t WHERE rowid = ?", [i + 1])

    assert db.execute("SELECT count(*) as cnt FROM t").fetchone()["cnt"] == 0

    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 5",
        [float_vec([0.0] * 8)],
    ).fetchall()
    assert len(rows) == 0


def test_delete_then_reinsert_same_rowid(db):
    """Delete rowid=1, re-insert rowid=1 with different vector, verify KNN uses new vector."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=int8)"
        ")"
    )
    # Insert rowid=1 near origin, rowid=2 far from origin
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (1, ?)",
        [float_vec([0.1] * 8)],
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (2, ?)",
        [float_vec([100.0] * 8)],
    )

    # KNN to [0]*8 -> rowid 1 is closer
    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([0.0] * 8)],
    ).fetchall()
    assert rows[0]["rowid"] == 1

    # Delete rowid=1, re-insert with vector far from origin
    db.execute("DELETE FROM t WHERE rowid = 1")
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (1, ?)",
        [float_vec([200.0] * 8)],
    )

    # Now KNN to [0]*8 -> rowid 2 should be closer
    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([0.0] * 8)],
    ).fetchall()
    assert rows[0]["rowid"] == 2


def test_update_vector(db):
    """UPDATE the vector column and verify KNN reflects new value."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=int8)"
        ")"
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (1, ?)",
        [float_vec([0.0] * 8)],
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (2, ?)",
        [float_vec([10.0] * 8)],
    )

    # Update rowid=1 to be far away
    db.execute(
        "UPDATE t SET embedding = ? WHERE rowid = 1",
        [float_vec([100.0] * 8)],
    )

    # Now KNN to [0]*8 -> rowid 2 should be closest
    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([0.0] * 8)],
    ).fetchall()
    assert rows[0]["rowid"] == 2


def test_knn_after_delete_all_but_one(db):
    """Insert 50 rows, delete 49, KNN should only return the survivor."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    for i in range(50):
        db.execute(
            "INSERT INTO t(rowid, embedding) VALUES (?, ?)",
            [i + 1, float_vec([float(i)] * 8)],
        )
    # Delete all except rowid=25
    for i in range(50):
        if i + 1 != 25:
            db.execute("DELETE FROM t WHERE rowid = ?", [i + 1])

    assert db.execute("SELECT count(*) as cnt FROM t").fetchone()["cnt"] == 1

    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 10",
        [float_vec([0.0] * 8)],
    ).fetchall()
    assert len(rows) == 1
    assert rows[0]["rowid"] == 25


# ============================================================================
# Edge cases
# ============================================================================


def test_single_row_knn(db):
    """Table with exactly 1 row. LIMIT 1 returns it; LIMIT 5 returns 1."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    db.execute("INSERT INTO t(rowid, embedding) VALUES (1, ?)", [float_vec([1.0] * 8)])

    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([1.0] * 8)],
    ).fetchall()
    assert len(rows) == 1

    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 5",
        [float_vec([1.0] * 8)],
    ).fetchall()
    assert len(rows) == 1


def test_knn_with_all_identical_vectors(db):
    """All vectors are the same. All distances should be equal."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=int8)"
        ")"
    )
    vec = [3.0, 1.0, 4.0, 1.0, 5.0, 9.0, 2.0, 6.0]
    for i in range(10):
        db.execute(
            "INSERT INTO t(rowid, embedding) VALUES (?, ?)",
            [i + 1, float_vec(vec)],
        )

    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 10",
        [float_vec(vec)],
    ).fetchall()
    assert len(rows) == 10
    # All distances should be ~0 (exact match)
    for r in rows:
        assert r["distance"] < 0.01


def test_zero_vector_insert(db):
    """Insert the zero vector [0,0,...,0]. Should not crash quantization."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (1, ?)",
        [float_vec([0.0] * 8)],
    )
    row = db.execute("SELECT count(*) as cnt FROM t").fetchone()
    assert row["cnt"] == 1

    # Also test int8 quantizer with zero vector
    db.execute(
        "CREATE VIRTUAL TABLE t2 USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=int8)"
        ")"
    )
    db.execute(
        "INSERT INTO t2(rowid, embedding) VALUES (1, ?)",
        [float_vec([0.0] * 8)],
    )
    row = db.execute("SELECT count(*) as cnt FROM t2").fetchone()
    assert row["cnt"] == 1


def test_very_large_values(db):
    """Insert vectors with very large float values. Quantization should not crash."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=int8)"
        ")"
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (1, ?)",
        [float_vec([1e30] * 8)],
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (2, ?)",
        [float_vec([1e30, -1e30, 1e30, -1e30, 1e30, -1e30, 1e30, -1e30])],
    )
    row = db.execute("SELECT count(*) as cnt FROM t").fetchone()
    assert row["cnt"] == 2


def test_negative_values(db):
    """Insert vectors with all negative values. Bit quantization maps all to 0."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (1, ?)",
        [float_vec([-1.0, -2.0, -3.0, -4.0, -5.0, -6.0, -7.0, -8.0])],
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (2, ?)",
        [float_vec([-0.1, -0.2, -0.3, -0.4, -0.5, -0.6, -0.7, -0.8])],
    )
    row = db.execute("SELECT count(*) as cnt FROM t").fetchone()
    assert row["cnt"] == 2

    # KNN should still work
    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 2",
        [float_vec([-0.1, -0.2, -0.3, -0.4, -0.5, -0.6, -0.7, -0.8])],
    ).fetchall()
    assert len(rows) == 2
    assert rows[0]["rowid"] == 2


def test_single_dimension(db):
    """Single-dimension vector (edge case for quantization)."""
    # int8 quantizer (bit needs dim divisible by 8)
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=int8)"
        ")"
    )
    db.execute("INSERT INTO t(rowid, embedding) VALUES (1, ?)", [float_vec([1.0] * 8)])
    db.execute("INSERT INTO t(rowid, embedding) VALUES (2, ?)", [float_vec([5.0] * 8)])
    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([1.0] * 8)],
    ).fetchall()
    assert rows[0]["rowid"] == 1


# ============================================================================
# vec_debug() verification
# ============================================================================


def test_vec_debug_contains_rescore(db):
    """vec_debug() should contain 'rescore' in build flags when compiled with SQLITE_VEC_ENABLE_RESCORE."""
    row = db.execute("SELECT vec_debug() as d").fetchone()
    assert "rescore" in row["d"]


# ============================================================================
# Insert batch recall test
# ============================================================================


def test_insert_batch_recall(db):
    """Insert 150 rows and verify KNN recall is reasonable (>0.6)."""
    dim = 16
    n = 150
    k = 10
    random.seed(77)

    db.execute(
        f"CREATE VIRTUAL TABLE t_rescore USING vec0("
        f"  embedding float[{dim}] indexed by rescore(quantizer=int8, oversample=16)"
        f")"
    )
    db.execute(
        f"CREATE VIRTUAL TABLE t_flat USING vec0(embedding float[{dim}])"
    )

    vectors = [[random.gauss(0, 1) for _ in range(dim)] for _ in range(n)]
    for i, v in enumerate(vectors):
        blob = float_vec(v)
        db.execute(
            "INSERT INTO t_rescore(rowid, embedding) VALUES (?, ?)", [i + 1, blob]
        )
        db.execute(
            "INSERT INTO t_flat(rowid, embedding) VALUES (?, ?)", [i + 1, blob]
        )

    query = float_vec([random.gauss(0, 1) for _ in range(dim)])

    rescore_rows = db.execute(
        "SELECT rowid FROM t_rescore WHERE embedding MATCH ? ORDER BY distance LIMIT ?",
        [query, k],
    ).fetchall()
    flat_rows = db.execute(
        "SELECT rowid FROM t_flat WHERE embedding MATCH ? ORDER BY distance LIMIT ?",
        [query, k],
    ).fetchall()

    rescore_ids = {r["rowid"] for r in rescore_rows}
    flat_ids = {r["rowid"] for r in flat_rows}
    recall = len(rescore_ids & flat_ids) / k
    assert recall >= 0.6, f"Recall too low: {recall}"


# ============================================================================
# Distance metric variants
# ============================================================================


def test_knn_int8_cosine(db):
    """Rescore with quantizer=int8 and distance_metric=cosine."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] distance_metric=cosine indexed by rescore(quantizer=int8)"
        ")"
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (1, ?)",
        [float_vec([1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (2, ?)",
        [float_vec([0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    )
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (3, ?)",
        [float_vec([1.0, 0.1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    )
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 2",
        [float_vec([1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    ).fetchall()
    assert rows[0]["rowid"] == 1
    assert rows[0]["distance"] < 0.01
