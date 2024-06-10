# ruff: noqa: E731

import re
from typing import List
import sqlite3
import unittest
from random import random
import struct
import inspect
import pytest
import json
import numpy as np
from math import isclose

EXT_PATH = "./dist/vec0"

SUPPORTS_SUBTYPE = sqlite3.version_info[1] > 38


def bitmap_full(n: int) -> bytearray:
    assert (n % 8) == 0
    return bytes([0xFF] * int(n / 8))


def bitmap_zerod(n: int) -> bytearray:
    assert (n % 8) == 0
    return bytes([0x00] * int(n / 8))


def f32_zerod(n: int) -> bytearray:
    return bytes([0x00, 0x00, 0x00, 0x00] * int(n))


CHAR_BIT = 8


def _f32(list):
    return struct.pack("%sf" % len(list), *list)


def _int8(list):
    return struct.pack("%sb" % len(list), *list)


def connect(ext, path=":memory:"):
    db = sqlite3.connect(path)

    db.execute(
        "create temp table base_functions as select name from pragma_function_list"
    )
    db.execute("create temp table base_modules as select name from pragma_module_list")

    db.enable_load_extension(True)
    db.load_extension(ext)

    db.execute(
        "create temp table loaded_functions as select name from pragma_function_list where name not in (select name from base_functions) order by name"
    )
    db.execute(
        "create temp table loaded_modules as select name from pragma_module_list where name not in (select name from base_modules) order by name"
    )

    db.row_factory = sqlite3.Row
    return db


db = connect(EXT_PATH)

# db.load_extension(EXT_PATH, entrypoint="trace_debug")


def explain_query_plan(sql):
    return db.execute("explain query plan " + sql).fetchone()["detail"]


def execute_all(cursor, sql, args=None):
    if args is None:
        args = []
    results = cursor.execute(sql, args).fetchall()
    return list(map(lambda x: dict(x), results))


def spread_args(args):
    return ",".join(["?"] * len(args))


FUNCTIONS = [
    "vec_add",
    "vec_bit",
    "vec_debug",
    "vec_distance_cosine",
    "vec_distance_hamming",
    "vec_distance_l2",
    "vec_f32",
    "vec_int8",
    "vec_length",
    "vec_normalize",
    "vec_quantize_binary",
    "vec_quantize_i8",
    "vec_quantize_i8",
    "vec_slice",
    "vec_sub",
    "vec_to_json",
    "vec_version",
]
MODULES = ["vec0", "vec_each", "vec_npy_each"]


def test_funcs():
    funcs = list(
        map(
            lambda a: a[0],
            db.execute("select name from loaded_functions").fetchall(),
        )
    )
    assert funcs == FUNCTIONS


def test_modules():
    modules = list(
        map(lambda a: a[0], db.execute("select name from loaded_modules").fetchall())
    )
    assert modules == MODULES


def test_vec_version():
    vec_version = lambda *args: db.execute("select vec_version()", args).fetchone()[0]
    assert vec_version()[0] == "v"


def test_vec_debug():
    vec_debug = lambda *args: db.execute("select vec_debug()", args).fetchone()[0]
    d = vec_debug().split("\n")
    assert len(d) == 4


def test_vec_bit():
    vec_bit = lambda *args: db.execute("select vec_bit(?)", args).fetchone()[0]
    assert vec_bit(b"\xff") == b"\xff"

    if SUPPORTS_SUBTYPE:
        assert db.execute("select subtype(vec_bit(X'FF'))").fetchone()[0] == 224

    with pytest.raises(
        sqlite3.OperationalError, match="zero-length vectors are not supported."
    ):
        db.execute("select vec_bit(X'')").fetchone()

    for x in [None, "text", 1, 1.999]:
        with pytest.raises(
            sqlite3.OperationalError, match="Unknown type for bitvector."
        ):
            db.execute("select vec_bit(?)", [x]).fetchone()


