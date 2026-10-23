import random
import struct


def _f32(values):
    return struct.pack(f"{len(values)}f", *values)


def test_knn_returns_the_k_nearest_in_order(db):
    # Distinct distances, so the order is fully determined; k below, at and
    # above the chunk size, with and without a filter that thins the chunks.
    rng = random.Random(5)
    db.execute(
        "create virtual table v using vec0(e float[3], g integer, chunk_size=16)"
    )
    rows = {}
    for i in range(1, 201):
        e = [rng.uniform(-1, 1) for _ in range(3)]
        rows[i] = (e, i % 3)
        db.execute("insert into v(rowid, e, g) values (?, ?, ?)", [i, _f32(e), i % 3])
    for _ in range(10):
        q = [rng.uniform(-1, 1) for _ in range(3)]
        for k in (1, 2, 7, 16, 17, 64, 200, 300):
            for g in (None, 1):
                kept = [i for i, (_, gi) in rows.items() if g is None or gi != g]
                got = [
                    r[0]
                    for r in db.execute(
                        "select rowid from v where e match ? and k = ?"
                        + ("" if g is None else " and g != ?"),
                        [_f32(q), k] + ([] if g is None else [g]),
                    )
                ]
                expected = sorted(
                    kept,
                    key=lambda i: db.execute(
                        "select vec_distance_l2(?, ?)", [_f32(q), _f32(rows[i][0])]
                    ).fetchone()[0],
                )[:k]
                assert got == expected


def test_knn_ties_keep_their_order(db):
    # Equal distances come back chunk by chunk, each chunk's rows from its last
    # slot to its first, whether or not k cuts the ties short.
    db.execute("create virtual table v using vec0(e float[2], chunk_size=8)")
    for i in range(1, 21):
        db.execute("insert into v(rowid, e) values (?, ?)", [i, _f32([1, 1])])
    q = _f32([1, 1])

    def knn(k):
        return [
            r[0]
            for r in db.execute("select rowid from v where e match ? and k = ?", [q, k])
        ]

    chunks = [range(8, 0, -1), range(16, 8, -1), range(20, 16, -1)]
    assert knn(20) == [rowid for chunk in chunks for rowid in chunk]
    assert knn(5) == [8, 7, 6, 5, 4]


def test_knn_ranks_a_nan_distance_last(db):
    # The cosine distance to a zero vector divides zero by zero, so it is NaN.
    db.execute(
        "create virtual table v using vec0("
        "e float[2] distance_metric=cosine, chunk_size=8)"
    )
    vectors = {1: [0, 0], 2: [1, 0], 3: [0.9, 0.1], 4: [0, 1]}
    for rowid, e in vectors.items():
        db.execute("insert into v(rowid, e) values (?, ?)", [rowid, _f32(e)])
    rows = db.execute("select rowid from v where e match ? and k = 4", [_f32([1, 0])])
    assert [r[0] for r in rows] == [2, 3, 4, 1]


def test_knn_ranks_a_nan_distance_last_across_chunks(db):
    # The zero vector's NaN distance is in the second chunk, whose numbers
    # are all nearer than the first chunk's, so the merge meets the NaN while
    # the first chunk's rows are still unmerged.
    db.execute(
        "create virtual table v using vec0("
        "e float[2] distance_metric=cosine, chunk_size=8)"
    )
    for i in range(1, 9):
        db.execute("insert into v(rowid, e) values (?, ?)", [i, _f32([1, 2 + i / 10])])
    for i in range(9, 16):
        db.execute("insert into v(rowid, e) values (?, ?)", [i, _f32([1, (i - 8) / 100])])
    db.execute("insert into v(rowid, e) values (16, ?)", [_f32([0, 0])])

    def knn(k):
        return [
            r[0]
            for r in db.execute(
                "select rowid from v where e match ? and k = ?", [_f32([1, 0]), k]
            )
        ]

    assert knn(16) == [*range(9, 16), *range(1, 9), 16]
    assert knn(9) == [*range(9, 16), 1, 2]
