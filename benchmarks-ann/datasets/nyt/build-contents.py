# /// script
# requires-python = ">=3.12"
# dependencies = [
#     "duckdb",
# ]
# ///

import argparse
import os
import sqlite3
import duckdb


def main():
    parser = argparse.ArgumentParser(
        description="Load NYT headline CSVs into a SQLite contents database via DuckDB",
    )
    parser.add_argument(
        "--data-dir", "-d", default="data",
        help="Directory containing NYT CSV files (default: data)",
    )
    parser.add_argument(
        "--output", "-o", required=True,
        help="Path to the output SQLite database",
    )
    args = parser.parse_args()

    glob_pattern = os.path.join(args.data_dir, "new_york_times_stories_*.csv")

    con = duckdb.connect()
    rows = con.execute(
        f"""
        SELECT
            row_number() OVER () AS id,
            headline
        FROM read_csv('{glob_pattern}', auto_detect=true, union_by_name=true)
        WHERE headline IS NOT NULL AND headline != ''
        """
    ).fetchall()
    con.close()

    db = sqlite3.connect(args.output)
    db.execute("CREATE TABLE contents(id INTEGER PRIMARY KEY, headline TEXT)")
    db.executemany("INSERT INTO contents VALUES (?, ?)", rows)
    db.commit()
    db.close()

    print(f"Wrote {len(rows)} headlines to {args.output}")


if __name__ == "__main__":
    main()
