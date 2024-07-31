# `sqlite-vec` in the Browser with WebAssembly

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
