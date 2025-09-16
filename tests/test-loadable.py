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

SUPPORTS_SUBTYPE = sqlite3.sqlite_version_info[1] > 38
SUPPORTS_DROP_COLUMN = sqlite3.sqlite_version_info[1] >= 35
SUPPORTS_VTAB_IN = sqlite3.sqlite_version_info[1] >= 38
SUPPORTS_VTAB_LIMIT = sqlite3.sqlite_version_info[1] >= 41


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


def _i64(list):
    return struct.pack("%sq" % len(list), *list)


def _int8(list):
    return struct.pack("%sb" % len(list), *list)


def bitmap(bitstring):
    return bytes([int(bitstring, 2)])


def connect(ext, path=":memory:", extra_entrypoint=None):
    db = sqlite3.connect(path)

    db.execute(
        "create temp table base_functions as select name from pragma_function_list"
    )
    db.execute("create temp table base_modules as select name from pragma_module_list")

    db.enable_load_extension(True)
    db.load_extension(ext)

    if extra_entrypoint:
        db.execute("select load_extension(?, ?)", [ext, extra_entrypoint])

    db.execute(
        "create temp table loaded_functions as select name from pragma_function_list where name not in (select name from base_functions) order by name"
    )
    db.execute(
        "create temp table loaded_modules as select name from pragma_module_list where name not in (select name from base_modules) order by name"
    )

    db.row_factory = sqlite3.Row
    return db


db = connect(EXT_PATH)


def explain_query_plan(sql, db=db):
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
    "vec_distance_l1",
    "vec_distance_l2",
    "vec_f32",
    "vec_int8",
    "vec_length",
    "vec_normalize",
    "vec_quantize_binary",
    "vec_quantize_int8",
    "vec_slice",
    "vec_sub",
    "vec_to_json",
    "vec_type",
    "vec_version",
]
MODULES = [
    "vec0",
    "vec_each",
    # "vec_static_blob_entries",
    # "vec_static_blobs",
]


def register_numpy(db, name: str, array):
    ptr = array.__array_interface__["data"][0]
    nvectors, dimensions = array.__array_interface__["shape"]
    element_type = array.__array_interface__["typestr"]

    assert element_type == "<f4"

    name_escaped = db.execute("select printf('%w', ?)", [name]).fetchone()[0]

    db.execute(
        """
          insert into temp.vec_static_blobs(name, data)
          select ?, vec_static_blob_from_raw(?, ?, ?, ?)
        """,
        [name, ptr, element_type, dimensions, nvectors],
    )

    db.execute(
        f'create virtual table "{name_escaped}" using vec_static_blob_entries({name_escaped})'
    )


def test_vec_static_blob_entries():
    db = connect(EXT_PATH, extra_entrypoint="sqlite3_vec_static_blobs_init")

    x = np.array([[0.1, 0.2, 0.3, 0.4], [0.9, 0.8, 0.7, 0.6]], dtype=np.float32)
    y = np.array([[0.2, 0.3], [0.9, 0.8], [0.6, 0.5]], dtype=np.float32)
    z = np.array(
        [
            [0.1, 0.1, 0.1, 0.1],
            [0.2, 0.2, 0.2, 0.2],
            [0.3, 0.3, 0.3, 0.3],
            [0.4, 0.4, 0.4, 0.4],
            [0.5, 0.5, 0.5, 0.5],
        ],
        dtype=np.float32,
    )

    register_numpy(db, "x", x)
    register_numpy(db, "y", y)
    register_numpy(db, "z", z)
    assert execute_all(
        db, "select *, dimensions, count from temp.vec_static_blobs;"
    ) == [
        {
            "count": 2,
            "data": None,
            "dimensions": 4,
            "name": "x",
        },
        {
            "count": 3,
            "data": None,
            "dimensions": 2,
            "name": "y",
        },
        {
            "count": 5,
            "data": None,
            "dimensions": 4,
            "name": "z",
        },
    ]

    assert execute_all(db, "select vec_to_json(vector) from x;") == [
        {
            "vec_to_json(vector)": "[0.100000,0.200000,0.300000,0.400000]",
        },
        {
            "vec_to_json(vector)": "[0.900000,0.800000,0.700000,0.600000]",
        },
    ]
    assert execute_all(db, "select (vector) from y limit 2;") == [
        {
            "vector": b"\xcd\xccL>\x9a\x99\x99>",
        },
        {
            "vector": b"fff?\xcd\xccL?",
        },
    ]
    assert execute_all(db, "select rowid, (vector) from z") == [
        {
            "rowid": 0,
            "vector": b"\xcd\xcc\xcc=\xcd\xcc\xcc=\xcd\xcc\xcc=\xcd\xcc\xcc=",
        },
        {
            "rowid": 1,
            "vector": b"\xcd\xccL>\xcd\xccL>\xcd\xccL>\xcd\xccL>",
        },
        {
            "rowid": 2,
            "vector": b"\x9a\x99\x99>\x9a\x99\x99>\x9a\x99\x99>\x9a\x99\x99>",
        },
        {
            "rowid": 3,
            "vector": b"\xcd\xcc\xcc>\xcd\xcc\xcc>\xcd\xcc\xcc>\xcd\xcc\xcc>",
        },
        {
            "rowid": 4,
            "vector": b"\x00\x00\x00?\x00\x00\x00?\x00\x00\x00?\x00\x00\x00?",
        },
    ]
    assert execute_all(
        db,
        "select rowid, vec_to_json(vector) as v from z where vector match ? and k = 3 order by distance;",
        [np.array([0.3, 0.3, 0.3, 0.3], dtype=np.float32)],
    ) == [
        {
            "rowid": 2,
            "v": "[0.300000,0.300000,0.300000,0.300000]",
        },
        {
            "rowid": 3,
            "v": "[0.400000,0.400000,0.400000,0.400000]",
        },
        {
            "rowid": 1,
            "v": "[0.200000,0.200000,0.200000,0.200000]",
        },
    ]
    assert execute_all(
        db,
        "select rowid, vec_to_json(vector) as v from z where vector match ? and k = 3 order by distance;",
        [np.array([0.6, 0.6, 0.6, 0.6], dtype=np.float32)],
    ) == [
        {
            "rowid": 4,
            "v": "[0.500000,0.500000,0.500000,0.500000]",
        },
        {
            "rowid": 3,
            "v": "[0.400000,0.400000,0.400000,0.400000]",
        },
        {
            "rowid": 2,
            "v": "[0.300000,0.300000,0.300000,0.300000]",
        },
    ]


def test_limits():
    db = connect(EXT_PATH)
    with _raises(
        "vec0 constructor error: Dimension on vector column too large, provided 8193, maximum 8192"
    ):
        db.execute("create virtual table v using vec0(a float[8193])")
    with _raises("vec0 constructor error: chunk_size too large"):
        db.execute("create virtual table v using vec0(a float[4], chunk_size=8200)")
    db.execute("create virtual table v using vec0(a float[1])")

    with _raises("k value in knn query too large, provided 8193 and the limit is 4096"):
        db.execute("select * from v where a match '[0.1]' and k = 8193")


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
    with _raises("zero-length vectors are not supported."):
        vec_f32("[")

    with _raises("zero-length vectors are not supported."):
        vec_f32("[]")
    # with _raises("zero-length vectors are not supported."):
    #    vec_f32("[1.2")

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


