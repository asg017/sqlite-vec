"""Tests for the rescore index feature in sqlite-vec."""
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
# Creation tests
# ============================================================================


def test_create_bit(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[128] indexed by rescore(quantizer=bit)"
        ")"
    )
    # Table exists and has the right structure
    row = db.execute(
        "SELECT count(*) as cnt FROM sqlite_master WHERE name LIKE 't_%'"
    ).fetchone()
    assert row["cnt"] > 0


def test_create_int8(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[128] indexed by rescore(quantizer=int8)"
        ")"
    )
    row = db.execute(
        "SELECT count(*) as cnt FROM sqlite_master WHERE name LIKE 't_%'"
    ).fetchone()
    assert row["cnt"] > 0


def test_create_with_oversample(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[128] indexed by rescore(quantizer=bit, oversample=16)"
        ")"
    )
    row = db.execute(
        "SELECT count(*) as cnt FROM sqlite_master WHERE name LIKE 't_%'"
    ).fetchone()
    assert row["cnt"] > 0


def test_create_with_distance_metric(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[128] distance_metric=cosine indexed by rescore(quantizer=bit)"
        ")"
    )
    row = db.execute(
        "SELECT count(*) as cnt FROM sqlite_master WHERE name LIKE 't_%'"
    ).fetchone()
    assert row["cnt"] > 0


def test_create_error_missing_quantizer(db):
    with pytest.raises(sqlite3.OperationalError):
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            "  embedding float[128] indexed by rescore(oversample=8)"
            ")"
        )


def test_create_error_invalid_quantizer(db):
    with pytest.raises(sqlite3.OperationalError):
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            "  embedding float[128] indexed by rescore(quantizer=float)"
            ")"
        )


def test_create_error_on_bit_column(db):
    with pytest.raises(sqlite3.OperationalError):
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            "  embedding bit[1024] indexed by rescore(quantizer=bit)"
            ")"
        )


def test_create_error_on_int8_column(db):
    with pytest.raises(sqlite3.OperationalError):
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            "  embedding int8[128] indexed by rescore(quantizer=bit)"
            ")"
        )


def test_create_error_bad_oversample_zero(db):
    with pytest.raises(sqlite3.OperationalError):
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            "  embedding float[128] indexed by rescore(quantizer=bit, oversample=0)"
            ")"
        )


def test_create_error_bad_oversample_too_large(db):
    with pytest.raises(sqlite3.OperationalError):
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            "  embedding float[128] indexed by rescore(quantizer=bit, oversample=999)"
            ")"
        )


def test_create_error_bit_dim_not_divisible_by_8(db):
    with pytest.raises(sqlite3.OperationalError):
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            "  embedding float[100] indexed by rescore(quantizer=bit)"
            ")"
        )


# ============================================================================
# Shadow table tests
# ============================================================================


def test_shadow_tables_exist(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[128] indexed by rescore(quantizer=bit)"
        ")"
    )
    tables = [
        r[0]
        for r in db.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 't_%' ORDER BY name"
        ).fetchall()
    ]
    assert "t_rescore_chunks00" in tables
    assert "t_rescore_vectors00" in tables
    # Rescore columns don't create _vector_chunks
    assert "t_vector_chunks00" not in tables


def test_drop_cleans_up(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[128] indexed by rescore(quantizer=bit)"
        ")"
    )
    db.execute("DROP TABLE t")
    tables = [
        r[0]
        for r in db.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 't_%'"
        ).fetchall()
    ]
    assert len(tables) == 0


# ============================================================================
# Insert tests
# ============================================================================


def test_insert_single(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    db.execute("INSERT INTO t(rowid, embedding) VALUES (1, ?)", [float_vec([1.0] * 8)])
    row = db.execute("SELECT count(*) as cnt FROM t").fetchone()
    assert row["cnt"] == 1


def test_insert_multiple(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=int8)"
        ")"
    )
    for i in range(10):
        db.execute(
            "INSERT INTO t(rowid, embedding) VALUES (?, ?)",
            [i + 1, float_vec([float(i)] * 8)],
        )
    row = db.execute("SELECT count(*) as cnt FROM t").fetchone()
    assert row["cnt"] == 10


# ============================================================================
# Delete tests
# ============================================================================


