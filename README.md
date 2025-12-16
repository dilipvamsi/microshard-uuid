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
| **JavaScript** | [`implementations/js`](implementations/js) | ✅ Stable | `bigint` Node/Browser Universal, Types, Zero deps |
| **Go** | [`implementations/go`](implementations/go) | ✅ Stable | `uint64` optimizations, Zero deps |
| **Rust** | [`implementations/rust`](implementations/rust) | ✅ Stable | `u128` Type-safe, Zero deps |
| **Lua** | [`implementations/lua`](implementations/lua) | ✅ Stable | Nginx/Redis optimized, 4x32-bit math |
| **C / C++** | [`implementations/c`](implementations/c) | ✅ Stable | Header-only, Thread-safe, Zero deps |

### Database Extensions
| Database | Path | Type | Status | Performance |
| :--- | :--- | :--- | :--- | :--- |
| **SQLite** | [`db-extensions/sqlite`](db-extensions/sqlite) | C Extension | ✅ Stable | ~50x faster than Text UUIDs |
| **PostgreSQL** | [`db-extensions/postgres`](db-extensions/postgres) | PL/pgSQL | ✅ Stable | Zero-allocation bytea parsing |
| **MySQL** | [`db-extensions/mysql`](db-extensions/mysql) | Stored Functions | ✅ Stable | Storage Optimized (BINARY 16) |
| **DuckDB** | [`db-extensions/duckdb`](db-extensions/duckdb) | SQL Macros | ✅ Stable | 128-bit Math |
| **ClickHouse** | [`db-extensions/clickhouse`](db-extensions/clickhouse) | SQL UDFs | ✅ Stable | Native `UInt128` |

### Technical Notes
*   **Lua:** Uses a 4x32-bit integer structure to bypass Lua 5.1's double-precision limits. It automatically detects and uses `ngx.now` (OpenResty) or `redis.call('TIME')` (Redis) for high-precision timing.
*   **C / C++:** A single header-only library that auto-detects Windows/POSIX for threading and time. It uses Xoshiro256** for high-performance random generation.
*   **SQLite:** Written in C as a loadable extension because SQLite's native SQL engine lacks 64-bit bitwise operators and 128-bit integers.

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

# 1. Generate an ID for Shard #500
uid = generate(shard_id=500)
print(f"Generated: {uid}")

# 2. Extract Shard ID (Routing)
target_shard = get_shard_id(uid)
assert target_shard == 500
```

### JavaScript / TypeScript
```javascript
import { generate } from 'microshard-uuid';

// 1. Generate an ID for Shard #101
const uid = generate(101);

// 2. Storage (Canonical String)
console.log(uid.toString());
// Output: 018e65c9-3a10-0400-8000-a4f1d3b8e1a1

// 3. Routing (Extract Shard ID)
// No string parsing required; uses internal bitwise logic.
if (uid.getShardId() === 101) {
    console.log("Routing to Shard 101...");
}
```

### Go
```go
package main

import (
	"fmt"
	"log"
	"github.com/dilipvamsi/microshard-uuid/implementations/go"
)

func main() {
	// 1. Generate an ID for Shard #101
	// Note: Shard ID must be uint32 (0 - 4,294,967,295)
	uid, err := microsharduuid.Generate(101)
	if err != nil {
		log.Fatal(err)
	}

	// String representation (Standard UUID format)
	fmt.Println("Generated:", uid.String())
	// Output: 018e65c9-3a10-0400-8000-a4f1d3b8e1a1
}
```

### Rust
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

### Lua (Nginx / Redis)
```lua
local uuid = require("microsharduuid")

-- 1. Generate an ID for Shard #101
-- Auto-detects Nginx/Redis for high precision timing
local u = uuid.new(101)

-- 2. Storage (Canonical String)
print("Generated: " .. uuid.tostring(u))
-- Output: 018e65c9-3a10-0400-8000-a4f1d3b8e1a1

-- 3. Routing (Extract Shard ID)
local shard_id = uuid.shard_id(u)
if shard_id == 101 then
    print("Routing to Shard 101...")
end
```

### C (Header-Only)
```c
#include <stdio.h>
#include "microshard_uuid.h"

int main() {
    // 1. Generate (Shard ID 101)
    ms_uuid_t uid = ms_generate(101);

    // 2. Convert to string
    char buffer[37];
    if (ms_to_string(uid, buffer, sizeof(buffer)) == MS_OK) {
        printf("Generated: %s\n", buffer);
    }

    // 3. Extract Shard ID (Zero-Lookup)
    uint32_t shard = ms_extract_shard(uid);
    printf("Origin Shard: %u\n", shard);

    return 0;
}
```

### C++ (Header-Only C Header Wrapper)
```cpp
#include <iostream>
#include "microshard_uuid.hpp"

using namespace microshard;

int main() {
    try {
        // 1. Generate
        UUID uid = UUID::generate(101);

        // 2. Print (Stream operator overloaded)
        std::cout << "Generated: " << uid << std::endl;

        // 3. Extract Metadata
        std::cout << "Shard ID: " << uid.getShardId() << std::endl;
        std::cout << "Time ISO: " << uid.toIsoTime() << std::endl;

        // 4. Parsing
        UUID parsed = UUID::fromString("018e65c9-3a10-0400-8000-a4f1d3b8e1a1");

        if (parsed == uid) {
            std::cout << "UUIDs match!" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
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
