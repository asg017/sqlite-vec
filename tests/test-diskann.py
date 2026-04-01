import sqlite3
import struct
import pytest
from helpers import _f32, exec


def test_diskann_create_basic(db):
    """Basic DiskANN table creation with binary quantizer should succeed."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[128] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    # Table should exist
    tables = [
        row[0]
        for row in db.execute(
            "select name from sqlite_master where name like 't%' order by 1"
        ).fetchall()
    ]
    assert "t" in tables


def test_diskann_create_int8_quantizer(db):
    """DiskANN with int8 quantizer should succeed."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[64] INDEXED BY diskann(neighbor_quantizer=int8)
        )
    """)
    tables = [
        row[0]
        for row in db.execute(
            "select name from sqlite_master where name like 't%' order by 1"
        ).fetchall()
    ]
    assert "t" in tables


def test_diskann_create_with_options(db):
    """DiskANN with custom n_neighbors and search_list_size."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[128] INDEXED BY diskann(
                neighbor_quantizer=binary,
                n_neighbors=48,
                search_list_size=256
            )
        )
    """)
    tables = [
        row[0]
        for row in db.execute(
            "select name from sqlite_master where name like 't%' order by 1"
        ).fetchall()
    ]
    assert "t" in tables


def test_diskann_create_with_distance_metric(db):
    """DiskANN combined with distance_metric should work."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[128] distance_metric=cosine INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    tables = [
        row[0]
        for row in db.execute(
            "select name from sqlite_master where name like 't%' order by 1"
        ).fetchall()
    ]
    assert "t" in tables


def test_diskann_create_error_missing_quantizer(db):
    """Error when neighbor_quantizer is not specified."""
    result = exec(db, """
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[128] INDEXED BY diskann(n_neighbors=72)
        )
    """)
    assert "error" in result


def test_diskann_create_error_empty_parens(db):
    """Error on empty parens."""
    result = exec(db, """
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[128] INDEXED BY diskann()
        )
    """)
    assert "error" in result


def test_diskann_create_error_unknown_quantizer(db):
    """Error on unknown quantizer type."""
    result = exec(db, """
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[128] INDEXED BY diskann(neighbor_quantizer=unknown)
        )
    """)
    assert "error" in result


def test_diskann_create_error_bit_column(db):
    """Error: DiskANN not supported on bit vector columns."""
    result = exec(db, """
        CREATE VIRTUAL TABLE t USING vec0(
            emb bit[128] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    assert "error" in result
    assert "bit" in result["message"].lower() or "DiskANN" in result["message"]


def test_diskann_create_error_binary_quantizer_odd_dims(db):
    """Error: binary quantizer requires dimensions divisible by 8."""
    result = exec(db, """
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[13] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    assert "error" in result
    assert "divisible" in result["message"].lower()


def test_diskann_create_error_bad_n_neighbors(db):
    """Error: n_neighbors must be divisible by 8."""
    result = exec(db, """
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[128] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=13)
        )
    """)
    assert "error" in result


def test_diskann_shadow_tables_created(db):
    """DiskANN table should create _vectors00 and _diskann_nodes00 shadow tables."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[64] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    tables = sorted([
        row[0]
        for row in db.execute(
            "select name from sqlite_master where type='table' and name like 't_%' order by 1"
        ).fetchall()
    ])
    assert "t_vectors00" in tables
    assert "t_diskann_nodes00" in tables
    # DiskANN columns should NOT have _vector_chunks00
    assert "t_vector_chunks00" not in tables


def test_diskann_medoid_in_info(db):
    """_info table should contain diskann_medoid_00 key with NULL value."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[64] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    row = db.execute(
        "SELECT key, value FROM t_info WHERE key = 'diskann_medoid_00'"
    ).fetchone()
    assert row is not None
    assert row[0] == "diskann_medoid_00"
    assert row[1] is None


def test_non_diskann_no_extra_tables(db):
    """Non-DiskANN table must NOT create _vectors or _diskann_nodes tables."""
    db.execute("CREATE VIRTUAL TABLE t USING vec0(emb float[64])")
    tables = [
        row[0]
        for row in db.execute(
            "select name from sqlite_master where type='table' and name like 't_%' order by 1"
        ).fetchall()
    ]
    assert "t_vectors00" not in tables
    assert "t_diskann_nodes00" not in tables
    assert "t_vector_chunks00" in tables


