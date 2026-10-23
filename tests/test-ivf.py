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

    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")

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

    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")
    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 2

    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")
    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 2
    assert ivf_assigned_count(db) == 20


@pytest.mark.parametrize(
    "command,quantizer",
    [
        ("compute-centroids", "none"),
        ("compute-centroids", "int8"),
        ("clear-centroids", "none"),
        ("assign-vectors", "none"),
    ],
)
def test_ivf_command_interrupted_at_each_step(command, quantizer):
    """However far a command gets before one of its statements is
    interrupted, it reports the interrupt and loses no row: every row is
    still found, and found again once the command is run to the end."""
    knn = "SELECT rowid FROM t WHERE v MATCH ? AND k = 20"
    query = _f32([0.5, 0.5, 0, 1])

    def table():
        db = sqlite3.connect(":memory:", isolation_level=None)
        db.enable_load_extension(True)
        db.load_extension("dist/vec0")
        db.enable_load_extension(False)
        db.execute(
            "CREATE VIRTUAL TABLE t USING vec0("
            f"v float[4] indexed by ivf(nlist=2, nprobe=2, quantizer={quantizer}))"
        )
        for i in range(20):
            db.execute(
                "INSERT INTO t(rowid, v) VALUES (?, ?)",
                [i, _f32([i / 20, (i % 7) / 7, 0, 1])],
            )
        if command == "clear-centroids":
            db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")
        if command == "assign-vectors":
            for cid, v in [(0, [0.2, 0.2, 0, 1]), (1, [0.8, 0.8, 0, 1])]:
                db.execute(
                    "INSERT INTO t(t, v) VALUES (?, ?)", [f"set-centroid:{cid}", _f32(v)]
                )
        return db

    n = 1
    while True:
        db = table()
        steps = 0

        def interrupt_step_n():
            nonlocal steps
            steps += 1
            return steps == n

        db.set_progress_handler(interrupt_step_n, 1)
        try:
            db.execute("INSERT INTO t(t) VALUES (?)", [command])
            error = None
        except sqlite3.OperationalError as e:
            error = str(e)
        db.set_progress_handler(None, 0)

        assert error in (None, "interrupted")
        assert len(db.execute(knn, [query]).fetchall()) == 20
        if steps < n:
            break
        if error is not None:
            db.execute("INSERT INTO t(t) VALUES (?)", [command])
            assert len(db.execute(knn, [query]).fetchall()) == 20
        n += 1
    assert n > 1


def _ivf_interrupt_table(trained, options):
    db = sqlite3.connect(":memory:", isolation_level=None)
    db.enable_load_extension(True)
    db.load_extension("dist/vec0")
    db.enable_load_extension(False)
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0("
        f"v float[4] indexed by ivf(nlist=2, nprobe=2, {options}))"
    )
    for i in range(20):
        db.execute(
            "INSERT INTO t(rowid, v) VALUES (?, ?)",
            [i, _f32([i / 20, (i % 7) / 7, 0, 1])],
        )
    if trained:
        db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")
    return db


@pytest.mark.parametrize(
    "trained,options",
    [(False, "quantizer=none"), (True, "quantizer=none"), (True, "quantizer=int8")],
)
def test_ivf_delete_interrupted_at_each_step(trained, options):
    """However far a delete gets before one of its statements is
    interrupted, a delete that reports success takes its row out of the
    index, and one that fails leaves the row in the table and the index, or,
    when the interrupt comes after the row is gone, as it can for any SQLite
    table, in neither."""
    knn = "SELECT rowid FROM t WHERE v MATCH ? AND k = 20"
    query = _f32([0.5, 0.5, 0, 1])
    kept = (list(range(20)), 1)
    deleted = ([i for i in range(20) if i != 5], 0)
    n = 1
    while True:
        db = _ivf_interrupt_table(trained, options)
        steps = 0

        def interrupt_step_n():
            nonlocal steps
            steps += 1
            return steps == n

        db.set_progress_handler(interrupt_step_n, 1)
        try:
            db.execute("DELETE FROM t WHERE rowid = 5")
            failed = False
        except sqlite3.OperationalError:
            failed = True
        db.set_progress_handler(None, 0)

        state = (
            sorted(row[0] for row in db.execute(knn, [query])),
            db.execute("SELECT count(*) FROM t WHERE rowid = 5").fetchone()[0],
        )
        if failed:
            assert state in (kept, deleted)
        else:
            assert state == deleted
        if steps < n:
            break
        n += 1
    assert n > 1


