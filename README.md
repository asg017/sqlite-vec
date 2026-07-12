# `sqlite-vec`

[![](https://dcbadge.vercel.app/api/server/VCtQ8cGhUs)](https://discord.gg/Ve7WeCJFXk)

An extremely small, "fast enough" vector search SQLite extension that runs
anywhere! A successor to [`sqlite-vss`](https://github.com/asg017/sqlite-vss)

<!-- deno-fmt-ignore-start -->

> [!IMPORTANT]
> _`sqlite-vec` is a pre-v1, so expect breaking changes!_

<!-- deno-fmt-ignore-end -->

- Store and query float, int8, and binary vectors in `vec0` virtual tables
- Written in pure C, no dependencies, runs anywhere SQLite runs
  (Linux/MacOS/Windows, in the browser with WASM, Raspberry Pis, etc.)
- Store non-vector data in metadata, auxiliary, or partition key columns

<p align="center">
  <a href="https://hacks.mozilla.org/2024/06/sponsoring-sqlite-vec-to-enable-more-powerful-local-ai-applications/">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./.github/logos/mozilla.dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="./.github/logos/mozilla.svg">
    <img alt="Mozilla Builders logo" width=400>
  </picture>
  </a>
</p>

<p align="center">
<i>
<code>sqlite-vec</code> is a
<a href="https://hacks.mozilla.org/2024/06/sponsoring-sqlite-vec-to-enable-more-powerful-local-ai-applications/">Mozilla Builders project</a>,
with additional sponsorship from
<a href="https://fly.io/"><img width=14px src="./.github/logos/flyio.small.ico"/> Fly.io </a>,
<a href="https://tur.so/sqlite-vec"><img width=14px src="./.github/logos/turso.small.ico"/> Turso</a>,
<a href="https://sqlitecloud.io/"><img width=14px src="./.github/logos/sqlitecloud.small.svg"/> SQLite Cloud</a>, and
<a href="https://shinkai.com/"><img width=14px src="./.github/logos/shinkai.small.svg"/> Shinkai</a>.
See <a href="#sponsors">the Sponsors section</a> for more details.
</i>
</p>

## Installing

See [Installing `sqlite-vec`](https://alexgarcia.xyz/sqlite-vec/installation.html)
for more details.

| Language       | Install                                              | More Info                                                                             |                                                                                                                                                                                                    |
| -------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Python         | `pip install sqlite-vec`                             | [`sqlite-vec` with Python](https://alexgarcia.xyz/sqlite-vec/python.html)             | [![PyPI](https://img.shields.io/pypi/v/sqlite-vec.svg?color=blue&logo=python&logoColor=white)](https://pypi.org/project/sqlite-vec/)                                                               |
| Node.js        | `npm install sqlite-vec`                             | [`sqlite-vec` with Node.js](https://alexgarcia.xyz/sqlite-vec/js.html)            | [![npm](https://img.shields.io/npm/v/sqlite-vec.svg?color=green&logo=nodedotjs&logoColor=white)](https://www.npmjs.com/package/sqlite-vec)                                                         |
| Ruby           | `gem install sqlite-vec`                             | [`sqlite-vec` with Ruby](https://alexgarcia.xyz/sqlite-vec/ruby.html)                 | ![Gem](https://img.shields.io/gem/v/sqlite-vec?color=red&logo=rubygems&logoColor=white)                                                                       |
| Go             | `go get -u github.com/asg017/sqlite-vec/bindings/go` | [`sqlite-vec` with Go](https://alexgarcia.xyz/sqlite-vec/go.html)                     | [![Go Reference](https://pkg.go.dev/badge/github.com/asg017/sqlite-vec-go-bindings/cgo.svg)](https://pkg.go.dev/github.com/asg017/asg017/sqlite-vec-go-bindings/cgo)                                              |
| Rust           | `cargo add sqlite-vec`                               | [`sqlite-vec` with Rust](https://alexgarcia.xyz/sqlite-vec/rust.html)                 | [![Crates.io](https://img.shields.io/crates/v/sqlite-vec?logo=rust)](https://crates.io/crates/sqlite-vec)                                                                                          |
| Datasette      | `datasette install datasette-sqlite-vec`             | [`sqlite-vec` with Datasette](https://alexgarcia.xyz/sqlite-vec/datasette.html)       | [![Datasette](https://img.shields.io/pypi/v/datasette-sqlite-vec.svg?color=B6B6D9&label=Datasette+plugin&logoColor=white&logo=python)](https://datasette.io/plugins/datasette-sqlite-vec)          |
| rqlite         | `rqlited -extensions-path=sqlite-vec.tar.gz`         | [`sqlite-vec` with rqlite](https://alexgarcia.xyz/sqlite-vec/rqlite.html)                        | [![rqlite](https://img.shields.io/badge/rqlite-sqlite_extensions-blue)](https://rqlite.io/docs/guides/extensions/)           |
| `sqlite-utils` | `sqlite-utils install sqlite-utils-sqlite-vec`       | [`sqlite-vec` with sqlite-utils](https://alexgarcia.xyz/sqlite-vec/sqlite-utils.html) | [![sqlite-utils](https://img.shields.io/pypi/v/sqlite-utils-sqlite-vec.svg?color=B6B6D9&label=sqlite-utils+plugin&logoColor=white&logo=python)](https://datasette.io/plugins/datasette-sqlite-vec) |
| Github Release |                                                      |                                                                                       | ![GitHub tag (latest SemVer pre-release)](https://img.shields.io/github/v/tag/asg017/sqlite-vec?color=lightgrey&include_prereleases&label=Github+release&logo=github)                              |


## Sample usage

```sql
.load ./vec0

create virtual table vec_examples using vec0(
  sample_embedding float[8]
);

-- vectors can be provided as JSON or in a compact binary format
insert into vec_examples(rowid, sample_embedding)
  values
    (1, '[-0.200, 0.250, 0.341, -0.211, 0.645, 0.935, -0.316, -0.924]'),
    (2, '[0.443, -0.501, 0.355, -0.771, 0.707, -0.708, -0.185, 0.362]'),
    (3, '[0.716, -0.927, 0.134, 0.052, -0.669, 0.793, -0.634, -0.162]'),
    (4, '[-0.710, 0.330, 0.656, 0.041, -0.990, 0.726, 0.385, -0.958]');


-- KNN style query
select
  rowid,
  distance
from vec_examples
where sample_embedding match '[0.890, 0.544, 0.825, 0.961, 0.358, 0.0196, 0.521, 0.175]'
order by distance
limit 2;
/*
┌───────┬──────────────────┐
│ rowid │     distance     │
├───────┼──────────────────┤
│ 2     │ 2.38687372207642 │
│ 1     │ 2.38978505134583 │
└───────┴──────────────────┘
*/
```

## Approximate nearest neighbor (ANN) indexes

By default a `vec0` table performs an **exact** KNN search — a brute-force
sequential scan that compares the query against every stored vector. This is
simple and always returns the true nearest neighbors, but its cost grows
linearly with both the number of rows and the vector dimensionality.

For larger collections you can attach an **approximate** index to a vector
column with an `indexed by` clause. Approximate indexes trade a small amount of
recall (they may miss some true neighbors) for substantially faster queries.
Three index types are available; each is enabled at compile time:

| Index | Compile flag | Idea | Best for |
| --- | --- | --- | --- |
| `rescore` | `-DSQLITE_VEC_ENABLE_RESCORE` | Scan a compact quantized copy, then re-rank the top candidates with full-precision vectors | Keeping near-exact recall while shrinking the scan |
| `ivf` | `-DSQLITE_VEC_EXPERIMENTAL_IVF_ENABLE=1` | Cluster vectors with k-means into `nlist` cells; at query time only scan the `nprobe` nearest cells | Large collections where you can afford a training step |
| `diskann` | `-DSQLITE_VEC_ENABLE_DISKANN=1` | Build a navigable graph and greedily walk it toward the query | Very large collections (millions of vectors) |

```sql
-- rescore: bit-quantized coarse scan, re-ranked with float32
create virtual table vec_rescore using vec0(
  embedding float[1024] indexed by rescore(quantizer=bit, oversample=8)
);

-- IVF: 128 k-means cells, probe the 16 nearest at query time
create virtual table vec_ivf using vec0(
  embedding float[1024] indexed by ivf(nlist=128, nprobe=16)
);
-- IVF requires a one-time training step once rows are inserted:
insert into vec_ivf(vec_ivf) values ('compute-centroids');

-- DiskANN: graph index with int8-quantized neighbor vectors
create virtual table vec_diskann using vec0(
  embedding float[1024] indexed by diskann(neighbor_quantizer=int8)
);
```

KNN queries use the same `match ... order by distance limit k` syntax regardless
of the index. IVF exposes a few runtime commands via its shadow (`compute-centroids`
to (re)train, `nprobe=N` to change the probe count without rebuilding).

### Performance

The table below benchmarks each index against the default sequential scan on
20,000 synthetic clustered vectors, measuring per-query latency, speedup, and
recall@10 (the fraction of the true 10 nearest neighbors returned). Numbers are
shown for both 256-dimensional and 1024-dimensional vectors.

| Index (256-dim) | ms/query | speedup | recall@10 |
| --- | --- | --- | --- |
| flat (sequential scan) | 1.69 | 1.0x | 1.000 |
| ivf nprobe=8 | 0.29 | 5.8x | 0.444 |
| ivf nprobe=20 | 0.69 | 2.4x | 0.664 |
| ivf int8, oversample=4 | 0.85 | 2.0x | 0.657 |
| rescore bit, oversample=8 | 0.98 | 1.7x | 0.282 |
| rescore int8, oversample=4 | 3.26 | 0.5x | 1.000 |
| diskann int8 | 2.41 | 0.7x | 0.698 |

| Index (1024-dim) | ms/query | speedup | recall@10 |
| --- | --- | --- | --- |
| flat (sequential scan) | 7.32 | 1.0x | 1.000 |
| ivf nprobe=8 | 1.08 | 6.8x | 0.267 |
| ivf nprobe=20 | 2.57 | 2.9x | 0.459 |
| ivf int8, oversample=4 | 3.55 | 2.1x | 0.419 |
| rescore bit, oversample=8 | 1.08 | 6.8x | 0.293 |
| rescore int8, oversample=4 | 13.83 | 0.5x | 0.994 |
| diskann int8 | 7.65 | 1.0x | 0.599 |

**How the picture changes from 256 to 1024 dimensions:**

- The **sequential scan gets ~4.3x slower** (1.69 → 7.32 ms) because its cost is
  dominated by per-vector distance math, which scales with dimensionality. This
  is precisely why an approximate index becomes more valuable as vectors grow.
- **`rescore` with bit quantization jumps from 1.7x to 6.8x.** Its coarse scan
  reads a 1-bit-per-dimension copy — 32x smaller than float32 — and that memory
  saving matters far more when each full vector is 4&nbsp;KB. It becomes as fast
  as IVF, though bit quantization is lossy, so recover recall by raising
  `oversample`.
- **IVF keeps its 2–7x speedup** with the same tunable recall/speed trade-off
  via `nprobe`.
- **`rescore` with int8 preserves near-perfect recall** (0.994) at a latency
  cost, since it still touches every vector in the coarse pass.
- **DiskANN's per-row insert cost scales poorly with dimension** (roughly 68s to
  load 20k rows at 256-dim vs 250s at 1024-dim). Graph indexes are designed for
  much larger collections than this benchmark and, for bulk loads, the batched
  insert path (`buffer_threshold`) rather than the default per-row path.

> **Note on recall:** the recall figures above use synthetic Gaussian clusters,
> whose neighborhoods overlap heavily at high dimensionality (the "curse of
> dimensionality"), so the 1024-dim recall looks pessimistic. Real embeddings
> carry genuine semantic structure and recall considerably better — treat these
> tables as a latency/throughput comparison rather than an absolute recall
> guide, and always measure recall on your own data.

## Sponsors

Development of `sqlite-vec` is supported by multiple generous sponsors! Mozilla
is the main sponsor through the new Builders project.
<p align="center">
  <a href="https://hacks.mozilla.org/2024/06/sponsoring-sqlite-vec-to-enable-more-powerful-local-ai-applications/">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./.github/logos/mozilla.dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="./.github/logos/mozilla.svg">
    <img alt="Mozilla Builders logo" width=400>
  </picture>
  </a>
</p>

`sqlite-vec` is also sponsored by the following companies:

<a href="https://fly.io/">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="./.github/logos/flyio.dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="./.github/logos/flyio.svg">
  <img alt="Fly.io logo" src="./.github/logos/flyio.svg" width="48%">
</picture>
</a>

<a href="https://tur.so/sqlite-vec">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="./.github/logos/turso.svg">
  <source media="(prefers-color-scheme: light)" srcset="./.github/logos/turso.svg">
  <img alt="Turso logo" src="./.github/logos/turso.svg" width="48%">
</picture>
</a>

<a href="https://sqlitecloud.io/">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="./.github/logos/sqlitecloud.dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="./.github/logos/sqlitecloud.svg">
  <img alt="SQLite Cloud logo" src="./.github/logos/flyio.svg" width="48%">
</picture>
</a>

<a href="https://shinkai.com">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="./.github/logos/shinkai.dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="./.github/logos/shinkai.svg">

  <img alt="Shinkai logo" src="./.github/logos/shinkai.svg" width="48%">
</picture>
</a>

As well as multiple individual supporters on
[Github sponsors](https://github.com/sponsors/asg017/)!

If your company interested in sponsoring `sqlite-vec` development, send me an
email to get more info: https://alexgarcia.xyz

## See Also

- [**`sqlite-ecosystem`**](https://github.com/asg017/sqlite-ecosystem), Maybe
  more 3rd party SQLite extensions I've developed
- [**`sqlite-rembed`**](https://github.com/asg017/sqlite-rembed), Generate text
  embeddings from remote APIs like OpenAI/Nomic/Ollama, meant for testing and
  SQL scripts
- [**`sqlite-lembed`**](https://github.com/asg017/sqlite-lembed), Generate text
  embeddings locally from embedding models in the `.gguf` format