def test_vec_distance_l1():
    vec_distance_l1 = lambda *args, a="?", b="?": db.execute(
        f"select vec_distance_l1({a}, {b})", args
    ).fetchone()[0]

    def check(a, b, dtype=np.float32):
        if dtype == np.float32:
            transform = "?"
        elif dtype == np.int8:
            transform = "vec_int8(?)"

        a_sql_t = np.array(a, dtype=dtype)
        b_sql_t = np.array(b, dtype=dtype)

        x = vec_distance_l1(a_sql_t, b_sql_t, a=transform, b=transform)
        # dont use dtype here bc overflow
        y = np.sum(np.abs(np.array(a) - np.array(b)))
        assert isclose(x, y, abs_tol=1e-6)

    check([1, 2, 3], [-9, -8, -7], dtype=np.int8)
    # check overflow
    check([127] * 20, [-128] * 20, dtype=np.int8)
    check([-128, 127], [127, -128], dtype=np.int8)
    check(
        [1, 2, 3, 4, 5, 6, 7, 8, 1, 1, 2, 3, 4, 5, 6, 7, 8, 1],
        [1, 20, 38, 23, 29, 4, 10, 9, 3, 1, 20, 38, 23, 29, 4, 10, 9, 3],
        dtype=np.int8,
    )
    check([0] * 20, [0] * 20, dtype=np.int8)
    check(
        [5, 15, -20, 5, 15, -20, 5, 15, -20, 5, 15, -20, 5, 15, -20, 5, 15, -20],
        [5, 15, -20, 5, 15, -20, 5, 15, -20, 5, 15, -20, 5, 15, -20, 5, 15, -20],
        dtype=np.int8,
    )
    check([100] * 20, [-100] * 20, dtype=np.int8)
    check([127] * 1000000, [-128] * 1000000, dtype=np.int8)

    check(
        [1.2, 0.1, 0.5, 0.9, 1.4, 4.5],
        [0.4, -0.4, 0.1, 0.1, 0.5, 0.9],
        dtype=np.float32,
    )
    check([1.0, 2.0, 3.0], [-1.0, -2.0, -3.0], dtype=np.float32)
    check(
        [1e10, 2e10, np.finfo(np.float32).max],
        [-1e10, -2e10, np.finfo(np.float32).min],
        dtype=np.float32,
    )
    # overflow in leftover elements
    check(
        [1e10, 2e10, 1e10, 2e10, np.finfo(np.float32).max],
        [-1e10, -2e10, -1e10, -2e10, np.finfo(np.float32).min],
        dtype=np.float32,
    )
    # overflow in neon elements
    check(
        [np.finfo(np.float32).max, 1e10, 2e10, 1e10, 2e10],
        [np.finfo(np.float32).min, -1e10, -2e10, -1e10, -2e10],
        dtype=np.float32,
    )


def test_vec_distance_l2():
    vec_distance_l2 = lambda *args, a="?", b="?": db.execute(
        f"select vec_distance_l2({a}, {b})", args
    ).fetchone()[0]

    def check(a, b, dtype=np.float32):
        if dtype == np.float32:
            transform = "?"
        elif dtype == np.int8:
            transform = "vec_int8(?)"

        a_sql_t = np.array(a, dtype=dtype)
        b_sql_t = np.array(b, dtype=dtype)

        x = vec_distance_l2(a_sql_t, b_sql_t, a=transform, b=transform)
        y = npy_l2(np.array(a), np.array(b))
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

    with _raises(
        "slice 'start' index is equal to the 'end' index, vectors must have non-zero length"
    ):
        vec_slice(b"\xab\xab\xab\xab", 0, 0)


def test_vec_type():
    vec_type = lambda *args, a="?": db.execute(
        f"select vec_type({a})", args
    ).fetchone()[0]
    assert vec_type("[1]") == "float32"
    assert vec_type(b"\xaa\xbb\xcc\xdd") == "float32"
    assert vec_type("[1]", a="vec_f32(?)") == "float32"
    assert vec_type("[1]", a="vec_int8(?)") == "int8"
    assert vec_type(b"\xaa", a="vec_bit(?)") == "bit"

    with _raises("invalid float32 vector"):
        vec_type(b"\xaa")
    with _raises("found NULL"):
        vec_type(None)


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
def test_vec_quantize_int8():
    vec_quantize_int8 = lambda *args: db.execute(
        "select vec_quantize_int8()", args
    ).fetchone()[0]
    assert vec_quantize_int8() == 111


def test_vec_quantize_binary():
    vec_quantize_binary = lambda *args, input="?": db.execute(
        f"select vec_quantize_binary({input})", args
    ).fetchone()[0]
    assert vec_quantize_binary("[-1, -1, -1, -1, 1, 1, 1, 1]") == b"\xf0"


@pytest.mark.skip(reason="TODO")
def test_vec0():
    pass


def test_vec0_inserts():
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
    # db.execute(
    #    "update t set aaa = ? where rowid = ?",
    #    [np.full((128,), 0.00011, dtype="float32"), 1],
    # )
    # assert execute_all(db, "select * from t") == [
    #    {
    #        "rowid": 1,
    #        "aaa": _f32([0.00011] * 128),
    #        "bbb": _int8([4] * 128),
    #        "ccc": bitmap_full(128),
    #    }
    # ]

    db.execute("create virtual table t1 using vec0(aaa float[4], chunk_size=8)")
    db.execute(
        "create virtual table txt_pk using vec0( txt_id text primary key, aaa float[4])"
    )

    # EVIDENCE-OF: V06519_23358 vec0 INSERT validates vector
    with _raises(
        'Inserted vector for the "aaa" column is invalid: Input must have type BLOB (compact format) or TEXT (JSON)'
    ):
        db.execute("insert into t1 values (1, ?)", [None])

    # EVIDENCE-OF: V08221_25059 vec0 INSERT validates vector type
    with _raises(
        'Inserted vector for the "aaa" column is expected to be of type float32, but a bit vector was provided.'
    ):
        db.execute("insert into t1 values (1, vec_bit(?))", [b"\xff\xff\xff\xff"])

    # EVIDENCE-OF: V01145_17984 vec0 INSERT validates vector dimension match
    with _raises(
        'Dimension mismatch for inserted vector for the "aaa" column. Expected 4 dimensions but received 3.'
    ):
        db.execute("insert into t1 values (1, ?)", ["[1,2,3]"])

    # EVIDENCE-OF: V24228_08298 vec0 INSERT ensure no value provided for "distance" hidden column.
    with _raises('A value was provided for the hidden "distance" column.'):
        db.execute("insert into t1(rowid, aaa, distance) values (1, '[1,2,3,4]', 1)")

    # EVIDENCE-OF: V11875_28713 vec0 INSERT ensure no value provided for "distance" hidden column.
    with _raises('A value was provided for the hidden "k" column.'):
        db.execute("insert into t1(rowid, aaa, k) values (1, '[1,2,3,4]', 1)")

    # EVIDENCE-OF: V17090_01160 vec0 INSERT duplicated int primary key raises uniqueness error
    db.execute("insert into t1 values (1, '[1,1,1,1]')")
    with _raises("UNIQUE constraint failed on t1 primary key"):
        db.execute("insert into t1 values (1, '[2,2,2,2]')")

    # similate error on rowids shadow table
    db.commit()
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_INSERT, "t1_rowids"))
    # EVIDENCE-OF: V04679_21517 vec0 INSERT failed on _rowid shadow insert raises error
    with _raises(
        "Internal sqlite-vec error: could not initialize 'insert rowids' statement",
        sqlite3.DatabaseError,
    ):
        db.execute("insert into t1 values (2, '[2,2,2,2]')")
    db.set_authorizer(None)
    db.rollback()
    db.execute("insert into t1 values (2, '[2,2,2,2]')")

    # test inserts where no rowid is provided
    db.execute("insert into t1(aaa) values ('[3,3,3,3]')")

    # EVIDENCE-OF: V30855_14925 vec0 INSERT non-integer/text primary key value rauses error
    with _raises("Only integers are allows for primary key values on t1"):
        db.execute("insert into t1 values (1.2, '[4,4,4,4]')")

    # similate error on rowids shadow table, when rowid is not provided
    # EVIDENCE-OF: V15177_32015 vec0 INSERT error on _rowids shadow insert raises error
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_INSERT, "t1_rowids"))
    with _raises("Error inserting id into rowids shadow table: not authorized"):
        db.execute("insert into t1(aaa) values ('[2,2,2,2]')")
    db.set_authorizer(None)

    # EVIDENCE-OF: V31559_15629 vec0 INSERT error on _chunks shadow insert raises error
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_READ, "t1_chunks", "chunk_id"))
    with _raises("Internal sqlite-vec error: Could not find latest chunk"):
        db.execute("insert into t1 values (999, '[2,2,2,2]')")
    db.set_authorizer(None)

    # EVIDENCE-OF: V22053_06123 vec0 INSERT error on reading validity blob
    if SUPPORTS_DROP_COLUMN:
        db.commit()
        db.execute("begin")
        db.execute("ALTER TABLE t1_chunks DROP COLUMN validity")
        with _raises(
            "Internal sqlite-vec error: could not open validity blob on main.t1_chunks.1"
        ):
            db.execute("insert into t1 values (9999, '[2,2,2,2]')")
        db.rollback()

    # EVIDENCE-OF: V29362_13432 vec0 INSERT validity blob size mismatch with chunk_size
    db.commit()
    db.execute("begin")
    db.execute("UPDATE t1_chunks SET validity = zeroblob(101)")
    with _raises(
        "Internal sqlite-vec error: validity blob size mismatch on main.t1_chunks.1, expected 1 but received 101."
    ):
        db.execute("insert into t1 values (9999, '[2,2,2,2]')")
    db.rollback()

    # EVIDENCE-OF: V16386_00456 vec0 INSERT valdates vector blob column sizes
    db.commit()
    db.execute("begin")
    db.execute("UPDATE t1_vector_chunks00 SET vectors = zeroblob(101)")
    with _raises(
        "Internal sqlite-vec error: vector blob size mismatch on main.t1_vector_chunks00.1. Expected 128, actual 101"
    ):
        db.execute("insert into t1 values (9999, '[2,2,2,2]')")
    db.rollback()

    # EVIDENCE-OF: V09221_26060 vec0 INSERT rowids blob open error
    if SUPPORTS_DROP_COLUMN:
        db.commit()
        db.execute("begin")
        db.execute("ALTER TABLE t1_chunks DROP COLUMN rowids")
        with _raises(
            "Internal sqlite-vec error: could not open rowids blob on main.t1_chunks.1"
        ):
            db.execute("insert into t1 values (9999, '[2,2,2,2]')")
        db.rollback()

    # EVIDENCE-OF: V12779_29618 vec0 INSERT rowids blob validates size
    db.commit()
    db.execute("begin")
    db.execute("UPDATE t1_chunks SET rowids = zeroblob(101)")
    with _raises(
        "Internal sqlite-vec error: rowids blob size mismatch on main.t1_chunks.1. Expected 64, actual 101"
    ):
        db.execute("insert into t1 values (9999, '[2,2,2,2]')")
    db.rollback()

    # EVIDENCE-OF: V21925_05995 vec0 INSERT error on "rowids update position" raises error
    db.commit()
    db.execute("begin")
    db.execute("insert into t1 values (9998, '[2,2,2,2]')")
    db.set_authorizer(
        authorizer_deny_on(sqlite3.SQLITE_UPDATE, "t1_rowids", "chunk_id")
    )
    with _raises(
        "Internal sqlite-vec error: could not update rowids position for rowid=9999, chunk_rowid=1, chunk_offset=4"
    ):
        db.execute("insert into t1 values (9999, '[2,2,2,2]')")
    db.set_authorizer(None)
    db.rollback()

    ########## testing inserts on text primary key tables ##########

    # EVIDENCE-OF: V04200_21039 vec0 table with text primary key ensure text values
    with _raises(
        "The txt_pk virtual table was declared with a TEXT primary key, but a non-TEXT value was provided in an INSERT."
    ):
        db.execute("insert into txt_pk(txt_id, aaa) values (1, '[1,2,3,4]')")

    db.execute("insert into txt_pk(txt_id, aaa) values ('a', '[1,2,3,4]')")

    # EVIDENCE-OF: V20497_04568 vec0 table with text primary key raises uniqueness error on duplicate values
    with _raises("UNIQUE constraint failed on txt_pk primary key"):
        db.execute("insert into txt_pk(txt_id, aaa) values ('a', '[5,6,7,8]')")

    # EVIDENCE-OF: V24016_08086 vec0 table with text primary key raises error on rowid write error
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_INSERT, "txt_pk_rowids"))
    with _raises("Error inserting id into rowids shadow table: not authorized"):
        db.execute("insert into txt_pk(txt_id, aaa) values ('b', '[2,2,2,2]')")
    db.set_authorizer(None)
    db.execute("insert into txt_pk(txt_id, aaa) values ('b', '[2,2,2,2]')")