def test_diskann_medoid_initial_null(db):
    """Medoid should be NULL initially (empty graph)."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[64] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    row = db.execute(
        "SELECT value FROM t_info WHERE key = 'diskann_medoid_00'"
    ).fetchone()
    assert row[0] is None


def test_diskann_medoid_set_via_info(db):
    """Setting medoid via _info table should be retrievable."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[64] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    # Manually set medoid to simulate first insert
    db.execute("UPDATE t_info SET value = 42 WHERE key = 'diskann_medoid_00'")
    row = db.execute(
        "SELECT value FROM t_info WHERE key = 'diskann_medoid_00'"
    ).fetchone()
    assert row[0] == 42

    # Reset to NULL (empty graph)
    db.execute("UPDATE t_info SET value = NULL WHERE key = 'diskann_medoid_00'")
    row = db.execute(
        "SELECT value FROM t_info WHERE key = 'diskann_medoid_00'"
    ).fetchone()
    assert row[0] is None


def test_diskann_single_insert(db):
    """Insert 1 vector. Verify _vectors00, _diskann_nodes00, and medoid."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    db.execute(
        "INSERT INTO t(rowid, emb) VALUES (1, ?)",
        [_f32([1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    )
    # Verify _vectors00 has 1 row
    count = db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0]
    assert count == 1

    # Verify _diskann_nodes00 has 1 row
    count = db.execute("SELECT count(*) FROM t_diskann_nodes00").fetchone()[0]
    assert count == 1

    # Verify medoid is set
    medoid = db.execute(
        "SELECT value FROM t_info WHERE key = 'diskann_medoid_00'"
    ).fetchone()[0]
    assert medoid == 1


def test_diskann_multiple_inserts(db):
    """Insert multiple vectors. Verify counts and that nodes have neighbors."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    import random
    random.seed(42)
    for i in range(1, 21):
        vec = [random.gauss(0, 1) for _ in range(8)]
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    # Verify counts
    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 20
    assert db.execute("SELECT count(*) FROM t_diskann_nodes00").fetchone()[0] == 20

    # Every node after the first should have at least 1 neighbor
    rows = db.execute(
        "SELECT rowid, neighbors_validity FROM t_diskann_nodes00"
    ).fetchall()
    nodes_with_neighbors = 0
    for row in rows:
        validity = row[1]
        has_neighbor = any(b != 0 for b in validity)
        if has_neighbor:
            nodes_with_neighbors += 1
    # At minimum, nodes 2-20 should have neighbors (node 1 gets neighbors via reverse edges)
    assert nodes_with_neighbors >= 19


def test_diskann_bidirectional_edges(db):
    """Insert A then B. B should be in A's neighbors and A in B's."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    db.execute(
        "INSERT INTO t(rowid, emb) VALUES (1, ?)",
        [_f32([1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    )
    db.execute(
        "INSERT INTO t(rowid, emb) VALUES (2, ?)",
        [_f32([0.9, 0.1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])],
    )

    # Check B(2) is in A(1)'s neighbor list
    row_a = db.execute(
        "SELECT neighbor_ids FROM t_diskann_nodes00 WHERE rowid = 1"
    ).fetchone()
    neighbor_ids_a = struct.unpack(f"{len(row_a[0])//8}q", row_a[0])
    assert 2 in neighbor_ids_a

    # Check A(1) is in B(2)'s neighbor list
    row_b = db.execute(
        "SELECT neighbor_ids FROM t_diskann_nodes00 WHERE rowid = 2"
    ).fetchone()
    neighbor_ids_b = struct.unpack(f"{len(row_b[0])//8}q", row_b[0])
    assert 1 in neighbor_ids_b


def test_diskann_delete_single(db):
    """Insert 3, delete 1. Verify counts."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    for i in range(1, 4):
        db.execute(
            "INSERT INTO t(rowid, emb) VALUES (?, ?)",
            [i, _f32([float(i)] * 8)],
        )
    db.execute("DELETE FROM t WHERE rowid = 2")

    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 2
    assert db.execute("SELECT count(*) FROM t_diskann_nodes00").fetchone()[0] == 2


