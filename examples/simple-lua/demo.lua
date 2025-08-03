#!/usr/bin/env lua

-- Simple Lua example demonstrating sqlite-vec usage
-- This example shows how to create vector tables, insert data, and perform KNN queries

local sqlite3 = require("lsqlite3")

-- You can either:
-- 1. Copy sqlite_vec.lua from ../../bindings/lua/sqlite_vec.lua to this directory
-- 2. Or modify the path below to point to the bindings directory
local sqlite_vec = require("sqlite_vec")

function main()
    print("=== SQLite-Vec Simple Lua Example ===")
    
    -- Create in-memory database
    local db = sqlite3.open_memory()
    if not db then
        error("Failed to create database")
    end
    
    -- Load sqlite-vec extension
    local load_success, load_error = pcall(function()
        sqlite_vec.load(db)
    end)
    
    if not load_success then
        error("Failed to load sqlite-vec extension: " .. tostring(load_error))
    end
    
    -- Check versions - handle the case where vec_version() might not be available
    local sqlite_version = nil
    local vec_version = nil
    
    for row in db:nrows("SELECT sqlite_version()") do
        sqlite_version = row.sqlite_version
        break
    end
    
    -- Try to get vec_version, but don't fail if it's not available
    local success, _ = pcall(function()
        for row in db:nrows("SELECT vec_version()") do
            vec_version = row.vec_version
            break
        end
    end)
    
    if sqlite_version then
        if vec_version then
            print(string.format("sqlite_version=%s, vec_version=%s", sqlite_version, vec_version))
        else
            print(string.format("sqlite_version=%s, vec_version=unknown", sqlite_version))
        end
    end
    
    -- Verify extension is loaded by checking for vec0 module
    local vec0_available = false
    for row in db:nrows("SELECT name FROM pragma_module_list() WHERE name='vec0'") do
        vec0_available = true
        break
    end
    
    if vec0_available then
        print("✓ sqlite-vec extension loaded successfully")
    else
        error("sqlite-vec extension loaded but vec0 module not found")
    end
    
    -- Test data - same as other examples for consistency
    local items = {
        {1, {0.1, 0.1, 0.1, 0.1}},
        {2, {0.2, 0.2, 0.2, 0.2}},
        {3, {0.3, 0.3, 0.3, 0.3}},
        {4, {0.4, 0.4, 0.4, 0.4}},
        {5, {0.5, 0.5, 0.5, 0.5}},
    }
    local query = {0.3, 0.3, 0.3, 0.3}
    
    -- Create virtual table
    local create_result = db:exec("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[4])")
    if create_result ~= sqlite3.OK then
        error("Failed to create virtual table: " .. db:errmsg())
    end
    
    -- Insert data using JSON format (more compatible)
    print("Inserting vector data...")
    db:exec("BEGIN")
    
    for _, item in ipairs(items) do
        local rowid = math.floor(item[1])
        local vector_json = sqlite_vec.serialize_json(item[2])
        
        local sql = string.format("INSERT INTO vec_items(rowid, embedding) VALUES (%d, '%s')", 
                                 rowid, vector_json)
        local result = db:exec(sql)
        if result ~= sqlite3.OK then
            error("Failed to insert item: " .. db:errmsg())
        end
    end
    
    db:exec("COMMIT")
    
    -- Verify data was inserted
    local count = 0
    for row in db:nrows("SELECT COUNT(*) as count FROM vec_items") do
        count = row.count
        break
    end
    print(string.format("✓ Inserted %d vector records", count))
    
    -- Perform KNN query using JSON format
    print("Executing KNN query...")
    local query_json = sqlite_vec.serialize_json(query)
    
    local sql = string.format([[
        SELECT
            rowid,
            distance
        FROM vec_items
        WHERE embedding MATCH '%s'
        ORDER BY distance
        LIMIT 3
    ]], query_json)
    
    print("Results:")
    for row in db:nrows(sql) do
        print(string.format("rowid=%d distance=%f", row.rowid, row.distance))
    end
    
    db:close()
    print("✓ Demo completed successfully")
end

-- Run the demo with error handling
local success, error_msg = pcall(main)
if not success then
    print("Error: " .. tostring(error_msg))
    os.exit(1)
end 
