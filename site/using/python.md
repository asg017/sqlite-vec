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

## Working with Vectors

### Lists

If the vectors you are working with are provided as a list of floats, you can convert them into the compact BLOB format that `sqlite-vec` uses with [`struct.pack()`](https://docs.python.org/3/library/struct.html#struct.pack).

```python
import struct

def serialize(vector: List[float]) -> bytes:
  """ serializes a list of floats into a compact "raw bytes" format """
  return struct.pack('%sf' % len(vector), *vector)


embedding = [0.1, 0.2, 0.3, 0.4]
result = db.execute('select vec_length(?)', [serialize(embedding)]).fetchone()[0]

print(result) # 4
```

### NumPy Arrays

If your vectors are from `numpy` arrays, the Python SQLite package allows you to pass it along as-is. Make sure you convert your array elements to 32-bit floats with [`.astype(np.float32)`](https://numpy.org/doc/stable/reference/generated/numpy.ndarray.astype.html), as some embedding services will use `np.float64` elements.


```python
import numpy as np
import sqlite3
import sqlite_vec

db = sqlite3.connect(":memory:")
db.enable_load_extension(True)
sqlite_vec.load(db)
db.enable_load_extension(False)

db.execute("CREATE VIRTUAL TABLE vec_demo(sample_embedding float[4])")

embedding = np.array([0.1, 0.2, 0.3, 0.4])
db.execute(
    "INSERT INTO vec_demo(sample_embedding) VALUES (?)", [embedding.astype(np.float32)]
)
```

## Recipes

### OpenAI

https://platform.openai.com/docs/guides/embeddings/what-are-embeddings?lang=python

TODO

```python
from openai import OpenAI
import sqlite3
import sqlite_vec

texts = [

  'Capri-Sun is a brand of juice concentrate–based drinks manufactured by the German company Wild and regional licensees.',
  'Shohei Ohtani is a Japanese professional baseball pitcher and designated hitter for the Los Angeles Dodgers of Major League Baseball.',
  'George V was King of the United Kingdom and the British Dominions, and Emperor of India, from 6 May 1910 until his death in 1936.',
  'Alan Mathison Turing was an English mathematician, computer scientist, logician, cryptanalyst, philosopher and theoretical biologist.',
  'Alaqua Cox is a Native American (Menominee) actress.'
]

# change ':memory:' to a filepath to persist data
db = sqlite3.connect(':memory:')
db.enable_load_extension(True)
sqlite_vec.load(db)
db.enable_load_extension(False)

client = OpenAI()

response = client.embeddings.create(
    input=[texts],
    model="text-embedding-3-small"
)

print(response.data[0].embedding)
```

### llamafile

https://github.com/Mozilla-Ocho/llamafile

TODO

### llama-cpp-python

https://github.com/abetlen/llama-cpp-python

TODO

### sentence-transformers (etc.)

https://github.com/UKPLab/sentence-transformers

TODO

## Using an up-to-date version of SQLite

Some features of `sqlite-vec` will require an up-to-date SQLite library. You can see what version of SQLite your Python environment uses with [`sqlite3.sqlite-version`](https://docs.python.org/3/library/sqlite3.html#sqlite3.sqlite_version), or with this one-line command:


```bash
python -c 'import sqlite3; print(sqlite3.sqlite_version)'
```

Currently, **SQLite version 3.41 or higher** is recommended but not required. `sqlite-vec` will work with older version, but certain features and queries will only work correctly in >=3.41.

To "upgrade" the SQLite version your Python installation uses, you have a few options.

### Compile your own SQLite version

You can compile an up-to-date version of SQLite and use some system environment variables (like `LD_PRELOAD` and `DYLD_LIBRARY_PATH`) to force Python to use a different SQLite library. [This guide](https://til.simonwillison.net/sqlite/sqlite-version-macos-python) goes into this approach in more details.

Although compiling SQLite can be straightforward, there are a lot of different compilation options to consider, which makes it confusing. This also doesn't work with Windows, which statically compiles its own SQLite library.

### Use `pysqlite3`

[`pysqlite3`](https://github.com/coleifer/pysqlite3) is a 3rd party PyPi package that bundles an up-to-date SQLite library as a separate pip package.

While it's mostly compatible with the Python `sqlite3` module, there are a few rare edge cases where the APIs don't match.

### Upgrading your Python version

Sometimes installing a latest version of Python will "magically" upgrade your SQLite version as well. This is a nuclear option, as upgrading Python installations can be quite the hassle, but most Python 3.12 builds will have a very recent SQLite version.
