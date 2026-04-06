# KNN Benchmarks for sqlite-vec

Benchmarking infrastructure for vec0 KNN configurations. Includes brute-force
baselines (float, int8, bit), rescore, IVF, and DiskANN index types.

## Datasets

Each dataset is a subdirectory containing a `Makefile` and `build_base_db.py`
that produce a `base.db`. The benchmark runner auto-discovers any subdirectory
with a `base.db` file.

```
cohere1m/           # Cohere 768d cosine, 1M vectors
  Makefile          # downloads parquets from Zilliz, builds base.db
  build_base_db.py
  base.db           # (generated)

cohere10m/          # Cohere 768d cosine, 10M vectors (10 train shards)
  Makefile          # make -j12 download to fetch all shards in parallel
  build_base_db.py
  base.db           # (generated)
```

Every `base.db` has the same schema:

| Table | Columns | Description |
|-------|---------|-------------|
| `train` | `id INTEGER PRIMARY KEY, vector BLOB` | Indexed vectors (f32 blobs) |
| `query_vectors` | `id INTEGER PRIMARY KEY, vector BLOB` | Query vectors for KNN evaluation |
| `neighbors` | `query_vector_id INTEGER, rank INTEGER, neighbors_id TEXT` | Ground-truth nearest neighbors |

To add a new dataset, create a directory with a `Makefile` that builds `base.db`
with the tables above. It will be available via `--dataset <dirname>` automatically.

### Building datasets

```bash
# Cohere 1M
cd cohere1m && make download && make && cd ..

# Cohere 10M (parallel download recommended — 10 train shards + test + neighbors)
cd cohere10m && make -j12 download && make && cd ..
```

## Prerequisites

- Built `dist/vec0` extension (run `make loadable` from repo root)
- Python 3.10+
- `uv`

## Quick start

```bash
# 1. Build a dataset
cd cohere1m && make && cd ..

# 2. Quick smoke test (5k vectors)
make bench-smoke

# 3. Full benchmark at 10k
make bench-10k
```

## Usage

```bash
uv run python bench.py --subset-size 10000 -k 10 -n 50 --dataset cohere1m \
  "brute-float:type=baseline,variant=float" \
  "rescore-bit-os8:type=rescore,quantizer=bit,oversample=8"
```

### Config format

`name:type=<index_type>,key=val,key=val`

| Index type | Keys |
|-----------|------|
| `baseline` | `variant` (float/int8/bit), `oversample` |
| `rescore` | `quantizer` (bit/int8), `oversample` |
| `ivf` | `nlist`, `nprobe` |
| `diskann` | `R`, `L`, `quantizer` (binary/int8), `buffer_threshold` |

### Make targets

| Target | Description |
|--------|-------------|
| `make seed` | Download and build default dataset |
| `make bench-smoke` | Quick 5k test (3 configs) |
| `make bench-10k` | All configs at 10k vectors |
| `make bench-50k` | All configs at 50k vectors |
| `make bench-100k` | All configs at 100k vectors |
| `make bench-all` | 10k + 50k + 100k |
| `make bench-ivf` | Baselines + IVF across 10k/50k/100k |
| `make bench-diskann` | Baselines + DiskANN across 10k/50k/100k |

## Results DB

Each run writes to `runs/<dataset>/<subset_size>/results.db` (SQLite, WAL mode).
Progress is written continuously — query from another terminal to monitor:

```bash
sqlite3 runs/cohere1m/10000/results.db "SELECT run_id, config_name, status FROM runs"
```

See `results_schema.sql` for the full schema (tables: `runs`, `run_results`,
`insert_batches`, `queries`).

## Adding an index type

Add an entry to `INDEX_REGISTRY` in `bench.py` and append configs to
`ALL_CONFIGS` in the `Makefile`. See existing entries for the pattern.