def test_vec_f32():
    vec_f32 = lambda *args: db.execute("select vec_f32(?)", args).fetchone()[0]
    assert vec_f32(b"\x00\x00\x00\x00") == b"\x00\x00\x00\x00"
    assert vec_f32("[0.0000]") == b"\x00\x00\x00\x00"
    # fmt: off
    tests = [
      [0],
      [0, 0, 0, 0],
      [1, -1, 10, -10],
      [-0, 0, .0001, -.0001],
    ]
    # fmt: on
    for test in tests:
        assert vec_f32(json.dumps(test)) == _f32(test)

    if SUPPORTS_SUBTYPE:
        assert db.execute("select subtype(vec_f32(X'00000000'))").fetchone()[0] == 223

    with pytest.raises(
        sqlite3.OperationalError, match="zero-length vectors are not supported."
    ):
        vec_f32(b"")

    for invalid in [None, 1, 1.2]:
        with pytest.raises(
            sqlite3.OperationalError,
            match=re.escape(
                "Input must have type BLOB (compact format) or TEXT (JSON)",
            ),
        ):
            vec_f32(invalid)

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid float32 vector BLOB length. Must be divisible by 4, found 5",
    ):
        vec_f32(b"aaaaa")
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("JSON array parsing error: Input does not start with '['"),
    ):
        vec_f32("1]")
    # TODO mas tests

    # TODO different error message
    with pytest.raises(
        sqlite3.OperationalError,
        match="zero-length vectors are not supported.",
    ):
        vec_f32("[")

    # vec_f32("[]")


def test_vec_int8():
    vec_int8 = lambda *args: db.execute("select vec_int8(?)", args).fetchone()[0]
    assert vec_int8(b"\x00") == _int8([0])
    assert vec_int8(b"\x00\x0f") == _int8([0, 15])
    assert vec_int8("[0]") == _int8([0])
    assert vec_int8("[1, 2, 3]") == _int8([1, 2, 3])

    if SUPPORTS_SUBTYPE:
        assert db.execute("select subtype(vec_int8(?))", [b"\x00"]).fetchone()[0] == 225


def npy_cosine(a, b):
    return 1 - (np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b)))


def npy_l2(a, b):
    return np.linalg.norm(a - b)


def test_vec_distance_cosine():
    vec_distance_cosine = lambda *args, a="?", b="?": db.execute(
        f"select vec_distance_cosine({a}, {b})", args
    ).fetchone()[0]

    def check(a, b, dtype=np.float32):
        if dtype == np.float32:
            transform = "?"
        elif dtype == np.int8:
            transform = "vec_int8(?)"
        a = np.array(a, dtype=dtype)
        b = np.array(b, dtype=dtype)

        x = vec_distance_cosine(a, b, a=transform, b=transform)
        y = npy_cosine(a, b)
        assert isclose(x, y, abs_tol=1e-6)

    check([1.2, 0.1], [0.4, -0.4])
    check([-1.2, -0.1], [-0.4, 0.4])
    check([1, 2, 3], [-9, -8, -7], dtype=np.int8)
    assert vec_distance_cosine("[1.1, 1.0]", "[1.2, 1.2]") == 0.001131898257881403


def test_vec_distance_hamming():
    vec_distance_hamming = lambda *args: db.execute(
        "select vec_distance_hamming(vec_bit(?), vec_bit(?))", args
    ).fetchone()[0]
    assert vec_distance_hamming(b"\xff", b"\x00") == 8
    assert vec_distance_hamming(b"\xff", b"\x01") == 7
    assert vec_distance_hamming(b"\xab", b"\xab") == 0

    with pytest.raises(
        sqlite3.OperationalError,
        match="Cannot calculate hamming distance between two float32 vectors.",
    ):
        db.execute("select vec_distance_hamming(vec_f32('[1.0]'), vec_f32('[1.0]'))")

    with pytest.raises(
        sqlite3.OperationalError,
        match="Cannot calculate hamming distance between two int8 vectors.",
    ):
        db.execute("select vec_distance_hamming(vec_int8(X'FF'), vec_int8(X'FF'))")


