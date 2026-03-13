-- sqlite_vec.lua Lua 5.1 compatible version with JSON fallback
local sqlite3 = require("lsqlite3")

local M = {}

-- Function to load extension
function M.load(db)
    local possible_paths = {
        "../../sqlite-vec.so",      -- Linux
        "../../sqlite-vec.dll",     -- Windows  
        "../../sqlite-vec.dylib",   -- macOS
        "./sqlite-vec.so",
        "./sqlite-vec.dll",
        "./sqlite-vec.dylib",
        "../sqlite-vec.so",
        "../sqlite-vec.dll", 
        "../sqlite-vec.dylib",
        "sqlite-vec",
    }
    
    local entry_point = "sqlite3_vec_init"
    
    if db.enable_load_extension then
        db:enable_load_extension(true)
        for _, path in ipairs(possible_paths) do
            local ok, result = pcall(function()
                return db:load_extension(path, entry_point)
            end)
            if ok then
                db:enable_load_extension(false)
                return result
            end
        end
        db:enable_load_extension(false)
        error("Failed to load extension from all paths")
    else
        for _, path in ipairs(possible_paths) do
            local ok, result = pcall(function()
                return db:load_extension(path, entry_point)
            end)
            if ok then
                return result
            else
                local ok2, result2 = pcall(function()
                    return db:load_extension(path)
                end)
                if ok2 then
                    return result2
                end
            end
        end
        error("Failed to load extension from all paths")
    end
end

-- Lua 5.1 compatible float to binary conversion function
local function float_to_bytes(f)
    if f == 0 then
        return string.char(0, 0, 0, 0)
    end
    
    local sign = 0
    if f < 0 then
        sign = 1
        f = -f
    end
    
    local mantissa, exponent = math.frexp(f)
    exponent = exponent - 1
    
    if exponent < -126 then
        mantissa = mantissa * 2^(exponent + 126)
        exponent = -127
    else
        mantissa = (mantissa - 0.5) * 2
    end
    
    exponent = exponent + 127
    mantissa = math.floor(mantissa * 2^23 + 0.5)
    
    local bytes = {}
    bytes[1] = mantissa % 256; mantissa = math.floor(mantissa / 256)
    bytes[2] = mantissa % 256; mantissa = math.floor(mantissa / 256)
    bytes[3] = mantissa % 256 + (exponent % 2) * 128; exponent = math.floor(exponent / 2)
    bytes[4] = exponent % 128 + sign * 128
    
    return string.char(bytes[1], bytes[2], bytes[3], bytes[4])
end

-- Helper function: serialize float vector to binary format (Lua 5.1 compatible)
function M.serialize_f32(vector)
    local buffer = {}
    
    if string.pack then
        for _, v in ipairs(vector) do
            table.insert(buffer, string.pack("f", v))
        end
    else
        for _, v in ipairs(vector) do
            table.insert(buffer, float_to_bytes(v))
        end
    end
    
    return table.concat(buffer)
end

-- New: JSON format vector serialization (more reliable fallback)
function M.serialize_json(vector)
    local values = {}
    for _, v in ipairs(vector) do
        table.insert(values, tostring(v))
    end
    return "[" .. table.concat(values, ",") .. "]"
end

return M 
