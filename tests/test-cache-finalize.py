"""Regression tests for #295: vec0 must finalize cached prepared statements
on every commit, not just the rowid subset.

Before the fix, `vec0Sync` only finalized `stmtLatestChunk` and the four
`stmtRowids*` stmts; the DiskANN/IVF/vectors-read stmts persisted on the
vtab indefinitely. Symptom: VACUUM after any DiskANN operation failed with
"SQL statements in progress" because the cached stmts kept the connection
busy. (The same leak also caused `sqlite3_close()` non-v2 to return
SQLITE_BUSY — the original Firefox case in issue #295.)

A separate latent bug — the DiskANN finalize block in `vec0_free_resources`
was nested inside `#if SQLITE_VEC_EXPERIMENTAL_IVF_ENABLE`, so even
xDisconnect/xDestroy didn't finalize DiskANN stmts in the default build.
"""
from helpers import _f32


def test_vacuum_after_diskann_inserts(db):
    db.execute(
        "create virtual table v using vec0("
        "a float[8] indexed by diskann(neighbor_quantizer=binary))"
    )
    for i in range(1, 11):
        db.execute("insert into v(rowid, a) values (?, ?)",
                   (i, _f32([0.1 * i] * 8)))
    db.commit()
    db.execute("VACUUM")


def test_vacuum_after_flat_inserts(db):
    db.execute("create virtual table v using vec0(a float[2])")
    db.execute("insert into v(rowid, a) values (1, ?)", (_f32([0.1, 0.2]),))
    db.commit()
    db.execute("VACUUM")
