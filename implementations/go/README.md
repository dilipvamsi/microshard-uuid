# MicroShard UUID (Go)

**A zero-lookup, partition-aware UUIDv8 implementation.**

`microshard-uuid` is a 128-bit identifier compliant with IETF RFC 9562. Unlike standard UUIDs (v4/v7), MicroShard embeds a **32-bit Shard ID** directly into the ID.

This enables **Zero-Lookup Routing**: your application can determine the database shard, tenant, or region of a record simply by looking at its Primary Key.

## 📦 Features

*   **Zero-Lookup Routing:** Extract Shard/Tenant IDs instantly from the UUID.
*   **Microsecond Precision:** 54-bit timestamp ensures strict chronological sorting.
*   **Massive Scale:** Supports **4.29 Billion** unique Shards/Tenants.
*   **Collision Resistant:** 36 bits of randomness *per microsecond* per shard.
*   **Native Go Types:** Uses `uint32`, `uint64`, and `time.Time`.
*   **Zero Dependencies:** Uses only the Go standard library.

---

## 🛠 Installation

```bash
go get github.com/dilipvamsi/microshard-uuid/implementations/go
```

---

## 🚀 Usage

### 1. Basic Generation (Stateless)
Ideal for simple scripts or when the Shard ID changes per request.

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

	fmt.Println("Generated:", uid)
	// Output: 018e65c9-3a10-0400-8000-a4f1d3b8e1a1
}
```

### 2. Extracting Metadata
You can decode any MicroShard UUID to retrieve its creation time and origin shard without a database lookup.

```go
func extract(uid string) {
	// A. Get Shard ID
	shardID, err := microsharduuid.GetShardID(uid)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("Origin Shard: %d\n", shardID)

	// B. Get Time (Go Time Object)
	ts, _ := microsharduuid.GetTime(uid)
	fmt.Println("Created At (Time):", ts.String())

	// C. Get Time (ISO 8601 String)
	// Useful for JSON responses. Preserves microsecond precision.
	iso, _ := microsharduuid.GetISOTime(uid)
	fmt.Println("Created At (ISO):", iso)
	// Output: 2025-12-12T10:00:00.123456Z
}
```

### 3. Stateful Generator (Struct)
Best for Dependency Injection or application configuration where the Shard ID is fixed for the service.

```go
func statefulExample() {
	// Configure once at startup
	gen, err := microsharduuid.NewGenerator(500)
	if err != nil {
		log.Fatal(err)
	}

	// Generate anywhere in your app
	uid, _ := gen.NewID()
	fmt.Println("Stateful UUID:", uid)
}
```

### 4. Backfilling (Explicit Time)
Generate UUIDs for past events while maintaining correct sort order.

```go
import "time"

func backfill() {
	// Create a specific time in the past
	past := time.Date(2023, 1, 1, 12, 0, 0, 0, time.UTC)

	// Generate UUID for that time
	oldUID, _ := microsharduuid.FromTime(past, 55)

	fmt.Println("Legacy UUID:", oldUID)
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

This library includes a comprehensive test suite covering integrity, timing, and error handling.

```bash
# From inside implementations/go/
go test -v .
```