def test_diskann_delete_no_stale_references(db):
    """After delete, no node should reference the deleted rowid."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    import random
    random.seed(123)
    for i in range(1, 11):
        vec = [random.gauss(0, 1) for _ in range(8)]
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    db.execute("DELETE FROM t WHERE rowid = 5")

    # Scan all remaining nodes and verify rowid 5 is not in any neighbor list
    rows = db.execute(
        "SELECT rowid, neighbors_validity, neighbor_ids FROM t_diskann_nodes00"
    ).fetchall()
    for row in rows:
        validity = row[1]
        neighbor_ids_blob = row[2]
        n_neighbors = len(validity) * 8
        ids = struct.unpack(f"{n_neighbors}q", neighbor_ids_blob)
        for i in range(n_neighbors):
            byte_idx = i // 8
            bit_idx = i % 8
            if validity[byte_idx] & (1 << bit_idx):
                assert ids[i] != 5, f"Node {row[0]} still references deleted rowid 5"


def test_diskann_delete_medoid(db):
    """Delete the medoid. Verify a new non-NULL medoid is selected."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    for i in range(1, 4):
        db.execute(
            "INSERT INTO t(rowid, emb) VALUES (?, ?)",
            [i, _f32([float(i)] * 8)],
        )

    medoid_before = db.execute(
        "SELECT value FROM t_info WHERE key = 'diskann_medoid_00'"
    ).fetchone()[0]
    assert medoid_before == 1

    db.execute("DELETE FROM t WHERE rowid = 1")

    medoid_after = db.execute(
        "SELECT value FROM t_info WHERE key = 'diskann_medoid_00'"
    ).fetchone()[0]
    assert medoid_after is not None
    assert medoid_after != 1


def test_diskann_delete_all(db):
    """Delete all vectors. Medoid should be NULL."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    for i in range(1, 4):
        db.execute(
            "INSERT INTO t(rowid, emb) VALUES (?, ?)",
            [i, _f32([float(i)] * 8)],
        )
    for i in range(1, 4):
        db.execute("DELETE FROM t WHERE rowid = ?", [i])

    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 0
    assert db.execute("SELECT count(*) FROM t_diskann_nodes00").fetchone()[0] == 0

    medoid = db.execute(
        "SELECT value FROM t_info WHERE key = 'diskann_medoid_00'"
    ).fetchone()[0]
    assert medoid is None


def test_diskann_insert_delete_insert_cycle(db):
    """Insert, delete, insert again. No crashes."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    db.execute("INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([1.0] * 8)])
    db.execute("INSERT INTO t(rowid, emb) VALUES (2, ?)", [_f32([2.0] * 8)])
    db.execute("DELETE FROM t WHERE rowid = 1")
    db.execute("INSERT INTO t(rowid, emb) VALUES (3, ?)", [_f32([3.0] * 8)])

    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 2
    assert db.execute("SELECT count(*) FROM t_diskann_nodes00").fetchone()[0] == 2


def test_diskann_knn_basic(db):
    """Basic KNN query should return results."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    db.execute("INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([1, 0, 0, 0, 0, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, emb) VALUES (2, ?)", [_f32([0, 1, 0, 0, 0, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, emb) VALUES (3, ?)", [_f32([0.9, 0.1, 0, 0, 0, 0, 0, 0])])

    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=2",
        [_f32([1, 0, 0, 0, 0, 0, 0, 0])],
    ).fetchall()
    assert len(rows) == 2
    # Closest should be rowid 1 (exact match)
    assert rows[0][0] == 1
    assert rows[0][1] < 0.01  # ~0 distance


def test_diskann_knn_distances_sorted(db):
    """Returned distances should be in ascending order."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=16)
        )
    """)
    import random
    random.seed(42)
    for i in range(1, 51):
        vec = [random.gauss(0, 1) for _ in range(8)]
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=10",
        [_f32([0.0] * 8)],
    ).fetchall()
    assert len(rows) == 10
    distances = [r[1] for r in rows]
    for i in range(len(distances) - 1):
        assert distances[i] <= distances[i + 1], f"Distances not sorted at index {i}"


def test_diskann_knn_empty_table(db):
    """KNN on empty table should return 0 results."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=5",
        [_f32([1, 0, 0, 0, 0, 0, 0, 0])],
    ).fetchall()
    assert len(rows) == 0


def test_diskann_knn_after_delete(db):
    """KNN after delete should not return deleted rows."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    db.execute("INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([1, 0, 0, 0, 0, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, emb) VALUES (2, ?)", [_f32([0, 1, 0, 0, 0, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, emb) VALUES (3, ?)", [_f32([0.5, 0.5, 0, 0, 0, 0, 0, 0])])
    db.execute("DELETE FROM t WHERE rowid = 1")

    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=3",
        [_f32([1, 0, 0, 0, 0, 0, 0, 0])],
    ).fetchall()
    rowids = [r[0] for r in rows]
    assert 1 not in rowids
    assert len(rows) == 2


def test_diskann_no_index_still_works(db):
    """Tables without INDEXED BY should still work identically."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[4]
        )
    """)
    db.execute("INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([1, 2, 3, 4])])
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=1",
        [_f32([1, 2, 3, 4])],
    ).fetchall()
    assert len(rows) == 1
    assert rows[0][0] == 1


def test_diskann_drop_table(db):
    """DROP TABLE should clean up all shadow tables."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[128] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    db.execute("DROP TABLE t")
    tables = [
        row[0]
        for row in db.execute(
            "select name from sqlite_master where name like 't%'"
        ).fetchall()
    ]
    assert len(tables) == 0


