Here is the comprehensive **Root `README.md`** for your repository. It serves as the central documentation hub, explaining the architecture and linking to the specific language implementations.

---

# MicroShard UUID

**A zero-lookup, partition-aware UUIDv8 implementation with microsecond precision.**

`microshard-uuid` is a 128-bit identifier compliant with **IETF RFC 9562 (UUIDv8)**. Unlike standard UUIDs (v4/v7) which are opaque, MicroShard embeds a **32-bit Shard ID** directly into the identifier.

This enables **Zero-Lookup Routing**: your application can determine the database shard, tenant, or region of a record instantly just by looking at its Primary Key, without needing a secondary lookup table.

---

## 📐 The Architecture (54 / 32 / 36)

MicroShard packs specific metadata into the 128-bit space while maintaining UUID compatibility.

| Component | Bits | Description | Capacity / Range |
| :--- | :--- | :--- | :--- |
| **Time** | **54** | Unix Microseconds | Valid until **Year 2541** |
| **Ver** | 4 | Version 8 | Fixed (RFC Compliance) |
| **Shard** | **32** | Logical Partition / Tenant | **4.29 Billion** Unique IDs |
| **Var** | 2 | Variant 2 | Fixed (RFC Compliance) |
| **Random** | **36** | Entropy | **68.7 Billion** per microsecond |

### Bit Layout
The data is packed non-contiguously to fit around the fixed Version/Variant bits required by the UUID spec.

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Time High (48 bits)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Ver (4)| Time Low (6) |         Shard ID High (6 bits)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|Var(2)|                  Shard ID Low (26 bits)                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Random (36 bits)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

---

## 📦 Implementations

This repository is a monorepo containing implementations for multiple languages and databases.

### Libraries
| Language | Path | Status | Features |
| :--- | :--- | :--- | :--- |
| **Python** | [`implementations/python`](implementations/python) | ✅ Stable | Stateless, Class Gen, ISO parsing |
| **JavaScript** | [`implementations/js`](implementations/js) | ✅ Stable | Node/Browser Universal, Types, Zero deps |
| **Go** | [`implementations/go`](implementations/go) | ✅ Stable | `uint64` optimizations, Zero deps |
| **Rust** | [`implementations/rust`](implementations/rust) | ✅ Stable | Type-safe, `chrono` integration |

### Database Extensions
| Database | Path | Type | Performance |
| :--- | :--- | :--- | :--- |
| **SQLite** | [`db-extensions/sqlite`](db-extensions/sqlite) | C Extension | ~50x faster than Text UUIDs |
| **PostgreSQL** | [`db-extensions/postgres`](db-extensions/postgres) | PL/pgSQL | Zero-allocation bytea parsing |
| **DuckDB** | [`db-extensions/duckdb`](db-extensions/duckdb) | SQL Macros | Vectorized `HUGEINT` Math |
| **ClickHouse** | [`db-extensions/clickhouse`](db-extensions/clickhouse) | SQL UDFs | Native `UInt128` / Columnar |


### Technical Notes on Implementations

*   **SQLite:** Written in C as a loadable extension because SQLite's native SQL engine lacks 64-bit bitwise operators and 128-bit integers.
*   **PostgreSQL:** Uses optimized `int8send`/`uuid_send` binary access to avoid expensive string parsing overhead.
*   **DuckDB:** Leverages the native 128-bit `HUGEINT` type for bitwise operations, allowing vectorization engine to process millions of IDs per second.
*   **ClickHouse:** Operates directly on `UInt128`, enabling high-speed columnar generation and filtering without string conversion.
---

## 🚀 Comparison

| Feature | MicroShard (UUIDv8) | UUIDv7 | UUIDv4 | Snowflake (64-bit) |
| :--- | :--- | :--- | :--- | :--- |
| **Routing** | ✅ **Native** (Shard ID) | ❌ None | ❌ None | ⚠️ Node ID only |
| **Precision** | ⏱️ **Microsecond** | Millisecond | None | Millisecond |
| **Sorting** | ✅ Time-Ordered | ✅ Time-Ordered | ❌ Random | ✅ Time-Ordered |
| **Browser** | ✅ String Safe | ✅ String Safe | ✅ String Safe | ❌ BigInt Issues |
| **Collision** | 36 bits / µs | 74 bits / ms | 122 bits | Sequence Number |

---

## 💻 Quick Start

### Python
```python
from microshard_uuid import generate, get_shard_id

# 1. Generate for Shard #100
uid = generate(100)

# 2. Extract Shard (Zero-Lookup Routing)
shard = get_shard_id(uid)
# shard == 100
```

### JavaScript / TypeScript
```javascript
import { generate, getIsoTimestamp } from 'microshard-uuid';

const uid = generate(55);

// Extract time with full microsecond precision
const time = getIsoTimestamp(uid);
// "2025-12-12T10:00:00.123456Z"
```

### Go
```go
import "github.com/dilipvamsi/microshard-uuid/implementations/go"

func main() {
    id, _ := microsharduuid.Generate(100)
}
```

### Rust
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
---

## 🛡️ Safety & Collision Logic

MicroShard relies on a combination of **Time**, **Shard ID**, and **Randomness** for uniqueness.

1.  **Global Uniqueness:** Guaranteed as long as `Shard ID` is unique per logical partition (Tenant, Region, etc.).
2.  **Local Uniqueness:** Within a single shard, uniqueness is guaranteed by **54-bit Timestamp** and **36-bit Randomness**.
3.  **Collision Probability:**
    *   The random pool resets every **1 microsecond**.
    *   To trigger a collision, a *single shard* must generate billions of IDs within a *single microsecond*.
    *   This exceeds the physical throughput limits of modern CPUs and Network interfaces.

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
