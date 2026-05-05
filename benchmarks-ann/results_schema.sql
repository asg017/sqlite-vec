-- Comprehensive results schema for vec0 KNN benchmark runs.
-- Created in WAL mode: PRAGMA journal_mode=WAL

CREATE TABLE IF NOT EXISTS runs (
  run_id          INTEGER PRIMARY KEY AUTOINCREMENT,
  config_name     TEXT    NOT NULL,
  index_type      TEXT    NOT NULL,
  params          TEXT    NOT NULL,   -- JSON: {"R":48,"L":128,"quantizer":"binary"}
  dataset         TEXT    NOT NULL,   -- "cohere1m"
  subset_size     INTEGER NOT NULL,
  k               INTEGER NOT NULL,
  n_queries       INTEGER NOT NULL,
  phase           TEXT    NOT NULL DEFAULT 'both',
    -- 'build', 'query', or 'both'
  status          TEXT    NOT NULL DEFAULT 'pending',
    -- pending → inserting → training → querying → done | built | error
  created_at_ns   INTEGER NOT NULL    -- time.time_ns()
);

CREATE TABLE IF NOT EXISTS run_results (
  run_id              INTEGER PRIMARY KEY REFERENCES runs(run_id),
  insert_started_ns   INTEGER,
  insert_ended_ns     INTEGER,
  insert_duration_ns  INTEGER,
  train_started_ns    INTEGER,       -- NULL if no training
  train_ended_ns      INTEGER,
  train_duration_ns   INTEGER,
  build_duration_ns   INTEGER,       -- insert + train
  db_file_size_bytes  INTEGER,
  db_file_path        TEXT,
  create_sql          TEXT,           -- CREATE VIRTUAL TABLE ...
  insert_sql          TEXT,           -- INSERT INTO vec_items ...
  train_sql           TEXT,           -- NULL if no training step
  query_sql           TEXT,           -- SELECT ... WHERE embedding MATCH ...
  k                   INTEGER,       -- denormalized from runs for easy filtering
  query_mean_ms       REAL,          -- denormalized aggregates
  query_median_ms     REAL,
  query_p99_ms        REAL,
  query_total_ms      REAL,
  qps                 REAL,
  recall              REAL
);

CREATE TABLE IF NOT EXISTS insert_batches (
  batch_id        INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id          INTEGER NOT NULL REFERENCES runs(run_id),
  batch_lo        INTEGER NOT NULL,   -- start index (inclusive)
  batch_hi        INTEGER NOT NULL,   -- end index (exclusive)
  rows_in_batch   INTEGER NOT NULL,
  started_ns      INTEGER NOT NULL,
  ended_ns        INTEGER NOT NULL,
  duration_ns     INTEGER NOT NULL,
  cumulative_rows INTEGER NOT NULL,   -- total rows inserted so far
  rate_rows_per_s REAL    NOT NULL    -- cumulative rate
);

CREATE TABLE IF NOT EXISTS queries (
  query_id          INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id            INTEGER NOT NULL REFERENCES runs(run_id),
  k                 INTEGER NOT NULL,
  query_vector_id   INTEGER NOT NULL,
  started_ns        INTEGER NOT NULL,
  ended_ns          INTEGER NOT NULL,
  duration_ms       REAL    NOT NULL,
  result_ids        TEXT    NOT NULL,  -- JSON array
  result_distances  TEXT    NOT NULL,  -- JSON array
  ground_truth_ids  TEXT    NOT NULL,  -- JSON array
  recall            REAL    NOT NULL,
  UNIQUE(run_id, k, query_vector_id)
);

CREATE INDEX IF NOT EXISTS idx_runs_config  ON runs(config_name);
CREATE INDEX IF NOT EXISTS idx_runs_type    ON runs(index_type);
CREATE INDEX IF NOT EXISTS idx_runs_status  ON runs(status);
CREATE INDEX IF NOT EXISTS idx_batches_run  ON insert_batches(run_id);
CREATE INDEX IF NOT EXISTS idx_queries_run  ON queries(run_id);