def test_vec_distance_l2():
    vec_distance_l2 = lambda *args, a="?", b="?": db.execute(
        f"select vec_distance_l2({a}, {b})", args
    ).fetchone()[0]

    def check(a, b, dtype=np.float32):
        if dtype == np.float32:
            transform = "?"
        elif dtype == np.int8:
            transform = "vec_int8(?)"
        a = np.array(a, dtype=dtype)
        b = np.array(b, dtype=dtype)

        x = vec_distance_l2(a, b, a=transform, b=transform)
        y = npy_l2(a, b)
        assert isclose(x, y, abs_tol=1e-6)

    check([1.2, 0.1], [0.4, -0.4])
    check([-1.2, -0.1], [-0.4, 0.4])
    check([1, 2, 3], [-9, -8, -7], dtype=np.int8)


def test_vec_length():
    def test_f32():
        vec_length = lambda *args: db.execute("select vec_length(?)", args).fetchone()[
            0
        ]
        assert vec_length(b"\xAA\xBB\xCC\xDD") == 1
        assert vec_length(b"\xAA\xBB\xCC\xDD\x01\x02\x03\x04") == 2
        assert vec_length(f32_zerod(1024)) == 1024

        with pytest.raises(
            sqlite3.OperationalError, match="zero-length vectors are not supported."
        ):
            assert vec_length(b"") == 0
        with pytest.raises(
            sqlite3.OperationalError, match="zero-length vectors are not supported."
        ):
            vec_length("[]")

    def test_int8():
        vec_length_int8 = lambda *args: db.execute(
            "select vec_length(vec_int8(?))", args
        ).fetchone()[0]
        assert vec_length_int8(b"\xAA") == 1
        assert vec_length_int8(b"\xAA\xBB\xCC\xDD") == 4
        assert vec_length_int8(b"\xAA\xBB\xCC\xDD\x01\x02\x03\x04") == 8

        with pytest.raises(
            sqlite3.OperationalError, match="zero-length vectors are not supported."
        ):
            assert vec_length_int8(b"") == 0

    def test_bit():
        vec_length_bit = lambda *args: db.execute(
            "select vec_length(vec_bit(?))", args
        ).fetchone()[0]
        assert vec_length_bit(b"\xAA") == 8
        assert vec_length_bit(b"\xAA\xBB\xCC\xDD") == 8 * 4
        assert vec_length_bit(b"\xAA\xBB\xCC\xDD\x01\x02\x03\x04") == 8 * 8

        with pytest.raises(
            sqlite3.OperationalError, match="zero-length vectors are not supported."
        ):
            assert vec_length_bit(b"") == 0

    test_f32()
    test_int8()
    test_bit()


def test_vec_normalize():
    vec_normalize = lambda *args: db.execute(
        "select vec_normalize(?)", args
    ).fetchone()[0]
    assert list(struct.unpack_from("4f", vec_normalize(_f32([1, 2, -1, -2])))) == [
        0.3162277638912201,
        0.6324555277824402,
        -0.3162277638912201,
        -0.6324555277824402,
    ]