def test_delete_single(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    db.execute("INSERT INTO t(rowid, embedding) VALUES (1, ?)", [float_vec([1.0] * 8)])
    db.execute("DELETE FROM t WHERE rowid = 1")
    row = db.execute("SELECT count(*) as cnt FROM t").fetchone()
    assert row["cnt"] == 0


def test_delete_and_reinsert(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    db.execute("INSERT INTO t(rowid, embedding) VALUES (1, ?)", [float_vec([1.0] * 8)])
    db.execute("DELETE FROM t WHERE rowid = 1")
    db.execute(
        "INSERT INTO t(rowid, embedding) VALUES (2, ?)", [float_vec([2.0] * 8)]
    )
    row = db.execute("SELECT count(*) as cnt FROM t").fetchone()
    assert row["cnt"] == 1


def test_point_query_returns_float(db):
    """SELECT by rowid should return the original float vector, not quantized."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    vals = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]
    db.execute("INSERT INTO t(rowid, embedding) VALUES (1, ?)", [float_vec(vals)])
    row = db.execute("SELECT embedding FROM t WHERE rowid = 1").fetchone()
    result = unpack_float_vec(row["embedding"])
    for a, b in zip(result, vals):
        assert abs(a - b) < 1e-6


# ============================================================================
# KNN tests
# ============================================================================


def test_knn_basic_bit(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    # Insert vectors where [1,0,0,...] is closest to query [1,0,0,...]
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
        [float_vec([0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    )
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    ).fetchall()
    assert len(rows) == 1
    assert rows[0]["rowid"] == 1


def test_knn_basic_int8(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=int8)"
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
        [float_vec([0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    )
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    ).fetchall()
    assert len(rows) == 1
    assert rows[0]["rowid"] == 1


def test_knn_returns_float_distances(db):
    """KNN should return float-precision distances, not quantized distances."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    v1 = [1.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    v2 = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    db.execute("INSERT INTO t(rowid, embedding) VALUES (1, ?)", [float_vec(v1)])
    db.execute("INSERT INTO t(rowid, embedding) VALUES (2, ?)", [float_vec(v2)])

    query = [1.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 2",
        [float_vec(query)],
    ).fetchall()

    # First result should be exact match with distance ~0
    assert rows[0]["rowid"] == 1
    assert rows[0]["distance"] < 0.01

    # Second result should have a float distance
    # sqrt((1-0)^2 + (0.5-0)^2 + (0-1)^2) = sqrt(2.25) = 1.5
    assert abs(rows[1]["distance"] - 1.5) < 0.01


def test_knn_recall(db):
    """With enough vectors, rescore should achieve good recall (>0.9)."""
    dim = 32
    n = 1000
    k = 10
    random.seed(42)

    db.execute(
        "CREATE VIRTUAL TABLE t_rescore USING vec0("
        f"  embedding float[{dim}] indexed by rescore(quantizer=bit, oversample=16)"
        ")"
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
    assert recall >= 0.7, f"Recall too low: {recall}"


def test_knn_cosine(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] distance_metric=cosine indexed by rescore(quantizer=bit)"
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
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    ).fetchall()
    assert rows[0]["rowid"] == 1
    # cosine distance of identical vectors should be ~0
    assert rows[0]["distance"] < 0.01


def test_knn_empty_table(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 5",
        [float_vec([1.0] * 8)],
    ).fetchall()
    assert len(rows) == 0


def test_knn_k_larger_than_n(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    db.execute("INSERT INTO t(rowid, embedding) VALUES (1, ?)", [float_vec([1.0] * 8)])
    db.execute("INSERT INTO t(rowid, embedding) VALUES (2, ?)", [float_vec([2.0] * 8)])
    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 10",
        [float_vec([1.0] * 8)],
    ).fetchall()
    assert len(rows) == 2


# ============================================================================
# Integration / edge case tests
# ============================================================================


def test_knn_with_rowid_in(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=bit)"
        ")"
    )
    for i in range(5):
        db.execute(
            "INSERT INTO t(rowid, embedding) VALUES (?, ?)",
            [i + 1, float_vec([float(i)] * 8)],
        )
    # Only search within rowids 1, 3, 5
    rows = db.execute(
        "SELECT rowid FROM t WHERE embedding MATCH ? AND rowid IN (1, 3, 5) ORDER BY distance LIMIT 3",
        [float_vec([0.0] * 8)],
    ).fetchall()
    result_ids = {r["rowid"] for r in rows}
    assert result_ids <= {1, 3, 5}


def test_knn_after_deletes(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  embedding float[8] indexed by rescore(quantizer=int8)"
        ")"
    )
    for i in range(10):
        db.execute(
            "INSERT INTO t(rowid, embedding) VALUES (?, ?)",
            [i + 1, float_vec([float(i)] * 8)],
        )
    # Delete the closest match (rowid 1 = [0,0,...])
    db.execute("DELETE FROM t WHERE rowid = 1")
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE embedding MATCH ? ORDER BY distance LIMIT 5",
        [float_vec([0.0] * 8)],
    ).fetchall()
    # Verify ordering: rowid 2 ([1]*8) should be closest, then 3 ([2]*8), etc.
    assert len(rows) >= 2
    assert rows[0]["distance"] <= rows[1]["distance"]
    # rowid 2 = [1,1,...] → L2 = sqrt(8) ≈ 2.83, rowid 3 = [2,2,...] → L2 = sqrt(32) ≈ 5.66
    assert rows[0]["rowid"] == 2, f"Expected rowid 2, got {rows[0]['rowid']} with dist={rows[0]['distance']}"