def test_diskann_create_split_search_list_size(db):
    """DiskANN with separate search_list_size_search and search_list_size_insert."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[128] INDEXED BY diskann(
                neighbor_quantizer=binary,
                search_list_size_search=256,
                search_list_size_insert=64
            )
        )
    """)
    tables = [
        row[0]
        for row in db.execute(
            "select name from sqlite_master where name like 't%' order by 1"
        ).fetchall()
    ]
    assert "t" in tables


def test_diskann_create_error_mixed_search_list_size(db):
    """Error when mixing search_list_size with search_list_size_search."""
    result = exec(db, """
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[128] INDEXED BY diskann(
                neighbor_quantizer=binary,
                search_list_size=128,
                search_list_size_search=256
            )
        )
    """)
    assert "error" in result


def test_diskann_command_search_list_size(db):
    """Runtime search_list_size override via command insert."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[64] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    import struct, random
    random.seed(42)
    for i in range(20):
        vec = struct.pack("64f", *[random.random() for _ in range(64)])
        db.execute("INSERT INTO t(emb) VALUES (?)", [vec])

    # Query with default search_list_size
    query = struct.pack("64f", *[random.random() for _ in range(64)])
    results_before = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k = 5", [query]
    ).fetchall()
    assert len(results_before) == 5

    # Override search_list_size_search at runtime
    db.execute("INSERT INTO t(t) VALUES ('search_list_size_search=256')")

    # Query should still work
    results_after = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k = 5", [query]
    ).fetchall()
    assert len(results_after) == 5

    # Override search_list_size_insert at runtime
    db.execute("INSERT INTO t(t) VALUES ('search_list_size_insert=32')")

    # Inserts should still work
    vec = struct.pack("64f", *[random.random() for _ in range(64)])
    db.execute("INSERT INTO t(emb) VALUES (?)", [vec])

    # Override unified search_list_size
    db.execute("INSERT INTO t(t) VALUES ('search_list_size=64')")

    results_final = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k = 5", [query]
    ).fetchall()
    assert len(results_final) == 5


def test_diskann_command_search_list_size_error(db):
    """Error on invalid search_list_size command value."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[64] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    result = exec(db, "INSERT INTO t(t) VALUES ('search_list_size=0')")
    assert "error" in result
    result = exec(db, "INSERT INTO t(t) VALUES ('search_list_size=-1')")
    assert "error" in result


# ======================================================================
# Error cases: DiskANN + auxiliary/metadata/partition columns
# ======================================================================

def test_diskann_create_with_auxiliary_column(db):
    """DiskANN tables should support auxiliary columns."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[64] INDEXED BY diskann(neighbor_quantizer=binary),
            +extra text
        )
    """)
    # Auxiliary shadow table should exist
    tables = [r[0] for r in db.execute(
        "SELECT name FROM sqlite_master WHERE name LIKE 't_%' ORDER BY 1"
    ).fetchall()]
    assert "t_auxiliary" in tables


def test_diskann_create_error_with_metadata_column(db):
    """DiskANN tables should not support metadata columns."""
    result = exec(db, """
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[64] INDEXED BY diskann(neighbor_quantizer=binary),
            metadata_col integer metadata
        )
    """)
    assert "error" in result
    assert "metadata" in result["message"].lower() or "Metadata" in result["message"]


def test_diskann_create_error_with_partition_key(db):
    """DiskANN tables should not support partition key columns."""
    result = exec(db, """
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[64] INDEXED BY diskann(neighbor_quantizer=binary),
            user_id text partition key
        )
    """)
    assert "error" in result
    assert "partition" in result["message"].lower() or "Partition" in result["message"]


# ======================================================================
# Insert edge cases
# ======================================================================