def test_vec_slice():
    vec_slice = lambda *args, f="?": db.execute(
        f"select vec_slice({f}, ?, ?)", args
    ).fetchone()[0]
    assert vec_slice(_f32([1.1, 2.2, 3.3]), 0, 3) == _f32([1.1, 2.2, 3.3])
    assert vec_slice(_f32([1.1, 2.2, 3.3]), 0, 2) == _f32([1.1, 2.2])
    assert vec_slice(_f32([1.1, 2.2, 3.3]), 0, 1) == _f32([1.1])
    assert vec_slice(_int8([1, 2, 3]), 0, 3, f="vec_int8(?)") == _int8([1, 2, 3])
    assert vec_slice(_int8([1, 2, 3]), 0, 2, f="vec_int8(?)") == _int8([1, 2])
    assert vec_slice(_int8([1, 2, 3]), 0, 1, f="vec_int8(?)") == _int8([1])
    assert vec_slice(b"\xAA\xBB\xCC\xDD", 0, 8, f="vec_bit(?)") == b"\xAA"
    assert vec_slice(b"\xAA\xBB\xCC\xDD", 8, 16, f="vec_bit(?)") == b"\xBB"
    assert vec_slice(b"\xAA\xBB\xCC\xDD", 8, 24, f="vec_bit(?)") == b"\xBB\xCC"
    assert vec_slice(b"\xAA\xBB\xCC\xDD", 0, 32, f="vec_bit(?)") == b"\xAA\xBB\xCC\xDD"

    with pytest.raises(
        sqlite3.OperationalError, match="start index must be divisible by 8."
    ):
        vec_slice(b"\xAA\xBB\xCC\xDD", 2, 32, f="vec_bit(?)")

    with pytest.raises(
        sqlite3.OperationalError, match="end index must be divisible by 8."
    ):
        vec_slice(b"\xAA\xBB\xCC\xDD", 0, 31, f="vec_bit(?)")

    with pytest.raises(
        sqlite3.OperationalError, match="slice 'start' index must be a postive number."
    ):
        vec_slice(b"\xab\xab\xab\xab", -1, 1)

    with pytest.raises(
        sqlite3.OperationalError, match="slice 'end' index must be a postive number."
    ):
        vec_slice(b"\xab\xab\xab\xab", 0, -3)
    with pytest.raises(
        sqlite3.OperationalError,
        match="slice 'start' index is greater than the number of dimensions",
    ):
        vec_slice(b"\xab\xab\xab\xab", 2, 3)
    with pytest.raises(
        sqlite3.OperationalError,
        match="slice 'end' index is greater than the number of dimensions",
    ):
        vec_slice(b"\xab\xab\xab\xab", 0, 2)
    with pytest.raises(
        sqlite3.OperationalError,
        match="slice 'start' index is greater than 'end' index",
    ):
        vec_slice(b"\xab\xab\xab\xab", 1, 0)


def test_vec_add():
    vec_add = lambda *args, a="?", b="?": db.execute(
        f"select vec_add({a}, {b})", args
    ).fetchone()[0]
    assert vec_add("[1]", "[2]") == _f32([3])
    assert vec_add("[.1]", "[.2]") == _f32([0.3])
    assert vec_add(_int8([1]), _int8([2]), a="vec_int8(?)", b="vec_int8(?)") == _int8(
        [3]
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match="Cannot add two bitvectors together.",
    ):
        vec_add(b"0xff", b"0xff", a="vec_bit(?)", b="vec_bit(?)")

    with pytest.raises(
        sqlite3.OperationalError,
        match="Vector type mistmatch. First vector has type float32, while the second has type int8.",
    ):
        vec_add(_f32([1]), _int8([2]), b="vec_int8(?)")
    with pytest.raises(
        sqlite3.OperationalError,
        match="Vector type mistmatch. First vector has type int8, while the second has type float32.",
    ):
        vec_add(_int8([2]), _f32([1]), a="vec_int8(?)")


def test_vec_sub():
    vec_sub = lambda *args, a="?", b="?": db.execute(
        f"select vec_sub({a}, {b})", args
    ).fetchone()[0]
    assert vec_sub("[1]", "[2]") == _f32([-1])
    assert vec_sub("[.1]", "[.2]") == _f32([-0.1])
    assert vec_sub(_int8([11]), _int8([2]), a="vec_int8(?)", b="vec_int8(?)") == _int8(
        [9]
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match="Cannot subtract two bitvectors together.",
    ):
        vec_sub(b"0xff", b"0xff", a="vec_bit(?)", b="vec_bit(?)")

    with pytest.raises(
        sqlite3.OperationalError,
        match="Vector type mistmatch. First vector has type float32, while the second has type int8.",
    ):
        vec_sub(_f32([1]), _int8([2]), b="vec_int8(?)")
    with pytest.raises(
        sqlite3.OperationalError,
        match="Vector type mistmatch. First vector has type int8, while the second has type float32.",
    ):
        vec_sub(_int8([2]), _f32([1]), a="vec_int8(?)")


