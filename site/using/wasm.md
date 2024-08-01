# `sqlite-vec` in the Browser with WebAssembly

`sqlite-vec` can be statically compiled into [official SQLite WASM](https://sqlite.org/wasm/doc/trunk/index.md) builds. The process is a bit complicated, but the result is a vector search in the browser, which is pretty cool!

```html
<html>
  <body>
    <script type="module">
      import {default as init} from "https://cdn.jsdelivr.net/npm/sqlite-vec-wasm-demo@latest/sqlite3.mjs";

      const sqlite3 = await init();
      const db = new sqlite3.oo1.DB(":memory:");

      const [sqlite_version, vec_version] = db.selectArray('select vec_version();')
      console.log(`vec_version=${vec_version}`);
    </script>
  </body>
</html>
```
[*Open in CodePen*](https://codepen.io/asg017_ucsd/pen/MWMpJNY)


It's not possibly to dynamically load a SQLite extension into a WASM build of SQLite. So `sqlite-vec` must be statically compiled into custom WASM builds.

## The `sqlite-vec-wasm-demo` NPM package

A **demonstration** of `sqlite-vec` in WASM is provided with the `sqlite-vec-wasm-demo` NPM package. This package is a demonstration and may change at any time. It doesn't follow the [Semantic version of `sqlite-vec`](./versioning.md).


See
[`simple-wasm/index.html`](https://github.com/asg017/sqlite-vec/blob/main/examples/simple-wasm/index.html)
for a more complete WASM demo using this package.
