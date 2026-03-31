# KNN Benchmarks for sqlite-vec

Benchmarking infrastructure for vec0 KNN configurations. Includes brute-force
baselines (float, int8, bit); index-specific branches add their own types
via the `INDEX_REGISTRY` in `bench.py`.

## Prerequisites

- Built `dist/vec0` extension (run `make` from repo root)
- Python 3.10+
- `uv` (for seed data prep): `pip install uv`

## Quick start

```bash
# 1. Download dataset and build seed DB (~3 GB download, ~5 min)
make seed

# 2. Run a quick smoke test (5k vectors, ~1 min)
make bench-smoke

# 3. Run full benchmark at 10k
make bench-10k
```

## Usage

### Direct invocation

```bash
python bench.py --subset-size 10000 \
  "brute-float:type=baseline,variant=float" \
  "brute-int8:type=baseline,variant=int8" \
  "brute-bit:type=baseline,variant=bit"
```

### Config format

`name:type=<index_type>,key=val,key=val`

| Index type | Keys | Branch |
|-----------|------|--------|
| `baseline` | `variant` (float/int8/bit), `oversample` | this branch |

Index branches register additional types in `INDEX_REGISTRY`. See the
docstring in `bench.py` for the extension API.

### Make targets

| Target | Description |
|--------|-------------|
| `make seed` | Download COHERE 1M dataset |
| `make ground-truth` | Pre-compute ground truth for 10k/50k/100k |
| `make bench-smoke` | Quick 5k baseline test |
| `make bench-10k` | All configs at 10k vectors |
| `make bench-50k` | All configs at 50k vectors |
| `make bench-100k` | All configs at 100k vectors |
| `make bench-all` | 10k + 50k + 100k |

## Adding an index type

In your index branch, add an entry to `INDEX_REGISTRY` in `bench.py` and
append your configs to `ALL_CONFIGS` in the `Makefile`. See the existing
`baseline` entry and the comments in both files for the pattern.

## Results

Results are stored in `runs/<dir>/results.db` using the schema in `schema.sql`.

```bash
sqlite3 runs/10k/results.db "
  SELECT config_name, recall, mean_ms, qps
  FROM bench_results
  ORDER BY recall DESC
"
```

## Dataset

[Zilliz COHERE Medium 1M](https://zilliz.com/learn/datasets-for-vector-database-benchmarks):
768 dimensions, cosine distance, 1M train vectors + 10k query vectors with precomputed neighbors.
