---
title: sqlite-vec in Python
---

# Using `sqlite-vec` in Python

```bash
pip install sqlite-vec
```

```python
import sqlite
import sqlite_vec

db = sqlite3.connect(":memory:")
db.enable_load_extension(True)
sqlite_vec.load(db)
db.enable_load_extension(False)

vec_version, = db.execute("select vec_version()").fetchone()
print(f"vec_version={vec_version}")

```

## Working with Vectors

### Vectors as Lists

### `numpy` Arrays

## Using an up-to-date version of SQLite
