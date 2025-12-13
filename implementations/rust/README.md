# MicroShard UUID (Rust)

**A zero-lookup, partition-aware UUIDv8 implementation.**

`microshard-uuid` is a 128-bit identifier compliant with IETF RFC 9562. Unlike standard UUIDs (v4/v7), MicroShard embeds a **32-bit Shard ID** directly into the ID.

This enables **Zero-Lookup Routing**: your application can determine the database shard, tenant, or region of a record simply by looking at its Primary Key.

## 📦 Features

*   **Zero Dependencies:** Built using only `std`. No `uuid`, `chrono`, or `rand` crates required.
*   **Zero-Lookup Routing:** Extract Shard/Tenant IDs instantly from the UUID.
*   **Microsecond Precision:** 54-bit timestamp ensures strict chronological sorting.
*   **Massive Scale:** Supports **4.29 Billion** unique Shards/Tenants.
*   **Collision Resistant:** 36 bits of randomness *per microsecond* per shard using a custom Xorshift64* PRNG.
*   **High Performance:** Optimized `u128` internal representation for fast sorting and hashing.

---

## 🛠 Installation

Add this to your `Cargo.toml`.

```toml
[dependencies]
microshard-uuid = "1.0.0" # Replace with actual version or path
```

---

## 🚀 Usage

### 1. Basic Generation
Generate a globally unique ID bound to a specific Shard ID (0 - 4,294,967,295).

```rust
use microshard_uuid::MicroShardUUID;

fn main() {
    // 1. Generate an ID for Shard #101
    // Returns Result<MicroShardUUID, MicroShardError>
    let uuid = MicroShardUUID::generate(101).expect("Shard ID too large");

    println!("Generated: {}", uuid);
    // Output: 018e65c9-3a10-0400-8000-a4f1d3b8e1a1

    // 2. Extract Shard ID (Routing)
    // No database lookup required.
    let shard = uuid.shard_id();
    assert_eq!(shard, 101);
}
```

### 2. Extracting Metadata
You can decode any MicroShard UUID to retrieve its creation time and origin shard. The library includes a zero-dependency ISO 8601 formatter.

```rust
use microshard_uuid::MicroShardUUID;

fn print_metadata() {
    let uuid = MicroShardUUID::generate(55).unwrap();

    // Option A: Get ISO 8601 String
    // Format: "YYYY-MM-DDTHH:MM:SS.mmmmmmZ"
    let iso = uuid.to_iso_string();
    println!("Created At (ISO): {}", iso);

    // Option B: Get Raw Microseconds (Unix Epoch)
    let micros = uuid.timestamp_micros();
    println!("Created At (Unix Micros): {}", micros);
}
```

### 3. Backfilling & Parsing (Explicit Time)
Generate UUIDs for past events while maintaining correct sort order using ISO 8601 strings.

```rust
use microshard_uuid::MicroShardUUID;

fn backfill() {
    // Create ID for a specific past event
    // Supports strict ISO 8601 with microsecond precision
    let past_event = "2023-01-01T12:00:00.654321Z";

    let old_uuid = MicroShardUUID::from_iso(past_event, 55).expect("Invalid Timestamp");

    println!("Legacy UUID: {}", old_uuid);
}
```

### 4. Interoperability
While `MicroShardUUID` is a custom type optimized for performance, it converts easily to bytes for network transmission or database storage.

```rust
use microshard_uuid::MicroShardUUID;

fn main() {
    let uuid = MicroShardUUID::generate(1).unwrap();

    // Convert to standard 16-byte array (Big Endian)
    let bytes: [u8; 16] = uuid.as_bytes();

    // Convert to raw u128 (Fastest for internal sorting)
    let raw: u128 = uuid.as_u128();
}
```

---

## 📐 Specification (54/32/36)

Total Size: **128 Bits**

| Component | Bits | Description | Capacity |
| :--- | :--- | :--- | :--- |
| **Time** | **54** | Unix Microseconds | Valid until **Year 2541** |
| **Ver** | 4 | Fixed (Version 8) | RFC Compliance |
| **Shard** | **32** | Logical Shard / Tenant | **4.29 Billion** IDs |
| **Var** | 2 | Fixed (Variant 2) | RFC Compliance |
| **Random** | **36** | Entropy | **68.7 Billion** per microsecond |

---

## 🧪 Running Tests

This library includes a comprehensive test suite covering integrity, timing, sorting, and parsing logic.

```bash
cargo test
```