def test_diskann_insert_no_rowid(db):
    """INSERT without explicit rowid (auto-generated) should work."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    db.execute("INSERT INTO t(emb) VALUES (?)", [_f32([1.0] * 8)])
    db.execute("INSERT INTO t(emb) VALUES (?)", [_f32([2.0] * 8)])
    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 2
    assert db.execute("SELECT count(*) FROM t_diskann_nodes00").fetchone()[0] == 2


def test_diskann_insert_large_batch(db):
    """INSERT 500+ vectors, verify all are queryable via KNN."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[16] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=16)
        )
    """)
    import random
    random.seed(99)
    N = 500
    for i in range(1, N + 1):
        vec = [random.gauss(0, 1) for _ in range(16)]
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == N
    assert db.execute("SELECT count(*) FROM t_diskann_nodes00").fetchone()[0] == N

    # KNN should return results
    query = [random.gauss(0, 1) for _ in range(16)]
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=10",
        [_f32(query)],
    ).fetchall()
    assert len(rows) == 10
    # Distances should be sorted
    distances = [r[1] for r in rows]
    for i in range(len(distances) - 1):
        assert distances[i] <= distances[i + 1]


def test_diskann_insert_zero_vector(db):
    """Insert an all-zero vector (edge case for binary quantizer)."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    db.execute("INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([0.0] * 8)])
    db.execute("INSERT INTO t(rowid, emb) VALUES (2, ?)", [_f32([1.0] * 8)])
    count = db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0]
    assert count == 2

    # Query with zero vector should find rowid 1 as closest
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=2",
        [_f32([0.0] * 8)],
    ).fetchall()
    assert len(rows) == 2
    assert rows[0][0] == 1


def test_diskann_insert_large_values(db):
    """Insert vectors with very large float values."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary)
        )
    """)
    import sys
    large = sys.float_info.max / 1e300  # Large but not overflowing
    db.execute("INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([large] * 8)])
    db.execute("INSERT INTO t(rowid, emb) VALUES (2, ?)", [_f32([-large] * 8)])
    db.execute("INSERT INTO t(rowid, emb) VALUES (3, ?)", [_f32([0.0] * 8)])
    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 3


def test_diskann_insert_int8_quantizer_knn(db):
    """Full insert + query cycle with int8 quantizer."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[16] INDEXED BY diskann(neighbor_quantizer=int8, n_neighbors=8)
        )
    """)
    import random
    random.seed(77)
    for i in range(1, 31):
        vec = [random.gauss(0, 1) for _ in range(16)]
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 30

    # KNN should work
    query = [random.gauss(0, 1) for _ in range(16)]
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=5",
        [_f32(query)],
    ).fetchall()
    assert len(rows) == 5
    distances = [r[1] for r in rows]
    for i in range(len(distances) - 1):
        assert distances[i] <= distances[i + 1]


# ======================================================================
# Delete edge cases
# ======================================================================

def test_diskann_delete_nonexistent(db):
    """DELETE of a nonexistent rowid should either be a no-op or return an error, not crash."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    db.execute("INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([1.0] * 8)])
    # Deleting a nonexistent rowid may error but should not crash
    result = exec(db, "DELETE FROM t WHERE rowid = 999")
    # Whether it succeeds or errors, the existing row should still be there
    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 1


def test_diskann_delete_then_reinsert_same_rowid(db):
    """Delete rowid 5, then reinsert rowid 5 with a new vector."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    for i in range(1, 6):
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32([float(i)] * 8)])

    db.execute("DELETE FROM t WHERE rowid = 5")
    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 4

    # Reinsert with new vector
    db.execute("INSERT INTO t(rowid, emb) VALUES (5, ?)", [_f32([99.0] * 8)])
    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 5
    assert db.execute("SELECT count(*) FROM t_diskann_nodes00").fetchone()[0] == 5


def test_diskann_delete_all_then_insert(db):
    """Delete everything, then insert new vectors. Graph should rebuild."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    for i in range(1, 6):
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32([float(i)] * 8)])

    # Delete all
    for i in range(1, 6):
        db.execute("DELETE FROM t WHERE rowid = ?", [i])
    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 0

    medoid = db.execute("SELECT value FROM t_info WHERE key = 'diskann_medoid_00'").fetchone()[0]
    assert medoid is None

    # Insert new vectors
    for i in range(10, 15):
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32([float(i)] * 8)])

    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == 5
    assert db.execute("SELECT count(*) FROM t_diskann_nodes00").fetchone()[0] == 5

    medoid = db.execute("SELECT value FROM t_info WHERE key = 'diskann_medoid_00'").fetchone()[0]
    assert medoid is not None

    # KNN should work
    rows = db.execute(
        "SELECT rowid FROM t WHERE emb MATCH ? AND k=3",
        [_f32([12.0] * 8)],
    ).fetchall()
    assert len(rows) == 3


def test_diskann_delete_preserves_graph_connectivity(db):
    """After deleting a node, remaining nodes should still be reachable via KNN."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    import random
    random.seed(456)
    for i in range(1, 21):
        vec = [random.gauss(0, 1) for _ in range(8)]
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    # Delete 5 nodes
    for i in [3, 7, 11, 15, 19]:
        db.execute("DELETE FROM t WHERE rowid = ?", [i])

    remaining = db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0]
    assert remaining == 15

    # Every remaining node should be reachable via KNN (appears somewhere in top-k)
    all_rowids = [r[0] for r in db.execute("SELECT rowid FROM t_vectors00").fetchall()]
    reachable = set()
    for rid in all_rowids:
        vec_blob = db.execute("SELECT vector FROM t_vectors00 WHERE rowid = ?", [rid]).fetchone()[0]
        rows = db.execute(
            "SELECT rowid FROM t WHERE emb MATCH ? AND k=5",
            [vec_blob],
        ).fetchall()
        assert len(rows) >= 1  # At least some results
        for r in rows:
            reachable.add(r[0])
    # Most nodes should be reachable through the graph
    assert len(reachable) >= len(all_rowids) * 0.8, \
        f"Only {len(reachable)}/{len(all_rowids)} nodes reachable"


