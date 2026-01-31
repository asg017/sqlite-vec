#!/usr/bin/env lua

-- Simple Lua example demonstrating sqlite-vec usage
-- This example shows how to create vector tables, insert data, and perform KNN queries

local sqlite3 = require("lsqlite3")

-- Add bindings directory to package path
package.path = package.path .. ";../../bindings/lua/?.lua"
local sqlite_vec = require("sqlite_vec")

local function main()
    print("=== SQLite-Vec Simple Lua Example ===")

    -- Create in-memory database
    local db = sqlite3.open_memory()
    if not db then
        error("Failed to create database")
    end

    -- Load sqlite-vec extension
    sqlite_vec.load(db)

    -- Check versions (also verifies extension loaded)
    for row in db:nrows("SELECT sqlite_version() as sv, vec_version() as vv") do
        print(string.format("sqlite_version=%s, vec_version=%s", row.sv, row.vv))
        print("sqlite-vec extension loaded successfully")
    end

    -- Test data
    local items = {
        {1, {0.1, 0.1, 0.1, 0.1}},
        {2, {0.2, 0.2, 0.2, 0.2}},
        {3, {0.3, 0.3, 0.3, 0.3}},
        {4, {0.4, 0.4, 0.4, 0.4}},
        {5, {0.5, 0.5, 0.5, 0.5}},
    }
    local query = {0.3, 0.3, 0.3, 0.3}

    -- Create virtual table
    local result = db:exec("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[4])")
    if result ~= sqlite3.OK then
        error("Failed to create virtual table: " .. db:errmsg())
    end

    -- Insert data using JSON format
    print("Inserting vector data...")
    db:exec("BEGIN")

    for _, item in ipairs(items) do
        local rowid = item[1]
        local vector_json = sqlite_vec.serialize_json(item[2])
        local sql = string.format("INSERT INTO vec_items(rowid, embedding) VALUES (%d, '%s')",
                                 rowid, vector_json)
        result = db:exec(sql)
        if result ~= sqlite3.OK then
            error("Failed to insert item: " .. db:errmsg())
        end
    end

    db:exec("COMMIT")
    print(string.format("Inserted %d vectors", #items))

    -- Perform KNN query
    print("Executing KNN query...")
    local query_json = sqlite_vec.serialize_json(query)

    local sql = string.format([[
        SELECT rowid, distance
        FROM vec_items
        WHERE embedding MATCH '%s'
        ORDER BY distance
        LIMIT 3
    ]], query_json)

    print("Results (closest to [0.3, 0.3, 0.3, 0.3]):")
    for row in db:nrows(sql) do
        print(string.format("  rowid=%d distance=%.6f", row.rowid, row.distance))
    end

    -- Demonstrate binary serialization
    print("\nTesting binary serialization...")
    db:exec("CREATE VIRTUAL TABLE vec_binary USING vec0(embedding float[4])")

    local stmt = db:prepare("INSERT INTO vec_binary(rowid, embedding) VALUES (1, ?)")
    stmt:bind_blob(1, sqlite_vec.serialize_f32({1.0, 2.0, 3.0, 4.0}))
    stmt:step()
    stmt:finalize()

    stmt = db:prepare("SELECT rowid, distance FROM vec_binary WHERE embedding MATCH ? LIMIT 1")
    stmt:bind_blob(1, sqlite_vec.serialize_f32({1.0, 2.0, 3.0, 4.0}))
    for row in stmt:nrows() do
        print(string.format("  Binary round-trip: rowid=%d distance=%.6f", row.rowid, row.distance))
    end
    stmt:finalize()

    db:close()
    print("\nDemo completed successfully")
end

-- Run with error handling
local success, err = pcall(main)
if not success then
    print("Error: " .. tostring(err))
    os.exit(1)
end
