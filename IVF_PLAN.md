# IVF Index for sqlite-vec

## Overview

IVF (Inverted File Index) is an approximate nearest neighbor index for
sqlite-vec's `vec0` virtual table. It partitions vectors into clusters via
k-means, then at query time only scans the nearest clusters instead of all
vectors. Combined with scalar or binary quantization, this gives 5-20x query
speedups over brute-force with tunable recall.

## SQL API

### Table Creation

```sql
CREATE VIRTUAL TABLE vec_items USING vec0(
  id INTEGER PRIMARY KEY,
  embedding float[768] distance_metric=cosine
    INDEXED BY ivf(nlist=128, nprobe=16)
);

-- With quantization (4x smaller cells, rescore for recall)
CREATE VIRTUAL TABLE vec_items USING vec0(
  id INTEGER PRIMARY KEY,
  embedding float[768] distance_metric=cosine
    INDEXED BY ivf(nlist=128, nprobe=16, quantizer=int8, oversample=4)
);
```

### Parameters

| Parameter | Values | Default | Description |
|-----------|--------|---------|-------------|
| `nlist` | 1-65536, or 0 | 128 | Number of k-means clusters. Rule of thumb: `sqrt(N)` |
| `nprobe` | 1-nlist | 10 | Clusters to search at query time. More = better recall, slower |
| `quantizer` | `none`, `int8`, `binary` | `none` | How vectors are stored in cells |
| `oversample` | >= 1 | 1 | Re-rank `oversample * k` candidates with full-precision distance |

### Inserting Vectors

```sql
-- Works immediately, even before training
INSERT INTO vec_items(id, embedding) VALUES (1, :vector);
```

Before centroids exist, vectors go to an "unassigned" partition and queries do
brute-force. After training, new inserts are assigned to the nearest centroid.

### Training (Computing Centroids)

```sql
-- Run built-in k-means on all vectors
INSERT INTO vec_items(id) VALUES ('compute-centroids');
```

This loads all vectors into memory, runs k-means++ with Lloyd's algorithm,
creates quantized centroids, and redistributes all vectors into cluster cells.
It's a blocking operation — run it once after bulk insert.

### Manual Centroid Import

```sql
-- Import externally-computed centroids
INSERT INTO vec_items(id, embedding) VALUES ('set-centroid:0', :centroid_0);
INSERT INTO vec_items(id, embedding) VALUES ('set-centroid:1', :centroid_1);

-- Assign vectors to imported centroids
INSERT INTO vec_items(id) VALUES ('assign-vectors');
```

### Runtime Parameter Tuning

```sql
-- Change nprobe without rebuilding the index
INSERT INTO vec_items(id) VALUES ('nprobe=32');
```

### KNN Queries

```sql
-- Same syntax as standard vec0
SELECT id, distance
FROM vec_items
WHERE embedding MATCH :query AND k = 10;
```

### Other Commands

```sql
-- Remove centroids, move all vectors back to unassigned
INSERT INTO vec_items(id) VALUES ('clear-centroids');
```

## How It Works

### Architecture

```
User vector (float32)
  → quantize to int8/binary (if quantizer != none)
  → find nearest centroid (quantized distance)
  → store quantized vector in cell blob
  → store full vector in KV table (if quantizer != none)
  → query:
      1. quantize query vector
      2. find top nprobe centroids by quantized distance
      3. scan cell blobs: quantized distance (fast, small I/O)
      4. if oversample > 1: re-score top N*k with full vectors
      5. return top k
```

### Shadow Tables

For a table `vec_items` with vector column index 0:

| Table | Schema | Purpose |
|-------|--------|---------|
| `vec_items_ivf_centroids00` | `centroid_id PK, centroid BLOB` | K-means centroids (quantized) |
| `vec_items_ivf_cells00` | `centroid_id, n_vectors, validity BLOB, rowids BLOB, vectors BLOB` | Packed vector cells, 64 vectors max per row. Multiple rows per centroid. Index on centroid_id. |
| `vec_items_ivf_rowid_map00` | `rowid PK, cell_id, slot` | Maps vector rowid → cell location for O(1) delete |
| `vec_items_ivf_vectors00` | `rowid PK, vector BLOB` | Full-precision vectors (only when quantizer != none) |

### Cell Storage

Cells use packed blob storage identical to vec0's chunk layout:
- **validity**: bitmap (1 bit per slot) marking live vectors
- **rowids**: packed i64 array
- **vectors**: packed array of quantized vectors

Cells are capped at 64 vectors (~200KB at 768-dim float32, ~48KB for int8,
~6KB for binary). When a cell fills, a new row is created for the same
centroid. This avoids SQLite overflow page traversal which was a 110x
performance bottleneck with unbounded cells.

### Quantization

**int8**: Each float32 dimension clamped to [-1,1] and scaled to int8
[-127,127]. 4x storage reduction. Distance computed via int8 L2.

**binary**: Sign-bit quantization — each bit is 1 if the float is positive.
32x storage reduction. Distance computed via hamming distance.

**Oversample re-ranking**: When `oversample > 1`, the quantized scan collects
`oversample * k` candidates, then looks up each candidate's full-precision
vector from the KV table and re-computes exact distance. This recovers nearly
all recall lost from quantization. At oversample=4 with int8, recall matches
non-quantized IVF exactly.

