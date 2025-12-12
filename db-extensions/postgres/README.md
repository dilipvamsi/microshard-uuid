# MicroShard UUID - PostgreSQL Implementation

**A zero-lookup, partition-aware UUIDv8 generator for PostgreSQL.**

This implementation is written in **Pure Optimized PL/pgSQL**. It requires **no C extensions, no Rust, and no root access**, making it compatible with all managed database services (AWS RDS, Google Cloud SQL, Azure Postgres, Supabase, Neon, etc.).

---

## ⚡ Performance Features

Unlike standard PL/pgSQL UUID generators that rely on slow string concatenation, this implementation uses **Binary Memory Access**:

*   **Binary Construction:** Uses `int8send` to build 128-bit integers directly in memory without string padding overhead.
*   **Zero-Allocation Extraction:** Uses `uuid_send` and bitwise pointer arithmetic (`get_byte`) to extract Shard IDs and Timestamps without parsing strings.
*   **High-Entropy Randomness:** Uses a `Multiply -> Shift` technique to utilize the full 53-bit precision of Postgres's random generator, preserving the highest quality bits.

---

## 📐 Architecture (54 / 32 / 36)

MicroShard embeds a **32-bit Shard ID** directly into the identifier while remaining compliant with **RFC 9562 (UUIDv8)**.

| Component | Bits | Description | Capacity |
| :--- | :--- | :--- | :--- |
| **Time** | **54** | Unix Microseconds | Valid until **Year 2541** |
| **Ver** | 4 | Version 8 | Fixed (RFC Compliance) |
| **Shard** | **32** | Logical Partition / Tenant | **4.29 Billion** Unique IDs |
| **Var** | 2 | Variant 2 | Fixed (RFC Compliance) |
| **Random** | **36** | High-Entropy Tail | **68.7 Billion** per microsecond |

---

## 📥 Installation

Simply run the `microshard_uuid.sql` script in your database. No extensions or restarts required.

```bash
psql -d my_database -f microshard_uuid.sql
```

### Dependencies
*   PostgreSQL 12 or higher.
*   Standard `pgcrypto` is **NOT** required (uses native `random()` and `uuid` types).

---

## 🚀 Usage

### 1. Generating IDs
Create a primary key for a specific Shard, Tenant, or Region.

```sql
-- Generate a UUID for Shard #100
SELECT microshard_generate(100);
-- Result: 018eb14a-f2b0-8190-8000-640a3f7812d4
```

### 2. Zero-Lookup Extraction
Extract metadata instantly without querying a lookup table. These functions are `IMMUTABLE` and safe for indexing.

```sql
-- Extract Shard ID
SELECT microshard_get_shard_id('018eb14a-f2b0-8190-8000-640a3f7812d4');
-- Result: 100

-- Extract Timestamp
SELECT microshard_get_timestamp('018eb14a-f2b0-8190-8000-640a3f7812d4');
-- Result: 2024-03-22 10:00:00.123456+00
```

### 3. Backfilling / Migration
If you need to migrate old data or backfill records while preserving their original creation time, use `microshard_from_micros` or `microshard_from_timestamp`.

```sql
-- Create a UUID sortable as of Jan 1st, 2023 for Shard 55
SELECT microshard_from_timestamp('2023-01-01 00:00:00+00', 55);
```

---

## 🛡️ Partitioning Strategy

Because the Shard ID is embedded, you can use Declarative Partitioning efficiently.

**Option A: Virtual Partitioning (Application Level)**
The application reads the ID, extracts the Shard, and knows exactly which database connection string to use.

**Option B: Native Table Partitioning**
You can use the extraction function in a generated column for automatic routing.

```sql
CREATE TABLE orders (
    id UUID PRIMARY KEY DEFAULT microshard_generate(1), -- Default Shard 1
    user_id INT,
    amount NUMERIC,

    -- Auto-extracted column for partitioning
    shard_id INT GENERATED ALWAYS AS (microshard_get_shard_id(id)) STORED
) PARTITION BY LIST (shard_id);

CREATE TABLE orders_shard_1 PARTITION OF orders FOR VALUES IN (1);
CREATE TABLE orders_shard_2 PARTITION OF orders FOR VALUES IN (2);
```

---

## 🔍 Performance Benchmarks

| Operation | Implementation | Speed (Ops/sec) | Latency |
| :--- | :--- | :--- | :--- |
| **Extraction** | **MicroShard (Binary)** | **~450,000 /s** | **2.2 µs** |
| **Generation** | **MicroShard (Binary)** | **~180,000 /s** | **5.5 µs** |
| Generation | Native UUIDv4 | ~2,500,000 /s | 0.4 µs |

> **Note:** While slower than native C-code UUIDv4, the binary PL/pgSQL implementation is fast enough for insert rates up to 50k/sec per connection. For analytics on millions of rows, the extraction functions are optimized to use minimal CPU cycles.

---

## 🛠️ API Reference

| Function | Type | Volatility | Description |
| :--- | :--- | :--- | :--- |
| `microshard_generate(shard_id int)` | `uuid` | `VOLATILE` | Generates a new ID using `clock_timestamp()`. |
| `microshard_from_micros(micros bigint, shard_id int)` | `uuid` | `VOLATILE` | Generates an ID from a specific unix epoch. |
| `microshard_get_shard_id(uid uuid)` | `int` | `IMMUTABLE` | Extracts the 32-bit Shard ID. |
| `microshard_get_timestamp(uid uuid)` | `timestamptz` | `IMMUTABLE` | Extracts the creation time. |