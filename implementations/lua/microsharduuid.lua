--- MicroShard UUID (Lua Implementation)
-- A zero-lookup, partition-aware UUIDv8 generator compliant with RFC 9562.
--
-- Features:
-- * Zero-Lookup: Extract Shard/Tenant ID directly from the UUID.
-- * Environment Aware: Auto-detects Nginx (OpenResty) and Redis for high-precision time/randomness.
-- * 32-bit Optimization: Uses 4x 32-bit integers to bypass Lua 5.1 floating-point precision limits.
--
-- @module microsharduuid
-- @author Dilip Vamsi
-- @license MIT
-- @version 1.0.0

local M = {}

-- ====================================================================
-- SECTION 1: POLYFILL IMPLEMENTATION (Pure Lua)
-- ====================================================================
-- These functions provide bitwise operations for vanilla Lua 5.1 environments
-- where the C-based 'bit' library is missing. They are exposed via M.mathbitops
-- for testing purposes but are used internally only as a fallback.

local mathbitops = {}

--- Bitwise Left Shift (Polyfill)
function mathbitops.lshift(a, b)
    return math.floor(a * (2 ^ b)) % 4294967296
end

--- Bitwise Right Shift (Polyfill)
function mathbitops.rshift(a, b)
    return math.floor(a / (2 ^ b))
end

--- Convert number to Hex String (Polyfill)
-- Handles negative numbers (Lua 5.1 doubles) by converting to unsigned 32-bit space.
function mathbitops.tohex(x, n)
    if x < 0 then x = x + 4294967296 end
    return string.format("%0" .. (n or 8) .. "x", x)
end

--- Bitwise AND (Polyfill)
function mathbitops.band(a, b)
    local res, p = 0, 1
    for i = 0, 31 do
        local ra, rb = a % 2, b % 2
        a, b = math.floor(a / 2), math.floor(b / 2)
        if ra + rb == 2 then res = res + p end
        p = p * 2
    end
    return res
end

--- Bitwise OR (Polyfill)
function mathbitops.bor(a, b)
    local res, p = 0, 1
    for i = 0, 31 do
        local ra, rb = a % 2, b % 2
        a, b = math.floor(a / 2), math.floor(b / 2)
        if ra + rb > 0 then res = res + p end
        p = p * 2
    end
    return res
end

-- Expose polyfills for unit testing
M.mathbitops = mathbitops

-- ====================================================================
-- SECTION 2: LIBRARY DETECTION & CONSTANTS
-- ====================================================================

-- Detect best available bit library
-- Priority: global 'bit' (LuaJIT/Redis) -> 'bit32' (Lua 5.2+) -> mathbitops (Fallback)
local bit_lib = _G.bit or (package.loaded["bit"] or require("bit"))

if not bit_lib then
    local ok, b32 = pcall(require, "bit32")
    if ok then
        bit_lib = b32
    else
        bit_lib = mathbitops
    end
end

-- Map functions for local access (Performance optimization)
local band, bor = bit_lib.band, bit_lib.bor
local lshift, rshift = bit_lib.lshift, bit_lib.rshift
local tohex = bit_lib.tohex

-- UUID Layout Constants
local MAX_SHARD_ID = 4294967295 -- 2^32 - 1
local VERSION = 8               -- UUIDv8
local VARIANT = 2               -- RFC 9562 Variant 2

-- ====================================================================
-- SECTION 3: INTERNAL HELPERS
-- ====================================================================

--- Generate 36 bits of randomness.
-- Auto-detects environment to provide the most secure seed available.
-- @return number high_4 (0-15)
-- @return number low_32 (0-4294967295)
local function get_random_parts()
    -- 1. Nginx / OpenResty (Cryptographically Secure)
    if _G.ngx and _G.require then
        local ok, resty_random = pcall(require, "resty.random")
        if ok then
            local bytes = resty_random.bytes(5)
            local b1, b2, b3, b4, b5 = string.byte(bytes, 1, 5)
            local hi = band(b1, 0xF)
            local lo = bor(lshift(b2, 24), lshift(b3, 16), lshift(b4, 8), b5)
            return hi, lo
        end
    end

    -- 2. Redis (Seeded via monotonic time)
    if _G.redis and _G.redis.call then
        local t = _G.redis.call('TIME')
        math.randomseed(tonumber(t[2]))
    end

    -- 3. Standard Lua (Fallback seeding)
    if not package.loaded["microshard_seeded"] then
        local seed = os.time()
        local ok, socket = pcall(require, "socket")
        if ok then seed = seed + (socket.gettime() * 10000) end
        -- Mix in table address for process uniqueness
        seed = seed + (tonumber(tostring({}):match("0x%x+")) or 0)
        math.randomseed(seed)
        package.loaded["microshard_seeded"] = true
    end

    local r1 = math.random(0, 0xFFFF)
    local r2 = math.random(0, 0xFFFF)
    return math.random(0, 15), bor(lshift(r1, 16), r2)
end

--- Construct the 4x32-bit UUID structure.
-- @param shard_id number 32-bit integer
-- @param micros number Timestamp in microseconds
-- @return table {h1, h2, l1, l2}
local function build_uuid(shard_id, micros)
    local rnd_hi, rnd_lo = get_random_parts()

    -- Split Time (at 2^22) to fit into 32-bit operations
    local divisor = 4194304
    local time_h = math.floor(micros / divisor)
    local time_rem = micros % divisor

    -- Shard Splits
    local shard_high = rshift(shard_id, 26)
    local shard_low = band(shard_id, 0x3FFFFFF)

    -- High 64 Bits
    local h1 = time_h
    local h2 = bor(
        lshift(rshift(time_rem, 6), 16),      -- Time Mid (16 bits)
        lshift(VERSION, 12),                  -- Version (4 bits)
        lshift(band(time_rem, 0x3F), 6),      -- Time Low (6 bits)
        shard_high                            -- Shard High (6 bits)
    )

    -- Low 64 Bits
    local l1 = bor(
        lshift(VARIANT, 30),                  -- Variant (2 bits)
        lshift(shard_low, 4),                 -- Shard Low (26 bits)
        band(rnd_hi, 0xF)                     -- Random High (4 bits)
    )
    local l2 = rnd_lo                         -- Random Low (32 bits)

    return { h1 = h1, h2 = h2, l1 = l1, l2 = l2 }
