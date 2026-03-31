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
    """Count vectors in unassigned cells (centroid_id=-1)."""
    return db.execute(
        f"SELECT COALESCE(SUM(n_vectors), 0) FROM {table}_ivf_cells{col:02d} WHERE centroid_id = -1"
    ).fetchone()[0]


def ivf_assigned_count(db, table="t", col=0):
    """Count vectors in trained cells (centroid_id >= 0)."""
    return db.execute(
        f"SELECT COALESCE(SUM(n_vectors), 0) FROM {table}_ivf_cells{col:02d} WHERE centroid_id >= 0"
    ).fetchone()[0]


# ============================================================================
# Parser tests
# ============================================================================


def test_ivf_create_defaults(db):
    """ivf() with no args uses defaults."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf())"
    )
    tables = [
        r[0]
        for r in db.execute(
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY 1"
        ).fetchall()
    ]
    assert "t_ivf_centroids00" in tables
    assert "t_ivf_cells00" in tables
    assert "t_ivf_rowid_map00" in tables


def test_ivf_create_custom_params(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=64, nprobe=8))"
    )
    tables = [
        r[0]
        for r in db.execute(
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY 1"
        ).fetchall()
    ]
    assert "t_ivf_centroids00" in tables
    assert "t_ivf_cells00" in tables


def test_ivf_create_with_distance_metric(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] distance_metric=cosine indexed by ivf(nlist=16))"
    )
    tables = [
        r[0]
        for r in db.execute(
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY 1"
        ).fetchall()
    ]
    assert "t_ivf_centroids00" in tables


def test_ivf_create_error_unknown_key(db):
    result = exec(
        db,
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(bogus=1))",
    )
    assert "error" in result


def test_ivf_create_error_nprobe_gt_nlist(db):
    result = exec(
        db,
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4, nprobe=10))",
    )
    assert "error" in result


# ============================================================================
# Shadow table tests
# ============================================================================


def test_ivf_shadow_tables_created(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=8))"
    )
    trained = db.execute(
        "SELECT value FROM t_info WHERE key = 'ivf_trained_0'"
    ).fetchone()[0]
    assert str(trained) == "0"

    # No cells yet (created lazily on first insert)
    count = db.execute(
        "SELECT count(*) FROM t_ivf_cells00"
    ).fetchone()[0]
    assert count == 0


def test_ivf_drop_cleans_up(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4))"
    )
    db.execute("DROP TABLE t")
    tables = [
        r[0]
        for r in db.execute(
            "SELECT name FROM sqlite_master WHERE type='table'"
        ).fetchall()
    ]
    assert not any("ivf" in t for t in tables)


# ============================================================================
# Insert tests (flat mode)
# ============================================================================


def test_ivf_insert_flat_mode(db):
    """Before training, vectors go to unassigned cell."""
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4))"
    )
    db.execute("INSERT INTO t(rowid, v) VALUES (1, ?)", [_f32([1, 2, 3, 4])])
    db.execute("INSERT INTO t(rowid, v) VALUES (2, ?)", [_f32([5, 6, 7, 8])])

    assert ivf_unassigned_count(db) == 2
    assert ivf_assigned_count(db) == 0

    # rowid_map should have 2 entries
    assert db.execute("SELECT count(*) FROM t_ivf_rowid_map00").fetchone()[0] == 2


def test_ivf_delete_flat_mode(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4))"
    )
    db.execute("INSERT INTO t(rowid, v) VALUES (1, ?)", [_f32([1, 2, 3, 4])])
    db.execute("INSERT INTO t(rowid, v) VALUES (2, ?)", [_f32([5, 6, 7, 8])])
    db.execute("DELETE FROM t WHERE rowid = 1")

    assert ivf_unassigned_count(db) == 1
    assert db.execute("SELECT count(*) FROM t_ivf_rowid_map00").fetchone()[0] == 1


# ============================================================================
# KNN flat mode tests
# ============================================================================


def test_ivf_knn_flat_mode(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4))"
    )
    db.execute("INSERT INTO t(rowid, v) VALUES (1, ?)", [_f32([1, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, v) VALUES (2, ?)", [_f32([2, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, v) VALUES (3, ?)", [_f32([9, 0, 0, 0])])

    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE v MATCH ? AND k = 2",
        [_f32([1.5, 0, 0, 0])],
    ).fetchall()
    assert len(rows) == 2
    rowids = {r[0] for r in rows}
    assert rowids == {1, 2}


def test_ivf_knn_flat_empty(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4))"
    )
    rows = db.execute(
        "SELECT rowid FROM t WHERE v MATCH ? AND k = 5",
        [_f32([1, 0, 0, 0])],
    ).fetchall()
    assert len(rows) == 0


# ============================================================================
# compute-centroids tests
# ============================================================================


def test_compute_centroids(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4))"
    )
    for i in range(40):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([i % 10, i // 10, 0, 0])],
        )

    assert ivf_unassigned_count(db) == 40

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    # After training: unassigned cell should be gone (or empty), vectors in trained cells
    assert ivf_unassigned_count(db) == 0
    assert ivf_assigned_count(db) == 40
    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 4
    trained = db.execute(
        "SELECT value FROM t_info WHERE key='ivf_trained_0'"
    ).fetchone()[0]
    assert str(trained) == "1"


def test_compute_centroids_recompute(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2))"
    )
    for i in range(20):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)", [i, _f32([i, 0, 0, 0])]
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")
    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 2

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")
    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 2
    assert ivf_assigned_count(db) == 20


# ============================================================================
# Insert after training (assigned mode)
# ============================================================================


def test_ivf_insert_after_training(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2))"
    )
    for i in range(20):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)", [i, _f32([i, 0, 0, 0])]
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    db.execute(
        "INSERT INTO t(rowid, v) VALUES (100, ?)", [_f32([5, 0, 0, 0])]
    )

    # Should be in a trained cell, not unassigned
    row = db.execute(
        "SELECT m.cell_id, c.centroid_id FROM t_ivf_rowid_map00 m "
        "JOIN t_ivf_cells00 c ON c.rowid = m.cell_id "
        "WHERE m.rowid = 100"
    ).fetchone()
    assert row is not None
    assert row[1] >= 0  # centroid_id >= 0 means trained cell


# ============================================================================
# KNN after training (IVF probe mode)
# ============================================================================


def test_ivf_knn_after_training(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=4, nprobe=4))"
    )
    for i in range(100):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)", [i, _f32([i, 0, 0, 0])]
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE v MATCH ? AND k = 5",
        [_f32([50.0, 0, 0, 0])],
    ).fetchall()
    assert len(rows) == 5
    assert rows[0][0] == 50
    assert rows[0][1] < 0.01


def test_ivf_knn_k_larger_than_n(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2, nprobe=2))"
    )
    for i in range(5):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)", [i, _f32([i, 0, 0, 0])]
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    rows = db.execute(
        "SELECT rowid FROM t WHERE v MATCH ? AND k = 100",
        [_f32([0, 0, 0, 0])],
    ).fetchall()
    assert len(rows) == 5


# ============================================================================
# Manual centroid import (set-centroid, assign-vectors)
# ============================================================================


def test_set_centroid_and_assign(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=0))"
    )
    for i in range(20):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)", [i, _f32([i, 0, 0, 0])]
        )

    db.execute(
        "INSERT INTO t(rowid, v) VALUES ('set-centroid:0', ?)",
        [_f32([5, 0, 0, 0])],
    )
    db.execute(
        "INSERT INTO t(rowid, v) VALUES ('set-centroid:1', ?)",
        [_f32([15, 0, 0, 0])],
    )

    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 2

    db.execute("INSERT INTO t(rowid) VALUES ('assign-vectors')")

    assert ivf_unassigned_count(db) == 0
    assert ivf_assigned_count(db) == 20


# ============================================================================
# clear-centroids
# ============================================================================


def test_clear_centroids(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2))"
    )
    for i in range(20):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)", [i, _f32([i, 0, 0, 0])]
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")
    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 2

    db.execute("INSERT INTO t(rowid) VALUES ('clear-centroids')")
    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 0
    assert ivf_unassigned_count(db) == 20
    trained = db.execute(
        "SELECT value FROM t_info WHERE key='ivf_trained_0'"
    ).fetchone()[0]
    assert str(trained) == "0"


# ============================================================================
# Delete after training
# ============================================================================


def test_ivf_delete_after_training(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2))"
    )
    for i in range(10):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)", [i, _f32([i, 0, 0, 0])]
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")
    assert ivf_assigned_count(db) == 10

    db.execute("DELETE FROM t WHERE rowid = 5")
    assert ivf_assigned_count(db) == 9
    assert db.execute("SELECT count(*) FROM t_ivf_rowid_map00").fetchone()[0] == 9


# ============================================================================
# Recall test
# ============================================================================


def test_ivf_recall_nprobe_equals_nlist(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=8, nprobe=8))"
    )
    for i in range(100):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)", [i, _f32([i, 0, 0, 0])]
        )

    db.execute("INSERT INTO t(rowid) VALUES ('compute-centroids')")

    rows = db.execute(
        "SELECT rowid FROM t WHERE v MATCH ? AND k = 10",
        [_f32([50.0, 0, 0, 0])],
    ).fetchall()
    rowids = {r[0] for r in rows}

    # 45 and 55 are equidistant from 50, so either may appear in top 10
    assert 50 in rowids
    assert len(rowids) == 10
    assert all(45 <= r <= 55 for r in rowids)
