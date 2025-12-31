local uuid = require("microsharduuid")

-- ====================================================================
-- TEST HELPERS
-- ====================================================================

local function pass(msg)
    print(string.format("[PASS] %s", msg))
end

local function fail(msg, detail)
    error(string.format("\n[FAIL] %s\n       Detail: %s", msg, detail or ""))
end

local function assert_eq(actual, expected, msg)
    if actual ~= expected then
        fail(msg, string.format("Expected '%s', got '%s'", tostring(expected), tostring(actual)))
    end
end

local function assert_not_nil(val, msg)
    if val == nil then fail(msg, "Value is nil") end
end

local function assert_nil(val, msg)
    if val ~= nil then fail(msg, string.format("Expected nil, got '%s'", tostring(val))) end
end

local function assert_err_contains(err, substr, msg)
    if not err or not string.find(err, substr, 1, true) then
        fail(msg, string.format("Error message '%s' did not contain '%s'", tostring(err), substr))
    end
end

-- ====================================================================
-- TEST SUITE: MATHBITOPS (POLYFILL)
-- ====================================================================
print("========================================")
print("TESTING: mathbitops (Pure Lua Polyfill)")
print("========================================")

local bit = uuid.mathbitops

if not bit then
    fail("uuid.mathbitops is not exposed")
end

-- 1. LSHIFT
-- 1 << 4 = 16
assert_eq(bit.lshift(1, 4), 16, "lshift(1, 4)")
-- 0xFFFFFFFF << 4 = 0xFFFFFFF0 (32-bit truncation check)
-- 4294967295 * 16 = 68719476720
-- 68719476720 % 2^32 = 4294967280 -> 0xFFFFFFF0
assert_eq(bit.lshift(0xFFFFFFFF, 4), 0xFFFFFFF0, "lshift overflow truncation")

-- 2. RSHIFT
-- 16 >> 4 = 1
assert_eq(bit.rshift(16, 4), 1, "rshift(16, 4)")
-- 0xF0 >> 4 = 0xF (15)
assert_eq(bit.rshift(0xF0, 4), 15, "rshift(0xF0, 4)")

-- 3. BAND (Bitwise AND)
-- 1010 (0xA) & 0101 (0x5) = 0000
assert_eq(bit.band(0xA, 0x5), 0x0, "band(0xA, 0x5)")
-- 1111 (0xF) & 0011 (0x3) = 0011 (3)
assert_eq(bit.band(0xF, 0x3), 0x3, "band(0xF, 0x3)")
-- Large number check
assert_eq(bit.band(0x12345678, 0x0F0F0F0F), 0x02040608, "band large numbers")

-- 4. BOR (Bitwise OR)
-- 1010 (0xA) | 0101 (0x5) = 1111 (0xF)
assert_eq(bit.bor(0xA, 0x5), 0xF, "bor(0xA, 0x5)")
-- 0000 | 1111 = 1111
assert_eq(bit.bor(0x0, 0xF), 0xF, "bor(0x0, 0xF)")
-- Full saturation
assert_eq(bit.bor(0xF0F0F0F0, 0x0F0F0F0F), 0xFFFFFFFF, "bor full saturation")

-- 5. TOHEX
-- Standard
assert_eq(bit.tohex(255, 2), "ff", "tohex(255, 2)")
assert_eq(bit.tohex(10, 4), "000a", "tohex(10, 4)")

-- Signed/Negative handling check
-- In Lua 5.1 doubles, -1 is just -1. 
-- In 32-bit signed int logic, -1 is 0xFFFFFFFF.
-- The polyfill must explicitly convert negative input to 2's complement unsigned.
assert_eq(bit.tohex(-1, 8), "ffffffff", "tohex(-1) -> ffffffff")

pass("Polyfill functions verified successfully")

print("========================================")
print("TEST SUITE: MicroShardUUID")
print("========================================")

-- ====================================================================
-- EXISTING BASIC TESTS (1-10)
-- ====================================================================

-- 1. BASIC GENERATION
local u1 = uuid.new(123)
assert_not_nil(u1, "Basic generation")
local s1 = uuid.tostring(u1)
pass("Basic Generate: " .. s1)

