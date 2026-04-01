# /// script
# requires-python = ">=3.10"
# dependencies = ["sqlite-vec==0.1.6"]
# ///
"""Generate a legacy sqlite-vec database for backwards-compat testing.

Usage:
  uv run --script generate_legacy_db.py

Creates tests/fixtures/legacy-v0.1.6.db with a vec0 table containing
test data that can be read by the current version of sqlite-vec.
"""
import sqlite3
import sqlite_vec
import struct
import os

FIXTURE_DIR = os.path.join(os.path.dirname(__file__), "fixtures")
DB_PATH = os.path.join(FIXTURE_DIR, "legacy-v0.1.6.db")

DIMS = 4
N_ROWS = 50


def _f32(vals):
    return struct.pack(f"{len(vals)}f", *vals)


def main():
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)

    db = sqlite3.connect(DB_PATH)
    db.enable_load_extension(True)
    sqlite_vec.load(db)

    # Print version for verification
    version = db.execute("SELECT vec_version()").fetchone()[0]
    print(f"sqlite-vec version: {version}")

    # Create a basic vec0 table — flat index, no fancy features
    db.execute(f"CREATE VIRTUAL TABLE legacy_vectors USING vec0(emb float[{DIMS}])")

    # Insert test data: vectors where element[0] == rowid for easy verification
    for i in range(1, N_ROWS + 1):
        vec = [float(i), 0.0, 0.0, 0.0]
        db.execute("INSERT INTO legacy_vectors(rowid, emb) VALUES (?, ?)", [i, _f32(vec)])

    db.commit()

    # Verify
    count = db.execute("SELECT count(*) FROM legacy_vectors").fetchone()[0]
    print(f"Inserted {count} rows")

    # Test KNN works
    query = _f32([1.0, 0.0, 0.0, 0.0])
    rows = db.execute(
        "SELECT rowid, distance FROM legacy_vectors WHERE emb MATCH ? AND k = 5",
        [query],
    ).fetchall()
    print(f"KNN top 5: {[(r[0], round(r[1], 4)) for r in rows]}")
    assert rows[0][0] == 1  # closest to [1,0,0,0]
    assert len(rows) == 5

    # Also create a table with name == column name (the conflict case)
    # This was allowed in old versions — new code must not break on reconnect
    db.execute("CREATE VIRTUAL TABLE emb USING vec0(emb float[4])")
    for i in range(1, 11):
        db.execute("INSERT INTO emb(rowid, emb) VALUES (?, ?)", [i, _f32([float(i), 0, 0, 0])])
    db.commit()

    count2 = db.execute("SELECT count(*) FROM emb").fetchone()[0]
    print(f"Table 'emb' with column 'emb': {count2} rows (name conflict case)")

    db.close()
    print(f"\nGenerated: {DB_PATH}")


if __name__ == "__main__":
    main()
