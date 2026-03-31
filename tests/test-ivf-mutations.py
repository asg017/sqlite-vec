"""
Thorough IVF mutation tests: insert, delete, update, KNN correctness,
error cases, edge cases, and cell overflow scenarios.
"""
import pytest
import sqlite3
import struct
import math
from helpers import _f32, exec


@pytest.fixture()
def db():
    db = sqlite3.connect(":memory:")
    db.row_factory = sqlite3.Row
    db.enable_load_extension(True)
    db.load_extension("dist/vec0")
    db.enable_load_extension(False)
    return db


def ivf_total_vectors(db, table="t", col=0):
    """Count total vectors across all IVF cells."""
    return db.execute(
        f"SELECT COALESCE(SUM(n_vectors), 0) FROM {table}_ivf_cells{col:02d}"
    ).fetchone()[0]


def ivf_unassigned_count(db, table="t", col=0):
    return db.execute(
        f"SELECT COALESCE(SUM(n_vectors), 0) FROM {table}_ivf_cells{col:02d} WHERE centroid_id = -1"
    ).fetchone()[0]


def ivf_assigned_count(db, table="t", col=0):
    return db.execute(
        f"SELECT COALESCE(SUM(n_vectors), 0) FROM {table}_ivf_cells{col:02d} WHERE centroid_id >= 0"
    ).fetchone()[0]


def knn(db, query, k, table="t", col="v"):
    """Run a KNN query and return list of (rowid, distance) tuples."""
    rows = db.execute(
        f"SELECT rowid, distance FROM {table} WHERE {col} MATCH ? AND k = ?",
        [_f32(query), k],
    ).fetchall()
    return [(r[0], r[1]) for r in rows]


# ============================================================================
# Single row insert + KNN
# ============================================================================


def test_insert_single_row_knn(db):
    db.execute("CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf())")
    db.execute("INSERT INTO t(rowid, v) VALUES (1, ?)", [_f32([1, 0, 0, 0])])
    results = knn(db, [1, 0, 0, 0], 5)
    assert len(results) == 1
    assert results[0][0] == 1
    assert results[0][1] < 0.001


# ============================================================================
# Batch insert + KNN recall
# ============================================================================


def test_batch_insert_knn_recall(db):
    """Insert 200 vectors, train, verify KNN recall with nprobe=nlist."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=8, nprobe=8))"
    )
    for i in range(200):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )
    assert ivf_total_vectors(db) == 200

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")
    assert ivf_assigned_count(db) == 200

    # Query near 100 -- closest should be rowid 100
    results = knn(db, [100.0, 0, 0, 0], 10)
    assert len(results) == 10
    assert results[0][0] == 100
    assert results[0][1] < 0.01

    # All results should be near 100
    rowids = {r[0] for r in results}
    assert all(95 <= r <= 105 for r in rowids)


# ============================================================================
# Delete rows, verify they're gone from KNN
# ============================================================================


def test_delete_rows_gone_from_knn(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2, nprobe=2))"
    )
    for i in range(20):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    # Delete rowid 10
    db.execute("DELETE FROM t WHERE rowid = 10")

    results = knn(db, [10.0, 0, 0, 0], 20)
    rowids = {r[0] for r in results}
    assert 10 not in rowids


def test_delete_all_rows_empty_results(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2, nprobe=2))"
    )
    for i in range(10):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    for i in range(10):
        db.execute("DELETE FROM t WHERE rowid = ?", [i])

    assert ivf_total_vectors(db) == 0
    results = knn(db, [5.0, 0, 0, 0], 10)
    assert len(results) == 0


# ============================================================================
# Insert after delete (reuse rowids)
# ============================================================================


def test_insert_after_delete_reuse_rowid(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2, nprobe=2))"
    )
    for i in range(10):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    # Delete rowid 5
    db.execute("DELETE FROM t WHERE rowid = 5")

    # Re-insert rowid 5 with a very different vector
    db.execute(
        "INSERT INTO t(rowid, v) VALUES (5, ?)", [_f32([999.0, 0, 0, 0])]
    )

    # KNN near 999 should find rowid 5
    results = knn(db, [999.0, 0, 0, 0], 1)
    assert len(results) >= 1
    assert results[0][0] == 5


# ============================================================================
# Update vectors (INSERT OR REPLACE), verify KNN reflects new values
# ============================================================================


def test_update_vector_via_delete_insert(db):
    """vec0 IVF update: delete then re-insert with new vector."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2, nprobe=2))"
    )
    for i in range(10):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    # "Update" rowid 3: delete and re-insert with new vector
    db.execute("DELETE FROM t WHERE rowid = 3")
    db.execute(
        "INSERT INTO t(rowid, v) VALUES (3, ?)",
        [_f32([100.0, 0, 0, 0])],
    )

    # KNN near 100 should find rowid 3
    results = knn(db, [100.0, 0, 0, 0], 1)
    assert results[0][0] == 3


# ============================================================================
# Error cases: IVF + auxiliary/metadata/partition key columns
# ============================================================================


