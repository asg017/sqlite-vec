# Using `sqlite-vec` in Node.js, Deno, and Bun

[![npm](https://img.shields.io/npm/v/sqlite-vec.svg?color=green&logo=nodedotjs&logoColor=white)](https://www.npmjs.com/package/sqlite-vec)

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

The `load()` function is compatible with
[`node:sqlite`](https://nodejs.org/api/sqlite.html#class-databasesync),
[`better-sqlite3`](https://github.com/WiseLibs/better-sqlite3),
[`node-sqlite3`](https://github.com/TryGhost/node-sqlite3),
[`jsr:@db/sqlite`](https://jsr.io/@db/sqlite) (Deno), and
[`bun:sqlite`](https://bun.sh/docs/api/sqlite).

## Working with vectors in JavaScript

if your vectors are represented as an array of numbers, wrap it in a
[`Float32Array`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Float32Array),
and use the
[`.buffer`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/TypedArray/buffer)
accessor to bind as a parameter to `sqlite-vec` SQL functions.

```js
const embedding = new Float32Array([0.1, 0.2, 0.3, 0.4]);
const stmt = db.prepare("select vec_length(?)");
console.log(stmt.run(embedding.buffer)); // 4
```

## Node.js

If you're on Node.js `23.5.0` or above, you can use [the builtin `node:sqlite` module](https://nodejs.org/api/sqlite.html) with `sqlite-vec` like so:

```js
import { DatabaseSync } from "node:sqlite";
import * as sqliteVec from "sqlite-vec";

const db = new DatabaseSync(":memory:", { allowExtension: true });
sqliteVec.load(db);

const embedding = new Float32Array([0.1, 0.2, 0.3, 0.4]);
const { result } = db
  .prepare("select vec_length(?) as result")
  .get(new Uint8Array(embedding.buffer));

console.log(result); // 4
```


See
[`simple-node2/demo.mjs`](https://github.com/asg017/sqlite-vec/blob/main/examples/simple-node2/demo.mjs)
for a complete `node:sqlite` + `sqlite-vec` demo.


Alternatively, you can use the
[`better-sqlite3`](https://github.com/WiseLibs/better-sqlite3)
NPM package with `sqlite-vec` in Node as well.

```js
import * as sqliteVec from "sqlite-vec";
import Database from "better-sqlite3";

const db = new Database(":memory:");
sqliteVec.load(db);

const embedding = new Float32Array([0.1, 0.2, 0.3, 0.4]);
const { result } = db
  .prepare("select vec_length(?)",)
  .get(embedding);

console.log(result); // 4

```

See
[`simple-node/demo.mjs`](https://github.com/asg017/sqlite-vec/blob/main/examples/simple-node/demo.mjs)
for a more complete demo.

## Deno

Here's a quick recipe of using `sqlite-vec` with
[`jsr:@db/sqlite`](https://jsr.io/@db/sqlite) in Deno. It will only work on Deno
version `1.44` or greater, because of a bug in previous Deno versions.



```ts
import { Database } from "jsr:@db/sqlite";
import * as sqliteVec from "npm:sqlite-vec";

const db = new Database(":memory:");
db.enableLoadExtension = true;
sqliteVec.load(db);
db.enableLoadExtension = false;

const embedding = new Float32Array([0.1, 0.2, 0.3, 0.4]);
const [result] = db
  .prepare("select vec_length(?)")
  .value<[string]>(new Uint8Array(embedding.buffer)!);
console.log(result); // 4
```

See
[`simple-deno/demo.ts`](https://github.com/asg017/sqlite-vec/blob/main/examples/simple-deno/demo.ts)
for a more complete Deno demo.

The `better-sqlite3` example above also works in Deno, when the `better-sqlite3` import is prefixed with `npm:`:

```js
import * from "better-sqlite3"; // [!code --]
import * from "npm:better-sqlite3"; // [!code ++]
```

## Bun

Here's a quick recipe of using `sqlite-vec` with
[`bun:sqlite`](https://bun.sh/docs/api/sqlite) in Bun. 

```ts
import { Database } from "bun:sqlite";
import * as sqliteVec from "sqlite-vec";

// MacOS *might* have to do this, as the builtin SQLite library on MacOS doesn't allow extensions
Database.setCustomSQLite("/usr/local/opt/sqlite3/lib/libsqlite3.dylib");

const db = new Database(":memory:");
sqliteVec.load(db);

const embedding = new Float32Array([0.1, 0.2, 0.3, 0.4]);
const { result } = db
  .prepare("select vec_length(?) as result",)
  .get(embedding);

console.log(result); // 4

```

See
[`simple-bun/demo.ts`](https://github.com/asg017/sqlite-vec/blob/main/examples/simple-bun/demo.ts)
for a more complete Bun demo.

The `better-sqlite3`
example above also works with Bun.