def test_vec0_insert_errors2():
    db = connect(EXT_PATH)
    db.execute("create virtual table t1 using vec0(aaa float[4], chunk_size=8)")
    db.execute(
        """
      insert into t1(aaa) values
      ('[1,1,1,1]'),
      ('[2,1,1,1]'),
      ('[3,1,1,1]'),
      ('[4,1,1,1]'),
      ('[5,1,1,1]'),
      ('[6,1,1,1]')
    """
    )
    assert execute_all(db, "select * from t1_chunks") == [
        {
            "chunk_id": 1,
            "rowids": b"\x01\x00\x00\x00\x00\x00\x00\x00"
            + b"\x02\x00\x00\x00\x00\x00\x00\x00"
            + b"\x03\x00\x00\x00\x00\x00\x00\x00"
            + b"\x04\x00\x00\x00\x00\x00\x00\x00"
            + b"\x05\x00\x00\x00\x00\x00\x00\x00"
            + b"\x06\x00\x00\x00\x00\x00\x00\x00"
            + b"\x00\x00\x00\x00\x00\x00\x00\x00"
            + b"\x00\x00\x00\x00\x00\x00\x00\x00",
            "size": 8,
            "validity": b"?",  # 0b00111111
        }
    ]
    db.execute(
        """
      insert into t1(aaa) values
      ('[7,1,1,1]'),
      ('[8,1,1,1]')
    """
    )
    assert execute_all(db, "select * from t1_chunks") == [
        {
            "chunk_id": 1,
            "rowids": b"\x01\x00\x00\x00\x00\x00\x00\x00"
            + b"\x02\x00\x00\x00\x00\x00\x00\x00"
            + b"\x03\x00\x00\x00\x00\x00\x00\x00"
            + b"\x04\x00\x00\x00\x00\x00\x00\x00"
            + b"\x05\x00\x00\x00\x00\x00\x00\x00"
            + b"\x06\x00\x00\x00\x00\x00\x00\x00"
            + b"\x07\x00\x00\x00\x00\x00\x00\x00"
            + b"\x08\x00\x00\x00\x00\x00\x00\x00",
            "size": 8,
            "validity": b"\xff",  # 0b11111111
        }
    ]
    # EVIDENCE-OF: V08441_25279 vec0 INSERT error on new chunk creation raises error
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_INSERT, "t1_chunks"))
    with _raises("Internal sqlite-vec error: Could not insert a new vector chunk"):
        db.execute("insert into t1(aaa) values ('[9,1,1,1]')")
    db.set_authorizer(None)


def test_vec0_drops():
    db = connect(EXT_PATH)
    db.execute(
        "create virtual table t1 using vec0(aaa float[4], bbb float[4], chunk_size=8)"
    )
    assert [
        row["name"]
        for row in execute_all(
            db, "select name from sqlite_master where name like 't1%' order by 1"
        )
    ] == [
        "t1",
        "t1_chunks",
        "t1_info",
        "t1_rowids",
        "t1_vector_chunks00",
        "t1_vector_chunks01",
    ]

    db.execute("drop table t1")
    assert [
        row["name"]
        for row in execute_all(
            db, "select name from sqlite_master where name like 't1%' order by 1"
        )
    ] == []


def test_vec0_delete():
    db = connect(EXT_PATH)
    db.execute("create virtual table t1 using vec0(aaa float[4], chunk_size=8)")
    db.execute(
        """
      insert into t1(aaa) values
      ('[1,1,1,1]'),
      ('[2,1,1,1]'),
      ('[3,1,1,1]'),
      ('[4,1,1,1]'),
      ('[5,1,1,1]'),
      ('[6,1,1,1]')
    """
    )
    assert execute_all(db, "select * from t1_rowids") == [
        {
            "chunk_id": 1,
            "chunk_offset": 0,
            "id": None,
            "rowid": 1,
        },
        {
            "chunk_id": 1,
            "chunk_offset": 1,
            "id": None,
            "rowid": 2,
        },
        {
            "chunk_id": 1,
            "chunk_offset": 2,
            "id": None,
            "rowid": 3,
        },
        {
            "chunk_id": 1,
            "chunk_offset": 3,
            "id": None,
            "rowid": 4,
        },
        {
            "chunk_id": 1,
            "chunk_offset": 4,
            "id": None,
            "rowid": 5,
        },
        {
            "chunk_id": 1,
            "chunk_offset": 5,
            "id": None,
            "rowid": 6,
        },
    ]
    assert execute_all(db, "select * from t1_chunks") == [
        {
            "chunk_id": 1,
            "rowids": _i64([1, 2, 3, 4, 5, 6, 0, 0]),
            "size": 8,
            "validity": bitmap("00111111"),
        }
    ]
    assert execute_all(db, "select * from t1_vector_chunks00") == [
        {
            "rowid": 1,
            "vectors": _f32([1, 1, 1, 1])
            + _f32([2, 1, 1, 1])
            + _f32([3, 1, 1, 1])
            + _f32([4, 1, 1, 1])
            + _f32([5, 1, 1, 1])
            + _f32([6, 1, 1, 1])
            + _f32([0, 0, 0, 0])
            + _f32([0, 0, 0, 0]),
        }
    ]

    db.execute("DELETE FROM t1 WHERE rowid = 1")
    assert execute_all(db, "select * from t1_rowids") == [
        {
            "chunk_id": 1,
            "chunk_offset": 1,
            "id": None,
            "rowid": 2,
        },
        {
            "chunk_id": 1,
            "chunk_offset": 2,
            "id": None,
            "rowid": 3,
        },
        {
            "chunk_id": 1,
            "chunk_offset": 3,
            "id": None,
            "rowid": 4,
        },
        {
            "chunk_id": 1,
            "chunk_offset": 4,
            "id": None,
            "rowid": 5,
        },
        {
            "chunk_id": 1,
            "chunk_offset": 5,
            "id": None,
            "rowid": 6,
        },
    ]
    # TODO finish delete support
    # assert execute_all(db, "select * from t1_chunks") == [
    #    {
    #        'chunk_id': 1,
    #        'rowids': _i64([0,2,3,4,5,6,0,0]),
    #        'size': 8,
    #        'validity': bitmap("00111110"),
    #    }
    # ]
    # assert execute_all(db, "select * from t1_vector_chunks00") == [
    #    {
    #        'rowid': 1,
    #        'vectors': _f32([0,0,0,0])
    #        +_f32([2,1,1,1])
    #        +_f32([3,1,1,1])
    #        +_f32([4,1,1,1])
    #        +_f32([5,1,1,1])
    #        +_f32([6,1,1,1])
    #        +_f32([0,0,0,0])
    #        +_f32([0,0,0,0])
    #    }
    # ]

    # TODO test with text primary keys