def test_error_ivf_with_auxiliary_column(db):
    result = exec(
        db,
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(), +extra text)",
    )
    assert "error" in result
    assert "auxiliary" in result.get("message", "").lower()


def test_error_ivf_with_metadata_column(db):
    result = exec(
        db,
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(), genre text)",
    )
    assert "error" in result
    assert "metadata" in result.get("message", "").lower()


def test_error_ivf_with_partition_key(db):
    result = exec(
        db,
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(), user_id integer partition key)",
    )
    assert "error" in result
    assert "partition" in result.get("message", "").lower()


def test_flat_with_auxiliary_still_works(db):
    """Regression guard: flat-indexed tables with aux columns should still work."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4], +extra text)"
    )
    db.execute(
        "INSERT INTO t(rowid, v, extra) VALUES (1, ?, 'hello')",
        [_f32([1, 0, 0, 0])],
    )
    row = db.execute("SELECT extra FROM t WHERE rowid = 1").fetchone()
    assert row[0] == "hello"


def test_flat_with_metadata_still_works(db):
    """Regression guard: flat-indexed tables with metadata columns should still work."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4], genre text)"
    )
    db.execute(
        "INSERT INTO t(rowid, v, genre) VALUES (1, ?, 'rock')",
        [_f32([1, 0, 0, 0])],
    )
    row = db.execute("SELECT genre FROM t WHERE rowid = 1").fetchone()
    assert row[0] == "rock"


def test_flat_with_partition_key_still_works(db):
    """Regression guard: flat-indexed tables with partition key should still work."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4], user_id integer partition key)"
    )
    db.execute(
        "INSERT INTO t(rowid, v, user_id) VALUES (1, ?, 42)",
        [_f32([1, 0, 0, 0])],
    )
    row = db.execute("SELECT user_id FROM t WHERE rowid = 1").fetchone()
    assert row[0] == 42


# ============================================================================
# Edge cases
# ============================================================================


def test_zero_vectors(db):
    """Insert zero vectors, verify KNN still works."""
    db.execute("CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf())")
    for i in range(5):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([0, 0, 0, 0])],
        )
    results = knn(db, [0, 0, 0, 0], 5)
    assert len(results) == 5
    # All distances should be 0
    for _, dist in results:
        assert dist < 0.001


def test_large_values(db):
    """Insert vectors with very large and small values."""
    db.execute("CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf())")
    db.execute(
        "INSERT INTO t(rowid, v) VALUES (1, ?)", [_f32([1e6, 1e6, 1e6, 1e6])]
    )
    db.execute(
        "INSERT INTO t(rowid, v) VALUES (2, ?)", [_f32([1e-6, 1e-6, 1e-6, 1e-6])]
    )
    db.execute(
        "INSERT INTO t(rowid, v) VALUES (3, ?)", [_f32([-1e6, -1e6, -1e6, -1e6])]
    )

    results = knn(db, [1e6, 1e6, 1e6, 1e6], 3)
    assert results[0][0] == 1


def test_single_row_compute_centroids(db):
    """Single row table, compute-centroids should still work."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=1))"
    )
    db.execute(
        "INSERT INTO t(rowid, v) VALUES (1, ?)", [_f32([1, 2, 3, 4])]
    )
    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")
    assert ivf_assigned_count(db) == 1

    results = knn(db, [1, 2, 3, 4], 1)
    assert len(results) == 1
    assert results[0][0] == 1


# ============================================================================
# Cell overflow (many vectors in one cell)
# ============================================================================


def test_cell_overflow_many_vectors(db):
    """Insert >64 vectors that all go to same centroid. Should create multiple cells."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=0))"
    )
    # Insert 100 very similar vectors
    for i in range(100):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([1.0 + i * 0.001, 0, 0, 0])],
        )

    # Set a single centroid so all vectors go there
    db.execute(
        "INSERT INTO t(rowid, v) VALUES ('set-centroid:0', ?)",
        [_f32([1.0, 0, 0, 0])],
    )
    db.execute("INSERT INTO t(rowid) VALUES ('assign-vectors')")

    assert ivf_assigned_count(db) == 100

    # Should have more than 1 cell (64 max per cell)
    cell_count = db.execute(
        "SELECT count(*) FROM t_ivf_cells00 WHERE centroid_id = 0"
    ).fetchone()[0]
    assert cell_count >= 2  # 100 / 64 = 2 cells needed

    # All vectors should be queryable
    results = knn(db, [1.0, 0, 0, 0], 100)
    assert len(results) == 100


# ============================================================================
# Large batch with training
# ============================================================================


def test_large_batch_with_training(db):
    """Insert 500, train, insert 500 more, verify total is 1000."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=16, nprobe=16))"
    )
    for i in range(500):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    for i in range(500, 1000):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )

    assert ivf_total_vectors(db) == 1000

    # KNN should still work
    results = knn(db, [750.0, 0, 0, 0], 5)
    assert len(results) == 5
    assert results[0][0] == 750