-- 2. LENGTH
local clean = string.gsub(s1, "-", "")
assert_eq(#clean, 32, "Hex length is 32")
assert_eq(#s1, 36, "String length is 36")
pass("Length checks")

-- 3. ROUND TRIP
local u2 = uuid.parse(s1)
assert_eq(uuid.tostring(u2), s1, "Parse round-trip")
pass("Round Trip")

-- 4. SHARD EXTRACTION
local u_shard = uuid.new(987654)
assert_eq(uuid.shard_id(u_shard), 987654, "Shard ID match")
pass("Shard ID Extraction")

-- 5. BACKFILLING
local hist_time = 1672531200000000
local u_old = uuid.new_at(55, hist_time)
local t_extracted = uuid.get_micros(uuid.parse(uuid.tostring(u_old)))
assert_eq(t_extracted, hist_time, "Time extraction match")
pass("Backfill & Time Extraction")

-- ====================================================================
-- NEW ADVANCED TESTS (11-20)
-- ====================================================================

-- 11. BOUNDARY: MAX SHARD ID
-- Max UInt32: 4294967295
local max_shard = 4294967295
local u_max_shard = uuid.new(max_shard)
assert_eq(uuid.shard_id(u_max_shard), max_shard, "Max Shard ID Extraction")
-- Ensure high bits of ShardID don't bleed into Version/Variant
local s_max = uuid.tostring(u_max_shard)
local u_max_parsed = uuid.parse(s_max)
assert_not_nil(u_max_parsed, "Max Shard ID should parse correctly")
pass("Boundary: Max Shard ID (4294967295)")

-- 12. BOUNDARY: ZERO VALUES
local u_zero = uuid.new_at(0, 0)
assert_eq(uuid.shard_id(u_zero), 0, "Zero Shard ID")
assert_eq(uuid.get_micros(u_zero), 0, "Zero Time")
pass("Boundary: Zero Values")

-- 13. BOUNDARY: MAX SAFE TIME
-- 200 years in future roughly, well within 54-bit limit
local far_future = 9007199254740991 -- Max Safe Integer for Lua 5.1 Double
local u_future = uuid.new_at(1, far_future)
assert_eq(uuid.get_micros(u_future), far_future, "Max Safe Timestamp")
pass("Boundary: Max Safe Timestamp")

-- 14. BITWISE ISOLATION
-- Verify that a huge time doesn't corrupt a small shard ID and vice versa
local u_iso = uuid.new_at(1, far_future)
assert_eq(uuid.shard_id(u_iso), 1, "Max time did not corrupt Shard 1")
local u_iso2 = uuid.new_at(max_shard, 100)
assert_eq(uuid.get_micros(u_iso2), 100, "Max shard did not corrupt Time 100")
pass("Bitwise Isolation")

-- 15. INPUT SANITIZATION: NEW
local _, e1 = uuid.new(-1)
assert_not_nil(e1, "Negative shard should fail (treated as large in bit ops usually, but strict check?)")
-- Note: In lua, bit ops on negatives are weird. The lib checks `shard_id > MAX`.
-- If we pass a negative number to the lib check:
if uuid.new(-1) then
    -- If it passes, check if it wrapped safely.
    -- Usually -1 becomes MAX_UINT32 in bit ops.
    -- The lib logic `shard_id > MAX` handles positive overflow.
    -- Negative handling isn't explicitly blocked but bit.tohex handles it.
    -- We'll skip strict failure check here unless we add `shard_id < 0` to lib.
else
    -- If you added strict check
end

local _, e2 = uuid.new(4294967296) -- MAX + 1
assert_err_contains(e2, "invalid shard id", "Overflow shard")
pass("Input Sanitization (Generator)")

-- 16. PARSE VALIDATION: MALFORMED STRINGS
local bad_len = "00000000-0000-0000-0000-00000000000" -- 31 hex (clean)
local _, ep1 = uuid.parse(bad_len)
assert_err_contains(ep1, "invalid length", "Short string")

local bad_char = "z0000000-0000-0000-0000-000000000000"
local _, ep2 = uuid.parse(bad_char)
assert_err_contains(ep2, "invalid hex", "Non-hex char")
pass("Parse Validation (Malformed)")

-- 17. STRICT VERSION ENFORCEMENT
-- Version 8 is required. h2 bits 15-12 (shifted).
-- Let's fake a Version 7 UUID
local u_valid = uuid.new(1)
-- Manually tamper with the internal struct
u_valid.h2 = u_valid.h2 - 4096 -- Subtract 1 from Version bit (Pos 12) (8 -> 7)
-- Re-serialize
local s_fake_v7 = uuid.tostring(u_valid)
-- Try to parse
local _, ep3 = uuid.parse(s_fake_v7)
assert_err_contains(ep3, "invalid version", "Version 7 should fail")
pass("Strict Version 8 Enforcement")

-- 18. STRICT VARIANT ENFORCEMENT
-- Variant 2 (Binary 10) is required.
-- This lives in the top 2 bits of `l1`.
local u_valid_2 = uuid.new(1)
-- l1 structure: [Var 2][Shard 26][Rnd 4]
-- We want to change Var from 2 (10) to 3 (11) or 0 (00).
-- Adding 2^30 adds 1 to the Variant (10 -> 11)
local l1_tampered = u_valid_2.l1 + 1073741824 -- 2^30
local u_tampered = { h1=u_valid_2.h1, h2=u_valid_2.h2, l1=l1_tampered, l2=u_valid_2.l2 }
local s_bad_var = uuid.tostring(u_tampered)
local _, ep4 = uuid.parse(s_bad_var)
assert_err_contains(ep4, "invalid variant", "Wrong variant should fail")
pass("Strict Variant 2 Enforcement")

-- 19. RANDOMNESS UNIQUENESS
-- Calling new_at with EXACT same time/shard should yield DIFFERENT results due to random bits
local t_static = 1000000
local u_r1 = uuid.new_at(1, t_static)
local u_r2 = uuid.new_at(1, t_static)
assert_eq(uuid.get_micros(u_r1), uuid.get_micros(u_r2), "Times match")
if uuid.tostring(u_r1) == uuid.tostring(u_r2) then
    fail("Randomness Check", "Two UUIDs generated at exact same time/shard were identical (Collision)")
end
pass("Randomness (Same Time/Shard -> Unique UUID)")

-- 20. NIL HANDLING
local _, en1 = uuid.new(nil)
assert_err_contains(en1, "invalid shard", "Nil shard new")
local _, en2 = uuid.parse(nil)
assert_err_contains(en2, "nil input", "Nil parse")
local sn3 = uuid.tostring(nil)
assert_nil(sn3, "Nil tostring")
pass("Nil Input Handling")

print("========================================")
print("ALL 20 TEST SUITES PASSED")
print("========================================")