# ======================================================================
# Update scenarios
# ======================================================================

def test_diskann_update_vector(db):
    """UPDATE a vector on DiskANN table should error (will be implemented soon)."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    db.execute("INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([1, 0, 0, 0, 0, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, emb) VALUES (2, ?)", [_f32([0, 1, 0, 0, 0, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, emb) VALUES (3, ?)", [_f32([0, 0, 1, 0, 0, 0, 0, 0])])

    with pytest.raises(sqlite3.OperationalError, match="UPDATE on vector column.*not supported for DiskANN"):
        db.execute("UPDATE t SET emb = ? WHERE rowid = 1", [_f32([0, 0.9, 0.1, 0, 0, 0, 0, 0])])


# ======================================================================
# KNN correctness after mutations
# ======================================================================

def test_diskann_knn_recall_after_inserts(db):
    """Insert N vectors, verify top-1 recall is 100% for exact matches."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=16)
        )
    """)
    import random
    random.seed(200)
    vectors = {}
    for i in range(1, 51):
        vec = [random.gauss(0, 1) for _ in range(8)]
        vectors[i] = vec
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    # Top-1 for each vector should return itself
    correct = 0
    for rid, vec in vectors.items():
        rows = db.execute(
            "SELECT rowid FROM t WHERE emb MATCH ? AND k=1",
            [_f32(vec)],
        ).fetchall()
        if rows and rows[0][0] == rid:
            correct += 1

    # With binary quantizer, approximate search may not always return exact match
    # but should have high recall (at least 80%)
    assert correct >= len(vectors) * 0.8, f"Top-1 recall too low: {correct}/{len(vectors)}"


def test_diskann_knn_k_larger_than_table(db):
    """k=100 on table with 5 rows should return 5."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    for i in range(1, 6):
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32([float(i)] * 8)])

    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=100",
        [_f32([3.0] * 8)],
    ).fetchall()
    assert len(rows) == 5


def test_diskann_knn_cosine_metric(db):
    """KNN with cosine distance metric."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] distance_metric=cosine INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    # Insert orthogonal-ish vectors
    db.execute("INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([1, 0, 0, 0, 0, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, emb) VALUES (2, ?)", [_f32([0, 1, 0, 0, 0, 0, 0, 0])])
    db.execute("INSERT INTO t(rowid, emb) VALUES (3, ?)", [_f32([0.7, 0.7, 0, 0, 0, 0, 0, 0])])

    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=3",
        [_f32([1, 0, 0, 0, 0, 0, 0, 0])],
    ).fetchall()
    assert len(rows) == 3
    # rowid 1 should be closest (exact match in direction)
    assert rows[0][0] == 1
    # Distances should be sorted
    distances = [r[1] for r in rows]
    for i in range(len(distances) - 1):
        assert distances[i] <= distances[i + 1]


def test_diskann_knn_after_heavy_churn(db):
    """Interleave many inserts and deletes, then query."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=16)
        )
    """)
    import random
    random.seed(321)

    # Insert 50 vectors
    for i in range(1, 51):
        vec = [random.gauss(0, 1) for _ in range(8)]
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    # Delete even-numbered rows
    for i in range(2, 51, 2):
        db.execute("DELETE FROM t WHERE rowid = ?", [i])

    # Insert more vectors with higher rowids
    for i in range(51, 76):
        vec = [random.gauss(0, 1) for _ in range(8)]
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    remaining = db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0]
    assert remaining == 50  # 25 odd + 25 new

    # KNN should still work and return results
    query = [random.gauss(0, 1) for _ in range(8)]
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=10",
        [_f32(query)],
    ).fetchall()
    assert len(rows) == 10
    # Distances should be sorted
    distances = [r[1] for r in rows]
    for i in range(len(distances) - 1):
        assert distances[i] <= distances[i + 1]


def test_diskann_knn_batch_recall(db):
    """Insert 100+ vectors and verify reasonable recall."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[16] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=16)
        )
    """)
    import random
    random.seed(42)
    N = 150
    vectors = {}
    for i in range(1, N + 1):
        vec = [random.gauss(0, 1) for _ in range(16)]
        vectors[i] = vec
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    # Brute-force top-5 for a query and compare with DiskANN
    query = [random.gauss(0, 1) for _ in range(16)]

    # Compute true distances
    true_dists = []
    for rid, vec in vectors.items():
        d = sum((a - b) ** 2 for a, b in zip(query, vec))
        true_dists.append((d, rid))
    true_dists.sort()
    true_top5 = set(r for _, r in true_dists[:5])

    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=5",
        [_f32(query)],
    ).fetchall()
    result_top5 = set(r[0] for r in rows)
    assert len(rows) == 5

    # At least 3 of top-5 should match (reasonable recall for approximate search)
    overlap = len(true_top5 & result_top5)
    assert overlap >= 3, f"Recall too low: only {overlap}/5 overlap"


# ======================================================================
# Additional edge cases
# ======================================================================

def test_diskann_insert_wrong_dimensions(db):
    """INSERT with wrong dimension vector should error."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    result = exec(db, "INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([1.0] * 4)])
    assert "error" in result


