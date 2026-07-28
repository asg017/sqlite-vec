import sqlite3
import pytest
from helpers import _f32


def _shadow_tables(db, prefix):
    """Return sorted list of shadow table names for a given prefix."""
    return sorted([
        row[0] for row in db.execute(
            r"select name from sqlite_master where name like ? escape '\' and type='table' order by 1",
            [f"{prefix}\\__%"],
        ).fetchall()
    ])


def test_rename_ivf_no_quantizer(db):
    """Rename should rename all IVF shadow tables (_ivf_centroids, _ivf_cells,
    _ivf_rowid_map). quantizer=none — no _ivf_vectors table."""
    db.execute("""
        CREATE VIRTUAL TABLE v USING vec0(
            a float[4] indexed by ivf(nlist=2, quantizer=none)
        )
    """)
    db.execute("insert into v(rowid, a) values (1, ?)", [_f32([0.1] * 4)])
    db.execute("insert into v(rowid, a) values (2, ?)", [_f32([0.9] * 4)])

    before = _shadow_tables(db, "v")
    assert "v_ivf_centroids00" in before
    assert "v_ivf_cells00" in before
    assert "v_ivf_rowid_map00" in before
    assert "v_ivf_vectors00" not in before  # quantizer=none -> no _ivf_vectors
    assert "v_vector_chunks00" not in before

    db.execute("ALTER TABLE v RENAME TO v2")

    # Querying the renamed table should still work — it hits _ivf_cells,
    # _ivf_centroids (when trained), and _ivf_rowid_map.
    rows = db.execute(
        "select rowid from v2 where a match ? and k=10",
        [_f32([0.1] * 4)],
    ).fetchall()
    assert any(r[0] == 1 for r in rows)

    after = _shadow_tables(db, "v2")
    assert "v2_ivf_centroids00" in after
    assert "v2_ivf_cells00" in after
    assert "v2_ivf_rowid_map00" in after

    # No old shadow tables should remain
    assert _shadow_tables(db, "v") == []


def test_rename_ivf_quantizer_binary(db):
    """Rename should also rename _ivf_vectors when quantizer != none."""
    db.execute("""
        CREATE VIRTUAL TABLE v USING vec0(
            a float[8] indexed by ivf(nlist=2, quantizer=binary)
        )
    """)
    db.execute("insert into v(rowid, a) values (1, ?)", [_f32([0.1] * 8)])

    before = _shadow_tables(db, "v")
    assert "v_ivf_centroids00" in before
    assert "v_ivf_cells00" in before
    assert "v_ivf_rowid_map00" in before
    assert "v_ivf_vectors00" in before  # quantizer=binary creates _ivf_vectors

    db.execute("ALTER TABLE v RENAME TO v2")

    rows = db.execute(
        "select rowid from v2 where a match ? and k=10",
        [_f32([0.1] * 8)],
    ).fetchall()
    assert rows[0][0] == 1

    after = _shadow_tables(db, "v2")
    assert "v2_ivf_centroids00" in after
    assert "v2_ivf_cells00" in after
    assert "v2_ivf_rowid_map00" in after
    assert "v2_ivf_vectors00" in after

    assert _shadow_tables(db, "v") == []


def test_rename_ivf_drop_after(db):
    """DROP TABLE on a renamed IVF table must drop every shadow table — leftover
    shadows from a half-renamed IVF index would orphan tables in the schema."""
    db.execute("""
        CREATE VIRTUAL TABLE v USING vec0(
            a float[8] indexed by ivf(nlist=2, quantizer=binary)
        )
    """)
    db.execute("insert into v(rowid, a) values (1, ?)", [_f32([0.1] * 8)])
    db.execute("ALTER TABLE v RENAME TO v2")
    db.execute("DROP TABLE v2")

    assert _shadow_tables(db, "v") == []
    assert _shadow_tables(db, "v2") == []