def test_vec_to_json():
    vec_to_json = lambda *args, input="?": db.execute(
        f"select vec_to_json({input})", args
    ).fetchone()[0]
    assert vec_to_json("[1, 2, 3]") == "[1.000000,2.000000,3.000000]"
    assert vec_to_json(b"\x00\x00\x00\x00\x00\x00\x80\xbf") == "[0.000000,-1.000000]"
    assert vec_to_json(b"\x04", input="vec_int8(?)") == "[4]"
    assert vec_to_json(b"\x04\xff", input="vec_int8(?)") == "[4,-1]"
    assert vec_to_json(b"\xff", input="vec_bit(?)") == "[1,1,1,1,1,1,1,1]"
    assert vec_to_json(b"\x0f", input="vec_bit(?)") == "[1,1,1,1,0,0,0,0]"


@pytest.mark.skip(reason="TODO")
def test_vec_quantize_i8():
    vec_quantize_i8 = lambda *args: db.execute(
        "select vec_quantize_i8()", args
    ).fetchone()[0]
    assert vec_quantize_i8() == 111


@pytest.mark.skip(reason="TODO")
def test_vec_quantize_binary():
    vec_quantize_binary = lambda *args: db.execute(
        "select vec_quantize_binary()", args
    ).fetchone()[0]
    assert vec_quantize_binary() == 111


@pytest.mark.skip(reason="TODO")
def test_vec0():
    pass


def test_vec0_updates():
    db = connect(EXT_PATH)
    db.execute(
        """
          create virtual table t using vec0(
            aaa float[128],
            bbb int8[128],
            ccc bit[128]
          );
        """
    )

    db.execute(
        "insert into t values (?, ?, vec_int8(?), vec_bit(?))",
        [
            1,
            np.full((128,), 0.0001, dtype="float32"),
            np.full((128,), 4, dtype="int8"),
            bitmap_full(128),
        ],
    )

    assert execute_all(db, "select * from t") == [
        {
            "rowid": 1,
            "aaa": _f32([0.0001] * 128),
            "bbb": _int8([4] * 128),
            "ccc": bitmap_full(128),
        }
    ]
    db.execute(
        "update t set aaa = ? where rowid = ?",
        [np.full((128,), 0.00011, dtype="float32"), 1],
    )
    assert execute_all(db, "select * from t") == [
        {
            "rowid": 1,
            "aaa": _f32([0.00011] * 128),
            "bbb": _int8([4] * 128),
            "ccc": bitmap_full(128),
        }
    ]


def test_vec_each():
    vec_each_f32 = lambda *args: execute_all(
        db, "select rowid, * from vec_each(vec_f32(?))", args
    )
    assert vec_each_f32(_f32([1.0, 2.0, 3.0])) == [
        {"rowid": 0, "value": 1.0},
        {"rowid": 1, "value": 2.0},
        {"rowid": 2, "value": 3.0},
    ]


import io


def to_npy(arr):
    buf = io.BytesIO()
    np.save(buf, arr)
    buf.seek(0)
    return buf.read()


def test_vec_npy_each():
    vec_npy_each = lambda *args: execute_all(
        db, "select rowid, * from vec_npy_each(?)", args
    )
    assert vec_npy_each(to_npy(np.array([1.1, 2.2, 3.3], dtype=np.float32))) == [
        {
            "rowid": 0,
            "vector": _f32([1.1, 2.2, 3.3]),
        },
    ]
    assert vec_npy_each(to_npy(np.array([[1.1, 2.2, 3.3]], dtype=np.float32))) == [
        {
            "rowid": 0,
            "vector": _f32([1.1, 2.2, 3.3]),
        },
    ]
    assert vec_npy_each(
        to_npy(np.array([[1.1, 2.2, 3.3], [9.9, 8.8, 7.7]], dtype=np.float32))
    ) == [
        {
            "rowid": 0,
            "vector": _f32([1.1, 2.2, 3.3]),
        },
        {
            "rowid": 1,
            "vector": _f32([9.9, 8.8, 7.7]),
        },
    ]


