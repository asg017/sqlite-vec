import sqlite3
import struct
import pytest


def _int8(list):
    """Helper to pack int8 vectors"""
    return struct.pack("%sb" % len(list), *list)


def bitmap(bitstring):
    """Helper to create bit vectors from binary string"""
    return bytes([int(bitstring, 2)])


@pytest.fixture()
def db():
    db = sqlite3.connect(":memory:")
    db.row_factory = sqlite3.Row
    db.enable_load_extension(True)
    db.load_extension("dist/vec0")
    db.enable_load_extension(False)
    return db


def test_distance_gt_basic(db):
    """Test distance > X constraint for basic pagination"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[3])")
    db.executemany(
        "INSERT INTO v(rowid, embedding) VALUES (?, ?)",
        [
            (1, "[1.0, 0.0, 0.0]"),
            (2, "[2.0, 0.0, 0.0]"),
            (3, "[3.0, 0.0, 0.0]"),
            (4, "[4.0, 0.0, 0.0]"),
            (5, "[5.0, 0.0, 0.0]"),
        ],
    )

    # First page: k=2
    result = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH '[0.0, 0.0, 0.0]' AND k = 2 ORDER BY distance"
    ).fetchall()
    assert len(result) == 2
    assert result[0]["rowid"] == 1
    assert result[1]["rowid"] == 2
    last_distance = result[1]["distance"]

    # Second page: distance > last_distance, k=2
    result = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH '[0.0, 0.0, 0.0]' AND k = 2 AND distance > ? ORDER BY distance",
        [last_distance],
    ).fetchall()
    assert len(result) == 2
    assert result[0]["rowid"] == 3
    assert result[1]["rowid"] == 4


def test_distance_ge_basic(db):
    """Test distance >= X constraint"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[2])")
    db.executemany(
        "INSERT INTO v(rowid, embedding) VALUES (?, ?)",
        [
            (1, "[1.0, 0.0]"),
            (2, "[2.0, 0.0]"),
            (3, "[3.0, 0.0]"),
        ],
    )

    result = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH '[0.0, 0.0]' AND k = 10 AND distance >= 2.0 ORDER BY distance"
    ).fetchall()
    # Should get rowid 2 (distance=2.0) and rowid 3 (distance=3.0)
    assert len(result) == 2
    assert result[0]["rowid"] == 2
    assert result[1]["rowid"] == 3


def test_distance_lt_basic(db):
    """Test distance < X constraint for range queries"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[2])")
    db.executemany(
        "INSERT INTO v(rowid, embedding) VALUES (?, ?)",
        [
            (1, "[1.0, 0.0]"),
            (2, "[2.0, 0.0]"),
            (3, "[3.0, 0.0]"),
            (4, "[4.0, 0.0]"),
        ],
    )

    result = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH '[0.0, 0.0]' AND k = 10 AND distance < 3.0 ORDER BY distance"
    ).fetchall()
    # Should get rowid 1 and 2 only
    assert len(result) == 2
    assert result[0]["rowid"] == 1
    assert result[1]["rowid"] == 2


def test_distance_le_basic(db):
    """Test distance <= X constraint"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[2])")
    db.executemany(
        "INSERT INTO v(rowid, embedding) VALUES (?, ?)",
        [
            (1, "[1.0, 0.0]"),
            (2, "[2.0, 0.0]"),
            (3, "[3.0, 0.0]"),
            (4, "[4.0, 0.0]"),
        ],
    )

    result = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH '[0.0, 0.0]' AND k = 10 AND distance <= 2.0 ORDER BY distance"
    ).fetchall()
    # Should get rowid 1 and 2
    assert len(result) == 2
    assert result[0]["rowid"] == 1
    assert result[1]["rowid"] == 2


def test_distance_range_query(db):
    """Test range query with both lower and upper bounds"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[1])")
    db.executemany(
        "INSERT INTO v(rowid, embedding) VALUES (?, ?)",
        [(i, f"[{float(i)}]") for i in range(1, 11)],
    )

    # Get vectors with distance between 3.0 and 6.0 (inclusive on both ends)
    result = db.execute(
        """SELECT rowid, distance FROM v
           WHERE embedding MATCH '[0.0]'
           AND k = 20
           AND distance >= 3.0
           AND distance <= 6.0
           ORDER BY distance"""
    ).fetchall()

    # Should get rowids 3, 4, 5, 6 (distances 3.0, 4.0, 5.0, 6.0)
    assert len(result) == 4
    assert [r["rowid"] for r in result] == [3, 4, 5, 6]


def test_distance_with_partition_keys(db):
    """Test distance constraints work with partition keys"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(category TEXT partition key, embedding float[2])")
    db.executemany(
        "INSERT INTO v(rowid, category, embedding) VALUES (?, ?, ?)",
        [
            (1, "A", "[1.0, 0.0]"),
            (2, "A", "[2.0, 0.0]"),
            (3, "A", "[3.0, 0.0]"),
            (4, "B", "[1.0, 0.0]"),
            (5, "B", "[2.0, 0.0]"),
        ],
    )

    # Query only category A with distance filter
    result = db.execute(
        """SELECT rowid, distance FROM v
           WHERE embedding MATCH '[0.0, 0.0]'
           AND category = 'A'
           AND k = 10
           AND distance > 1.0
           ORDER BY distance"""
    ).fetchall()

    # Should only get category A items with distance > 1.0
    assert len(result) == 2
    assert result[0]["rowid"] == 2
    assert result[1]["rowid"] == 3


