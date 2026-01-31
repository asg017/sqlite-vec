#!/bin/bash

# Run script for the Lua example

set -e

echo "=== SQLite-Vec Lua Example Runner ==="

# Check if Lua is available
if ! command -v lua &> /dev/null; then
    echo "Error: Lua is not installed or not in PATH"
    exit 1
fi

# Check if lsqlite3 is available
if ! lua -e "require('lsqlite3')" 2>/dev/null; then
    echo "Error: lsqlite3 module is not installed"
    echo "Install with: luarocks install lsqlite3"
    exit 1
fi

# Check if sqlite-vec extension exists
if [ ! -f "../../dist/vec0.so" ] && [ ! -f "../../dist/vec0.dylib" ] && [ ! -f "../../dist/vec0.dll" ]; then
    echo "Error: sqlite-vec extension not found in ../../dist/"
    echo "Build with: cd ../.. && make loadable"
    exit 1
fi

# Run the demo
lua demo.lua
