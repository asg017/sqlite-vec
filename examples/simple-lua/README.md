# SQLite-Vec Simple Lua Example

This example demonstrates how to use sqlite-vec with Lua and the lsqlite3 binding.

## Prerequisites

1. **Lua** (5.1 or later) - The example is compatible with Lua 5.1+
2. **lsqlite3** - Lua SQLite3 binding
3. **sqlite-vec extension** - Compiled for your platform

## Installation

### Install lsqlite3

Using LuaRocks:
```bash
luarocks install lsqlite3
```

Or on Ubuntu/Debian:
```bash
apt-get install lua-sql-sqlite3
```

### Build sqlite-vec

From the sqlite-vec root directory:
```bash
make
```

This will create the appropriate extension file (.so, .dll, or .dylib) for your platform.

### Setup sqlite_vec.lua

You have two options:

1. **Copy the binding file** (recommended):
   ```bash
   cp ../../bindings/lua/sqlite_vec.lua ./
   ```

2. **Modify the require path** in `demo.lua` to point to the bindings directory.

## Running the Example

```bash
lua demo.lua
```

Expected output:
```
=== SQLite-Vec Simple Lua Example ===
sqlite_version=3.x.x, vec_version=v0.x.x
Inserting vector data...
Executing KNN query...
Results:
rowid=3 distance=0.000000
rowid=2 distance=0.200000
rowid=1 distance=0.400000
✓ Demo completed successfully
``` 
