# bench-delete: Recall degradation after random deletion

Measures how KNN recall changes after deleting a random percentage of rows
from different index types (flat, rescore, DiskANN).

## Quick start

```bash
# Ensure dataset exists
make -C ../datasets/cohere1m

# Ensure extension is built
make -C ../.. loadable

# Quick smoke test
make smoke

# Full benchmark at 10k vectors
make bench-10k
```

## Usage

```bash
python bench_delete.py --subset-size 10000 --delete-pct 10,25,50,75 \
  "flat:type=vec0-flat,variant=float" \
  "diskann-R72:type=diskann,R=72,L=128,quantizer=binary" \
  "rescore-bit:type=rescore,quantizer=bit,oversample=8"
```

## What it measures

For each config and delete percentage:

| Metric | Description |
|--------|-------------|
| **recall** | KNN recall@k after deletion (ground truth recomputed over surviving rows) |
| **delta** | Recall change vs 0% baseline |
| **query latency** | Mean/median query time after deletion |
| **db_size_mb** | DB file size before VACUUM |
| **vacuum_size_mb** | DB file size after VACUUM (space reclaimed) |
| **delete_time_s** | Wall time for the DELETE operations |

## How it works

1. Build index with N vectors (one copy per config)
2. Measure recall at k=10 (pre-delete baseline)
3. For each delete %:
   - Copy the master DB
   - Delete a random selection of rows (deterministic seed)
   - Measure recall (ground truth recomputed over surviving rows only)
   - VACUUM and measure size savings
4. Print comparison table

## Expected behavior

- **Flat index**: Recall should be 1.0 at all delete percentages (brute-force is always exact)
- **Rescore**: Recall should stay close to baseline (quantized scan + rescore is robust)
- **DiskANN**: Recall may degrade at high delete % due to graph fragmentation (dangling edges, broken connectivity)

## Results DB

Results are stored in `runs/<dataset>/<subset_size>/delete_results.db`:

```sql
SELECT config_name, delete_pct, recall, vacuum_size_mb
FROM delete_runs
ORDER BY config_name, delete_pct;
```
