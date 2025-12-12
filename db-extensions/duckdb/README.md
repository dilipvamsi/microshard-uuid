# MicroShard UUID - DuckDB Macros

A portable SQL implementation of MicroShard UUIDv8 for DuckDB.
Works in **DuckDB CLI**, **Python**, **Node.js**, **WASM**, and **MotherDuck**.

DuckDB is an in-process OLAP database (like SQLite but columnar). It has native support for `HUGEINT` (128-bit integers) and `UUID` types.

Because DuckDB is often used with **MotherDuck** (Managed Service) or in-browser via WASM, you should **not** build a C++ extension. Instead, use **SQL Macros**. They are portable, zero-dependency, and execute at native C++ speed due to DuckDB's vectorized engine.

---

## 🚀 Installation

Since these are macros, you just run the SQL file once per connection.

```python
import duckdb

con = duckdb.connect()

# Load the macros
with open('db-extensions/duckdb/microshard_uuid.sql', 'r') as f:
    con.execute(f.read())
```

---

## ⚡ Usage: Standard UUIDs (Easiest)

Use these macros if you want standard `UUID` types that look like strings (`018e...`).

### 1. Generating Data
```sql
CREATE TABLE events (
    id UUID,
    event_name TEXT
);

-- Insert a single row for Shard #100
INSERT INTO events VALUES (microshard_generate(100), 'Login');

-- Bulk Insert (1 Million rows) for Shard #1
INSERT INTO events
SELECT microshard_generate(1), 'Bulk Event'
FROM range(1000000);
```

### 2. Zero-Lookup Routing (Extraction)
Extract the Shard ID directly from the UUID.

```sql
SELECT
    id,
    microshard_get_shard_id(id) as shard_id,
    microshard_get_timestamp(id) as created_at
FROM events
LIMIT 5;
```

---

## 🚀 Usage: High-Performance INT128 (Best for Analytics)

DuckDB can store 128-bit IDs as raw `HUGEINT` numbers. This avoids string parsing overhead, making extraction/routing **~50x faster**.

### 1. Generating Raw Integers
```sql
CREATE TABLE analytics (
    id HUGEINT, -- Raw 128-bit integer
    metric DOUBLE
);

-- Uses pure CPU math (No string formatting)
INSERT INTO analytics
SELECT microshard_generate_int(100), random()
FROM range(1000000);
```

### 2. Fast Filtering
Filtering by Shard ID using `_int` macros is instant because it uses bitwise operations instead of text casting.

```sql
SELECT count(*)
FROM analytics
WHERE microshard_get_shard_id_int(id) = 100;
```

### 3. Converters (Hybrid Strategy)
Store as `HUGEINT` for speed, but convert to `UUID` for display.

```sql
SELECT
    microshard_int_to_uuid(id) as uuid_str,
    metric
FROM analytics
LIMIT 5;
```

---

## 📊 Performance Comparison

| Feature | Standard `UUID` Macros | `INT128` Macros | Winner |
| :--- | :--- | :--- | :--- |
| **Storage** | 16 Bytes | 16 Bytes | Tie |
| **Generation** | ~0.4s / 1M rows | **~0.2s / 1M rows** | **INT128** |
| **Routing** | ~1.5s / 1M rows | **~0.03s / 1M rows** | **INT128 (50x)** |
| **Readability**| Human Readable | Big Number | UUID |

**Recommendation:** Use **INT128** for internal storage and heavy analytics. Use **UUID** for data export and debugging.

---

## 🧠 Technical Nuance

In C/Python, we handled bits using explicit hex masking. In SQL, we use `HUGEINT` math.

**128-Bit Integer Layout (MSB 127 to LSB 0):**

1.  **Time High (48 bits):** Bits `127` to `80`.
    *   *Shift:* `(Time >> 6) << 80`
2.  **Version (4 bits):** Bits `79` to `76`.
    *   *Fixed Value:* `8` (Binary `1000`)
3.  **Time Low (6 bits):** Bits `75` to `70`.
    *   *Shift:* `(Time & 63) << 70`
4.  **Shard High (6 bits):** Bits `69` to `64`.
    *   *Shift:* `(Shard >> 26) << 64`
5.  **Variant (2 bits):** Bits `63` to `62`.
    *   *Fixed Value:* `2` (Binary `10`)
6.  **Shard Low (26 bits):** Bits `61` to `36`.
    *   *Shift:* `(Shard & 0x3FFFFFF) << 36`
7.  **Random (36 bits):** Bits `35` to `0`.
    *   *Shift:* No shift.

The provided SQL macros handle this bit-splitting correctly using `>>` (shift) and `&` (mask).
