import sqlite3
import struct

import pytest


def _f32(values):
    return struct.pack(f"{len(values)}f", *values)


def test_fullscan_reads_indexed_columns(db):
    # Rescore and DiskANN columns keep their vectors outside _vector_chunks, so
    # a scan reads them row by row, beside a flat column read by chunk.
    db.execute(
        "create virtual table v using vec0("
        "flat float[8], "
        "r float[8] indexed by rescore(quantizer=bit), "
        "d float[8] indexed by diskann(neighbor_quantizer=binary), "
        "chunk_size=8)"
    )
    for i in range(1, 21):
        db.execute(
            "insert into v(rowid, flat, r, d) values (?, ?, ?, ?)",
            [i, _f32([i] * 8), _f32([-i] * 8), _f32([i, -i] * 4)],
        )
    rows = db.execute("select rowid, flat, r, d from v").fetchall()
    assert len(rows) == 20
    for rowid, flat, r, d in rows:
        assert flat == _f32([rowid] * 8)
        assert r == _f32([-rowid] * 8)
        assert d == _f32([rowid, -rowid] * 4)


def test_fullscan_reads_every_vector_across_chunks(db):
    # Two vector columns, so each keeps its own handle, over chunks with gaps.
    db.execute(
        "create virtual table v using vec0(a float[3], b float[2], chunk_size=8)"
    )
    for i in range(1, 51):
        db.execute(
            "insert into v(rowid, a, b) values (?, ?, ?)",
            [i, _f32([i, i + 0.5, -i]), _f32([2 * i, 1 / i])],
        )
    db.execute("delete from v where rowid % 7 = 0")

    scanned = {r[0]: (r[1], r[2]) for r in db.execute("select rowid, a, b from v")}
    assert len(scanned) == 50 - 7
    for rowid, vectors in scanned.items():
        point = db.execute("select a, b from v where rowid = ?", [rowid]).fetchone()
        assert vectors == (point[0], point[1])
        assert vectors[0] == _f32([rowid, rowid + 0.5, -rowid])


def test_fullscan_reads_a_chunk_rewritten_during_the_scan(db):
    # Rewriting a chunk's row expires a blob handle open on it; the scan reads
    # the rest of the chunk from the new row, not from what it held before.
    db.execute("create virtual table v using vec0(a float[2], chunk_size=8)")
    for i in range(1, 17):
        db.execute("insert into v(rowid, a) values (?, ?)", [i, _f32([i, 0])])
    first = db.execute("select min(chunk_id) from v_chunks").fetchone()[0]

    scan = db.execute("select rowid, a from v")
    head = [scan.fetchone() for _ in range(2)]
    rewritten = b"".join(_f32([100 + i, 1]) for i in range(1, 9))
    db.execute(
        "update v_vector_chunks00 set vectors = ? where rowid = ?", [rewritten, first]
    )
    rest = scan.fetchall()

    rows = {r[0]: struct.unpack("2f", r[1]) for r in head + rest}
    assert sorted(rows) == list(range(1, 17))
    # Python's cursor may have stepped to the third row before the update, so
    # the rows checked are those it can only have read after it.
    for rowid in range(4, 9):
        assert rows[rowid] == (100 + rowid, 1)
    for rowid in range(9, 17):
        assert rows[rowid] == (rowid, 0)


def _scan_around(db, write):
    # Fetches one row of a full scan, runs `write` on the same connection, then
    # fetches the rest.
    db.execute("create virtual table v using vec0(a float[2], chunk_size=8)")
    for i in range(1, 17):
        db.execute("insert into v(rowid, a) values (?, ?)", [i, _f32([i, 0])])
    db.execute("delete from v where rowid between 2 and 7")
    scan = db.execute("select rowid, a from v")
    head = [scan.fetchone()]
    write()
    return {r[0]: struct.unpack("2f", r[1]) for r in head + scan.fetchall()}


def test_fullscan_errors_on_a_row_deleted_during_the_scan(db):
    def write():
        db.execute("delete from v where rowid = 12")
        db.execute("insert into v(rowid, a) values (99, ?)", [_f32([99, 99])])

    # Row 99 takes row 12's slot; the scan must not return its vector as row 12.
    with pytest.raises(sqlite3.OperationalError, match="rowid 12"):
        _scan_around(db, write)


def test_fullscan_keeps_an_interrupt_raised_while_moving_to_the_next_chunk(db):
    # A progress handler that returns nonzero once cancels the statement. Moving
    # the scan's blob handle to the next chunk runs SQLite's VM, so cancelling
    # there must fail the scan like cancelling anywhere else. A cancellation
    # inside vec0's own statements is reported as a vec0 error rather than
    # "interrupted", so the check is that the scan fails.
    db.execute("create virtual table v using vec0(a float[2], chunk_size=8)")
    for i in range(1, 17):
        db.execute("insert into v(rowid, a) values (?, ?)", [i, _f32([i, 0])])

    def finish_scan(handler):
        scan = db.execute("select rowid, a from v")
        for _ in range(7):
            scan.fetchone()
        db.set_progress_handler(handler, 1)
        try:
            return scan.fetchall()
        finally:
            db.set_progress_handler(None, 1)

    calls = 0

    def count():
        nonlocal calls
        calls += 1
        return 0

    finish_scan(count)
    total = calls
    for n in range(1, total + 1):
        calls = 0

        def cancel_at_n():
            nonlocal calls
            calls += 1
            return calls == n

        with pytest.raises(sqlite3.OperationalError):
            finish_scan(cancel_at_n)