@pytest.mark.parametrize(
    "trained,options",
    [
        (False, "quantizer=none"),
        (True, "quantizer=none"),
        (True, "quantizer=int8"),
        (True, "quantizer=int8, oversample=4"),
    ],
)
def test_ivf_knn_interrupted_at_each_step(trained, options):
    """However far a KNN query gets before one of its statements is
    interrupted, it reports the interrupt or returns every row it should."""
    db = _ivf_interrupt_table(trained, options)
    knn = "SELECT rowid, distance FROM t WHERE v MATCH ? AND k = 20"
    query = _f32([0.5, 0.5, 0, 1])
    expected = db.execute(knn, [query]).fetchall()
    assert len(expected) == 20
    n = 1
    while True:
        steps = 0

        def interrupt_step_n():
            nonlocal steps
            steps += 1
            return steps == n

        db.set_progress_handler(interrupt_step_n, 1)
        try:
            rows = db.execute(knn, [query]).fetchall()
            error = None
        except sqlite3.OperationalError as e:
            error = str(e)
        db.set_progress_handler(None, 0)

        assert error in (None, "interrupted")
        if error is None:
            assert rows == expected
        if steps < n:
            break
        n += 1
    assert n > 1


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

    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")

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

    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")

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

    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")

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
        "INSERT INTO t(t, v) VALUES ('set-centroid:0', ?)",
        [_f32([5, 0, 0, 0])],
    )
    db.execute(
        "INSERT INTO t(t, v) VALUES ('set-centroid:1', ?)",
        [_f32([15, 0, 0, 0])],
    )

    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 2

    db.execute("INSERT INTO t(t) VALUES ('assign-vectors')")

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

    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")
    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 2

    db.execute("INSERT INTO t(t) VALUES ('clear-centroids')")
    assert db.execute("SELECT count(*) FROM t_ivf_centroids00").fetchone()[0] == 0
    assert ivf_unassigned_count(db) == 20
    trained = db.execute(
        "SELECT value FROM t_info WHERE key='ivf_trained_0'"
    ).fetchone()[0]
    assert str(trained) == "0"


# ============================================================================
# Trained state across connections and rollbacks
# ============================================================================


def ivf_connect(path):
    db = sqlite3.connect(path, isolation_level=None)
    db.enable_load_extension(True)
    db.load_extension("dist/vec0")
    db.enable_load_extension(False)
    return db


def ivf_create_rows(db):
    db.execute(
        "CREATE VIRTUAL TABLE t USING vec0(v float[4] indexed by ivf(nlist=2, nprobe=2))"
    )
    for i in range(20):
        db.execute("INSERT INTO t(rowid, v) VALUES (?, ?)", [i, _f32([i, 0, 0, 0])])


IVF_KNN = "SELECT rowid FROM t WHERE v MATCH ? AND k = 3"


def test_ivf_knn_after_another_connection_trains(tmp_path):
    """A connection that queried before another ran compute-centroids still
    finds the rows, which have moved into the trained cells."""
    path = str(tmp_path / "ivf.db")
    a = ivf_connect(path)
    ivf_create_rows(a)
    assert len(a.execute(IVF_KNN, [_f32([10, 0, 0, 0])]).fetchall()) == 3

    ivf_connect(path).execute("INSERT INTO t(t) VALUES ('compute-centroids')")
    assert len(a.execute(IVF_KNN, [_f32([10, 0, 0, 0])]).fetchall()) == 3


def test_ivf_insert_after_another_connection_clears(tmp_path):
    """A connection that trained the index can insert after another
    connection ran clear-centroids."""
    path = str(tmp_path / "ivf.db")
    a = ivf_connect(path)
    ivf_create_rows(a)
    a.execute("INSERT INTO t(t) VALUES ('compute-centroids')")

    ivf_connect(path).execute("INSERT INTO t(t) VALUES ('clear-centroids')")
    a.execute("INSERT INTO t(rowid, v) VALUES (20, ?)", [_f32([20, 0, 0, 0])])
    rows = a.execute(IVF_KNN, [_f32([20, 0, 0, 0])]).fetchall()
    assert rows[0][0] == 20


def test_ivf_trained_state_after_rollback(db):
    """A rolled-back compute-centroids leaves the index untrained, so inserts
    and queries work as before it."""
    db.isolation_level = None
    ivf_create_rows(db)
    db.execute("BEGIN")
    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")
    db.execute("ROLLBACK")

    db.execute("INSERT INTO t(rowid, v) VALUES (20, ?)", [_f32([20, 0, 0, 0])])
    rows = db.execute(IVF_KNN, [_f32([20, 0, 0, 0])]).fetchall()
    assert rows[0][0] == 20


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

    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")
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

    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")

    rows = db.execute(
        "SELECT rowid FROM t WHERE v MATCH ? AND k = 10",
        [_f32([50.0, 0, 0, 0])],
    ).fetchall()
    rowids = {r[0] for r in rows}

    # 45 and 55 are equidistant from 50, so either may appear in top 10
    assert 50 in rowids
    assert len(rowids) == 10
    assert all(45 <= r <= 55 for r in rowids)