# ============================================================================
# KNN after interleaved insert/delete
# ============================================================================


def test_knn_after_interleaved_insert_delete(db):
    """Insert 20, train, delete 10 closest to query, verify remaining."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4, nprobe=4))"
    )
    for i in range(20):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    # Delete rowids 0-9 (closest to query at 5.0)
    for i in range(10):
        db.execute("DELETE FROM t WHERE rowid = ?", [i])

    results = knn(db, [5.0, 0, 0, 0], 10)
    rowids = {r[0] for r in results}
    # None of the deleted rowids should appear
    assert all(r >= 10 for r in rowids)
    assert len(results) == 10


def test_knn_empty_centroids_after_deletes(db):
    """Some centroids may become empty after deletes. Should not crash."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4, nprobe=2))"
    )
    # Insert vectors clustered near 4 points
    for i in range(40):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i % 10) * 10, 0, 0, 0])],
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    # Delete a bunch, potentially emptying some centroids
    for i in range(30):
        db.execute("DELETE FROM t WHERE rowid = ?", [i])

    # Should not crash even with empty centroids
    results = knn(db, [50.0, 0, 0, 0], 20)
    assert len(results) <= 10  # only 10 left


# ============================================================================
# KNN returns correct distances
# ============================================================================


def test_knn_correct_distances(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2, nprobe=2))"
    )
    db.execute("INSERT INTO t(rowid, v) VALUES (1, ?)", [_f32([0, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, v) VALUES (2, ?)", [_f32([3, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, v) VALUES (3, ?)", [_f32([0, 4, 0, 0])])

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    results = knn(db, [0, 0, 0, 0], 3)
    result_map = {r[0]: r[1] for r in results}

    # L2 distances (sqrt of sum of squared differences)
    assert abs(result_map[1] - 0.0) < 0.01
    assert abs(result_map[2] - 3.0) < 0.01   # sqrt(3^2) = 3
    assert abs(result_map[3] - 4.0) < 0.01   # sqrt(4^2) = 4


# ============================================================================
# Delete in flat mode leaves no orphan rowid_map entries
# ============================================================================


def test_delete_flat_mode_rowid_map_count(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4))"
    )
    for i in range(5):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )

    db.execute("DELETE FROM t WHERE rowid = 0")
    db.execute("DELETE FROM t WHERE rowid = 2")
    db.execute("DELETE FROM t WHERE rowid = 4")

    assert db.execute("SELECT count(*) FROM t_ivf_rowid_map00").fetchone()[0] == 2
    assert ivf_unassigned_count(db) == 2


# ============================================================================
# Duplicate rowid insert
# ============================================================================


def test_delete_reinsert_as_update(db):
    """Simulate update via delete + insert on same rowid."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2, nprobe=2))"
    )
    db.execute("INSERT INTO t(rowid, v) VALUES (1, ?)", [_f32([1, 0, 0, 0])])

    # Delete then re-insert as "update"
    db.execute("DELETE FROM t WHERE rowid = 1")
    db.execute("INSERT INTO t(rowid, v) VALUES (1, ?)", [_f32([99, 0, 0, 0])])

    results = knn(db, [99, 0, 0, 0], 1)
    assert len(results) == 1
    assert results[0][0] == 1
    assert results[0][1] < 0.01


def test_duplicate_rowid_insert_fails(db):
    """Inserting a duplicate rowid should fail with a constraint error."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2, nprobe=2))"
    )
    db.execute("INSERT INTO t(rowid, v) VALUES (1, ?)", [_f32([1, 0, 0, 0])])

    result = exec(
        db,
        "INSERT INTO t(rowid, v) VALUES (1, ?)",
        [_f32([99, 0, 0, 0])],
    )
    assert "error" in result


# ============================================================================
# Interleaved insert/delete with KNN correctness
# ============================================================================


def test_interleaved_ops_correctness(db):
    """Complex sequence of inserts and deletes, verify KNN is always correct."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4, nprobe=4))"
    )

    # Phase 1: Insert 50 vectors
    for i in range(50):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    # Phase 2: Delete even-numbered rowids
    for i in range(0, 50, 2):
        db.execute("DELETE FROM t WHERE rowid = ?", [i])

    # Phase 3: Insert new vectors with higher rowids
    for i in range(50, 75):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([float(i), 0, 0, 0])],
        )

    # Phase 4: Delete some of the new ones
    for i in range(60, 70):
        db.execute("DELETE FROM t WHERE rowid = ?", [i])

    # KNN query: should only find existing vectors
    results = knn(db, [25.0, 0, 0, 0], 50)
    rowids = {r[0] for r in results}

    # Verify no deleted rowids appear
    deleted = set(range(0, 50, 2)) | set(range(60, 70))
    assert len(rowids & deleted) == 0

    # Verify we get the right count (25 odd + 15 new - 10 deleted new = 30)
    expected_alive = set(range(1, 50, 2)) | set(range(50, 60)) | set(range(70, 75))
    assert rowids.issubset(expected_alive)
