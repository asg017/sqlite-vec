# `sqlite-vec`

[![](https://dcbadge.vercel.app/api/server/VCtQ8cGhUs)](https://discord.gg/VCtQ8cGhUs)

An extremely small, "fast enough" vector search SQLite extension that runs
anywhere! A successor to [sqlite-vss](https://github.com/asg017/sqlite-vss)

> [!IMPORTANT] > _`sqlite-vec` is a work-in-progress and not ready for general usage! I plan to launch a "beta" version in the next month or so. Watch this repo for updates, and read [this blog post](https://alexgarcia.xyz/blog/2024/building-new-vector-search-sqlite/index.html) for more info._

- Store and query float, int8, and binary vectors in `vec0` virtual tables
- Pre-filter vectors with `rowid IN (...)` subqueries
- Written in pure C, no dependencies,
  runs anywhere SQLite runs (Linux/MacOS/Windows, in the browser with WASM,
  Raspberry Pis, etc.)

<p align="center">
<img src="./.github/logos/mozilla.svg" width=400 />
</p>

<p align="center">
<i>
<code>sqlite-vec</code> is a
<a href="#">Mozilla Builders project</a>,
with additional sponsorship from
<a href="https://fly.io/"><img width=14px src="./.github/logos/flyio.small.ico"/> Fly.io </a>,
<a href="https://turso.tech/"><img width=14px src="./.github/logos/turso.small.ico"/> Turso</a>, and
<a href="https://sqlitecloud.io/"><img width=14px src="./.github/logos/sqlitecloud.small.svg"/> SQLite Cloud</a>.
See <a href="#sponsors">the Sponsors section</a> for more details.
</i>
</p>

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

Not currently implemented, but planned in the future (after initial `v0.1.0` version):

- Approximate nearest neighbors search (DiskANN, IVF, maybe HNSW?)
- Metadata filtering + custom internal partitioning
- More vector types (float16, int16, sparse, etc.) and distance functions

Additionally, there will be pre-compiled and pre-packaged packages of `sqlite-vec` for the following platforms:

- `pip` for Python
- `npm` for Node.js / Deno / Bun
- `gem` for Ruby
- `cargo` for Rust
- A single `.c` and `.h` amalgammation for C/C++
- Go module for Golang (requires CGO)
- Datasette and sqlite-utils plugins
- Pre-compiled loadable extensions on Github releases

## Sponors

Development of `sqlite-vec` is supported by multiple generous sponsors! Mozilla is the main sponsor through the new Builders project.

<p align="center">
<img src="./.github/logos/mozilla.svg" width=400 />
</p>

`sqlite-vec` is also sponsored by the following companies:

<a href="https://fly.io/"><img src="./.github/logos/flyio.svg" width=33%/></a> <a href="https://turso.tech/"><img src="./.github/logos/turso.svg" width=33%/> </a> <a href="https://sqlitecloud.io/"><img src="./.github/logos/sqlitecloud.svg" width=33%/></a>

As well as multiple individual supporters on [Github sponsors](https://github.com/sponsors/asg017/)!

If your company interested in sponsoring `sqlite-vec` development, send me an email to get more info: https://alexgarcia.xyz

## See Also

- [**`sqlite-ecosystem`**](https://github.com/asg017/sqlite-ecosystem), Maybe more 3rd party SQLite extensions I've developed
- [**`sqlite-rembed`**](https://github.com/asg017/sqlite-rembed), Generate text embeddings from remote APIs like OpenAI/Nomic/Ollama, meant for testing and SQL scripts
- [**`sqlite-lembed`**](https://github.com/asg017/sqlite-lembed), Generate text embeddings locally from embedding models in the `.gguf` format