def test_smoke():
    db.execute("create virtual table vec_xyz using vec0( a float[2] )")
    assert execute_all(
        db,
        "select name from sqlite_master where name like 'vec_xyz%' order by name;",
    ) == [
        {
            "name": "vec_xyz",
        },
        {
            "name": "vec_xyz_chunks",
        },
        {
            "name": "vec_xyz_rowids",
        },
        {
            "name": "vec_xyz_vector_chunks00",
        },
    ]
    chunk = db.execute("select * from vec_xyz_chunks").fetchone()
    assert chunk["chunk_id"] == 1
    assert chunk["validity"] == bytearray(int(1024 / 8))
    assert chunk["rowids"] == bytearray(int(1024 * 8))
    vchunk = db.execute("select * from vec_xyz_vector_chunks00").fetchone()
    assert vchunk["rowid"] == 1
    assert vchunk["vectors"] == bytearray(int(1024 * 4 * 2))

    assert re.match(
        "SCAN (TABLE )?vec_xyz VIRTUAL TABLE INDEX 0:knn:",
        explain_query_plan(
            "select * from vec_xyz where a match X'' and k = 10 order by distance"
        ),
    )
    assert re.match(
        "SCAN (TABLE )?vec_xyz VIRTUAL TABLE INDEX 0:fullscan",
        explain_query_plan("select * from vec_xyz"),
    )
    assert re.match(
        "SCAN (TABLE )?vec_xyz VIRTUAL TABLE INDEX 3:point",
        explain_query_plan("select * from vec_xyz where rowid = 4"),
    )

    db.execute("insert into vec_xyz(rowid, a) select 1, X'000000000000803f'")
    chunk = db.execute("select * from vec_xyz_chunks").fetchone()
    assert chunk["chunk_id"] == 1
    assert chunk["validity"] == b"\x01" + bytearray(int(1024 / 8) - 1)
    assert chunk["rowids"] == b"\x01\x00\x00\x00\x00\x00\x00\x00" + bytearray(
        int(1024 * 8) - 8
    )
    vchunk = db.execute("select * from vec_xyz_vector_chunks00").fetchone()
    assert vchunk["rowid"] == 1
    assert vchunk["vectors"] == b"\x00\x00\x00\x00\x00\x00\x80\x3f" + bytearray(
        int(1024 * 4 * 2) - (2 * 4)
    )

    db.execute("insert into vec_xyz(rowid, a) select 2, X'0000000000000040'")
    chunk = db.execute("select * from vec_xyz_chunks").fetchone()
    assert chunk[
        "rowids"
    ] == b"\x01\x00\x00\x00\x00\x00\x00\x00" + b"\x02\x00\x00\x00\x00\x00\x00\x00" + bytearray(
        int(1024 * 8) - 8 * 2
    )
    assert chunk["chunk_id"] == 1
    assert chunk["validity"] == b"\x03" + bytearray(int(1024 / 8) - 1)
    vchunk = db.execute("select * from vec_xyz_vector_chunks00").fetchone()
    assert vchunk["rowid"] == 1
    assert vchunk[
        "vectors"
    ] == b"\x00\x00\x00\x00\x00\x00\x80\x3f" + b"\x00\x00\x00\x00\x00\x00\x00\x40" + bytearray(
        int(1024 * 4 * 2) - (2 * 4 * 2)
    )

    db.execute("insert into vec_xyz(rowid, a) select 3, X'00000000000080bf'")
    chunk = db.execute("select * from vec_xyz_chunks").fetchone()
    assert chunk["chunk_id"] == 1
    assert chunk["validity"] == b"\x07" + bytearray(int(1024 / 8) - 1)
    assert chunk[
        "rowids"
    ] == b"\x01\x00\x00\x00\x00\x00\x00\x00" + b"\x02\x00\x00\x00\x00\x00\x00\x00" + b"\x03\x00\x00\x00\x00\x00\x00\x00" + bytearray(
        int(1024 * 8) - 8 * 3
    )
    vchunk = db.execute("select * from vec_xyz_vector_chunks00").fetchone()
    assert vchunk["rowid"] == 1
    assert vchunk[
        "vectors"
    ] == b"\x00\x00\x00\x00\x00\x00\x80\x3f" + b"\x00\x00\x00\x00\x00\x00\x00\x40" + b"\x00\x00\x00\x00\x00\x00\x80\xbf" + bytearray(
        int(1024 * 4 * 2) - (2 * 4 * 3)
    )

    # db.execute("select * from vec_xyz")
    assert execute_all(db, "select * from vec_xyz") == [
        {"rowid": 1, "a": b"\x00\x00\x00\x00\x00\x00\x80?"},
        {"rowid": 2, "a": b"\x00\x00\x00\x00\x00\x00\x00@"},
        {"rowid": 3, "a": b"\x00\x00\x00\x00\x00\x00\x80\xbf"},
    ]


