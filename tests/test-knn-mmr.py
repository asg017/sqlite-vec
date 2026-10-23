import sqlite3
from collections import OrderedDict


def test_mmr_cosine_diversity(db, snapshot):
    """MMR should select diverse results from clustered candidates."""
    db.execute(
        "create virtual table v using vec0(embedding float[3] distance_metric=cosine)"
    )
    db.executemany(
        "insert into v(rowid, embedding) values (?, ?)",
        [
            [1, "[1,0,0]"],
            [2, "[0.99,0.1,0]"],
            [3, "[0.98,0.2,0]"],
            [4, "[0,1,0]"],
            [5, "[0,0,1]"],
        ],
    )

    BASE_KNN = "select rowid, distance from v where embedding match ? and k = ?"

    # Baseline: no MMR — returns nearest neighbors
    assert exec(db, BASE_KNN, ["[1,0,0]", 3]) == snapshot()

    # lambda=1.0 — pure relevance, same order as baseline
    assert (
        exec(db, BASE_KNN + " and mmr_lambda = ?", ["[1,0,0]", 3, 1.0]) == snapshot()
    )

    # lambda=0.5 — balanced: should include diverse results
    assert (
        exec(db, BASE_KNN + " and mmr_lambda = ?", ["[1,0,0]", 3, 0.5]) == snapshot()
    )

    # lambda=0.0 — pure diversity
    assert (
        exec(db, BASE_KNN + " and mmr_lambda = ?", ["[1,0,0]", 3, 0.0]) == snapshot()
    )


def test_mmr_l2_metric(db, snapshot):
    """MMR should work with default L2 distance metric."""
    db.execute("create virtual table v using vec0(embedding float[3])")
    db.executemany(
        "insert into v(rowid, embedding) values (?, ?)",
        [
            [1, "[1,0,0]"],
            [2, "[0.99,0.01,0]"],
            [3, "[0.98,0.02,0]"],
            [4, "[0,1,0]"],
            [5, "[0,0,1]"],
        ],
    )

    BASE_KNN = "select rowid, distance from v where embedding match ? and k = ?"
    assert (
        exec(db, BASE_KNN + " and mmr_lambda = ?", ["[1,0,0]", 3, 0.5]) == snapshot()
    )


def test_mmr_int8_vectors(db, snapshot):
    """MMR should work with int8 vector element type."""
    db.execute(
        "create virtual table v using vec0(embedding int8[3] distance_metric=cosine)"
    )
    for rowid, vec in [
        (1, "[100,0,0]"),
        (2, "[99,1,0]"),
        (3, "[98,2,0]"),
        (4, "[0,100,0]"),
        (5, "[0,0,100]"),
    ]:
        db.execute(
            "insert into v(rowid, embedding) values (?, vec_int8(?))", [rowid, vec]
        )

    assert (
        exec(
            db,
            "select rowid, distance from v where embedding match vec_int8(?) and k = ? and mmr_lambda = ?",
            ["[100,0,0]", 3, 0.5],
        )
        == snapshot()
    )


def test_mmr_clustering(db, snapshot):
    """MMR should break cluster monopoly in results."""
    db.execute(
        "create virtual table v using vec0(embedding float[3] distance_metric=cosine)"
    )
    db.executemany(
        "insert into v(rowid, embedding) values (?, ?)",
        [
            # cluster 1: near [1,0,0]
            [1, "[1.0,0,0]"],
            [2, "[0.99,0.1,0]"],
            [3, "[0.98,0.15,0]"],
            [4, "[0.97,0.2,0]"],
            [5, "[0.96,0.25,0]"],
            # cluster 2: near [0,1,0]
            [6, "[0,1.0,0]"],
            [7, "[0.1,0.99,0]"],
            # cluster 3: near [0,0,1]
            [8, "[0,0,1.0]"],
            [9, "[0,0.1,0.99]"],
        ],
    )

    BASE_KNN = "select rowid, distance from v where embedding match ? and k = ?"

    # Without MMR: top 5 all from cluster 1
    assert exec(db, BASE_KNN, ["[1,0,0]", 5]) == snapshot()

    # With MMR: should include results from other clusters
    assert (
        exec(db, BASE_KNN + " and mmr_lambda = ?", ["[1,0,0]", 5, 0.5]) == snapshot()
    )


