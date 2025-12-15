# MicroShard UUID v8 - ClickHouse Implementation

A pure SQL implementation of **MicroShard UUIDv8** for ClickHouse.
Compatible with **ClickHouse Cloud**, **Altinity**, and **Self-Hosted** clusters.

This library provides **SQL User Defined Functions (UDFs)**. Since ClickHouse uses a **JIT (Just-In-Time) compiler**, these SQL functions are compiled to native machine code at runtime, making them extremely fast.

## 🚀 Features

1.  **Time-Sortable:** Generated UUIDs are k-sortable (roughly sorted by time).
2.  **Sharded:** Embeds a 32-bit `Shard ID` (e.g., Tenant ID) directly into the UUID.
3.  **Zero-Lookup Routing:** Extract the Shard ID or Timestamp from the UUID without joining other tables.
4.  **ClickHouse Native:** Uses `UInt128` bitwise math for maximum performance.

## 📦 Installation

Run the contents of `microshard.sql` in your ClickHouse console or via `clickhouse-client`.

```bash
clickhouse-client --multiquery < microshard.sql
```

## 🛠 Usage

### 1. Table Definition (Critical: Primary Key)
Use `UUID` as the column type.

**Important:** Because of how x86 CPUs store memory (Little Endian), the raw `UUID` value is mathematically dominated by random bits, even though the string looks sorted.

You **must** use `microshard_sort_key(id)` in your `ORDER BY` / `PRIMARY KEY` to ensure data is physically stored on disk sorted by time.

```sql
CREATE TABLE events
(
    id UUID DEFAULT microshard_generate(101), -- 101 is example Shard/Tenant ID
    event_data String,
    created_at DateTime64(6) MATERIALIZED microshard_get_timestamp(id)
)
ENGINE = MergeTree
-- ⚠️ Sort by the swapped key so data is physically sorted by Time
ORDER BY (microshard_sort_key(id));
```

### 2. Inserting Data

**Auto-generate (Current Time):**
```sql
INSERT INTO events (event_data) VALUES ('User Login');
```

**Explicit Shard Generation:**
Useful for multi-tenant apps where you insert data for specific tenants.
```sql
INSERT INTO events (id, event_data)
VALUES (microshard_generate(500), 'Tenant 500 Event');
```

### 3. Zero-Lookup Extraction
You can filter or extract metadata directly from the UUID column without joins.

```sql
SELECT
    id,
    microshard_get_shard_id(id) AS shard_id,
    microshard_get_iso_timestamp(id) AS timestamp_iso
FROM events
LIMIT 5;
```

**Output:**
```text
┌──────────────────────────────────────┬─shard_id─┬───────────────timestamp_iso─┐
│ 018e65c9-3a10-0400-8000-a4f1d3b8e1a1 │      100 │ 2025-12-12T10:00:00.123456Z │
└──────────────────────────────────────┴──────────┴─────────────────────────────┘
```

### 4. Backfilling (Explicit Time)
If you need to generate a UUID for a specific past time, pass the timestamp in **microseconds**.

```sql
-- Generate ID for 2023-01-01
WITH toUInt64(toUnixTimestamp64Micro(toDateTime64('2023-01-01 00:00:00', 6))) AS target_time
SELECT microshard_from_micros(target_time, 100);
```

## ⚡ Performance & Architecture

### The "Little Endian" Trick
ClickHouse (and x86 CPUs) stores numbers in Little Endian format (least significant bytes first).
*   **The String:** To make the UUID string print with the time first (e.g., `018...`), we store the timestamp in the **Low 64 bits** of the `UInt128`.
*   **The Math:** This means the mathematical value of the UUID is `(Random * 2^64) + Time`.
*   **The Solution:** The `microshard_sort_key` function flips the High and Low bits, resulting in `(Time * 2^64) + Random`, allowing ClickHouse to sort data efficiently by time.

### Compression
Because the primary key sorts by time, the high bits of the sorted integer change very slowly. This results in **excellent compression** ratios in ClickHouse (often ~2x better than random UUIDv4), reducing storage costs.

### JIT Compilation
The functions use standard SQL bitwise operators (`bitOr`, `bitShiftLeft`, etc.). ClickHouse compiles these chains into optimized machine code, allowing generation of **millions of UUIDs per second** per core.