def test_distance_with_metadata(db):
    """Test distance constraints work with metadata columns"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[2], label TEXT)")
    db.executemany(
        "INSERT INTO v(rowid, embedding, label) VALUES (?, ?, ?)",
        [
            (1, "[1.0, 0.0]", "important"),
            (2, "[2.0, 0.0]", "important"),
            (3, "[3.0, 0.0]", "spam"),
            (4, "[4.0, 0.0]", "important"),
        ],
    )

    # Query with both metadata filter and distance constraint
    result = db.execute(
        """SELECT rowid, distance FROM v
           WHERE embedding MATCH '[0.0, 0.0]'
           AND label = 'important'
           AND k = 10
           AND distance >= 2.0
           ORDER BY distance"""
    ).fetchall()

    # Should get rowid 2 and 4 (both important, distance >= 2.0)
    assert len(result) == 2
    assert result[0]["rowid"] == 2
    assert result[1]["rowid"] == 4


def test_distance_empty_result(db):
    """Test distance constraint that filters out all results"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[2])")
    db.executemany(
        "INSERT INTO v(rowid, embedding) VALUES (?, ?)",
        [
            (1, "[1.0, 0.0]"),
            (2, "[2.0, 0.0]"),
        ],
    )

    # Distance constraint that excludes everything
    result = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH '[0.0, 0.0]' AND k = 10 AND distance > 100.0"
    ).fetchall()

    assert len(result) == 0


def test_distance_pagination_multi_page(db):
    """Test multi-page pagination scenario"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[1])")
    db.executemany(
        "INSERT INTO v(rowid, embedding) VALUES (?, ?)",
        [(i, f"[{float(i)}]") for i in range(1, 21)],  # 20 vectors
    )

    page_size = 5
    all_results = []
    last_distance = None

    # Paginate through all results
    for page in range(4):  # 4 pages of 5 items each
        if last_distance is None:
            query = "SELECT rowid, distance FROM v WHERE embedding MATCH '[0.0]' AND k = ? ORDER BY distance"
            result = db.execute(query, [page_size]).fetchall()
        else:
            query = "SELECT rowid, distance FROM v WHERE embedding MATCH '[0.0]' AND k = ? AND distance > ? ORDER BY distance"
            result = db.execute(query, [page_size, last_distance]).fetchall()

        assert len(result) == page_size
        all_results.extend(result)
        last_distance = result[-1]["distance"]

    # Verify we got all 20 items in order
    assert len(all_results) == 20
    assert [r["rowid"] for r in all_results] == list(range(1, 21))


def test_distance_binary_vectors(db):
    """Test distance constraints with binary vectors"""
    # Use 32 bits = 4 bytes to satisfy alignment requirements
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding bit[32])")
    # Use vec_bit() constructor to properly type the vectors
    db.execute("INSERT INTO v(rowid, embedding) VALUES (1, vec_bit(?))", [b"\x00\x00\x00\x00"])
    db.execute("INSERT INTO v(rowid, embedding) VALUES (2, vec_bit(?))", [b"\x01\x00\x00\x00"])
    db.execute("INSERT INTO v(rowid, embedding) VALUES (3, vec_bit(?))", [b"\x03\x00\x00\x00"])
    db.execute("INSERT INTO v(rowid, embedding) VALUES (4, vec_bit(?))", [b"\x0F\x00\x00\x00"])

    # Use vec_bit() directly in the MATCH clause to preserve type
    result = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH vec_bit(?) AND k = 10 AND distance > 0.0 ORDER BY distance",
        [b"\x00\x00\x00\x00"],
    ).fetchall()

    # Should exclude exact match (rowid 1, distance 0.0)
    assert len(result) == 3
    assert result[0]["rowid"] == 2


def test_distance_int8_vectors(db):
    """Test distance constraints with int8 vectors"""
    # Use 4 elements to match 4-byte alignment
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding int8[4])")
    # Use vec_int8() constructor to properly type the vectors
    db.execute("INSERT INTO v(rowid, embedding) VALUES (1, vec_int8(?))", [_int8([1, 0, 0, 0])])
    db.execute("INSERT INTO v(rowid, embedding) VALUES (2, vec_int8(?))", [_int8([2, 0, 0, 0])])
    db.execute("INSERT INTO v(rowid, embedding) VALUES (3, vec_int8(?))", [_int8([3, 0, 0, 0])])
    db.execute("INSERT INTO v(rowid, embedding) VALUES (4, vec_int8(?))", [_int8([4, 0, 0, 0])])

    # Use vec_int8() directly in the MATCH clause to preserve type
    result = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH vec_int8(?) AND k = 10 AND distance <= 2.0 ORDER BY distance",
        [_int8([0, 0, 0, 0])],
    ).fetchall()

    # Distances will be 1.0, 2.0, 3.0, 4.0 - filter to <= 2.0
    assert len(result) == 2
    assert result[0]["rowid"] == 1
    assert result[1]["rowid"] == 2


def test_distance_equal_distances_caveat(db):
    """Test behavior with equal distances (documents the limitation)"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[2])")

    # Create vectors with same distance from query point
    # All at distance 1.0 from [0, 0]
    db.executemany(
        "INSERT INTO v(rowid, embedding) VALUES (?, ?)",
        [
            (1, "[1.0, 0.0]"),  # distance 1.0
            (2, "[0.0, 1.0]"),  # distance 1.0
            (3, "[-1.0, 0.0]"), # distance 1.0
            (4, "[0.0, -1.0]"), # distance 1.0
            (5, "[2.0, 0.0]"),  # distance 2.0
        ],
    )

    # Query with distance > 1.0 may miss some vectors with distance == 1.0
    # This documents the expected behavior
    result = db.execute(
        "SELECT rowid, distance FROM v WHERE embedding MATCH '[0.0, 0.0]' AND k = 10 AND distance > 1.0 ORDER BY distance"
    ).fetchall()

    # Should only get rowid 5 (distance 2.0)
    assert len(result) == 1
    assert result[0]["rowid"] == 5


