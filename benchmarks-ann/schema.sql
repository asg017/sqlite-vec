-- Canonical results schema for vec0 KNN benchmark comparisons.
-- The index_type column is a free-form TEXT field. Baseline configs use
-- "baseline"; index-specific branches add their own types (registered
-- via INDEX_REGISTRY in bench.py).

CREATE TABLE IF NOT EXISTS build_results (
  config_name  TEXT NOT NULL,
  index_type   TEXT NOT NULL,
  subset_size  INTEGER NOT NULL,
  db_path      TEXT NOT NULL,
  insert_time_s REAL NOT NULL,
  train_time_s REAL,            -- NULL when no training/build step is needed
  total_time_s REAL NOT NULL,
  rows         INTEGER NOT NULL,
  file_size_mb REAL NOT NULL,
  created_at   TEXT NOT NULL DEFAULT (datetime('now')),
  PRIMARY KEY (config_name, subset_size)
);

CREATE TABLE IF NOT EXISTS bench_results (
  config_name  TEXT NOT NULL,
  index_type   TEXT NOT NULL,
  subset_size  INTEGER NOT NULL,
  k            INTEGER NOT NULL,
  n            INTEGER NOT NULL,
  mean_ms      REAL NOT NULL,
  median_ms    REAL NOT NULL,
  p99_ms       REAL NOT NULL,
  total_ms     REAL NOT NULL,
  qps          REAL NOT NULL,
  recall       REAL NOT NULL,
  db_path      TEXT NOT NULL,
  created_at   TEXT NOT NULL DEFAULT (datetime('now')),
  PRIMARY KEY (config_name, subset_size, k)
);
