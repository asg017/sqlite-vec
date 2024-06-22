# Using `sqlite-vec` in Node.js, Deno, and Bun

To use `sqlite-vec` in Node.js, Deno or Bun, install the
[`sqlite-vec` NPM package](https://npmjs.com/package/sqlite-vec) using your
favorite package manager:

::: code-group

```bash [npm]
npm install sqlite-vec
```

```bash [Bun]
bun install sqlite-vec
```

```bash [Deno]
deno add npm:sqlite-vec
```

:::

Once installed, use the `sqliteVec.load()` function to load `sqlite-vec` SQL
functions into a SQLite connection.

```js
import * as sqliteVec from "sqlite-vec";
import Database from "better-sqlite3";

const db = new Database(":memory:");
sqliteVec.load(db);

const { vec_version } = db
  .prepare("select vec_version() as vec_version;")
  .get();

console.log(`vec_version=${vec_version}`);
```

The `load()` function is compatable with
[`better-sqlite3`](https://github.com/WiseLibs/better-sqlite3),
[`node-sqlite3`](https://github.com/TryGhost/node-sqlite3),
[`js:@db/sqlite`](https://jsr.io/@db/sqlite) (Deno), and
[`bun:sqlite`](https://bun.sh/docs/api/sqlite).

## Working with vectors in JavaScript

if your vectors are represented as an array of numbers, use
[Float32Array](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Float32Array),
use the
[`.buffer`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/TypedArray/buffer)
accessor to insert the underlying ArrayBuffer.

```js
const embedding = new Float32Array([0.1, 0.2, 0.3]);
const stmt = db.prepare("INSERT INTO vss_demo VALUES (?)");
stmt.run(embedding.buffer);


## Node.js

## Deno

## Bun
```