def test_vec0_delete_errors():
    db = connect(EXT_PATH)
    db.execute("create virtual table t1 using vec0(aaa float[4], chunk_size=8)")
    db.execute(
        """
      insert into t1(aaa) values
      ('[1,1,1,1]'),
      ('[2,1,1,1]'),
      ('[3,1,1,1]'),
      ('[4,1,1,1]'),
      ('[5,1,1,1]'),
      ('[6,1,1,1]')
    """
    )

    # db.commit()
    # db.execute("begin")
    # db.execute("DELETE FROM t1_rowids WHERE rowid = 1")
    # with _raises("XXX"):
    #   db.execute("DELETE FROM t1 where rowid = 1")
    # db.rollback()

    # EVIDENCE-OF: V26002_10073 vec0 DELETE error on reading validity blob
    if SUPPORTS_DROP_COLUMN:
        db.commit()
        db.execute("begin")
        db.execute("ALTER TABLE t1_chunks DROP COLUMN validity")
        with _raises("could not open validity blob for main.t1_chunks.1"):
            db.execute("delete from t1 where rowid = 1")
        db.rollback()

    # EVIDENCE-OF: V21193_05263 vec0 DELETE verifies that the validity bit is 1 before clearing
    db.commit()
    db.execute("begin")
    db.execute("UPDATE t1_chunks SET validity = zeroblob(1)")
    with _raises(
        "vec0 deletion error: validity bit is not set for main.t1_chunks.1 at 0"
    ):
        db.execute("delete from t1 where rowid = 1")
    db.rollback()

    # EVIDENCE-OF: V21193_05263 vec0 DELETE raises error on validity blob error
    db.commit()
    db.execute("begin")
    db.execute("UPDATE t1_chunks SET validity = zeroblob(0)")
    with _raises("could not read validity blob for main.t1_chunks.1 at 0"):
        db.execute("delete from t1 where rowid = 1")
    db.rollback()

    if False:  # TODO
        with _raises("XXX"):
            db.execute("DELETE FROM t1 WHERE rowid = 999")
    if False:  # TODO
        db.commit()
        db.execute("begin")
        db.execute("DELETE FROM t1_rowids WHERE rowid = 1")
        with _raises("XXX"):
            db.execute("DELETE FROM t1 where rowid = 1")
        db.rollback()


def test_vec0_updates():
    db = connect(EXT_PATH)
    db.execute(
        """
          create virtual table t3 using vec0(
            aaa float[8],
            bbb int8[8],
            ccc bit[8]
          );
        """
    )
    db.execute(
        """
               INSERT INTO t3 VALUES
                (1, :x, vec_quantize_int8(:x, 'unit') ,vec_quantize_binary(:x)),
                (2, :y, vec_quantize_int8(:y, 'unit') ,vec_quantize_binary(:y)),
                (3, :z, vec_quantize_int8(:z, 'unit') ,vec_quantize_binary(:z));
        """,
        {
            "x": "[.1, .1, .1, .1, -.1, -.1, -.1, -.1]",
            "y": "[-.2, .2, .2, .2, .2, .2, -.2, .2]",
            "z": "[.3, .3, .3, .3, .3, .3, .3, .3]",
        },
    )
    assert execute_all(db, "select * from t3") == [
        {
            "rowid": 1,
            "aaa": _f32([0.1, 0.1, 0.1, 0.1, -0.1, -0.1, -0.1, -0.1]),
            "bbb": _int8([12, 12, 12, 12, -13, -13, -13, -13]),
            "ccc": bitmap("00001111"),
        },
        {
            "rowid": 2,
            "aaa": _f32([-0.2, 0.2, 0.2, 0.2, 0.2, 0.2, -0.2, 0.2]),
            "bbb": _int8([-26, 24, 24, 24, 24, 24, -26, 24]),
            "ccc": bitmap("10111110"),
        },
        {
            "rowid": 3,
            "aaa": _f32([0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3]),
            "bbb": _int8(
                [
                    37,
                    37,
                    37,
                    37,
                    37,
                    37,
                    37,
                    37,
                ]
            ),
            "ccc": bitmap("11111111"),
        },
    ]

    db.execute("UPDATE t3 SET aaa = ? WHERE rowid = 1", ["[.9,.9,.9,.9,.9,.9,.9,.9]"])
    assert execute_all(db, "select * from t3") == [
        {
            "rowid": 1,
            "aaa": _f32([0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9]),
            "bbb": _int8([12, 12, 12, 12, -13, -13, -13, -13]),
            "ccc": bitmap("00001111"),
        },
        {
            "rowid": 2,
            "aaa": _f32([-0.2, 0.2, 0.2, 0.2, 0.2, 0.2, -0.2, 0.2]),
            "bbb": _int8([-26, 24, 24, 24, 24, 24, -26, 24]),
            "ccc": bitmap("10111110"),
        },
        {
            "rowid": 3,
            "aaa": _f32([0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3]),
            "bbb": _int8(
                [
                    37,
                    37,
                    37,
                    37,
                    37,
                    37,
                    37,
                    37,
                ]
            ),
            "ccc": bitmap("11111111"),
        },
    ]

    # EVIDENCE-OF: V15203_32042 vec0 UPDATE validates vector
    with _raises(
        'Updated vector for the "aaa" column is invalid: invalid float32 vector BLOB length. Must be divisible by 4, found 1'
    ):
        db.execute("UPDATE t3 SET aaa = X'AB' WHERE rowid = 1")

    # EVIDENCE-OF: V25739_09810 vec0 UPDATE validates dimension length
    with _raises(
        'Dimension mismatch for new updated vector for the "aaa" column. Expected 8 dimensions but received 1.'
    ):
        db.execute("UPDATE t3 SET aaa = vec_bit(X'AABBCCDD') WHERE rowid = 1")

    # EVIDENCE-OF: V03643_20481 vec0 UPDATE validates vector column type
    with _raises(
        'Updated vector for the "bbb" column is expected to be of type int8, but a float32 vector was provided.'
    ):
        db.execute("UPDATE t3 SET bbb = X'ABABABAB' WHERE rowid = 1")

    db.execute("CREATE VIRTUAL TABLE t2 USING vec0(a float[2], b float[2])")
    db.execute("INSERT INTO t2(rowid, a, b) VALUES (1, '[.1, .1]', '[.2, .2]')")
    assert execute_all(db, "select * from t2") == [
        {
            "rowid": 1,
            "a": _f32([0.1, 0.1]),
            "b": _f32([0.2, 0.2]),
        }
    ]
    # sanity check: the 1st column UPDATE "works", but since the 2nd one fails,
    # then aaa should remain unchanged.
    with _raises(
        'Dimension mismatch for new updated vector for the "b" column. Expected 2 dimensions but received 3.'
    ):
        db.execute(
            "UPDATE t2 SET a = '[.11, .11]', b = '[.22, .22, .22]' WHERE rowid = 1"
        )
    assert execute_all(db, "select * from t2") == [
        {
            "rowid": 1,
            "a": _f32([0.1, 0.1]),
            "b": _f32([0.2, 0.2]),
        }
    ]
    # TODO: set UPDATEs on int8/bit columns

    # db.execute("UPDATE t3 SET ccc = vec_bit(?) WHERE rowid = 3", [bitmap('01010101')])
    # assert execute_all(db, "select * from t3") == [
    #     {
    #         "rowid": 1,
    #         "aaa": _f32([0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9]),
    #         "bbb": _int8([12, 12, 12, 12, -13, -13, -13, -13]),
    #         "ccc": bitmap("00001111"),
    #     },
    #     {
    #         "rowid": 2,
    #         "aaa": _f32([-0.2, 0.2, 0.2, 0.2, 0.2, 0.2, -0.2, 0.2]),
    #         "bbb": _int8([-26, 24,  24,  24,  24,  24,  -26,  24]),
    #         "ccc": bitmap("10111110"),
    #     },
    #     {
    #         "rowid": 3,
    #         "aaa": _f32([0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3]),
    #         "bbb": _int8([37, 37, 37, 37, 37, 37, 37, 37, ]),
    #         "ccc": bitmap("11111111"),
    #     },
    # ]