def test_diskann_knn_wrong_query_dimensions(db):
    """KNN MATCH with wrong dimension query should error."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    db.execute("INSERT INTO t(rowid, emb) VALUES (1, ?)", [_f32([1.0] * 8)])

    result = exec(db, "SELECT rowid FROM t WHERE emb MATCH ? AND k=1", [_f32([1.0] * 4)])
    assert "error" in result


def test_diskann_graph_connectivity_after_many_deletes(db):
    """After many deletes, the graph should still be connected enough for search."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=16)
        )
    """)
    import random
    random.seed(789)
    N = 40
    for i in range(1, N + 1):
        vec = [random.gauss(0, 1) for _ in range(8)]
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    # Delete 30 of 40 nodes
    to_delete = list(range(1, 31))
    for i in to_delete:
        db.execute("DELETE FROM t WHERE rowid = ?", [i])

    remaining = db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0]
    assert remaining == 10

    # Search should still work and return results
    query = [random.gauss(0, 1) for _ in range(8)]
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=10",
        [_f32(query)],
    ).fetchall()
    # Should return some results (graph may be fragmented after heavy deletion)
    assert len(rows) >= 1
    # Distances should be sorted
    distances = [r[1] for r in rows]
    for i in range(len(distances) - 1):
        assert distances[i] <= distances[i + 1]


def test_diskann_large_batch_insert_500(db):
    """Insert 500+ vectors and verify counts and KNN."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=16)
        )
    """)
    import random
    random.seed(555)
    N = 500
    for i in range(1, N + 1):
        vec = [random.gauss(0, 1) for _ in range(8)]
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    assert db.execute("SELECT count(*) FROM t_vectors00").fetchone()[0] == N

    query = [random.gauss(0, 1) for _ in range(8)]
    rows = db.execute(
        "SELECT rowid, distance FROM t WHERE emb MATCH ? AND k=20",
        [_f32(query)],
    ).fetchall()
    assert len(rows) == 20
    distances = [r[1] for r in rows]
    for i in range(len(distances) - 1):
        assert distances[i] <= distances[i + 1]


def test_corrupt_truncated_node_blob(db):
    """KNN should error (not crash) when DiskANN node blob is truncated."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    for i in range(5):
        vec = [0.0] * 8
        vec[i % 8] = 1.0
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i + 1, _f32(vec)])

    # Corrupt a DiskANN node: truncate neighbor_ids to 1 byte (wrong size)
    db.execute(
        "UPDATE t_diskann_nodes00 SET neighbor_ids = x'00' WHERE rowid = 1"
    )

    # Should not crash — may return wrong results or error
    try:
        db.execute(
            "SELECT rowid FROM t WHERE emb MATCH ? AND k=3",
            [_f32([1, 0, 0, 0, 0, 0, 0, 0])],
        ).fetchall()
    except sqlite3.OperationalError:
        pass  # Error is acceptable — crash is not


