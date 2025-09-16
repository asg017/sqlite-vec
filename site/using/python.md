---
title: sqlite-vec in Python
---

# Using `sqlite-vec` in Python

[![PyPI](https://img.shields.io/pypi/v/sqlite-vec.svg?color=blue&logo=python&logoColor=white)](https://pypi.org/project/sqlite-vec/)

To use `sqlite-vec` from Python, install the
[`sqlite-vec` PyPi package](https://pypi.org/project/sqlite-vec/) using your
favorite Python package manager:

```bash
pip install sqlite-vec
```

Once installed, use the `sqlite_vec.load()` function to load `sqlite-vec` SQL
functions into a SQLite connection.

```python
import sqlite3
import sqlite_vec

db = sqlite3.connect(":memory:")
db.enable_load_extension(True)
sqlite_vec.load(db)
db.enable_load_extension(False)

vec_version, = db.execute("select vec_version()").fetchone()
print(f"vec_version={vec_version}")
```

See
[`simple-python/demo.py`](https://github.com/asg017/sqlite-vec/blob/main/examples/simple-python/demo.py)
for a more complete Python demo.

## Working with Vectors

### Lists

If your vectors in Python are provided as a list of floats, you can
convert them into the compact BLOB format that `sqlite-vec` uses with
`serialize_float32()`. This internally calls [`struct.pack()`](https://docs.python.org/3/library/struct.html#struct.pack).

```python
from sqlite_vec import serialize_float32

embedding = [0.1, 0.2, 0.3, 0.4]
result = db.execute('select vec_length(?)', [serialize_float32(embedding)])

print(result.fetchone()[0]) # 4
```

### NumPy Arrays

If your vectors are NumPy arrays, the Python SQLite package allows you to
pass it along as-is, since NumPy arrays implement [the Buffer protocol](https://docs.python.org/3/c-api/buffer.html). Make sure you cast your array elements to 32-bit floats
with
[`.astype(np.float32)`](https://numpy.org/doc/stable/reference/generated/numpy.ndarray.astype.html),
as some embeddings will use `np.float64`.

```python
import numpy as np
embedding = np.array([0.1, 0.2, 0.3, 0.4])
db.execute(
    "SELECT vec_length(?)", [embedding.astype(np.float32)]
) # 4
```


## Using an up-to-date version of SQLite {#updated-sqlite}

Some features of `sqlite-vec` will require an up-to-date SQLite library. You can
see what version of SQLite your Python environment uses with
[`sqlite3.sqlite_version`](https://docs.python.org/3/library/sqlite3.html#sqlite3.sqlite_version),
or with this one-line command:

```bash
python -c 'import sqlite3; print(sqlite3.sqlite_version)'
```

Currently, **SQLite version 3.41 or higher** is recommended but not required.
`sqlite-vec` will work with older versions, but certain features and queries will
only work correctly in >=3.41.

To "upgrade" the SQLite version your Python installation uses, you have a few
options.

### Compile your own SQLite version

You can compile an up-to-date version of SQLite and use some system environment
variables (like `LD_PRELOAD` and `DYLD_LIBRARY_PATH`) to force Python to use a
different SQLite library.
[This guide](https://til.simonwillison.net/sqlite/sqlite-version-macos-python)
goes into this approach in more details.

Although compiling SQLite can be straightforward, there are a lot of different
compilation options to consider, which makes it confusing. This also doesn't
work with Windows, which statically compiles its own SQLite library.

### Use `pysqlite3`

[`pysqlite3`](https://github.com/coleifer/pysqlite3) is a 3rd party PyPi package
that bundles an up-to-date SQLite library as a separate pip package.

While it's mostly compatible with the Python `sqlite3` module, there are a few
rare edge cases where the APIs don't match.

### Upgrading your Python version

Sometimes installing a latest version of Python will "magically" upgrade your
SQLite version as well. This is a nuclear option, as upgrading Python
installations can be quite the hassle, but most Python 3.12 builds will have a
very recent SQLite version.


## MacOS blocks SQLite extensions by default

The default SQLite library that is bundled with Mac operating systems do not include support for SQLite extensions. That means the default Python library that is bundled with MacOS also does not support SQLite extensions.

This is the case if you come across the following error message:

```
AttributeError: 'sqlite3.Connection' object has no attribute 'enable_load_extension'
```

As a workaround, use the Homebrew version of Python (`brew install python`, new version at `/opt/homebrew/bin/python3`), which will use the Homebrew version of SQLite that allows SQLite extensions.

Other workarounds can be found at [Using an up-to-date version of SQLite](#updated-sqlite);