def test_vec0_point():
    db = connect(EXT_PATH)
    db.execute("CREATE VIRTUAL TABLE t USING vec0(a float[1], b float[1])")
    db.execute(
        "INSERT INTO t VALUES (1, X'AABBCCDD', X'00112233'), (2, X'AABBCCDD', X'99887766');"
    )

    assert execute_all(db, "select * from t where rowid = 1") == [
        {
            "a": b"\xaa\xbb\xcc\xdd",
            "b": b'\x00\x11"3',
            "rowid": 1,
        }
    ]
    assert execute_all(db, "select * from t where rowid = 999") == []

    db.execute(
        "CREATE VIRTUAL TABLE t2 USING vec0(id text primary key, a float[1], b float[1])"
    )
    db.execute(
        "INSERT INTO t2 VALUES ('A', X'AABBCCDD', X'00112233'), ('B', X'AABBCCDD', X'99887766');"
    )

    assert execute_all(db, "select * from t2 where id = 'A'") == [
        {
            "a": b"\xaa\xbb\xcc\xdd",
            "b": b'\x00\x11"3',
            "id": "A",
        }
    ]

    assert execute_all(db, "select * from t2 where id = 'xxx'") == []


def test_vec0_text_pk():
    db = connect(EXT_PATH)
    db.execute(
        """
          create virtual table t using vec0(
            t_id text primary key,
            aaa float[1],
            bbb float8[1],
            chunk_size=8
          );
        """
    )
    assert execute_all(db, "select * from t") == []

    with _raises(
        "The t virtual table was declared with a TEXT primary key, but a non-TEXT value was provided in an INSERT."
    ):
        db.execute("INSERT INTO t VALUES (1, X'AABBCCDD', X'AABBCCDD')")

    db.executemany(
        "INSERT INTO t VALUES (:t_id, :aaa, :bbb)",
        [
            {
                "t_id": "t_1",
                "aaa": "[.1]",
                "bbb": "[-.1]",
            },
            {
                "t_id": "t_2",
                "aaa": "[.2]",
                "bbb": "[-.2]",
            },
            {
                "t_id": "t_3",
                "aaa": "[.3]",
                "bbb": "[-.3]",
            },
        ],
    )
    assert execute_all(db, "select t_id from t") == [
        {"t_id": "t_1"},
        {"t_id": "t_2"},
        {"t_id": "t_3"},
    ]
    assert execute_all(db, "select * from t") == [
        {"t_id": "t_1", "aaa": _f32([0.1]), "bbb": _f32([-0.1])},
        {"t_id": "t_2", "aaa": _f32([0.2]), "bbb": _f32([-0.2])},
        {"t_id": "t_3", "aaa": _f32([0.3]), "bbb": _f32([-0.3])},
    ]

    # EVIDENCE-OF: V09901_26739 vec0 full scan catches _rowid prep error
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_READ, "t_rowids", "rowid"))
    with _raises(
        "Error preparing rowid scan: access to t_rowids.rowid is prohibited",
        sqlite3.DatabaseError,
    ):
        db.execute("select * from t")
    db.set_authorizer(None)

    assert execute_all(
        db, "select t_id, distance from t where aaa match ? and k = 3", ["[.01]"]
    ) == [
        {
            "t_id": "t_1",
            "distance": 0.09000000357627869,
        },
        {
            "t_id": "t_2",
            "distance": 0.1899999976158142,
        },
        {
            "t_id": "t_3",
            "distance": 0.2900000214576721,
        },
    ]

    if SUPPORTS_VTAB_IN:
        assert re.match(
            ("SCAN (TABLE )?t VIRTUAL TABLE INDEX 0:3{___}___\[___"),
            explain_query_plan(
                "select t_id, distance from t where aaa match '' and k = 3 and t_id in ('t_2', 't_3')",
                db=db,
            ),
        )
        assert execute_all(
            db,
            "select t_id, distance from t where aaa match ? and k = 3 and t_id in ('t_2', 't_3')",
            ["[.01]"],
        ) == [
            {
                "t_id": "t_2",
                "distance": 0.1899999976158142,
            },
            {
                "t_id": "t_3",
                "distance": 0.2900000214576721,
            },
        ]

    # test deletes on text primary keys
    db.execute("delete from t where t_id = 't_1'")
    assert execute_all(db, "select * from t") == [
        {"t_id": "t_2", "aaa": _f32([0.2]), "bbb": _f32([-0.2])},
        {"t_id": "t_3", "aaa": _f32([0.3]), "bbb": _f32([-0.3])},
    ]

    # test updates on text primary keys
    db.execute("update t set aaa = '[999]' where t_id = 't_2'")
    assert execute_all(db, "select * from t") == [
        {"t_id": "t_2", "aaa": _f32([999]), "bbb": _f32([-0.2])},
        {"t_id": "t_3", "aaa": _f32([0.3]), "bbb": _f32([-0.3])},
    ]

    # EVIDENCE-OF: V08886_25725 vec0 primary keys don't allow updates on PKs
    with _raises("UPDATEs on vec0 primary key values are not allowed."):
        db.execute("update t set t_id = 'xxx' where t_id = 't_2'")


def test_vec0_best_index():
    db = connect(EXT_PATH)
    db.execute(
        """
          create virtual table t using vec0(
            aaa float[1],
            bbb float8[1]
          );
        """
    )

    with _raises("only 1 MATCH operator is allowed in a single vec0 query"):
        db.execute("select * from t where aaa match NULL and bbb match NULL")

    if SUPPORTS_VTAB_IN:
        with _raises(
            "only 1 'rowid in (..)' operator is allowed in a single vec0 query"
        ):
            db.execute("select * from t where rowid in(4,5,6) and rowid in (1, 2,3)")

    with _raises("A LIMIT or 'k = ?' constraint is required on vec0 knn queries."):
        db.execute("select * from t where aaa MATCH ?")

    if SUPPORTS_VTAB_LIMIT:
        with _raises("Only LIMIT or 'k =?' can be provided, not both"):
            db.execute("select * from t where aaa MATCH ? and k = 10 limit 20")

        with _raises(
            "Only a single 'ORDER BY distance' clause is allowed on vec0 KNN queries"
        ):
            db.execute(
                "select * from t where aaa MATCH NULL and k = 10 order by distance, distance"
            )

    with _raises(
        "Only ascending in ORDER BY distance clause is supported, DESC is not supported yet."
    ):
        db.execute(
            "select * from t where aaa MATCH NULL and k = 10 order by distance desc"
        )


def authorizer_deny_on(operation, x1, x2=None):
    def _auth(op, p1, p2, p3, p4):
        if op == operation and p1 == x1 and p2 == x2:
            return sqlite3.SQLITE_DENY
        return sqlite3.SQLITE_OK

    return _auth


def authorizer_debug(op, p1, p2, p3, p4):
    print(op, p1, p2, p3, p4)
    return sqlite3.SQLITE_OK


from contextlib import contextmanager


@contextmanager
def _raises(message, error=sqlite3.OperationalError):
    with pytest.raises(error, match=re.escape(message)):
        yield


def test_vec_each():
    vec_each_f32 = lambda *args: execute_all(
        db, "select rowid, * from vec_each(vec_f32(?))", args
    )
    assert vec_each_f32(_f32([1.0, 2.0, 3.0])) == [
        {"rowid": 0, "value": 1.0},
        {"rowid": 1, "value": 2.0},
        {"rowid": 2, "value": 3.0},
    ]

    with _raises("Input must have type BLOB (compact format) or TEXT (JSON), found NULL"):
      vec_each_f32(None)


