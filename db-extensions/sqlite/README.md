# MicroShard UUID - SQLite Extension

A native C extension for SQLite to generate and manipulate 128-bit MicroShard UUIDs as `BLOB`s (16 bytes).

Using this extension is **significantly faster** (~50x) than storing UUIDs as Text strings and enables native **time-sorting** of the BLOBs without parsing.

---

## 🛠 Compilation

You need `gcc` (Linux/macOS) or MinGW (Windows).

```bash
# Navigate to this directory
cd extensions/sqlite

# Compile
make
```

**Output:**
*   **Linux:** `microshard_uuid.so`
*   **macOS:** `microshard_uuid.dylib`
*   **Windows:** `microshard_uuid.dll`

---

## 📋 SQL API Reference

Once loaded, the following functions are available in SQL:

| Function | Arguments | Return | Description |
| :--- | :--- | :--- | :--- |
| `microshard_uuid_generate` | `shard_id` (int) | `BLOB` | Generates a UUID using the current system time. |
| `microshard_uuid_from_iso` | `iso_str` (text), `shard_id` (int) | `BLOB` | Generates UUID from ISO string (e.g. `'2025-12-12T10:00:00.123456Z'`). |
| `microshard_uuid_from_micros` | `micros` (int), `shard_id` (int) | `BLOB` | Generates UUID from Unix Microseconds integer. |
| `microshard_uuid_get_shard_id` | `uuid` (blob) | `INT` | Extracts the 32-bit Shard ID. |
| `microshard_uuid_get_time` | `uuid` (blob) | `INT` | Extracts creation time as Unix Microseconds. |
| `microshard_uuid_get_iso` | `uuid` (blob) | `TEXT` | Extracts creation time as ISO 8601 string. |
| `microshard_uuid_generate` | `shard_id` (int) | `BLOB` | Generates 16-byte binary UUID. |
| `microshard_uuid_to_string` | `uuid` (blob) | `TEXT` | Converts BLOB to standard string (`018e...`). |
| `microshard_uuid_from_string` | `uuid` (text) | `BLOB` | Converts UUID string (with or without dashes) to binary. |
---

## 🚀 Usage Scenarios

### 1. Lookup by String
Since your Primary Key is a `BLOB`, you cannot query it directly with a string like `WHERE id = '018e...'`. You must cast the input string to a BLOB first.

**Wrong (Will return 0 rows):**
```sql
SELECT * FROM users WHERE id = '018e65c9-3a10-0400-8000-a4f1d3b8e1a1';
```

**Correct (Fast):**
```sql
SELECT * FROM users 
WHERE id = microshard_uuid_from_string('018e65c9-3a10-0400-8000-a4f1d3b8e1a1');
```

## 🐍 Loading in Python

Python's `sqlite3` module supports extensions, but you **must** call `enable_load_extension(True)`.

```python
import sqlite3
import os
import sys

# 1. Determine extension path based on OS
if sys.platform == "win32":
    ext_path = "./microshard_uuid.dll"
elif sys.platform == "darwin":
    ext_path = "./microshard_uuid.dylib"
else:
    ext_path = "./microshard_uuid.so"

# 2. Connect
conn = sqlite3.connect(":memory:")

# 3. Enable Extensions (Critical Step)
conn.enable_load_extension(True)

try:
    # 4. Load
    conn.load_extension(ext_path)
    print("✅ Extension loaded successfully")
except sqlite3.OperationalError as e:
    print(f"❌ Failed to load: {e}")
    exit(1)

# 5. Usage
cursor = conn.execute("SELECT microshard_uuid_to_string(microshard_uuid_generate(100))")
print(f"UUID: {cursor.fetchone()[0]}")
```

> **Note:** If you get `AttributeError: 'sqlite3.Connection' object has no attribute 'enable_load_extension'`, your Python installation was compiled without extension support (common on default macOS Python). Install Python via `brew` or `pyenv`.

---

## 📦 Loading in Node.js

The standard `sqlite3` package has poor extension support. We recommend using **`better-sqlite3`**, which is faster and supports extensions natively.

```bash
npm install better-sqlite3
```

```javascript
const Database = require('better-sqlite3');
const path = require('path');

// 1. Connect
const db = new Database(':memory:');

// 2. Resolve Path (OS-dependent extension not strictly required for loadExtension, 
//    but good practice to point to the file explicitly)
const extPath = path.join(__dirname, 'microshard_uuid'); // It auto-detects .so/.dll/.dylib

// 3. Load
try {
    db.loadExtension(extPath);
    console.log("✅ Extension loaded");
} catch (err) {
    console.error("❌ Failed to load extension:", err);
    process.exit(1);
}

// 4. Usage
const row = db.prepare(`
    SELECT 
        microshard_uuid_to_string(microshard_uuid_generate(55)) as uuid,
        microshard_uuid_get_iso(microshard_uuid_generate(55)) as time_iso
`).get();

console.log(row);
// Output: { id_hex: '018E...', time_iso: '2025-12-12T...' }
```

---

## 🖥️ Usage in SQLite CLI

```sql
-- Load the file (omit extension to auto-detect)
.load ./microshard_uuid

-- Create a table with an optimized BLOB Primary Key
CREATE TABLE events (
    id BLOB PRIMARY KEY DEFAULT (microshard_uuid_generate(1)),
    payload TEXT
);

-- Insert
INSERT INTO events (payload) VALUES ('User Click');

-- Insert with Backfilling (Past Time)
INSERT INTO events (id, payload) 
VALUES (microshard_uuid_from_iso('2023-01-01T12:00:00.000000Z', 1), 'Old Event');

-- Query (Sorted by Time automatically)
SELECT 
    microshard_uuid_to_string(id) as uuid, 
    microshard_uuid_get_iso(id) as timestamp, 
    payload 
FROM events;
```
