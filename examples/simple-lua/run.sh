#!/bin/bash

# Simple run script for the Lua example

echo "=== SQLite-Vec Lua Example Runner ==="

# Check if Lua is available
if ! command -v lua &> /dev/null; then
    echo "Error: Lua is not installed or not in PATH"
    exit 1
fi

# Check if lsqlite3 is available
lua -e "require('lsqlite3')" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Error: lsqlite3 module is not installed"
    echo "Install it with: luarocks install lsqlite3"
    exit 1
fi

# Check if sqlite-vec extension exists
if [ ! -f "../../sqlite-vec.so" ] && [ ! -f "../../sqlite-vec.dll" ] && [ ! -f "../../sqlite-vec.dylib" ]; then
    echo "Error: sqlite-vec extension not found"
    echo "Build it with: cd ../.. && make"
    exit 1
fi

# Run the demo
echo "Running demo..."
lua demo.lua 