def test_mmr_with_distance_constraint(db, snapshot):
    """MMR should compose with distance column constraints."""
    db.execute(
        "create virtual table v using vec0(embedding float[3] distance_metric=cosine)"
    )
    db.executemany(
        "insert into v(rowid, embedding) values (?, ?)",
        [
            [1, "[1,0,0]"],
            [2, "[0.99,0.1,0]"],
            [3, "[0.98,0.2,0]"],
            [4, "[0,1,0]"],
            [5, "[0,0,1]"],
        ],
    )

    # distance > 0.001 excludes the exact match, MMR diversifies the rest
    assert (
        exec(
            db,
            "select rowid, distance from v where embedding match ? and k = ? and mmr_lambda = ? and distance > 0.001",
            ["[1,0,0]", 3, 0.5],
        )
        == snapshot()
    )


def test_mmr_with_partition_key(db, snapshot):
    """MMR should compose with partition key constraints."""
    db.execute(
        "create virtual table v using vec0(category text partition key, embedding float[3] distance_metric=cosine)"
    )
    db.executemany(
        "insert into v(rowid, category, embedding) values (?, ?, ?)",
        [
            [1, "a", "[1,0,0]"],
            [2, "a", "[0.99,0.1,0]"],
            [3, "a", "[0.98,0.2,0]"],
            [4, "a", "[0,1,0]"],
            [5, "a", "[0,0,1]"],
            [6, "b", "[1,0,0]"],
        ],
    )

    BASE_KNN = "select rowid, distance from v where embedding match ? and k = ? and category = ?"
    assert (
        exec(db, BASE_KNN + " and mmr_lambda = ?", ["[1,0,0]", 3, "a", 0.5])
        == snapshot()
    )


def test_mmr_edge_cases(db, snapshot):
    """Edge cases: k=1, k=0, single row, lambda boundaries."""
    db.execute(
        "create virtual table v using vec0(embedding float[3] distance_metric=cosine)"
    )
    db.executemany(
        "insert into v(rowid, embedding) values (?, ?)",
        [
            [1, "[1,0,0]"],
            [2, "[0,1,0]"],
        ],
    )

    BASE_KNN = "select rowid, distance from v where embedding match ? and k = ?"

    # k=1 with MMR — should return closest
    assert (
        exec(db, BASE_KNN + " and mmr_lambda = ?", ["[1,0,0]", 1, 0.5]) == snapshot()
    )

    # k=0 with MMR — should return empty
    assert (
        exec(db, BASE_KNN + " and mmr_lambda = ?", ["[1,0,0]", 0, 0.5]) == snapshot()
    )


def test_mmr_error_invalid_lambda(db, snapshot):
    """Out-of-range mmr_lambda should return an error."""
    db.execute(
        "create virtual table v using vec0(embedding float[3] distance_metric=cosine)"
    )
    db.execute("insert into v(rowid, embedding) values (1, '[1,0,0]')")

    BASE_KNN = "select rowid, distance from v where embedding match ? and k = ?"

    # lambda > 1.0
    assert (
        exec(db, BASE_KNN + " and mmr_lambda = ?", ["[1,0,0]", 1, 1.5]) == snapshot()
    )

    # lambda < 0.0
    assert (
        exec(db, BASE_KNN + " and mmr_lambda = ?", ["[1,0,0]", 1, -0.1]) == snapshot()
    )


def test_mmr_insert_guard(db, snapshot):
    """Cannot insert a value for the hidden mmr_lambda column."""
    db.execute("create virtual table v using vec0(embedding float[3])")

    assert (
        exec(
            db,
            "insert into v(rowid, embedding, mmr_lambda) values (1, '[1,0,0]', 0.5)",
        )
        == snapshot()
    )


def exec(db, sql, parameters=[]):
    try:
        rows = db.execute(sql, parameters).fetchall()
    except (sqlite3.OperationalError, sqlite3.DatabaseError) as e:
        return {
            "error": e.__class__.__name__,
            "message": str(e),
        }
    a = []
    for row in rows:
        o = OrderedDict()
        for k in row.keys():
            o[k] = row[k]
        a.append(o)
    result = OrderedDict()
    result["sql"] = sql
    result["rows"] = a
    return result
