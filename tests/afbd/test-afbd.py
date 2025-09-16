import numpy as np
from tqdm import tqdm
from deepdiff import DeepDiff

import tarfile
import json
from io import BytesIO
import sqlite3
from typing import List
from struct import pack
import time
from pathlib import Path
import argparse


def serialize_float32(vector: List[float]) -> bytes:
    """Serializes a list of floats into the "raw bytes" format sqlite-vec expects"""
    return pack("%sf" % len(vector), *vector)


def build_command(file_path, metadata_set=None):
    if metadata_set:
        metadata_set = set(metadata_set.split(","))

    file_path = Path(file_path)
    print(f"reading {file_path}...")
    t0 = time.time()
    with tarfile.open(file_path, "r:gz") as archive:
        for file in archive:
            if file.name == "./payloads.jsonl":
                payloads = [
                    json.loads(line)
                    for line in archive.extractfile(file.name).readlines()
                ]
            if file.name == "./tests.jsonl":
                tests = [
                    json.loads(line)
                    for line in archive.extractfile(file.name).readlines()
                ]
            if file.name == "./vectors.npy":
                f = BytesIO()
                f.write(archive.extractfile(file.name).read())
                f.seek(0)
                vectors = np.load(f)

    assert payloads is not None
    assert tests is not None
    assert vectors is not None
    dimensions = vectors.shape[1]
    metadata_columns = sorted(list(payloads[0].keys()))

    def col_type(v):
        if isinstance(v, int):
            return "integer"
        if isinstance(v, float):
            return "float"
        if isinstance(v, str):
            return "text"
        raise Exception(f"Unknown column type: {v}")

    metadata_columns_types = [col_type(payloads[0][col]) for col in metadata_columns]

    print(time.time() - t0)
    t0 = time.time()
    print("seeding...")

    db = sqlite3.connect(f"{file_path.stem}.db")
    db.execute("PRAGMA page_size = 16384")
    db.row_factory = sqlite3.Row
    db.enable_load_extension(True)
    db.load_extension("../../dist/vec0")
    db.enable_load_extension(False)

    with db:
        db.execute("create table tests(data)")

        for test in tests:
            db.execute("insert into tests values (?)", [json.dumps(test)])

    with db:
        create_sql = f"create virtual table v using vec0(vector float[{dimensions}] distance_metric=cosine"
        insert_sql = "insert into v(rowid, vector"
        for name, type in zip(metadata_columns, metadata_columns_types):
            if metadata_set:
                if name in metadata_set:
                    create_sql += f", {name} {type}"
                else:
                    create_sql += f", +{name} {type}"
            else:
                create_sql += f", {name} {type}"

            insert_sql += f", {name}"
        create_sql += ")"
        insert_sql += ") values (" + ",".join("?" * (2 + len(metadata_columns))) + ")"
        print(create_sql)
        print(insert_sql)

        db.execute(create_sql)

        for idx, (payload, vector) in enumerate(
            tqdm(zip(payloads, vectors), total=len(payloads))
        ):
            params = [idx, vector]
            for c in metadata_columns:
                params.append(payload[c])
            db.execute(insert_sql, params)

    print(time.time() - t0)


def tests_command(file_path):
    file_path = Path(file_path)
    db = sqlite3.connect(f"{file_path.stem}.db")
    db.execute("PRAGMA cache_size = -100000000")
    db.row_factory = sqlite3.Row
    db.enable_load_extension(True)
    db.load_extension("../../dist/vec0")
    db.enable_load_extension(False)

    tests = [
        json.loads(row["data"])
        for row in db.execute("select data from tests").fetchall()
    ]

    num_or_skips = 0
    num_1off_errors = 0

    t0 = time.time()
    print("testing...")
    for idx, test in enumerate(tqdm(tests)):
        query = test["query"]
        conditions = test["conditions"]
        expected_closest_ids = test["closest_ids"]
        expected_closest_scores = test["closest_scores"]

        sql = "select rowid, 1 - distance as similarity from v where vector match ? and k = ?"
        params = [serialize_float32(query), len(expected_closest_ids)]

        if "and" in conditions:
            for condition in conditions["and"]:
                assert len(condition.keys()) == 1
                column = list(condition.keys())[0]
                assert len(list(condition[column].keys())) == 1
                condition_type = list(condition[column].keys())[0]
                if condition_type == "match":
                    value = condition[column]["match"]["value"]
                    sql += f" and {column} = ?"
                    params.append(value)
                elif condition_type == "range":
                    sql += f" and {column} between ? and ?"
                    params.append(condition[column]["range"]["gt"])
                    params.append(condition[column]["range"]["lt"])
                else:
                    raise Exception(f"Unknown condition type: {condition_type}")
        elif "or" in conditions:
            column = list(conditions["or"][0].keys())[0]
            condition_type = list(conditions["or"][0][column].keys())[0]
            assert condition_type == "match"
            sql += f" and {column} in ("
            for idx, condition in enumerate(conditions["or"]):
                if condition_type == "match":
                    value = condition[column]["match"]["value"]
                    if idx != 0:
                        sql += ","
                    sql += "?"
                    params.append(value)
                elif condition_type == "range":
                    breakpoint()
                else:
                    raise Exception(f"Unknown condition type: {condition_type}")
            sql += ")"

        # print(sql, params[1:])
        rows = db.execute(sql, params).fetchall()
        actual_closest_ids = [row["rowid"] for row in rows]
        matches = expected_closest_ids == actual_closest_ids
        if not matches:
            diff = DeepDiff(
                expected_closest_ids, actual_closest_ids, ignore_order=False
            )
            assert len(list(diff.keys())) == 1
            assert "values_changed" in diff.keys()
            keys_changed = list(diff["values_changed"].keys())
            if len(keys_changed) == 2:
                akey, bkey = keys_changed
                a = int(akey.lstrip("root[").rstrip("]"))
                b = int(bkey.lstrip("root[").rstrip("]"))
                assert abs(a - b) == 1
                assert (
                    diff["values_changed"][akey]["new_value"]
                    == diff["values_changed"][bkey]["old_value"]
                )
                assert (
                    diff["values_changed"][akey]["old_value"]
                    == diff["values_changed"][bkey]["new_value"]
                )
            elif len(keys_changed) == 1:
                v = int(keys_changed[0].lstrip("root[").rstrip("]"))
                assert (v + 1) == len(expected_closest_ids)
            else:
                raise Exception("fuck")
            num_1off_errors += 1
        # print(closest_scores)
        # print([row["similarity"] for row in rows])
        # assert closest_scores == [row["similarity"] for row in rows]
    print("Number skipped: ", num_or_skips)
    print("Num 1 off errors: ", num_1off_errors)
    print("1 off error rate: ", num_1off_errors / (len(tests) - num_or_skips))
    print(time.time() - t0)
    print("done")


def main():
    parser = argparse.ArgumentParser(description="CLI tool")
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_parser = subparsers.add_parser("build")
    build_parser.add_argument("file", type=str, help="Path to input file")
    build_parser.add_argument("--metadata", type=str, help="Metadata columns")
    build_parser.set_defaults(func=lambda args: build_command(args.file, args.metadata))

    tests_parser = subparsers.add_parser("test")
    tests_parser.add_argument("file", type=str, help="Path to input file")
    tests_parser.set_defaults(func=lambda args: tests_command(args.file))

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
