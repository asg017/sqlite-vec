-- demo_final.lua - Final clean version
local sqlite3 = require("lsqlite3")
local sqlite_vec = require("sqlite_vec")

print("=== SQLite-Vec Lua Demo ===")

-- Create in-memory database and load extension
local db = sqlite3.open_memory()
sqlite_vec.load(db)

-- Create vector table
db:exec("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[4])")

-- Prepare test data
local items = {
    {1, {0.1, 0.1, 0.1, 0.1}},
    {2, {0.2, 0.2, 0.2, 0.2}},
    {3, {0.3, 0.3, 0.3, 0.3}},
    {4, {0.4, 0.4, 0.4, 0.4}},
    {5, {0.5, 0.5, 0.5, 0.5}}
}

-- Insert data (using JSON format for reliability)
print("Inserting vector data...")
db:exec("BEGIN")
for _, item in ipairs(items) do
    local json_vector = sqlite_vec.serialize_json(item[2])
    local sql = string.format("INSERT INTO vec_items(rowid, embedding) VALUES (%d, '%s')", 
                             item[1], json_vector)
    db:exec(sql)
end
db:exec("COMMIT")

-- Execute K-nearest neighbors query
print("\nExecuting KNN query...")
local query = {0.3, 0.3, 0.3, 0.3}
local query_json = sqlite_vec.serialize_json(query)

print("Query vector:", query_json)
print("Query results:")

local sql = string.format([[
    SELECT rowid, distance
    FROM vec_items
    WHERE embedding MATCH '%s'
    ORDER BY distance
    LIMIT 3
]], query_json)

for row in db:nrows(sql) do
    print(string.format("  rowid=%s  distance=%.6f", row.rowid, row.distance))
end

db:close()
print("\n✓ Demo completed") 