end

-- ====================================================================
-- SECTION 4: PUBLIC API
-- ====================================================================

--- Create a new MicroShard UUID using the current system time.
-- Automatically detects Nginx or Redis environments for high-precision timing.
-- @param shard_id number (0 - 4294967295) The partition/tenant ID.
-- @return table|nil UUID struct or nil on error.
-- @return string|nil Error message.
function M.new(shard_id)
    if not shard_id or shard_id < 0 or shard_id > MAX_SHARD_ID then
        return nil, "invalid shard id"
    end

    local micros
    if _G.ngx and _G.ngx.now then
        micros = _G.ngx.now() * 1000000
    elseif _G.redis and _G.redis.call then
        local t = _G.redis.call('TIME')
        micros = (tonumber(t[1]) * 1000000) + tonumber(t[2])
    else
        micros = os.time() * 1000000
    end

    return build_uuid(shard_id, micros)
end

--- Create a new UUID for a specific time (Backfilling).
-- @param shard_id number (0 - 4294967295)
-- @param micros number Timestamp in microseconds (e.g., 1672531200000000).
-- @return table|nil UUID struct or nil on error.
function M.new_at(shard_id, micros)
    if not shard_id or shard_id < 0 or shard_id > MAX_SHARD_ID then
        return nil, "invalid shard id"
    end
    if not micros or micros < 0 then return nil, "invalid micros" end
    return build_uuid(shard_id, micros)
end

--- Convert a UUID struct to a canonical string.
-- Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
-- @param u table The 4x32 UUID struct.
-- @return string|nil Canonical UUID string.
function M.tostring(u)
    if not u then return nil end
    return string.format("%s-%04s-%04s-%04s-%04s%s",
        tohex(u.h1, 8),
        tohex(rshift(u.h2, 16), 4),
        tohex(band(u.h2, 0xFFFF), 4),
        tohex(rshift(u.l1, 16), 4),
        tohex(band(u.l1, 0xFFFF), 4),
        tohex(u.l2, 8)
    )
end

--- Parse a UUID string into a MicroShard struct.
-- Validates hex characters, length, Version 8, and Variant 2.
-- @param uuid_str string The UUID string.
-- @return table|nil UUID struct or nil on error.
-- @return string|nil Error message.
function M.parse(uuid_str)
    if not uuid_str then return nil, "nil input" end
    local clean = string.gsub(uuid_str, "-", "")
    if #clean ~= 32 then return nil, "invalid length" end

    -- Parse 4 chunks of 8 hex characters
    local h1 = tonumber(string.sub(clean, 1, 8), 16)
    local h2 = tonumber(string.sub(clean, 9, 16), 16)
    local l1 = tonumber(string.sub(clean, 17, 24), 16)
    local l2 = tonumber(string.sub(clean, 25, 32), 16)

    if not (h1 and h2 and l1 and l2) then return nil, "invalid hex" end

    -- Validate Version (Bits 48-51)
    if band(rshift(h2, 12), 0xF) ~= VERSION then return nil, "invalid version" end
    -- Validate Variant (Bits 64-65)
    if band(rshift(l1, 30), 0x3) ~= VARIANT then return nil, "invalid variant" end

    return { h1 = h1, h2 = h2, l1 = l1, l2 = l2 }
end

--- Extract the Shard ID from a UUID struct.
-- @param u table The UUID struct.
-- @return number The 32-bit Shard ID.
function M.shard_id(u)
    if not u then return nil end
    local shard_high = band(u.h2, 0x3F)
    local shard_low = band(rshift(u.l1, 4), 0x3FFFFFF)

    local res = bor(lshift(shard_high, 26), shard_low)

    -- Normalize LuaJIT signed 32-bit int to unsigned Lua double
    if res < 0 then res = res + 4294967296 end
    return res
end

--- Decompose the timestamp into safe Seconds and Microseconds.
-- This bypasses Lua 5.1's 53-bit precision limit for timestamps > Year 2255.
-- @param u table The UUID struct.
-- @return number Seconds (integer)
-- @return number Microseconds (integer, 0-999999)
function M.get_time_parts(u)
    if not u then return nil end
    local time_h = u.h1

    -- Reconstruct Remainder (22 bits)
    local time_mid = rshift(u.h2, 16)
    local time_low = band(rshift(u.h2, 6), 0x3F)
    local time_rem = bor(lshift(time_mid, 6), time_low)

    -- Algebra: Time = h1 * 2^22 + rem
    -- 2^22 = 4,194,304 = (4 * 1,000,000) + 194,304
    local total_remainder = (time_h * 194304) + time_rem
    local extra_seconds = math.floor(total_remainder / 1000000)
    local final_micros = total_remainder % 1000000
    local final_seconds = (time_h * 4) + extra_seconds

    return final_seconds, final_micros
end

--- Extract the total timestamp in microseconds.
-- Warning: May lose precision if date > Year 2255 due to Lua doubles.
-- Use get_time_parts() for future-proof safety.
-- @param u table The UUID struct.
-- @return number Timestamp in microseconds.
function M.get_micros(u)
    if not u then return nil end
    local sec, mic = M.get_time_parts(u)
    return (sec * 1000000) + mic
end

return M
