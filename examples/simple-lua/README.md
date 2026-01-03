# SQLite-Vec Simple Lua Example

This example demonstrates how to use sqlite-vec with Lua and the lsqlite3 binding.

## Prerequisites

1. **Lua 5.1+** - The example is compatible with Lua 5.1 and later
2. **lsqlite3** - Lua SQLite3 binding
3. **sqlite-vec extension** - Built for your platform

## Installation

### Install lsqlite3

Using LuaRocks:
```bash
luarocks install lsqlite3
```

Or on Ubuntu/Debian:
```bash
apt install lua-sql-sqlite3
```

### Build sqlite-vec

From the repository root:
```bash
make loadable
```

This creates `dist/vec0.so` (Linux), `dist/vec0.dylib` (macOS), or `dist/vec0.dll` (Windows).

## Running the Example

From this directory:
```bash
lua demo.lua
```

Or using the run script:
```bash
./run.sh
```

## Expected Output

```
=== SQLite-Vec Simple Lua Example ===
sqlite_version=3.x.x, vec_version=v0.x.x
sqlite-vec extension loaded successfully
Inserting vector data...
Inserted 5 vectors
Executing KNN query...
Results (closest to [0.3, 0.3, 0.3, 0.3]):
  rowid=3 distance=0.000000
  rowid=2 distance=0.200000
  rowid=4 distance=0.200000

Testing binary serialization...
  Binary round-trip: rowid=1 distance=0.000000

Demo completed successfully
```

## Using the Binding in Your Project

```lua
local sqlite3 = require("lsqlite3")
local sqlite_vec = require("sqlite_vec")

local db = sqlite3.open_memory()

-- Option 1: Auto-detect extension path
sqlite_vec.load(db)

-- Option 2: Explicit path
sqlite_vec.load(db, "/path/to/vec0.so")

-- Serialize vectors
local json_vec = sqlite_vec.serialize_json({1.0, 2.0, 3.0})  -- "[1.0,2.0,3.0]"
local binary_vec = sqlite_vec.serialize_f32({1.0, 2.0, 3.0}) -- 12 bytes
```
