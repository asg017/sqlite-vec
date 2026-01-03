-- sqlite_vec.lua Lua 5.1 compatible version with JSON fallback
local sqlite3 = require("lsqlite3")

local M = {}

-- Function to load extension
function M.load(db)
    local possible_paths = {
        -- vec0 naming (this fork)
        "../../dist/vec0.so",       -- Linux
        "../../dist/vec0.dll",      -- Windows
        "../../dist/vec0.dylib",    -- macOS
        "./dist/vec0.so",
        "./dist/vec0.dll",
        "./dist/vec0.dylib",
        "../dist/vec0.so",
        "../dist/vec0.dll",
        "../dist/vec0.dylib",
        "vec0",
        -- sqlite-vec naming (upstream)
        "../../sqlite-vec.so",
        "../../sqlite-vec.dll",
        "../../sqlite-vec.dylib",
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
            -- lsqlite3 load_extension returns true on success
            if ok and result then
                db:enable_load_extension(false)
                return true
            end
        end
        db:enable_load_extension(false)
        error("Failed to load extension from all paths")
    else
        for _, path in ipairs(possible_paths) do
            local ok, result = pcall(function()
                return db:load_extension(path, entry_point)
            end)
            -- lsqlite3 load_extension returns true on success
            if ok and result then
                return true
            end
        end
        error("Failed to load extension from all paths")
    end
end

-- Lua 5.1 compatible float to binary conversion function (IEEE 754 single precision, little-endian)
local function float_to_bytes(f)
    -- Handle special cases: NaN, Inf, -Inf, -0.0
    if f ~= f then
        -- NaN: exponent=255, mantissa!=0, sign=0 (quiet NaN)
        return string.char(0, 0, 192, 127)
    elseif f == math.huge then
        -- +Inf: exponent=255, mantissa=0, sign=0
        return string.char(0, 0, 128, 127)
    elseif f == -math.huge then
        -- -Inf: exponent=255, mantissa=0, sign=1
        return string.char(0, 0, 128, 255)
    elseif f == 0 then
        -- Check for -0.0 vs +0.0
        if 1/f == -math.huge then
            -- -0.0: sign=1, exponent=0, mantissa=0
            return string.char(0, 0, 0, 128)
        else
            -- +0.0
            return string.char(0, 0, 0, 0)
        end
    end

    local sign = 0
    if f < 0 then
        sign = 1
        f = -f
    end

    local mantissa, exponent = math.frexp(f)
    -- math.frexp returns mantissa in [0.5, 1), we need [1, 2) for IEEE 754
    exponent = exponent - 1

    local is_subnormal = exponent < -126
    if is_subnormal then
        -- Subnormal number: exponent field is 0, mantissa is denormalized
        -- Formula: mantissa_stored = value * 2^149 = m * 2^(e + 149)
        -- Since exponent = e - 1, we need: m * 2^(exponent + 1 + 149) = m * 2^(exponent + 150)
        -- After multiplying by 2^23 later: m * 2^(exponent + 150) becomes the stored mantissa
        -- Simplified: mantissa = m * 2^(exponent + 127) before the 2^23 scaling
        mantissa = mantissa * 2^(exponent + 127)
        exponent = 0
    else
        -- Normal number: remove implicit leading 1
        -- frexp returns mantissa in [0.5, 1), convert to [0, 1) for IEEE 754
        mantissa = (mantissa - 0.5) * 2
        exponent = exponent + 127
    end

    -- Round half to even (banker's rounding) for IEEE 754 compliance
    local scaled = mantissa * 2^23
    local floor_val = math.floor(scaled)
    local frac = scaled - floor_val
    -- Use epsilon comparison for 0.5 to handle floating-point precision issues
    local is_half = math.abs(frac - 0.5) < 1e-9
    if frac > 0.5 + 1e-9 or (is_half and floor_val % 2 == 1) then
        mantissa = floor_val + 1
    else
        mantissa = floor_val
    end

    -- Handle mantissa overflow from rounding (mantissa >= 2^23)
    if mantissa >= 2^23 then
        if is_subnormal then
            -- Subnormal rounded up to smallest normal
            mantissa = 0
            exponent = 1
        else
            -- Normal number: carry into exponent
            mantissa = 0
            exponent = exponent + 1
        end
    end

    -- Handle exponent overflow -> Infinity
    if exponent >= 255 then
        -- Return ±Infinity
        if sign == 1 then
            return string.char(0, 0, 128, 255)  -- -Inf
        else
            return string.char(0, 0, 128, 127)  -- +Inf
        end
    end

    -- Encode as little-endian IEEE 754 single precision
    local bytes = {}
    bytes[1] = mantissa % 256
    mantissa = math.floor(mantissa / 256)
    bytes[2] = mantissa % 256
    mantissa = math.floor(mantissa / 256)
    bytes[3] = (mantissa % 128) + (exponent % 2) * 128
    exponent = math.floor(exponent / 2)
    bytes[4] = exponent + sign * 128

    return string.char(bytes[1], bytes[2], bytes[3], bytes[4])
end

-- Helper function: serialize float vector to binary format (little-endian IEEE 754)
function M.serialize_f32(vector)
    local buffer = {}

    if string.pack then
        -- Use "<f" for little-endian float (Lua 5.3+)
        for _, v in ipairs(vector) do
            table.insert(buffer, string.pack("<f", v))
        end
    else
        -- Lua 5.1/5.2 fallback
        for _, v in ipairs(vector) do
            table.insert(buffer, float_to_bytes(v))
        end
    end

    return table.concat(buffer)
end

-- JSON format vector serialization
-- Note: JSON does not support NaN, Inf, or -0.0, so these will error
function M.serialize_json(vector)
    local values = {}
    for i, v in ipairs(vector) do
        -- Check for NaN
        if v ~= v then
            error("serialize_json: NaN at index " .. i .. " is not valid JSON")
        end
        -- Check for Inf/-Inf
        if v == math.huge or v == -math.huge then
            error("serialize_json: Infinity at index " .. i .. " is not valid JSON")
        end
        -- Handle -0.0 (convert to 0.0 for JSON compatibility)
        if v == 0 and 1/v == -math.huge then
            v = 0.0
        end
        table.insert(values, tostring(v))
    end
    return "[" .. table.concat(values, ",") .. "]"
end

return M