import io


def to_npy(arr):
    buf = io.BytesIO()
    np.save(buf, arr)
    buf.seek(0)
    return buf.read()


def test_vec_npy_each():
    db = connect(EXT_PATH, extra_entrypoint="sqlite3_vec_numpy_init")
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

    assert vec_npy_each(to_npy(np.array([], dtype=np.float32))) == []


def test_vec_npy_each_errors():
    db = connect(EXT_PATH, extra_entrypoint="sqlite3_vec_numpy_init")
    vec_npy_each = lambda *args: execute_all(
        db, "select rowid, * from vec_npy_each(?)", args
    )

    full = b"\x93NUMPY\x01\x00v\x00{'descr': '<f4', 'fortran_order': False, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"

    # EVIDENCE-OF: V03312_20150 numpy validation too short
    with _raises("numpy array too short"):
        vec_npy_each(b"")
    # EVIDENCE-OF: V11954_28792 numpy validate magic
    with _raises("numpy array does not contain the 'magic' header"):
        vec_npy_each(b"\x93NUMPX\x01\x00v\x00")

    with _raises("numpy array header length is invalid"):
        vec_npy_each(b"\x93NUMPY\x01\x00v\x00")

    with _raises("numpy header did not start with '{'"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00c'descr': '<f4', 'fortran_order': False, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("expected key in numpy header"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{                                                                                                                    \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("expected a string as key in numpy header"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{False: '<f4', 'fortran_order': False, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("expected a ':' after key in numpy header"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'descr'                                                                                                           \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )
    with _raises("expected a ':' after key in numpy header"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'descr' False                                                                                                           \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("expected a string value after 'descr' key"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'descr':                                                                                                  \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("Only '<f4' values are supported in sqlite-vec numpy functions"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'descr': '=f4', 'fortran_order': False, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises(
        "Only fortran_order = False is supported in sqlite-vec numpy functions"
    ):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'descr': '<f4', 'fortran_order': True, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises(
        "Error parsing numpy array: Expected left parenthesis '(' after shape key"
    ):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'shape':  2, 'descr': '<f4', 'fortran_order': False, }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises(
        "Error parsing numpy array: Expected an initial number in shape value"
    ):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'shape':  (, 'descr': '<f4', 'fortran_order': False, }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("Error parsing numpy array: Expected comma after first shape value"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'shape':  (2), 'descr': '<f4', 'fortran_order': False, }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises(
        "Error parsing numpy array: unexpected header EOF while parsing shape"
    ):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'shape':  (2,                                                                                             \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("Error parsing numpy array: unknown type in shape value"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'shape':  (2, 'nope'                                                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises(
        "Error parsing numpy array: expected right parenthesis after shape value"
    ):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'shape':  (2,4 (                                                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("Error parsing numpy array: unknown key in numpy header"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'no': '<f4', 'fortran_order': False, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("Error parsing numpy array: unknown extra token after value"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'descr': '<f4' 'asdf', 'fortran_order': False, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("numpy array error: Expected a data size of 32, found 31"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'descr': '<f4', 'fortran_order': False, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3"
        )

    # with _raises("XXX"):
    #    vec_npy_each(b"\x93NUMPY\x01\x00v\x00{'descr': '<f4', 'fortran_order': False, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@")


import tempfile


def test_vec_npy_each_errors_files():
    db = connect(EXT_PATH, extra_entrypoint="sqlite3_vec_numpy_init")

    def vec_npy_each(data):
        with tempfile.NamedTemporaryFile(delete_on_close=False) as f:
            f.write(data)
            f.close()
            try:
                return execute_all(
                    db, "select rowid, * from vec_npy_each(vec_npy_file(?))", [f.name]
                )
            finally:
                f.close()

    with _raises("Could not open numpy file"):
        db.execute('select * from vec_npy_each(vec_npy_file("not exist"))')

    with _raises("numpy array file too short"):
        vec_npy_each(b"\x93NUMPY\x01\x00v")

    with _raises("numpy array file does not contain the 'magic' header"):
        vec_npy_each(b"\x93XUMPY\x01\x00v\x00")

    with _raises("numpy array file header length is invalid"):
        vec_npy_each(b"\x93NUMPY\x01\x00v\x00")

    with _raises(
        "Error parsing numpy array: Only fortran_order = False is supported in sqlite-vec numpy functions"
    ):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'descr': '<f4', 'fortran_order': True, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3@"
        )

    with _raises("numpy array file error: Expected a data size of 32, found 31"):
        vec_npy_each(
            b"\x93NUMPY\x01\x00v\x00{'descr': '<f4', 'fortran_order': False, 'shape': (2, 4), }                                                          \n\xcd\xcc\x8c?\xcd\xcc\x0c@33S@\xcd\xcc\x8c@ff\x1eA\xcd\xcc\x0cAff\xf6@33\xd3"
        )

    assert vec_npy_each(to_npy(np.array([1.1, 2.2, 3.3], dtype=np.float32))) == [
        {
            "rowid": 0,
            "vector": _f32([1.1, 2.2, 3.3]),
        },
    ]
    assert vec_npy_each(
        to_npy(np.array([[1.1, 2.2, 3.3], [4.4, 5.5, 6.6]], dtype=np.float32))
    ) == [
        {
            "rowid": 0,
            "vector": _f32([1.1, 2.2, 3.3]),
        },
        {
            "rowid": 1,
            "vector": _f32([4.4, 5.5, 6.6]),
        },
    ]
    assert vec_npy_each(to_npy(np.array([], dtype=np.float32))) == []
    x1025 = vec_npy_each(to_npy(np.array([[0.1, 0.2, 0.3]] * 1025, dtype=np.float32)))
    assert len(x1025) == 1025

    # np.array([[.1, .2, 3]] * 99, dtype=np.float32).shape


def test_vec0_constructor():
    vec_constructor_error_prefix = "vec0 constructor error: {}"
    vec_col_error_prefix = "vec0 constructor error: could not parse vector column '{}'"
    with _raises(
        vec_col_error_prefix.format("aaa float[0]"),
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table v using vec0(aaa float[0])")

    with _raises(
        vec_col_error_prefix.format("aaa float[-1]"),
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table v using vec0(aaa float[-1])")

    with _raises(
        "vec0 constructor error: More than one primary key definition was provided, vec0 only suports a single primary key column",
        sqlite3.DatabaseError,
    ):
        db.execute(
            "create virtual table v using vec0(aaa float[1], a int primary key, b int primary key)"
        )

    with _raises(
        "vec0 constructor error: Too many provided vector columns, maximum 16",
        sqlite3.DatabaseError,
    ):
        db.execute(
            "create virtual table v using vec0( a1 float[1], a2 float[1], a3 float[1], a4 float[1], a5 float[1], a6 float[1], a7 float[1], a8 float[1], a9 float[1], a10 float[1], a11 float[1], a12 float[1], a13 float[1], a14 float[1], a15 float[1], a16 float[1], a17 float[1])"
        )

    with _raises(
        "vec0 constructor error: At least one vector column is required",
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table v using vec0( )")

    with _raises(
        "vec0 constructor error: could not declare virtual table, 'duplicate column name: a'",
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table v using vec0(a float[1], a float[1] )")

    # EVIDENCE-OF: V27642_11712 vec0 table option key validate
    with _raises(
        "Unknown table option: chunk_sizex",
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table v using vec0(chunk_sizex=8)")

    # EVIDENCE-OF: V01931_18769 vec0 chunk_size option positive
    with _raises(
        "vec0 constructor error: chunk_size must be a non-zero positive integer",
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table v using vec0(chunk_size=0)")

    # EVIDENCE-OF: V14110_30948 vec0 chunk_size divisble by 8
    with _raises(
        "vec0 constructor error: chunk_size must be divisible by 8",
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table v using vec0(chunk_size=7)")

    table_option_errors = ["chunk_size=", "chunk_size=8 x"]

    for x in table_option_errors:
        with _raises(
            f"vec0 constructor error: could not parse table option '{x}'",
            sqlite3.DatabaseError,
        ):
            db.execute(f"create virtual table v using vec0({x})")

    with _raises(
        "vec0 constructor error: Could not parse '4'",
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table v using vec0(4)")


def test_vec0_create_errors():
    # EVIDENCE-OF: V17740_01811 vec0 create _chunks error handling
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_CREATE_TABLE, "t1_chunks"))
    with _raises(
        "Could not create '_chunks' shadow table: not authorized",
    ):
        db.execute("create virtual table t1 using vec0(a float[1])")
    db.set_authorizer(None)

    # EVIDENCE-OF: V11631_28470 vec0 create _rowids error handling
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_CREATE_TABLE, "t1_rowids"))
    with _raises(
        "Could not create '_rowids' shadow table: not authorized",
    ):
        db.execute("create virtual table t1 using vec0(a float[1])")
    db.set_authorizer(None)

    # EVIDENCE-OF: V25919_09989 vec0 create _vectorchunks error handling
    db.set_authorizer(
        authorizer_deny_on(sqlite3.SQLITE_CREATE_TABLE, "t1_vector_chunks00")
    )
    with _raises(
        "Could not create '_vector_chunks00' shadow table: not authorized",
    ):
        db.execute("create virtual table t1 using vec0(a float[1])")
    db.set_authorizer(None)

    # EVIDENCE-OF: V21406_05476 vec0 init raises error on 'latest chunk' init error
    db.execute("BEGIN")
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_READ, "t1_chunks", ""))
    with _raises(
        "Internal sqlite-vec error: could not initialize 'latest chunk' statement",
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table t1 using vec0(a float[1])")
        db.execute("insert into t1(a) values (X'AABBCCDD')")
    db.set_authorizer(None)
    db.rollback()

    db.execute("BEGIN")
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_INSERT, "t1_rowids"))
    with _raises(
        "Internal sqlite-vec error: could not initialize 'insert rowids id' statement",
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table t1 using vec0(a float[1])")
        db.execute("insert into t1(a) values (X'AABBCCDD')")
    db.set_authorizer(None)
    db.rollback()

    db.commit()
    db.execute("BEGIN")
    db.set_authorizer(
        authorizer_deny_on(sqlite3.SQLITE_UPDATE, "t1_rowids", "chunk_id")
    )
    with _raises(
        "Internal sqlite-vec error: could not initialize 'update rowids position' statement",
        sqlite3.DatabaseError,
    ):
        db.execute("create virtual table t1 using vec0(a float[1])")
        db.execute("insert into t1(a) values (X'AABBCCDD')")
    db.set_authorizer(None)
    db.rollback()

    # TODO wut
    # db.commit()
    # db.execute("BEGIN")
    # db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_UPDATE, "t1_rowids", "id"))
    # with _raises(
    #    "Internal sqlite-vec error: could not initialize 'rowids get chunk position' statement", sqlite3.DatabaseError
    # ):
    #    db.execute("create virtual table t1 using vec0(a float[1])")
    #    db.execute("insert into t1(a) values (X'AABBCCDD')")
    # db.set_authorizer(None)
    # db.rollback()


def test_vec0_knn():
    db = connect(EXT_PATH)
    db.execute(
        """
          create virtual table v using vec0(
            aaa float[8],
            bbb int8[8],
            ccc bit[8],
            chunk_size=8
          );
        """
    )

    with _raises(
        'Query vector on the "aaa" column is invalid: Input must have type BLOB (compact format) or TEXT (JSON), found NULL'
    ):
        db.execute("select * from v where aaa match NULL and k = 10")

    with _raises(
        'Query vector for the "aaa" column is expected to be of type float32, but a bit vector was provided.'
    ):
        db.execute("select * from v where aaa match vec_bit(X'AA') and k = 10")

    with _raises(
        'Dimension mismatch for query vector for the "aaa" column. Expected 8 dimensions but received 1.'
    ):
        db.execute("select * from v where aaa match vec_f32('[.1]') and k = 10")

    qaaa = json.dumps([0.01, 0.01, 0.01, 0.01, 0.01, 0.01, 0.01, 0.01])
    with _raises("k value in knn queries must be greater than or equal to 0."):
        db.execute("select * from v where aaa match vec_f32(?) and k = -1", [qaaa])

    assert (
        execute_all(db, "select * from v where aaa match vec_f32(?) and k = 0", [qaaa])
        == []
    )

    # EVIDENCE-OF: V06942_23781
    db.set_authorizer(authorizer_deny_on(sqlite3.SQLITE_READ, "v_chunks", "chunk_id"))
    with _raises(
        "Error preparing stmtChunk: access to v_chunks.chunk_id is prohibited",
        sqlite3.DatabaseError,
    ):
        db.execute("select * from v where aaa match vec_f32(?) and k = 5", [qaaa])
    db.set_authorizer(None)

    assert (
        execute_all(db, "select * from v where aaa match vec_f32(?) and k = 5", [qaaa])
        == []
    )

    db.executemany(
        """
               INSERT INTO v VALUES
                (:id, :vector, vec_quantize_int8(:vector, 'unit') ,vec_quantize_binary(:vector));
        """,
        [
            {
                "id": i,
                "vector": json.dumps(
                    [
                        i * 0.01,
                        i * 0.01,
                        i * 0.01,
                        i * 0.01,
                        i * 0.01,
                        i * 0.01,
                        i * 0.01,
                        i * 0.01,
                    ]
                ),
            }
            for i in range(24)
        ],
    )

    assert execute_all(
        db, "select rowid from v where aaa match vec_f32(?) and k = 9", [qaaa]
    ) == [
        {"rowid": 1},
        {"rowid": 2},  # ordering of 2 and 0 here depends on if min_idx uses < or <=
        {"rowid": 0},  #
        {"rowid": 3},
        {"rowid": 4},
        {"rowid": 5},
        {"rowid": 6},
        {"rowid": 7},
        {"rowid": 8},
    ]
    # TODO separate test, DELETE FROM WHERE rowid in (...) is fullscan that calls vec0Rowid. try on text PKs
    db.execute("delete from v where rowid in (1, 0, 8, 9)")
    assert execute_all(
        db, "select rowid from v where aaa match vec_f32(?) and k = 9", [qaaa]
    ) == [
        {"rowid": 2},
        {"rowid": 3},
        {"rowid": 4},
        {"rowid": 5},
        {"rowid": 6},
        {"rowid": 7},
        {"rowid": 10},
        {"rowid": 11},
        {"rowid": 12},
    ]

    # EVIDENCE-OF: V05271_22109 vec0 knn validates chunk size
    db.commit()
    db.execute("BEGIN")
    db.execute("update v_chunks set validity = zeroblob(100)")
    with _raises("chunk validity size doesn't match - expected 1, found 100"):
        db.execute("select * from v where aaa match ? and k = 2", [qaaa])
    db.rollback()

    # EVIDENCE-OF: V02796_19635 vec0 knn validates rowids size
    db.commit()
    db.execute("BEGIN")
    db.execute("update v_chunks set rowids = zeroblob(100)")
    with _raises("chunk rowids size doesn't match - expected 64, found 100"):
        db.execute("select * from v where aaa match ? and k = 2", [qaaa])
    db.rollback()

    # EVIDENCE-OF: V16465_00535 vec0 knn validates vector chunk size
    db.commit()
    db.execute("BEGIN")
    db.execute("update v_vector_chunks00 set vectors = zeroblob(100)")
    with _raises("vectors blob size doesn't match - expected 256, found 100"):
        db.execute("select * from v where aaa match ? and k = 2", [qaaa])
    db.rollback()


import numpy.typing as npt


def np_distance_l2(
    vec: npt.NDArray[np.float32], mat: npt.NDArray[np.float32]
) -> npt.NDArray[np.float32]:
    return np.sqrt(np.sum((mat - vec) ** 2, axis=1))


def np_topk(
    vec: npt.NDArray[np.float32],
    mat: npt.NDArray[np.float32],
    k: int = 5,
) -> tuple[npt.NDArray[np.int32], npt.NDArray[np.float32]]:
    distances = np_distance_l2(vec, mat)
    # Rather than sorting all similarities and taking the top K, it's faster to
    # argpartition and then just sort the top K.
    # The difference is O(N logN) vs O(N + k logk)
    indices = np.argpartition(distances, kth=k)[:k]
    top_indices = indices[np.argsort(distances[indices])]
    return top_indices, distances[top_indices]


# import faiss
@pytest.mark.skip(reason="TODO")
def test_correctness_npy():
    db = connect(EXT_PATH)
    np.random.seed(420 + 1 + 2)
    mat = np.random.uniform(low=-1.0, high=1.0, size=(10000, 24)).astype(np.float32)
    queries = np.random.uniform(low=-1.0, high=1.0, size=(1000, 24)).astype(np.float32)

    # sqlite-vec with vec0
    db.execute("create virtual table v using vec0(a float[24], chunk_size=8)")
    for v in mat:
        db.execute("insert into v(a) values (?)", [v])

    # sqlite-vec with scalar functions
    db.execute("create table t(a float[24])")
    for v in mat:
        db.execute("insert into t(a) values (?)", [v])

    faiss_index = faiss.IndexFlatL2(24)
    faiss_index.add(mat)

    k = 10000 - 1
    for idx, q in enumerate(queries):
        print(idx)
        result = execute_all(
            db,
            "select rowid - 1 as idx, distance from v where a match ? and k = ?",
            [q, k],
        )
        vec_vtab_rowids = [row["idx"] for row in result]
        vec_vtab_distances = [row["distance"] for row in result]

        result = execute_all(
            db,
            "select rowid - 1 as idx, vec_distance_l2(a, ?) as distance from t order by 2 limit ?",
            [q, k],
        )
        vec_scalar_rowids = [row["idx"] for row in result]
        vec_scalar_distances = [row["distance"] for row in result]
        assert vec_scalar_rowids == vec_vtab_rowids
        assert vec_scalar_distances == vec_vtab_distances

        faiss_distances, faiss_rowids = faiss_index.search(np.array([q]), k)
        faiss_distances = np.sqrt(faiss_distances)
        assert faiss_rowids[0].tolist() == vec_scalar_rowids
        assert faiss_distances[0].tolist() == vec_scalar_distances

        assert faiss_distances[0].tolist() == vec_vtab_distances
        assert faiss_rowids[0].tolist() == vec_vtab_rowids

        np_rowids, np_distances = np_topk(mat, q, k=k)
        # assert vec_vtab_rowids == np_rowids.tolist()
        # assert vec_vtab_distances == np_distances.tolist()


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
            "name": "vec_xyz_info",
        },
        {
            "name": "vec_xyz_rowids",
        },
        {
            "name": "vec_xyz_vector_chunks00",
        },
    ]
    chunk = db.execute("select * from vec_xyz_chunks").fetchone()
    # as of TODO, no initial row is inside the chunks table
    assert chunk is None
    # assert chunk["chunk_id"] == 1
    # assert chunk["validity"] == bytearray(int(1024 / 8))
    # assert chunk["rowids"] == bytearray(int(1024 * 8))
    # vchunk = db.execute("select * from vec_xyz_vector_chunks00").fetchone()
    # assert vchunk["rowid"] == 1
    # assert vchunk["vectors"] == bytearray(int(1024 * 4 * 2))

    assert re.match(
        "SCAN (TABLE )?vec_xyz VIRTUAL TABLE INDEX 0:3{___}___",
        explain_query_plan(
            "select * from vec_xyz where a match X'' and k = 10 order by distance"
        ),
    )
    if SUPPORTS_VTAB_LIMIT:
        assert re.match(
            "SCAN (TABLE )?vec_xyz VIRTUAL TABLE INDEX 0:3{___}___",
            explain_query_plan(
                "select * from vec_xyz where a match X'' order by distance limit 10"
            ),
        )
    assert re.match(
        "SCAN (TABLE )?vec_xyz VIRTUAL TABLE INDEX 0:1",
        explain_query_plan("select * from vec_xyz"),
    )
    assert re.match(
        "SCAN (TABLE )?vec_xyz VIRTUAL TABLE INDEX 3:2",
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
    assert (
        chunk["rowids"]
        == b"\x01\x00\x00\x00\x00\x00\x00\x00"
        + b"\x02\x00\x00\x00\x00\x00\x00\x00"
        + bytearray(int(1024 * 8) - 8 * 2)
    )
    assert chunk["chunk_id"] == 1
    assert chunk["validity"] == b"\x03" + bytearray(int(1024 / 8) - 1)
    vchunk = db.execute("select * from vec_xyz_vector_chunks00").fetchone()
    assert vchunk["rowid"] == 1
    assert (
        vchunk["vectors"]
        == b"\x00\x00\x00\x00\x00\x00\x80\x3f"
        + b"\x00\x00\x00\x00\x00\x00\x00\x40"
        + bytearray(int(1024 * 4 * 2) - (2 * 4 * 2))
    )

    db.execute("insert into vec_xyz(rowid, a) select 3, X'00000000000080bf'")
    chunk = db.execute("select * from vec_xyz_chunks").fetchone()
    assert chunk["chunk_id"] == 1
    assert chunk["validity"] == b"\x07" + bytearray(int(1024 / 8) - 1)
    assert (
        chunk["rowids"]
        == b"\x01\x00\x00\x00\x00\x00\x00\x00"
        + b"\x02\x00\x00\x00\x00\x00\x00\x00"
        + b"\x03\x00\x00\x00\x00\x00\x00\x00"
        + bytearray(int(1024 * 8) - 8 * 3)
    )
    vchunk = db.execute("select * from vec_xyz_vector_chunks00").fetchone()
    assert vchunk["rowid"] == 1
    assert (
        vchunk["vectors"]
        == b"\x00\x00\x00\x00\x00\x00\x80\x3f"
        + b"\x00\x00\x00\x00\x00\x00\x00\x40"
        + b"\x00\x00\x00\x00\x00\x00\x80\xbf"
        + bytearray(int(1024 * 4 * 2) - (2 * 4 * 3))
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
    assert execute_all(
        db,
        """
              select rowid, a, distance
              from vec_small
              where a match ?
                and k = 9
              order by distance
            """,
        [_f32([50.0] * 8)],
    ) == [
        {
            "a": _f32([500 * 0.1] * 8),
            "distance": 0.0,
            "rowid": 500,
        },
        {
            "a": _f32([501 * 0.1] * 8),
            "distance": 0.2828384041786194,
            "rowid": 501,
        },
        {
            "a": _f32([499 * 0.1] * 8),
            "distance": 0.2828384041786194,
            "rowid": 499,
        },
        {
            "a": _f32([502 * 0.1] * 8),
            "distance": 0.5656875967979431,
            "rowid": 502,
        },
        {
            "a": _f32([498 * 0.1] * 8),
            "distance": 0.5656875967979431,
            "rowid": 498,
        },
        {
            "a": _f32([503 * 0.1] * 8),
            "distance": 0.8485260009765625,
            "rowid": 503,
        },
        {
            "a": _f32([497 * 0.1] * 8),
            "distance": 0.8485260009765625,
            "rowid": 497,
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


def test_vec0_distance_metric():
    base = "('[1, 2]'), ('[3, 4]'), ('[5, 6]')"
    q = "[-1, -2]"

    db = connect(EXT_PATH)
    db.execute("create virtual table v1 using vec0( a float[2])")
    db.execute(f"insert into v1(a) values {base}")

    db.execute("create virtual table v2 using vec0( a float[2] distance_metric=l2)")
    db.execute(f"insert into v2(a) values {base}")

    db.execute("create virtual table v3 using vec0( a float[2] distance_metric=l1)")
    db.execute(f"insert into v3(a) values {base}")

    db.execute("create virtual table v4 using vec0( a float[2] distance_metric=cosine)")
    db.execute(f"insert into v4(a) values {base}")

    # default (L2)
    assert execute_all(
        db, "select rowid, distance from v1 where a match ? and k = 3", [q]
    ) == [
        {"rowid": 1, "distance": 4.4721360206604},
        {"rowid": 2, "distance": 7.211102485656738},
        {"rowid": 3, "distance": 10.0},
    ]

    # l2
    assert execute_all(
        db, "select rowid, distance from v2 where a match ? and k = 3", [q]
    ) == [
        {"rowid": 1, "distance": 4.4721360206604},
        {"rowid": 2, "distance": 7.211102485656738},
        {"rowid": 3, "distance": 10.0},
    ]
    # l1
    assert execute_all(
        db, "select rowid, distance from v3 where a match ? and k = 3", [q]
    ) == [
        {"rowid": 1, "distance": 6},
        {"rowid": 2, "distance": 10},
        {"rowid": 3, "distance": 14},
    ]
    # consine
    assert execute_all(
        db, "select rowid, distance from v4 where a match ? and k = 3", [q]
    ) == [
        {"rowid": 3, "distance": 1.9734171628952026},
        {"rowid": 2, "distance": 1.9838699102401733},
        {"rowid": 1, "distance": 2},
    ]


def test_vec0_vacuum():
    db = connect(EXT_PATH)
    db.execute("create virtual table vec_t using vec0(a float[1]);")
    db.execute("begin")
    db.execute("insert into vec_t(a) values (X'AABBCCDD')")
    db.commit()
    db.execute("vacuum")


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