def test_vec0_stress_small_chunks():
    data = np.zeros((1000, 8), dtype=np.float32)
    for i in range(1000):
        data[i] = np.array([(i + 1) * 0.1] * 8)
    db.execute("create virtual table vec_small using vec0(chunk_size=8, a float[8])")
    assert execute_all(db, "select rowid, * from vec_small") == []
    with db:
        for row in data:
            db.execute("insert into vec_small(a) values (?) ", [row])
    assert execute_all(db, "select rowid, * from vec_small limit 8") == [
        {"rowid": 1, "a": _f32([0.1] * 8)},
        {"rowid": 2, "a": _f32([0.2] * 8)},
        {"rowid": 3, "a": _f32([0.3] * 8)},
        {"rowid": 4, "a": _f32([0.4] * 8)},
        {"rowid": 5, "a": _f32([0.5] * 8)},
        {"rowid": 6, "a": _f32([0.6] * 8)},
        {"rowid": 7, "a": _f32([0.7] * 8)},
        {"rowid": 8, "a": _f32([0.8] * 8)},
    ]
    assert db.execute("select count(*) from vec_small").fetchone()[0] == 1000
    assert execute_all(
        db, "select rowid, * from vec_small order by rowid desc limit 8"
    ) == [
        {"rowid": 1000, "a": _f32([100.0] * 8)},
        {"rowid": 999, "a": _f32([99.9] * 8)},
        {"rowid": 998, "a": _f32([99.8] * 8)},
        {"rowid": 997, "a": _f32([99.7] * 8)},
        {"rowid": 996, "a": _f32([99.6] * 8)},
        {"rowid": 995, "a": _f32([99.5] * 8)},
        {"rowid": 994, "a": _f32([99.4] * 8)},
        {"rowid": 993, "a": _f32([99.3] * 8)},
    ]
    assert (
        execute_all(
            db,
            """
              select rowid, a, distance
              from vec_small
              where a match ?
                and k = 9
              order by distance
            """,
            [_f32([50.0] * 8)],
        )
        == [
            {
                "a": _f32([500 * 0.1] * 8),
                "distance": 0.0,
                "rowid": 500,
            },
            {
                "a": _f32([499 * 0.1] * 8),
                "distance": 0.2828384041786194,
                "rowid": 499,
            },
            {
                "a": _f32([501 * 0.1] * 8),
                "distance": 0.2828384041786194,
                "rowid": 501,
            },
            {
                "a": _f32([498 * 0.1] * 8),
                "distance": 0.5656875967979431,
                "rowid": 498,
            },
            {
                "a": _f32([502 * 0.1] * 8),
                "distance": 0.5656875967979431,
                "rowid": 502,
            },
            {
                "a": _f32([497 * 0.1] * 8),
                "distance": 0.8485260009765625,
                "rowid": 497,
            },
            {
                "a": _f32([503 * 0.1] * 8),
                "distance": 0.8485260009765625,
                "rowid": 503,
            },
            {
                "a": _f32([496 * 0.1] * 8),
                "distance": 1.1313751935958862,
                "rowid": 496,
            },
            {
                "a": _f32([504 * 0.1] * 8),
                "distance": 1.1313751935958862,
                "rowid": 504,
            },
        ]
    )


