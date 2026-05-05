# TODO: `ann` base branch + consolidated benchmarks

## 1. Create `ann` branch with shared code

### 1.1 Branch setup
- [x] `git checkout -B ann origin/main`
- [x] Cherry-pick `624f998` (vec0_distance_full shared distance dispatch)
- [x] Cherry-pick stdint.h fix for test header
- [ ] Pull NEON cosine optimization from ivf-yolo3 into shared code
  - Currently only in ivf branch but is general-purpose (benefits all distance calcs)
  - Lives in `distance_cosine_float()` — ~57 lines of ARM NEON vectorized cosine

### 1.2 Benchmark infrastructure (`benchmarks-ann/`)
- [x] Seed data pipeline (`seed/Makefile`, `seed/build_base_db.py`)
- [x] Ground truth generator (`ground_truth.py`)
- [x] Results schema (`schema.sql`)
- [x] Benchmark runner with `INDEX_REGISTRY` extension point (`bench.py`)
  - Baseline configs (float, int8-rescore, bit-rescore) implemented
  - Index branches register their types via `INDEX_REGISTRY` dict
- [x] Makefile with baseline targets
- [x] README

### 1.3 Rebase feature branches onto `ann`
- [x] Rebase `diskann-yolo2` onto `ann` (1 commit: DiskANN implementation)
- [x] Rebase `ivf-yolo3` onto `ann` (1 commit: IVF implementation)
- [x] Rebase `annoy-yolo2` onto `ann` (2 commits: Annoy implementation + schema fix)
- [x] Verify each branch has only its index-specific commits remaining
- [ ] Force-push all 4 branches to origin

---

## 2. Per-branch: register index type in benchmarks

Each index branch should add to `benchmarks-ann/` when rebased onto `ann`:

### 2.1 Register in `bench.py`

Add an `INDEX_REGISTRY` entry. Each entry provides:
- `defaults` — default param values
- `create_table_sql(params)` — CREATE VIRTUAL TABLE with INDEXED BY clause
- `insert_sql(params)` — custom insert SQL, or None for default
- `post_insert_hook(conn, params)` — training/building step, returns time
- `run_query(conn, params, query, k)` — custom query, or None for default MATCH
- `describe(params)` — one-line description for report output

### 2.2 Add configs to `Makefile`

Append index-specific config variables and targets. Example pattern:

```makefile
DISKANN_CONFIGS = \
    "diskann-R48-binary:type=diskann,R=48,L=128,quantizer=binary" \
    ...

ALL_CONFIGS += $(DISKANN_CONFIGS)

bench-diskann: seed
    $(BENCH) --subset-size 10000 -k 10 -o runs/diskann $(BASELINES) $(DISKANN_CONFIGS)
    ...
```

### 2.3 Migrate existing benchmark results/docs

- Move useful results docs (RESULTS.md, etc.) into `benchmarks-ann/results/`
- Delete redundant per-branch benchmark directories once consolidated infra is proven

---

## 3. Future improvements

- [ ] Reporting script (`report.py`) — query results.db, produce markdown comparison tables
- [ ] Profiling targets in Makefile (lift from ivf-yolo3's Instruments/perf wrappers)
- [ ] Pre-computed ground truth integration (use GT DB files instead of on-the-fly brute-force)
