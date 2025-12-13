# MicroShard UUID (Go)

**A zero-lookup, partition-aware UUIDv8 implementation.**

`microshard-uuid` is a 128-bit identifier compliant with IETF RFC 9562. Unlike standard UUIDs (v4/v7), MicroShard embeds a **32-bit Shard ID** directly into the ID.

This enables **Zero-Lookup Routing**: your application can determine the database shard, tenant, or region of a record simply by looking at its Primary Key, without needing a lookup table or index.

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

	// String representation (Standard UUID format)
	fmt.Println("Generated:", uid.String())
	// Output: 018e65c9-3a10-0400-8000-a4f1d3b8e1a1
}
```

### 2. Parsing & Extracting Metadata
To read metadata from a UUID string, you must first `Parse` it into a struct. You can then extract the creation time and origin shard without a database lookup.

```go
func extract(uuidStr string) {
	// A. Parse the string into a MicroShardUUID struct
	uid, err := microsharduuid.Parse(uuidStr)
	if err != nil {
		log.Fatalf("Invalid UUID: %v", err)
	}

	// B. Get Shard ID (returns uint32)
	fmt.Printf("Origin Shard: %d\n", uid.ShardID())

	// C. Get Time (Go Time Object)
	fmt.Println("Created At (Time):", uid.Time().String())

	// D. Get Time (ISO 8601 String)
	// Useful for JSON responses. Preserves microsecond precision.
	fmt.Println("Created At (ISO):", uid.ISOTime())
	// Output: 2025-12-12T10:00:00.123456Z
}
```

### 3. Stateful Generator
Best for Dependency Injection or application configuration where the Shard ID is fixed for the service instance.

```go
func statefulExample() {
	// Configure once at startup
	gen, err := microsharduuid.NewGenerator(500)
	if err != nil {
		log.Fatal(err)
	}

	// Generate anywhere in your app
	uid, _ := gen.NewID()
	fmt.Println("Stateful UUID:", uid.String())
}
```

### 4. Backfilling (Explicit Time)
Generate UUIDs for past events while maintaining correct sort order.

```go
import "time"

func backfill() {
	// Create a specific time in the past
	past := time.Date(2023, 1, 1, 12, 0, 0, 0, time.UTC)

	// Generate UUID for that time with Shard ID 55
	oldUID, _ := microsharduuid.FromTime(past, 55)

	fmt.Println("Legacy UUID:", oldUID.String())
}
```

### 5. Database Storage (Bytes)
For optimal storage, use the 16-byte binary representation (e.g., `BINARY(16)` in MySQL or `UUID` in PostgreSQL).

```go
func toBytes(uid microsharduuid.MicroShardUUID) {
	// Get 16-byte slice
	rawBytes := uid.Bytes()

	fmt.Printf("Raw bytes: %x\n", rawBytes)
}
```

### 6. Comparison & Sorting
MicroShard UUIDs are designed to be sortable by creation time. The library provides helper methods and a `sort.Interface` implementation.

```go
import "sort"

func comparisonExample(id1, id2 microsharduuid.MicroShardUUID) {
	// Check chronological order
	if id1.Before(id2) {
		fmt.Println("ID1 is older than ID2")
	}

	// Check equality
	if id1.Equals(id2) {
		fmt.Println("IDs are identical")
	}

	// Sort a slice of UUIDs
	list := microsharduuid.ByTime{id2, id1}
	sort.Sort(list) // Sorts in-place chronologically
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
# From inside implementations/go/
go test -v .
```