def test_distance_with_auxiliary_columns(db):
    """Test distance constraints work with auxiliary columns"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[2], +metadata TEXT)")
    db.executemany(
        "INSERT INTO v(rowid, embedding, metadata) VALUES (?, ?, ?)",
        [
            (1, "[1.0, 0.0]", "doc1"),
            (2, "[2.0, 0.0]", "doc2"),
            (3, "[3.0, 0.0]", "doc3"),
        ],
    )

    result = db.execute(
        """SELECT rowid, distance, metadata FROM v
           WHERE embedding MATCH '[0.0, 0.0]'
           AND k = 10
           AND distance >= 2.0
           ORDER BY distance"""
    ).fetchall()

    assert len(result) == 2
    assert result[0]["rowid"] == 2
    assert result[0]["metadata"] == "doc2"
    assert result[1]["rowid"] == 3
    assert result[1]["metadata"] == "doc3"


def test_distance_precision_boundary(db):
    """Test distance constraints with precise boundary values"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[1])")

    # Insert vectors with very precise distances
    db.executemany(
        "INSERT INTO v(rowid, embedding) VALUES (?, ?)",
        [
            (1, "[0.1]"),
            (2, "[0.2]"),
            (3, "[0.3]"),
        ],
    )

    # Test exact boundary
    result = db.execute(
        "SELECT rowid FROM v WHERE embedding MATCH '[0.0]' AND k = 10 AND distance >= 0.2 ORDER BY distance"
    ).fetchall()

    # Should include 0.2 and 0.3
    assert len(result) == 2
    assert result[0]["rowid"] == 2


def test_distance_k_interaction(db):
    """Test that distance filter is applied during KNN search, k limits final results"""
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[1])")
    db.executemany(
        "INSERT INTO v(rowid, embedding) VALUES (?, ?)",
        [(i, f"[{float(i)}]") for i in range(1, 11)],
    )

    # Distance filter is applied during search, k limits how many results we get back
    result = db.execute(
        "SELECT rowid FROM v WHERE embedding MATCH '[0.0]' AND k = 5 AND distance > 2.0 ORDER BY distance"
    ).fetchall()

    # Distance > 2.0 filters to: 3,4,5,6,7,8,9,10
    # k=5 limits to first 5: 3,4,5,6,7
    assert len(result) == 5
    assert [r["rowid"] for r in result] == [3, 4, 5, 6, 7]
