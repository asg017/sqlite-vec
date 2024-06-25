# `sqlite-vec`

[![](https://dcbadge.vercel.app/api/server/VCtQ8cGhUs)](https://discord.gg/VCtQ8cGhUs)

An extremely small, "fast enough" vector search SQLite extension that runs
anywhere! A successor to [sqlite-vss](https://github.com/asg017/sqlite-vss)

<!-- deno-fmt-ignore-start -->

> [!IMPORTANT]
> _`sqlite-vec` is a work-in-progress and not ready for general usage! I plan to launch a "beta" version in the next month or so. Watch this repo for updates, and read [this blog post](https://alexgarcia.xyz/blog/2024/building-new-vector-search-sqlite/index.html) for more info._

<!-- deno-fmt-ignore-end -->

- Store and query float, int8, and binary vectors in `vec0` virtual tables
- Pre-filter vectors with `rowid IN (...)` subqueries
- Written in pure C, no dependencies, runs anywhere SQLite runs
  (Linux/MacOS/Windows, in the browser with WASM, Raspberry Pis, etc.)

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
<a href="https://turso.tech/"><img width=14px src="./.github/logos/turso.small.ico"/> Turso</a>, and
<a href="https://sqlitecloud.io/"><img width=14px src="./.github/logos/sqlitecloud.small.svg"/> SQLite Cloud</a>.
See <a href="#sponsors">the Sponsors section</a> for more details.
</i>
</p>

<!--
## Installing

See [Installing `sqlite-vec`](https://alexgarcia.xyz/sqlite-vec/installing.html)
for more details.

| Language       | Install                                              | More Info                                                                             |                                                                                                                                                                                                    |
| -------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Python         | `pip install sqlite-vec`                             | [`sqlite-vec` with Python](https://alexgarcia.xyz/sqlite-vec/python.html)             | [![PyPI](https://img.shields.io/pypi/v/sqlite-vec.svg?color=blue&logo=python&logoColor=white)](https://pypi.org/project/sqlite-vec/)                                                               |
| Node.js        | `npm install sqlite-vec`                             | [`sqlite-vec` with Node.js](https://alexgarcia.xyz/sqlite-vec/nodejs.html)            | [![npm](https://img.shields.io/npm/v/sqlite-vec.svg?color=green&logo=nodedotjs&logoColor=white)](https://www.npmjs.com/package/sqlite-vec)                                                         |
| Ruby           | `gem install sqlite-vec`                             | [`sqlite-vec` with Ruby](https://alexgarcia.xyz/sqlite-vec/ruby.html)                 | ![Gem](https://img.shields.io/gem/v/sqlite-vec?color=red&logo=rubygems&logoColor=white)                                                                                                            |
| Go             | `go get -u github.com/asg017/sqlite-vec/bindings/go` | [`sqlite-vec` with Go](https://alexgarcia.xyz/sqlite-vec/go.html)                     | [![Go Reference](https://pkg.go.dev/badge/github.com/asg017/sqlite-vec/bindings/go.svg)](https://pkg.go.dev/github.com/asg017/sqlite-vec/bindings/go)                                              |
| Rust           | `cargo add sqlite-vec`                               | [`sqlite-vec` with Rust](https://alexgarcia.xyz/sqlite-vec/rust.html)                 | [![Crates.io](https://img.shields.io/crates/v/sqlite-vec?logo=rust)](https://crates.io/crates/sqlite-vec)                                                                                          |
| Datasette      | `datasette install datasette-sqlite-vec`             | [`sqlite-vec` with Datasette](https://alexgarcia.xyz/sqlite-vec/datasette.html)       | [![Datasette](https://img.shields.io/pypi/v/datasette-sqlite-vec.svg?color=B6B6D9&label=Datasette+plugin&logoColor=white&logo=python)](https://datasette.io/plugins/datasette-sqlite-vec)          |
| `sqlite-utils` | `sqlite-utils install sqlite-utils-sqlite-vec`       | [`sqlite-vec` with sqlite-utils](https://alexgarcia.xyz/sqlite-vec/sqlite-utils.html) | [![sqlite-utils](https://img.shields.io/pypi/v/sqlite-utils-sqlite-vec.svg?color=B6B6D9&label=sqlite-utils+plugin&logoColor=white&logo=python)](https://datasette.io/plugins/datasette-sqlite-vec) |
| Github Release |                                                      |                                                                                       | ![GitHub tag (latest SemVer pre-release)](https://img.shields.io/github/v/tag/asg017/sqlite-vec?color=lightgrey&include_prereleases&label=Github+release&logo=github)                              |

-->

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


-- KNN style query goes brrrr
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

## Roadmap

Not currently implemented, but planned in the future (after initial `v0.1.0`
version):

- Approximate nearest neighbors search (DiskANN, IVF, maybe HNSW?)
- Metadata filtering + custom internal partitioning
- More vector types (float16, int16, sparse, etc.) and distance functions

Additionally, there will be pre-compiled and pre-packaged packages of
`sqlite-vec` for the following platforms:

- `pip` for Python
- `npm` for Node.js / Deno / Bun
- `gem` for Ruby
- `cargo` for Rust
- A single `.c` and `.h` amalgammation for C/C++
- Go module for Golang (requires CGO)
- Datasette and sqlite-utils plugins
- Pre-compiled loadable extensions on Github releases

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
  <img alt="Fly.io logo" src="./.github/logos/flyio.svg" width="32%">
</picture>
</a>

<a href="https://turso.tech/">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="./.github/logos/turso.svg">
  <source media="(prefers-color-scheme: light)" srcset="./.github/logos/turso.svg">
  <img alt="Turso logo" src="./.github/logos/turso.svg" width="32%">
</picture>
</a>

<a href="https://sqlitecloud.io/">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="./.github/logos/sqlitecloud.dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="./.github/logos/sqlitecloud.svg">
  <img alt="SQLite Cloud logo" src="./.github/logos/flyio.svg" width="32%">
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