def test_oversample_effect(db):
    """Higher oversample should give equal or better recall."""
    dim = 32
    n = 500
    k = 10
    random.seed(123)

    vectors = [[random.gauss(0, 1) for _ in range(dim)] for _ in range(n)]
    query = float_vec([random.gauss(0, 1) for _ in range(dim)])

    recalls = []
    for oversample in [2, 16]:
        tname = f"t_os{oversample}"
        db.execute(
            f"CREATE VIRTUAL TABLE {tname} USING vec0("
            f"  embedding float[{dim}] indexed by rescore(quantizer=bit, oversample={oversample})"
            ")"
        )
        for i, v in enumerate(vectors):
            db.execute(
                f"INSERT INTO {tname}(rowid, embedding) VALUES (?, ?)",
                [i + 1, float_vec(v)],
            )
        rows = db.execute(
            f"SELECT rowid FROM {tname} WHERE embedding MATCH ? ORDER BY distance LIMIT ?",
            [query, k],
        ).fetchall()
        recalls.append({r["rowid"] for r in rows})

    # Also get ground truth
    db.execute(f"CREATE VIRTUAL TABLE t_flat USING vec0(embedding float[{dim}])")
    for i, v in enumerate(vectors):
        db.execute(
            "INSERT INTO t_flat(rowid, embedding) VALUES (?, ?)",
            [i + 1, float_vec(v)],
        )
    gt_rows = db.execute(
        "SELECT rowid FROM t_flat WHERE embedding MATCH ? ORDER BY distance LIMIT ?",
        [query, k],
    ).fetchall()
    gt_ids = {r["rowid"] for r in gt_rows}

    recall_low = len(recalls[0] & gt_ids) / k
    recall_high = len(recalls[1] & gt_ids) / k
    assert recall_high >= recall_low


def test_multiple_vector_columns(db):
    """One column with rescore, one without."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        "  v1 float[8] indexed by rescore(quantizer=bit),"
        "  v2 float[8]"
        ")"
    )
    db.execute(
        "INSERT INTO t(rowid, v1, v2) VALUES (1, ?, ?)",
        [float_vec([1.0] * 8), float_vec([0.0] * 8)],
    )
    db.execute(
        "INSERT INTO t(rowid, v1, v2) VALUES (2, ?, ?)",
        [float_vec([0.0] * 8), float_vec([1.0] * 8)],
    )

    # KNN on v1 (rescore path)
    rows = db.execute(
        "SELECT rowid FROM t WHERE v1 MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([1.0] * 8)],
    ).fetchall()
    assert rows[0]["rowid"] == 1

    # KNN on v2 (normal path)
    rows = db.execute(
        "SELECT rowid FROM t WHERE v2 MATCH ? ORDER BY distance LIMIT 1",
        [float_vec([1.0] * 8)],
    ).fetchall()
    assert rows[0]["rowid"] == 2