def rowids_value(buffer: bytearray) -> List[int]:
    assert (len(buffer) % 8) == 0
    n = int(len(buffer) / 8)
    return list(struct.unpack_from(f"<{n}q", buffer))


import numpy.typing as npt


def cosine_similarity(
    vec: npt.NDArray[np.float32], mat: npt.NDArray[np.float32], do_norm: bool = True
) -> npt.NDArray[np.float32]:
    sim = vec @ mat.T
    if do_norm:
        sim /= np.linalg.norm(vec) * np.linalg.norm(mat, axis=1)
    return sim


def topk(
    vec: npt.NDArray[np.float32],
    mat: npt.NDArray[np.float32],
    k: int = 5,
    do_norm: bool = True,
) -> tuple[npt.NDArray[np.int32], npt.NDArray[np.float32]]:
    sim = cosine_similarity(vec, mat, do_norm=do_norm)
    # Rather than sorting all similarities and taking the top K, it's faster to
    # argpartition and then just sort the top K.
    # The difference is O(N logN) vs O(N + k logk)
    indices = np.argpartition(-sim, kth=k)[:k]
    top_indices = np.argsort(-sim[indices])
    return indices[top_indices], sim[top_indices]


def test_stress1():
    np.random.seed(1234)
    data = np.random.uniform(-1.0, 1.0, (8000, 128)).astype(np.float32)
    db.execute(
        "create virtual table vec_stress1 using vec0( a float[128] distance_metric=cosine)"
    )
    with db:
        for idx, row in enumerate(data):
            db.execute("insert into vec_stress1 values (?, ?)", [idx, row])
    queries = np.random.uniform(-1.0, 1.0, (100, 128)).astype(np.float32)
    for q in queries:
        ids, distances = topk(q, data, k=10)
        rows = db.execute(
            """
              select rowid, distance
              from vec_stress1
              where a match ? and k = ?
              order by distance
             """,
            [q, 10],
        ).fetchall()
        assert len(ids) == 10
        assert len(rows) == 10
        vec_ids = [row[0] for row in rows]
        assert ids.tolist() == vec_ids


@pytest.mark.skip(reason="slow")
def test_stress():
    db.execute("create virtual table vec_t1 using vec0( a float[1536])")

    def rand_vec(n):
        return struct.pack("%sf" % n, *list(map(lambda x: random(), range(n))))

    for i in range(1025):
        db.execute("insert into vec_t1(a) values (?)", [rand_vec(1536)])
    rows = db.execute("select validity, rowids from vec_t1_chunks").fetchall()
    assert len(rows) == 2

    assert len(rows[0]["validity"]) == 1024 / CHAR_BIT
    assert len(rows[0]["rowids"]) == 1024 * CHAR_BIT
    assert rows[0]["validity"] == bitmap_full(1024)
    assert rowids_value(rows[0]["rowids"]) == [x + 1 for x in range(1024)]

    assert len(rows[1]["validity"]) == 1024 / CHAR_BIT
    assert len(rows[1]["rowids"]) == 1024 * CHAR_BIT
    assert rows[1]["validity"] == bytes([0b0000_0001]) + bitmap_zerod(1024)[1:]
    assert rowids_value(rows[1]["rowids"])[0] == 1025


def test_coverage():
    current_module = inspect.getmodule(inspect.currentframe())
    test_methods = [
        member[0]
        for member in inspect.getmembers(current_module)
        if member[0].startswith("test_")
    ]
    funcs_with_tests = set([x.replace("test_", "") for x in test_methods])
    for func in [*FUNCTIONS, *MODULES]:
        assert func in funcs_with_tests, f"{func} is not tested"


if __name__ == "__main__":
    unittest.main()