### K-Means

Uses Lloyd's algorithm with k-means++ initialization:
1. K-means++ picks initial centroids weighted by distance
2. Lloyd's iterations: assign vectors to nearest centroid, recompute centroids as cluster means
3. Empty cluster handling: reassign to farthest point
4. K-means runs in float32; centroids are quantized before storage

Training data: recommend 16× nlist vectors. At nlist=1000, that's 16k
vectors — k-means takes ~140s on 768-dim data.

## Performance

### 100k vectors (COHERE 768-dim cosine)

```
                          name  qry(ms)  recall
───────────────────────────────────────────────
          ivf(q=int8,os=4),p=8    5.3ms  0.934  ← 6x faster than flat
         ivf(q=int8,os=4),p=16    5.4ms  0.968
               ivf(q=none),p=8    5.3ms  0.934
      ivf(q=binary,os=10),p=16    1.3ms  0.832  ← 26x faster than flat
         ivf(q=int8,os=4),p=32    7.4ms  0.990
              ivf(q=none),p=32   15.5ms  0.992
                    int8(os=4)   18.7ms  0.996
                     bit(os=8)   18.7ms  0.884
                          flat   33.7ms  1.000
```

### 1M vectors (COHERE 768-dim cosine)

```
                            name  insert  train    MB  qry(ms)  recall
──────────────────────────────────────────────────────────────────────
            ivf(q=int8,os=4),p=8   163s   142s  4725   16.3ms  0.892
        ivf(q=binary,os=10),p=16   118s   144s  4073   17.7ms  0.830
           ivf(q=int8,os=4),p=16   163s   142s  4725   24.3ms  0.950
           ivf(q=int8,os=4),p=32   163s   142s  4725   41.6ms  0.980
                 ivf(q=none),p=8   497s   144s  3101   52.1ms  0.890
                 ivf(q=none),p=16  497s   144s  3101   56.6ms  0.950
                       bit(os=8)    18s      -  3048   83.5ms  0.918
                 ivf(q=none),p=32  497s   144s  3101  103.9ms  0.980
                      int8(os=4)    19s      -  3689  169.1ms  0.994
                            flat    20s      -  2955  338.0ms  1.000
```

**Best config at 1M: `ivf(quantizer=int8, oversample=4, nprobe=16)`** —
24ms query, 0.95 recall, 14x faster than flat, 7x faster than int8 baseline.

### Scaling Characteristics

| Metric | 100k | 1M | Scaling |
|--------|------|-----|---------|
| Flat query | 34ms | 338ms | 10x (linear) |
| IVF int8 p=16 | 5.4ms | 24.3ms | 4.5x (sublinear) |
| IVF insert rate | ~10k/s | ~6k/s | Slight degradation |
| Training (nlist=1000) | 13s | 142s | ~11x |

## Implementation

### File Structure

```
sqlite-vec-ivf-kmeans.c    K-means++ algorithm (pure C, no SQLite deps)
sqlite-vec-ivf.c           All IVF logic: parser, shadow tables, insert,
                           delete, query, centroid commands, quantization
sqlite-vec.c               ~50 lines of additions: struct fields, #includes,
                           dispatch hooks in parse/create/insert/delete/filter
```

Both IVF files are `#include`d into `sqlite-vec.c`. No Makefile changes needed.

### Key Design Decisions

1. **Fixed-size cells (64 vectors)** instead of one blob per centroid. Avoids
   SQLite overflow page traversal which caused 110x insert slowdown.

2. **Multiple cell rows per centroid** with an index on centroid_id. When a
   cell fills, a new row is created. Query scans all rows for probed centroids
   via `WHERE centroid_id IN (...)`.

3. **Always store full vectors** when quantizer != none (in `_ivf_vectors` KV
   table). Enables oversample re-ranking and point queries returning original
   precision.

4. **K-means in float32, quantize after**. Simpler than quantized k-means,
   and assignment accuracy doesn't suffer much since nprobe compensates.

5. **NEON SIMD for cosine distance**. Added `cosine_float_neon()` with 4-wide
   FMA for dot product + magnitudes. Benefits all vec0 queries, not just IVF.

6. **Runtime nprobe tuning**. `INSERT INTO t(id) VALUES ('nprobe=N')` changes
   the probe count without rebuilding — enables fast parameter sweeps.

### Optimization History

| Optimization | Impact |
|-------------|--------|
| Fixed-size cells (64 max) | 110x insert speedup |
| Skip chunk writes for IVF | 2x DB size reduction |
| NEON cosine distance | 2x query speedup + 13% recall improvement (correct metric) |
| Cached prepared statements | Eliminated per-insert prepare/finalize |
| Batched cell reads (IN clause) | Fewer SQLite queries per KNN |
| int8 quantization | 2.5x query speedup at same recall |
| Binary quantization | 32x less cell I/O |
| Oversample re-ranking | Recovers quantization recall loss |

## Remaining Work

See `ivf-benchmarks/TODO.md` for the full list. Key items:

- **Cache centroids in memory** — each insert re-reads all centroids from SQLite
- **Runtime oversample** — same pattern as nprobe runtime command
- **SIMD k-means** — training uses scalar distance, could be 4x faster
- **Top-k heap** — replace qsort with min-heap for large nprobe
- **IVF-PQ** — product quantization for better compression/recall tradeoff
