# /// script
# requires-python = ">=3.12"
# dependencies = [
#     "duckdb",
# ]
# ///

import argparse
import sqlite3
import duckdb


def main():
    parser = argparse.ArgumentParser(
        description="Load NYT headline CSVs into a SQLite contents database (most recent 1M, deduplicated)",
    )
    parser.add_argument(
        "--data-dir", "-d", default="../nyt/data",
        help="Directory containing NYT CSV files (default: ../nyt/data)",
    )
    parser.add_argument(
        "--limit", "-l", type=int, default=1_000_000,
        help="Maximum number of headlines to keep (default: 1000000)",
    )
    parser.add_argument(
        "--output", "-o", required=True,
        help="Path to the output SQLite database",
    )
    args = parser.parse_args()

    glob_pattern = f"{args.data_dir}/new_york_times_stories_*.csv"

    con = duckdb.connect()
    rows = con.execute(
        f"""
        WITH deduped AS (
            SELECT
                headline,
                max(pub_date) AS pub_date
            FROM read_csv('{glob_pattern}', auto_detect=true, union_by_name=true)
            WHERE headline IS NOT NULL AND trim(headline) != ''
            GROUP BY headline
        )
        SELECT
            row_number() OVER (ORDER BY pub_date DESC) AS id,
            headline
        FROM deduped
        ORDER BY pub_date DESC
        LIMIT {args.limit}
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