def test_diskann_delete_reinsert_cycle_knn(db):
    """Repeatedly delete and reinsert rows, verify KNN stays correct."""
    import random
    random.seed(101)
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    N = 30
    vecs = {}
    for i in range(1, N + 1):
        v = [random.gauss(0, 1) for _ in range(8)]
        vecs[i] = v
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(v)])

    # 3 cycles: delete half, reinsert with new vectors, verify KNN
    for cycle in range(3):
        to_delete = random.sample(sorted(vecs.keys()), len(vecs) // 2)
        for r in to_delete:
            db.execute("DELETE FROM t WHERE rowid = ?", [r])
            del vecs[r]

        # Reinsert with new rowids
        new_start = 100 + cycle * 50
        for i in range(len(to_delete)):
            rid = new_start + i
            v = [random.gauss(0, 1) for _ in range(8)]
            vecs[rid] = v
            db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [rid, _f32(v)])

        # KNN should return only alive rows
        query = [0.0] * 8
        rows = db.execute(
            "SELECT rowid FROM t WHERE emb MATCH ? AND k=10",
            [_f32(query)],
        ).fetchall()
        returned = {r["rowid"] for r in rows}
        assert returned.issubset(set(vecs.keys())), \
            f"Cycle {cycle}: deleted rowid in KNN results"
        assert len(rows) >= 1


def test_diskann_delete_interleaved_with_knn(db):
    """Delete one row at a time, querying KNN after each delete."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    N = 20
    for i in range(1, N + 1):
        vec = [0.0] * 8
        vec[i % 8] = float(i)
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    alive = set(range(1, N + 1))
    for to_del in [1, 5, 10, 15, 20]:
        db.execute("DELETE FROM t WHERE rowid = ?", [to_del])
        alive.discard(to_del)

        rows = db.execute(
            "SELECT rowid FROM t WHERE emb MATCH ? AND k=5",
            [_f32([1, 0, 0, 0, 0, 0, 0, 0])],
        ).fetchall()
        returned = {r["rowid"] for r in rows}
        assert returned.issubset(alive), \
            f"Deleted rowid {to_del} found in KNN results"


# ======================================================================
# Text primary key + DiskANN
# ======================================================================


def test_diskann_text_pk_insert_knn_delete(db):
    """DiskANN with text primary key: insert, KNN, delete, KNN again."""
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            id text primary key,
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)

    vecs = {
        "alpha": [1, 0, 0, 0, 0, 0, 0, 0],
        "beta": [0, 1, 0, 0, 0, 0, 0, 0],
        "gamma": [0, 0, 1, 0, 0, 0, 0, 0],
        "delta": [0, 0, 0, 1, 0, 0, 0, 0],
        "epsilon": [0, 0, 0, 0, 1, 0, 0, 0],
    }
    for name, vec in vecs.items():
        db.execute("INSERT INTO t(id, emb) VALUES (?, ?)", [name, _f32(vec)])

    # KNN should return text IDs
    rows = db.execute(
        "SELECT id, distance FROM t WHERE emb MATCH ? AND k=3",
        [_f32([1, 0, 0, 0, 0, 0, 0, 0])],
    ).fetchall()
    assert len(rows) >= 1
    ids = [r["id"] for r in rows]
    assert "alpha" in ids  # closest to query

    # Delete and verify
    db.execute("DELETE FROM t WHERE id = 'alpha'")
    rows = db.execute(
        "SELECT id FROM t WHERE emb MATCH ? AND k=3",
        [_f32([1, 0, 0, 0, 0, 0, 0, 0])],
    ).fetchall()
    ids = [r["id"] for r in rows]
    assert "alpha" not in ids


def test_diskann_delete_scrubs_all_references(db):
    """After DELETE, no shadow table should contain the deleted rowid or its data."""
    import struct
    db.execute("""
        CREATE VIRTUAL TABLE t USING vec0(
            emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8)
        )
    """)
    for i in range(20):
        vec = struct.pack("8f", *[float(i + d) for d in range(8)])
        db.execute("INSERT INTO t(rowid, emb) VALUES (?, ?)", [i, vec])

    target = 5
    db.execute("DELETE FROM t WHERE rowid = ?", [target])

    # Node row itself should be gone
    assert db.execute(
        "SELECT count(*) FROM t_diskann_nodes00 WHERE rowid=?", [target]
    ).fetchone()[0] == 0

    # Vector should be gone
    assert db.execute(
        "SELECT count(*) FROM t_vectors00 WHERE rowid=?", [target]
    ).fetchone()[0] == 0

    # No other node should reference the deleted rowid in neighbor_ids
    for row in db.execute("SELECT rowid, neighbor_ids FROM t_diskann_nodes00"):
        node_rowid = row[0]
        ids_blob = row[1]
        for j in range(0, len(ids_blob), 8):
            nid = struct.unpack("<q", ids_blob[j : j + 8])[0]
            assert nid != target, (
                f"Node {node_rowid} slot {j // 8} still references "
                f"deleted rowid {target}"
            )
