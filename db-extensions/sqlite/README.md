# MicroShard UUID - SQLite Extension

**A native high-performance C extension for SQLite.**

This extension allows SQLite to generate, parse, and manipulate 128-bit MicroShard UUIDs directly as `BLOB`s (16 bytes).

### Why use this?
1.  **Storage Efficiency:** Stores UUIDs as 16 bytes (BLOB) instead of 36 bytes (TEXT), reducing database size by **~55%**.
2.  **Performance:** C-level generation is significantly faster than Python/Node.js application-level generation.
3.  **Time-Sorting:** The binary layout of MicroShard UUIDs allows `ORDER BY id` to work chronologically without parsing.
4.  **Zero-Lookup:** You can extract the Shard ID or Timestamp inside SQL queries without joining extra tables.

---

## 📂 Requirements

To compile this extension, you need two files in the same directory:
1.  `sqlite_extension.c` (The extension wrapper)
2.  `microshard_uuid.h` (The core C library)

---

## 🛠 Compilation

You need `gcc` (Linux/macOS) or MinGW (Windows).

### Linux
```bash
gcc -g -O2 -fPIC -shared -I../../implementations/c_cpp sqlite_extension.c -o microshard_uuid.so
```

### macOS
```bash
gcc -g -O2 -fPIC -dynamiclib -I../../implementations/c_cpp sqlite_extension.c -o microshard_uuid.dylib
```

### Windows (MinGW)
```bash
gcc -g -O2 -shared -I../../implementations/c_cpp sqlite_extension.c -o microshard_uuid.dll
```

---

## 📋 SQL API Reference

Once loaded, the following functions are available:

### Generators
| Function | Arguments | Return | Description |
| :--- | :--- | :--- | :--- |
| `microshard_uuid_generate` | `shard_id` (int) | `BLOB` | Generates a 16-byte binary UUID using system time. |
| `microshard_uuid_generate_text`| `shard_id` (int) | `TEXT` | Generates a 36-char UUID string using system time. |
| `microshard_uuid_from_iso` | `iso_str`, `shard_id` | `BLOB` | Generates UUID from ISO string (e.g. `'2023-01-01T12:00:00Z'`). |
| `microshard_uuid_from_micros` | `micros`, `shard_id` | `BLOB` | Generates UUID from Unix Microseconds integer. |

### Converters
| Function | Arguments | Return | Description |
| :--- | :--- | :--- | :--- |
| `microshard_uuid_to_string` | `uuid` (blob) | `TEXT` | Converts binary BLOB to standard string (`018e...`). |
| `microshard_uuid_from_string` | `uuid` (text) | `BLOB` | Converts standard string to binary BLOB. |

### Extractors
| Function | Arguments | Return | Description |
| :--- | :--- | :--- | :--- |
| `microshard_uuid_get_shard_id` | `uuid` (blob) | `INT` | Extracts the 32-bit Shard ID. |
| `microshard_uuid_get_time` | `uuid` (blob) | `INT` | Extracts creation time as Unix Microseconds. |
| `microshard_uuid_get_iso` | `uuid` (blob) | `TEXT` | Extracts creation time as ISO 8601 string. |
| `microshard_uuid_validate_iso`| `iso_str` (text) | `INT` | Returns `1` if string is valid ISO 8601, `0` otherwise. |

---

## 🚀 Advanced Usage: Virtual Columns (Zero-Lookup)

The most powerful feature of this extension is using **SQLite Generated Columns** (available in SQLite 3.31+). This allows you to query by Shard ID without storing it in a separate column.

```sql
CREATE TABLE users (
    id BLOB PRIMARY KEY,
    username TEXT,
    -- Automatically extracts Shard ID from the BLOB virtually
    -- Takes 0 extra storage space!
    shard_id INTEGER GENERATED ALWAYS AS (microshard_uuid_get_shard_id(id)) VIRTUAL
);

-- Insert (Generate ID for Shard 101)
INSERT INTO users (id, username) VALUES (microshard_uuid_generate(101), 'alice');

-- Query (Zero-Lookup Routing)
-- SQLite optimizes this to extract the shard directly from the key
SELECT * FROM users WHERE shard_id = 101;
```

---

## 🐍 Loading in Python

Python's `sqlite3` requires explicit permission to load extensions.

```python
import sqlite3
import sys
import os

# Path to compiled extension
ext_file = "microshard_uuid.dll" if sys.platform == "win32" else "microshard_uuid.so"
ext_path = os.path.abspath(ext_file)

conn = sqlite3.connect(":memory:")

# 1. Enable Extension Loading (Critical)
conn.enable_load_extension(True)

# 2. Load
try:
    conn.load_extension(ext_path)
    print("✅ Extension loaded")
except Exception as e:
    print(f"❌ Load failed: {e}")

# 3. Usage
row = conn.execute("SELECT microshard_uuid_generate_text(55)").fetchone()
print(f"UUID: {row[0]}")
```

---

## 📦 Loading in Node.js

We recommend **`better-sqlite3`** for native extension support.

```javascript
const Database = require('better-sqlite3');
const path = require('path');

const db = new Database(':memory:');

// Point to the compiled .so/.dll file (extension optional in path)
const extPath = path.join(__dirname, 'microshard_uuid');

try {
    db.loadExtension(extPath);
    console.log("✅ Extension loaded");
} catch (err) {
    console.error("❌ Error:", err);
}

const row = db.prepare("SELECT microshard_uuid_generate_text(101) as uuid").get();
console.log(row.uuid);
```

---

## 🖥️ Usage in SQLite CLI

```sql
-- Load the extension
.load ./microshard_uuid

-- Create table
CREATE TABLE events (
    id BLOB PRIMARY KEY,
    data TEXT
);

-- Insert using backfilling (Old timestamp)
INSERT INTO events (id, data)
VALUES (microshard_uuid_from_iso('2023-01-01T00:00:00Z', 1), 'Old Log');

-- Insert current time
INSERT INTO events (id, data)
VALUES (microshard_uuid_generate(1), 'New Log');

-- Query sorted chronologically
SELECT
    microshard_uuid_to_string(id) as uuid,
    microshard_uuid_get_iso(id) as time,
    data
FROM events
ORDER BY id ASC;
```
