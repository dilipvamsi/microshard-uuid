# MicroShard UUID (Rust)

**A zero-lookup, partition-aware UUIDv8 implementation.**

`microshard-uuid` is a 128-bit identifier compliant with IETF RFC 9562. Unlike standard UUIDs (v4/v7), MicroShard embeds a **32-bit Shard ID** directly into the ID.

This enables **Zero-Lookup Routing**: your application can determine the database shard, tenant, or region of a record simply by looking at its Primary Key.

## 📦 Features

*   **Zero-Lookup Routing:** Extract Shard/Tenant IDs instantly from the UUID.
*   **Microsecond Precision:** 54-bit timestamp ensures strict chronological sorting.
*   **Massive Scale:** Supports **4.29 Billion** unique Shards/Tenants.
*   **Collision Resistant:** 36 bits of randomness *per microsecond* per shard.
*   **Type Safe:** Built on top of the standard `uuid` and `chrono` crates.
*   **Zero Dependencies:** (Aside from standard ecosystem crates `uuid`, `rand`, `chrono`).

---

## 🛠 Installation

Add this to your `Cargo.toml`.

If you are using this inside the monorepo:
```toml
[dependencies]
microshard-uuid = { path = "implementations/rust" }
```

If/when published to Crates.io:
```toml
[dependencies]
microshard-uuid = "1.0.0"
```

---

## 🚀 Usage

### 1. Basic Generation (Stateless)
Ideal for simple scripts or when the Shard ID changes per request.

```rust
use microshard_uuid::{generate, get_shard_id};

fn main() {
    // 1. Generate an ID for Shard #101
    // Note: Shard ID must be u32 (0 - 4,294,967,295)
    let uuid = generate(101).expect("Invalid Shard ID");

    println!("Generated: {}", uuid);
    // Output: 018e65c9-3a10-0400-8000-a4f1d3b8e1a1

    // 2. Extract Shard ID (Routing)
    let shard = get_shard_id(&uuid);
    assert_eq!(shard, 101);
}
```

### 2. Extracting Metadata
You can decode any MicroShard UUID to retrieve its creation time and origin shard without a database lookup.

```rust
use microshard_uuid::{get_timestamp, get_iso_timestamp};

fn print_metadata(uuid: &uuid::Uuid) {
    // Option A: Get Chrono DateTime object
    let dt = get_timestamp(uuid);
    println!("Created At (Chrono): {:?}", dt);

    // Option B: Get ISO 8601 String
    // Preserves full microsecond precision (e.g. .123456Z)
    let iso = get_iso_timestamp(uuid);
    println!("Created At (ISO): {}", iso);
}
```

### 3. Stateful Generator (Struct)
Best for dependency injection or application configuration where the Shard ID is fixed for the lifecycle of the service.

```rust
use microshard_uuid::Generator;

fn main() {
    // Configure once at startup
    let gen = Generator::new(500).unwrap();

    // Generate anywhere in your app
    let id = gen.new_id().unwrap();
    println!("Stateful ID: {}", id);
}
```

### 4. Backfilling (Explicit Time)
Generate UUIDs for past events while maintaining correct sort order.

```rust
use microshard_uuid::from_iso;

fn backfill() {
    // Create ID for a specific past event
    // Supports ISO strings with microsecond precision
    let past_event = "2023-01-01T12:00:00.654321Z";

    let old_uuid = from_iso(past_event, 55).unwrap();

    println!("Legacy UUID: {}", old_uuid);
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

This library includes a comprehensive test suite covering integrity, timing, sorting, and error handling.

```bash
# From inside implementations/rust/
cargo test
```
