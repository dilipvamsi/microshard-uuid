# MicroShard UUID - ClickHouse UDFs

A pure SQL implementation of MicroShard UUIDv8 for ClickHouse.
Compatible with **ClickHouse Cloud**, **Altinity**, and **Self-Hosted** clusters.

This uses **SQL User Defined Functions (UDFs)**. Since ClickHouse uses a **JIT (Just-In-Time) compiler**, these SQL functions are compiled to native machine code at runtime, making them extremely fast and compatible with **ClickHouse Cloud**.

## 🚀 Installation

Run the contents of `microshard.sql` in your ClickHouse console or via `clickhouse-client`.

```bash
clickhouse-client --multiquery < microshard.sql
```

## ⚡ Performance

ClickHouse uses JIT compilation for expressions. These bitwise operations are compiled into machine code, allowing generation of **millions of UUIDs per second** per core.

## 🛠 Usage

### 1. Table Definition
Use `UUID` as the primary key. Because MicroShard is time-ordered, this provides excellent data locality and compression (Run-Length Encoding).

```sql
CREATE TABLE events (
    id UUID DEFAULT microshard_generate(1), -- Shard ID 1
    event_type String,
    payload String
)
ENGINE = MergeTree()
ORDER BY id; -- Primary Key
```

### 2. Inserting Data

**Auto-generate (Default):**
```sql
INSERT INTO events (event_type, payload) VALUES ('Login', 'User 123');
```

**Explicit Shard Generation:**
Useful for multi-tenant apps where you insert data for specific tenants.
```sql
INSERT INTO events (id, event_type)
VALUES (microshard_generate(500), 'Tenant 500 Event');
```

### 3. Zero-Lookup Routing (Extraction)
You can filter or extract metadata directly from the UUID column without joins.

```sql
SELECT
    id,
    microshard_get_shard_id(id) AS shard_id,
    microshard_get_timestamp(id) AS created_at
FROM events
LIMIT 5;
```

**Output:**
```text
┌──────────────────────────────────────┬─shard_id─┬──────────────created_at─┐
│ 018e65c9-3a10-0400-8000-a4f1d3b8e1a1 │      100 │ 2025-12-12 10:00:00.123 │
└──────────────────────────────────────┴──────────┴─────────────────────────┘
```

### 4. Backfilling (Explicit Time)
If you need to generate a UUID for a specific past time, you can construct it manually or create a wrapper function.

```sql
-- Generate ID for 2023-01-01
-- Note: Requires manual microsecond calc in query or a separate UDF
WITH toUInt128(toUnixTimestamp64Micro(toDateTime64('2023-01-01 00:00:00', 6))) AS target_micros
SELECT
    toUUID(
        bitOr(
            bitShiftLeft(target_micros, 74),
            ... -- (Remaining packing logic similar to generator)
        )
    )
```

### Key Technical Details for ClickHouse

1.  **`toUInt128`**: ClickHouse treats UUIDs natively as 128-bit integers for storage. Casting `UUID -> UInt128` is a zero-cost operation (it just reinterprets bytes).
2.  **`ORDER BY id`**: Since the 54-bit timestamp is at the most significant bits (High bits), `ORDER BY id` automatically sorts your data chronologically. This is **critical** for ClickHouse performance (MergeTree data skipping indices).
3.  **Compression**: Time-ordered UUIDs compress significantly better than random UUIDv4 because the top bytes (timestamp) change slowly. Expect **~2x better compression** for the ID column compared to UUIDv4.