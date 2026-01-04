# `sqlite-vec`

[![](https://dcbadge.vercel.app/api/server/VCtQ8cGhUs)](https://discord.gg/Ve7WeCJFXk)

> [!NOTE]
> **Community Fork Notice:** This is a temporary fork of [`asg017/sqlite-vec`](https://github.com/asg017/sqlite-vec)
> Created to merge pending upstream PRs and provide community support while the original author is unavailable.
> Once development resumes on the original repository, users are encouraged to switch back.
> All credit for the original implementation goes to [Alex Garcia](https://github.com/asg017).

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

### From Original Package Registries

The original packages on PyPI, npm, RubyGems, and crates.io are maintained by the original author.
For the latest features from this fork, see "Installing from This Fork" below.

| Language       | Install                                              | More Info                                                                             |                                                                                                                                                                                                    |
| -------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Python         | `pip install sqlite-vec`                             | [`sqlite-vec` with Python](https://alexgarcia.xyz/sqlite-vec/python.html)             | [![PyPI](https://img.shields.io/pypi/v/sqlite-vec.svg?color=blue&logo=python&logoColor=white)](https://pypi.org/project/sqlite-vec/)                                                               |
| Node.js        | `npm install sqlite-vec`                             | [`sqlite-vec` with Node.js](https://alexgarcia.xyz/sqlite-vec/js.html)            | [![npm](https://img.shields.io/npm/v/sqlite-vec.svg?color=green&logo=nodedotjs&logoColor=white)](https://www.npmjs.com/package/sqlite-vec)                                                         |
| Ruby           | `gem install sqlite-vec`                             | [`sqlite-vec` with Ruby](https://alexgarcia.xyz/sqlite-vec/ruby.html)                 | ![Gem](https://img.shields.io/gem/v/sqlite-vec?color=red&logo=rubygems&logoColor=white)                                                                       |
| Rust           | `cargo add sqlite-vec`                               | [`sqlite-vec` with Rust](https://alexgarcia.xyz/sqlite-vec/rust.html)                 | [![Crates.io](https://img.shields.io/crates/v/sqlite-vec?logo=rust)](https://crates.io/crates/sqlite-vec)                                                                                          |
| Datasette      | `datasette install datasette-sqlite-vec`             | [`sqlite-vec` with Datasette](https://alexgarcia.xyz/sqlite-vec/datasette.html)       | [![Datasette](https://img.shields.io/pypi/v/datasette-sqlite-vec.svg?color=B6B6D9&label=Datasette+plugin&logoColor=white&logo=python)](https://datasette.io/plugins/datasette-sqlite-vec)          |
| rqlite         | `rqlited -extensions-path=sqlite-vec.tar.gz`         | [`sqlite-vec` with rqlite](https://alexgarcia.xyz/sqlite-vec/rqlite.html)                        | [![rqlite](https://img.shields.io/badge/rqlite-sqlite_extensions-blue)](https://rqlite.io/docs/guides/extensions/)           |
| `sqlite-utils` | `sqlite-utils install sqlite-utils-sqlite-vec`       | [`sqlite-vec` with sqlite-utils](https://alexgarcia.xyz/sqlite-vec/sqlite-utils.html) | [![sqlite-utils](https://img.shields.io/pypi/v/sqlite-utils-sqlite-vec.svg?color=B6B6D9&label=sqlite-utils+plugin&logoColor=white&logo=python)](https://datasette.io/plugins/datasette-sqlite-vec) |

### Installing from This Fork

Install directly from GitHub to get the latest features from this community fork.

#### Available Languages

| Language | Install Latest (main branch) | Install Specific Version |
|----------|------------------------------|--------------------------|
| **Go** | `go get github.com/vlasky/sqlite-vec/bindings/go/cgo@main` | `go get github.com/vlasky/sqlite-vec/bindings/go/cgo@v0.2.4-alpha` |
| **Lua** | `luarocks install lsqlite3` then copy [`sqlite_vec.lua`](bindings/lua/) to your project. See [Lua example](/examples/simple-lua/) | Download [`sqlite_vec.lua` at v0.2.4-alpha](https://github.com/vlasky/sqlite-vec/blob/v0.2.4-alpha/bindings/lua/sqlite_vec.lua) |
| **Python** | `pip install git+https://github.com/vlasky/sqlite-vec.git` | `pip install git+https://github.com/vlasky/sqlite-vec.git@v0.2.4-alpha` |
| **Rust** | `cargo add sqlite-vec --git https://github.com/vlasky/sqlite-vec` | `cargo add sqlite-vec --git https://github.com/vlasky/sqlite-vec --tag v0.2.4-alpha` |
| **Node.js** | `npm install vlasky/sqlite-vec` | `npm install vlasky/sqlite-vec#v0.2.4-alpha` |
| **Ruby** | `gem 'sqlite-vec', git: 'https://github.com/vlasky/sqlite-vec'` | `gem 'sqlite-vec', git: 'https://github.com/vlasky/sqlite-vec', tag: 'v0.2.4-alpha'` |

**Python Note:** Requires Python built with loadable extension support (`--enable-loadable-sqlite-extensions`). If you encounter an error about extension support not being available:
- Use `uv` to create virtual environments (automatically uses system Python which typically has extension support)
- Or use system Python instead of pyenv/custom builds
- Or rebuild your Python with `./configure --enable-loadable-sqlite-extensions`

**Available version tags:** See [Releases](https://github.com/vlasky/sqlite-vec/releases)

#### Build from Source

For direct C usage or other languages:

```bash
git clone https://github.com/vlasky/sqlite-vec.git
cd sqlite-vec
./scripts/vendor.sh  # Download vendored dependencies
make loadable        # Builds dist/vec0.so (or .dylib/.dll)
```

#### Not Yet Available

- Pre-built binaries via GitHub Releases
- Package registry publications (PyPI, npm, RubyGems, crates.io)
- Datasette/sqlite-utils plugins

For these, use the original packages until this fork's CI/CD is configured.

See the [original documentation](https://alexgarcia.xyz/sqlite-vec/installation.html) for detailed usage information.

## What's New

See [CHANGELOG.md](CHANGELOG.md) for a complete list of improvements, bug fixes, and merged upstream PRs.

## Basic Usage

**Vector types:** `sqlite-vec` supports three vector types with different trade-offs:

```sql
-- Float vectors (32-bit floating point, most common)
CREATE VIRTUAL TABLE vec_floats USING vec0(embedding float[384]);

-- Int8 vectors (8-bit integers, smaller memory footprint)
CREATE VIRTUAL TABLE vec_int8 USING vec0(embedding int8[384]);

-- Binary vectors (1 bit per dimension, maximum compression)
CREATE VIRTUAL TABLE vec_binary USING vec0(embedding bit[384]);
```

**Usage example:**

```sql
.load ./vec0

create virtual table vec_examples using vec0(
  sample_embedding float[8]
);

-- vectors can be provided as JSON or in a compact binary format
insert into vec_examples(rowid, sample_embedding)
  values
    (1, '[0.279, -0.95, -0.45, -0.554, 0.473, 0.353, 0.784, -0.826]'),
    (2, '[-0.156, -0.94, -0.563, 0.011, -0.947, -0.602, 0.3, 0.09]'),
    (3, '[-0.559, 0.179, 0.619, -0.987, 0.612, 0.396, -0.319, -0.689]'),
    (4, '[0.914, -0.327, -0.815, -0.807, 0.695, 0.207, 0.614, 0.459]'),
    (5, '[0.072, 0.946, -0.243, 0.104, 0.659, 0.237, 0.723, 0.155]'),
    (6, '[0.409, -0.908, -0.544, -0.421, -0.84, -0.534, -0.798, -0.444]'),
    (7, '[0.271, -0.27, -0.26, -0.581, -0.466, 0.873, 0.296, 0.218]'),
    (8, '[-0.658, 0.458, -0.673, -0.241, 0.979, 0.28, 0.114, 0.369]'),
    (9, '[0.686, 0.552, -0.542, -0.936, -0.369, -0.465, -0.578, 0.886]'),
    (10, '[0.753, -0.371, 0.311, -0.209, 0.829, -0.082, -0.47, -0.507]'),
    (11, '[0.123, -0.475, 0.169, 0.796, -0.201, -0.561, 0.995, 0.019]'),
    (12, '[-0.818, -0.906, -0.781, 0.255, 0.584, -0.156, -0.873, -0.237]'),
    (13, '[0.992, 0.058, 0.942, 0.722, -0.977, 0.441, 0.363, 0.074]'),
    (14, '[-0.466, 0.282, -0.777, -0.13, -0.093, 0.908, 0.752, -0.473]'),
    (15, '[0.001, -0.643, 0.825, 0.741, -0.403, 0.278, 0.218, -0.694]'),
    (16, '[0.525, 0.079, 0.557, 0.061, -0.999, -0.352, -0.961, 0.858]'),
    (17, '[0.757, 0.663, -0.385, -0.884, 0.756, 0.894, -0.829, -0.028]'),
    (18, '[-0.862, 0.521, 0.532, -0.743, -0.049, 0.1, -0.47, 0.745]'),
    (19, '[-0.154, -0.576, 0.079, 0.46, -0.598, -0.377, 0.99, 0.3]'),
    (20, '[-0.124, 0.035, -0.758, -0.551, -0.324, 0.177, -0.54, -0.56]');


-- Find 3 nearest neighbors using LIMIT
select
  rowid,
  distance
from vec_examples
where sample_embedding match '[0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5]'
order by distance
limit 3;
/*
┌───────┬──────────────────┐
│ rowid │     distance     │
├───────┼──────────────────┤
│ 5     │ 1.16368770599365 │
│ 13    │ 1.75137972831726 │
│ 11    │ 1.83941268920898 │
└───────┴──────────────────┘
*/
```

**How vector search works:** The `MATCH` operator finds vectors similar to your query vector. In the example above, `sample_embedding MATCH '[0.5, ...]'` searches for vectors closest to `[0.5, ...]` and returns them ordered by distance (smallest = most similar).

**Note:** All vector similarity queries require `LIMIT` or `k = ?` (where k is the number of nearest neighbors to return). This prevents accidentally returning too many results on large datasets, since finding all vectors within a distance threshold requires calculating distance to every vector in the table.

## Advanced Usage

This fork adds several powerful features for production use:

### Distance Constraints for KNN Queries

Filter results by distance thresholds using `>`, `>=`, `<`, `<=` operators on the `distance` column:

```sql
-- KNN query with distance constraint
-- Requests k=10 neighbors, but only returns those with distance < 1.5
select rowid, distance
from vec_examples
where sample_embedding match '[0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5]'
  and k = 10
  and distance < 1.5
order by distance;
/*
┌───────┬──────────────────┐
│ rowid │     distance     │
├───────┼──────────────────┤
│ 5     │ 1.16368770599365 │
└───────┴──────────────────┘
*/

-- KNN query with range constraint: find vectors in a specific distance range
select rowid, distance
from vec_examples
where sample_embedding match '[0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5]'
  and k = 20
  and distance between 1.5 and 2.0
order by distance;
/*
┌───────┬──────────────────┐
│ rowid │     distance     │
├───────┼──────────────────┤
│ 13    │ 1.75137972831726 │
│ 11    │ 1.83941268920898 │
│ 7     │ 1.89339029788971 │
│ 8     │ 1.92658650875092 │
│ 10    │ 1.93983662128448 │
└───────┴──────────────────┘
*/
```

### Cursor-based Pagination

Instead of using `OFFSET` (which is slow for large datasets), you can use the last result's distance value as a 'cursor' to fetch the next page. This is more efficient because you're filtering directly rather than skipping rows.

```sql
-- First page: get initial results
select rowid, distance
from vec_examples
where sample_embedding match '[0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5]'
  and k = 3
order by distance;
/*
┌───────┬──────────────────┐
│ rowid │     distance     │
├───────┼──────────────────┤
│ 5     │ 1.16368770599365 │
│ 13    │ 1.75137972831726 │
│ 11    │ 1.83941268920898 │
└───────┴──────────────────┘
*/

-- Next page: use last distance as cursor (distance > 1.83941268920898)
select rowid, distance
from vec_examples
where sample_embedding match '[0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5]'
  and k = 3
  and distance > 1.83941268920898
order by distance;
/*
┌───────┬──────────────────┐
│ rowid │     distance     │
├───────┼──────────────────┤
│ 7     │ 1.89339029788971 │
│ 8     │ 1.92658650875092 │
│ 10    │ 1.93983662128448 │
└───────┴──────────────────┘
*/
```

### Space Reclamation with Optimize

`optimize` compacts vec shadow tables. To shrink the database file:

```sql
-- Before creating vec tables: enable autovacuum and apply it (recommended)
PRAGMA auto_vacuum = FULL;  -- or INCREMENTAL
VACUUM;                     -- activates the setting

-- Use WAL for better concurrency
PRAGMA journal_mode = WAL;
```

After deletes, reclaim space:

```sql
-- Compact shadow tables
INSERT INTO vec_examples(vec_examples) VALUES('optimize');

- Flush WAL
PRAGMA wal_checkpoint(TRUNCATE);

-- Reclaim freed pages (if using auto_vacuum=INCREMENTAL)
PRAGMA incremental_vacuum;

-- If you did NOT enable autovacuum, run VACUUM (after checkpoint) to shrink the file.
-- With autovacuum on, VACUUM is optional.
VACUUM;
```

`VACUUM` should not corrupt vec tables; a checkpoint first is recommended when
using WAL so the rewrite starts from a clean state.

## Sponsors

> [!NOTE]
> The sponsors listed below support the original [`asg017/sqlite-vec`](https://github.com/asg017/sqlite-vec) project by Alex Garcia, not this community fork.

Development of the original `sqlite-vec` is supported by multiple generous sponsors! Mozilla
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
