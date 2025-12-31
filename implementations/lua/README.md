# MicroShard UUID (Lua)

**A zero-lookup, partition-aware UUIDv8 implementation.**

`microshard-uuid` for Lua is a lightweight, dependency-free library compliant with IETF RFC 9562. Unlike standard UUIDs (v4/v7), MicroShard embeds a **32-bit Shard ID** directly into the ID.

This allows applications to determine the partition, tenant, or region of a record simply by parsing the ID, eliminating the need for lookup tables.

## 📦 Features

*   **Zero-Lookup Routing:** Extract Shard/Tenant IDs instantly from the UUID.
*   **Universal Compatibility:** Runs in **Nginx (OpenResty)**, **Redis (EVAL)**, and Standard Lua 5.1+.
*   **Microsecond Precision:** 54-bit timestamp ensures strict chronological sorting.
*   **Massive Scale:** Supports **4.29 Billion** unique Shards/Tenants.
*   **Native Optimization:** Uses a 4x32-bit integer structure to bypass Lua 5.1's floating-point precision limits.
*   **Environment Aware:** Automatically detects `ngx.now` or `redis.call` for high-precision timing.

---

## 🛠 Installation

MicroShard UUID is a single-file library.

1.  Download `microsharduuid.lua`.
2.  Place it in your package path (e.g., your project root or `lualib` folder).

```lua
local uuid = require("microsharduuid")
```

---

## 🚀 Usage

### 1. Basic Generation
Ideal for general usage. The library automatically finds the best time source.

```lua
local uuid = require("microsharduuid")

-- Generate an ID for Shard #101
-- Shard ID must be 0 - 4,294,967,295
local u = uuid.new(101)

if u then
    print("Generated: " .. uuid.tostring(u))
    -- Output: 018e65c9-3a10-0400-8000-a4f1d3b8e1a1
else
    print("Error generating UUID")
end
```

### 2. Parsing & Metadata Extraction
Extract the Shard ID and Timestamp without a database lookup.

```lua
local function inspect_uuid(uuid_str)
    -- Parse string into struct
    local u, err = uuid.parse(uuid_str)
    if not u then return print("Error: " .. err) end

    -- Extract Shard ID
    print("Origin Shard: " .. uuid.shard_id(u))

    -- Extract Time
    local sec, mic = uuid.get_time_parts(u)
    print("Time: " .. os.date("!%Y-%m-%dT%H:%M:%S", sec) .. "." .. mic .. "Z")
end
```

### 3. Usage in Redis (EVAL)
The library detects Redis and uses `redis.call('TIME')` to guarantee microsecond precision and monotonic ordering per script execution.

```lua
-- EVAL script
local uuid = require("microsharduuid")
local shard_id = tonumber(ARGV[1])

local u = uuid.new(shard_id)
local key = uuid.tostring(u)

redis.call('SET', key, "value")
return key
```

### 4. Backfilling (Explicit Time)
Generate UUIDs for past events.

```lua
local historic_time = 1672531200000000 -- Jan 1, 2023
local u = uuid.new_at(55, historic_time)
```

---

## 📚 API Reference

| Function | Description |
| :--- | :--- |
| `new(shard_id)` | Creates a UUID using system time. Returns struct or `nil, err`. |
| `new_at(shard_id, micros)` | Creates a UUID for a specific timestamp (microsecond precision). |
| `parse(string)` | Converts UUID string to struct. Validates Version 8 / Variant 2. |
| `tostring(struct)` | Converts struct to canonical string (`xxxxxxxx-xxxx...`). |
| `shard_id(struct)` | Extracts the 32-bit Shard ID from a struct. |
| `get_micros(struct)` | Returns total microseconds (Warning: Precision loss > Year 2255). |
| `get_time_parts(struct)` | Returns `seconds, microseconds`. Safe for all dates. |

---

## ⚡ Under the Hood: 4x32-bit Optimization

Lua 5.1 (Redis/LuaJIT) stores numbers as 64-bit doubles, which only have **53 bits** of integer precision. 
Additionally, standard bitwise libraries often only support **signed 32-bit** integers.

To handle a 128-bit UUID without data corruption, this library:
1.  **Splits the ID** into four 32-bit integers (`h1`, `h2`, `l1`, `l2`).
2.  **Splits the Timestamp** (54-bit) mathematically using algebraic division (`/ 2^22`) rather than bitwise shifts, preventing overflow.
3.  **Polyfills** missing bitwise operations with pure Lua arithmetic if the environment lacks a native `bit` library.

---

## 🧪 Testing

A comprehensive test suite is provided in `test_microshard.lua`. It verifies:
*   Bitwise logic integrity (Native vs Polyfill).
*   Boundary conditions (Max Shard ID, Max Time).
*   Mocked environments (Nginx/Redis).

```bash
# Run with LuaJIT (Recommended)
luajit test_microshard.lua

# Run with Standard Lua
lua test_microshard.lua
```
