# Using `sqlite-vec` in Datasette

[![Datasette](https://img.shields.io/pypi/v/datasette-sqlite-vec.svg?color=B6B6D9&label=Datasette+plugin&logoColor=white&logo=python)](https://datasette.io/plugins/datasette-sqlite-vec)

[Datasette](https://datasette.io/) users can install `sqlite-vec` into their Datasette instances with the `datasette-sqlite-vec` plugin:

```bash
datasette install datasette-sqlite-vec
```

After installing, future Datasette instances will have `sqlite-vec` SQL functions loaded in.

"Unsafe" functions like static blobs and NumPy file reading are not available with `datasette-sqlite-vec`.
