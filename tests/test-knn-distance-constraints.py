import random

import pytest
import sqlite3
from helpers import exec


def test_normal(db, snapshot):
    db.execute("create virtual table v using vec0(embedding float[1], is_odd boolean, chunk_size=8)")
    db.executemany(
        "insert into v(rowid, is_odd, embedding) values (?1, ?1 % 2, ?2)",
        [
            [1, "[1]"],
            [2, "[2]"],
            [3, "[3]"],
            [4, "[4]"],
            [5, "[5]"],
            [6, "[6]"],
            [7, "[7]"],
            [8, "[8]"],
            [9, "[9]"],
            [10, "[10]"],
            [11, "[11]"],
            [12, "[12]"],
            [13, "[13]"],
            [14, "[14]"],
            [15, "[15]"],
            [16, "[16]"],
            [17, "[17]"],
        ],
    )
    assert exec(db,"SELECT * FROM v") == snapshot()

    BASE_KNN = "select rowid, distance from v where embedding match ? and k = ? "
    assert exec(db, BASE_KNN, ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance > 5", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance >= 5", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance < 3", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance <= 3", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance > 7 AND distance <= 10", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND distance BETWEEN 7 AND 10", ["[1]", 5]) == snapshot()
    assert exec(db, BASE_KNN + "AND is_odd == TRUE AND distance BETWEEN 7 AND 10", ["[1]", 5]) == snapshot()


# vec0BestIndex() sets `omit = 1` on distance constraints, which promises
# SQLite that the vtab applies them itself. Every KNN backend must honor that
# promise -- if one doesn't, the WHERE clause is silently dropped from the
# query plan and rows that violate it are returned with no error at all.
def _has_ivf():
    db = sqlite3.connect(":memory:")
    db.enable_load_extension(True)
    db.load_extension("dist/vec0")
    flags = db.execute("SELECT vec_debug()").fetchone()[0]
    return "ivf" in flags.split("Build flags:")[-1].split()


ANN_INDEX_DEFS = [
    pytest.param("", id="flat"),
    pytest.param(
        "INDEXED BY rescore(quantizer=bit, oversample=16)", id="rescore-bit"
    ),
    pytest.param(
        "INDEXED BY rescore(quantizer=int8, oversample=16)", id="rescore-int8"
    ),
    pytest.param(
        "INDEXED BY diskann(neighbor_quantizer=int8)", id="diskann"
    ),
    pytest.param(
        "INDEXED BY ivf(nlist=16, nprobe=8)",
        id="ivf",
        marks=pytest.mark.skipif(
            not _has_ivf(),
            reason="IVF not enabled (compile with -DSQLITE_VEC_EXPERIMENTAL_IVF_ENABLE=1)",
        ),
    ),
]

# Indexes that apply the constraint to their candidate pool *before* the top-k
# truncation, and can therefore still return a full k rows. DiskANN and IVF
# filter their final result set instead, so a lower-bound constraint there
# legitimately yields fewer than k rows.
INDEX_DEFS_FILLING_K = [p for p in ANN_INDEX_DEFS if p.id in ("flat", "rescore-bit", "rescore-int8")]

DIMENSIONS = 8
NROWS = 200


def _seed(db, index_def):
    db.execute(
        f"CREATE VIRTUAL TABLE v USING vec0(embedding float[{DIMENSIONS}] {index_def})"
    )
    rng = random.Random(0)
    rows = [
        (i, "[" + ",".join(str(rng.random()) for _ in range(DIMENSIONS)) + "]")
        for i in range(1, NROWS + 1)
    ]
    db.executemany("INSERT INTO v(rowid, embedding) VALUES (?, ?)", rows)
    return rows


@pytest.mark.parametrize("index_def", ANN_INDEX_DEFS)
@pytest.mark.parametrize(
    "op,predicate",
    [
        ("<=", lambda d, t: d <= t),
        ("<", lambda d, t: d < t),
        (">=", lambda d, t: d >= t),
        (">", lambda d, t: d > t),
    ],
)
def test_distance_constraint_is_honored_by_every_index(db, index_def, op, predicate):
    """Regression test for #308.

    Distance constraints were only implemented in the FLAT chunk scan. The
    rescore path (added later, in #276) never read them back out of idxStr, so
    `AND distance <= x` was silently ignored and every top-k row was returned.
    """
    rows = _seed(db, index_def)
    query = rows[0][1]

    unfiltered = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH ? AND k = 20",
        (query,),
    ).fetchall()
    assert len(unfiltered) == 20

    # Pick a threshold in the middle of the observed distance range, so that
    # the constraint is neither a no-op nor filters everything out.
    distances = sorted(row["distance"] for row in unfiltered)
    threshold = distances[len(distances) // 2]

    filtered = db.execute(
        f"SELECT rowid, distance FROM v WHERE embedding MATCH ? AND k = 20 "
        f"AND distance {op} ?",
        (query, threshold),
    ).fetchall()

    violations = [row["distance"] for row in filtered if not predicate(row["distance"], threshold)]
    assert violations == [], (
        f"{len(violations)} row(s) violating `distance {op} {threshold}` were "
        f"returned by index `{index_def or 'flat'}`"
    )


@pytest.mark.parametrize("index_def", ANN_INDEX_DEFS)
def test_distance_constraint_filtering_everything_is_not_an_error(db, index_def):
    """An impossible constraint must yield an empty result set, not an error.

    On the rescore path this exercises the case where every rescored candidate
    is filtered out: the result arrays are then zero-length, and a naive
    sqlite3_malloc(0) returning NULL would be misreported as SQLITE_NOMEM.
    """
    rows = _seed(db, index_def)
    result = db.execute(
        "SELECT rowid FROM v WHERE embedding MATCH ? AND k = 10 AND distance < ?",
        (rows[0][1], -1.0),
    ).fetchall()
    assert result == []


@pytest.mark.parametrize("index_def", INDEX_DEFS_FILLING_K)
def test_distance_constraint_lower_bound_still_fills_k(db, index_def):
    """A `distance >` constraint must not shrink the result set.

    The constraint has to be applied to the candidate pool *before* the top-k
    truncation. Applying it afterwards would chop off the head of the sorted
    results and return fewer than k rows.
    """
    rows = _seed(db, index_def)
    query = rows[0][1]

    unfiltered = db.execute(
        "SELECT distance FROM v WHERE embedding MATCH ? AND k = 5", (query,)
    ).fetchall()
    # Exclude the 5 nearest neighbors; there are plenty of rows left beyond them.
    threshold = max(row["distance"] for row in unfiltered)

    filtered = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH ? AND k = 5 AND distance > ?",
        (query, threshold),
    ).fetchall()

    assert len(filtered) == 5
    assert all(row["distance"] > threshold for row in filtered)


class Row:
    def __init__(self):
        pass

    def __repr__(self) -> str:
        return repr()


