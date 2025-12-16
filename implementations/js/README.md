# MicroShard UUID (JavaScript / TypeScript)

**A zero-lookup, partition-aware UUIDv8 implementation.**

`microshard-uuid` is a 128-bit identifier compliant with IETF RFC 9562 (UUIDv8). Unlike standard UUIDs (v4/v7), MicroShard embeds a **32-bit Shard ID** directly into the ID.

This enables **Zero-Lookup Routing**: your application can determine the database shard, tenant, or region of a record simply by parsing its Primary Key, without hitting a lookup table or cache.

## 📦 Features

*   **Zero-Lookup Routing:** Extract Shard/Tenant IDs instantly (O(1) bitwise op).
*   **Chronologically Sortable:** 54-bit high-precision timestamp ensures correct ordering.
*   **High Performance:** Built on `BigInt` for efficient 64-bit register operations.
*   **Comparison API:** Native methods for sorting (`compare`, `lt`, `gt`) that handle unsigned 64-bit logic correctly.
*   **ISO 8601 Support:** Lossless microsecond precision.
*   **Universal:** Works in **Node.js** (native `crypto`) and **Browsers** (Web Crypto API).
*   **Zero Dependencies:** Lightweight and fast.

---

## 🛠 Installation

```bash
npm install microshard-uuid
```

---

## 🚀 Usage

### 1. Basic Generation & Routing
Generate a UUID object, convert it to a string for the DB, or extract the Shard ID for routing.

```javascript
const { generate } = require('microshard-uuid');

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

### 2. Parsing (Handling DB Data)
Since `microshard-uuid` uses a Class to optimize performance, you must parse strings coming from your database to access the helper methods.

```javascript
const { MicroShardUUID } = require('microshard-uuid');

const dbValue = "018e65c9-3a10-0400-8000-a4f1d3b8e1a1";

// Parse string back into Object
const uid = MicroShardUUID.parse(dbValue);

console.log(uid.getShardId()); // 101
console.log(uid.getDate());    // Date Object
```

### 3. Sorting & Comparison (New!)
The library provides optimized comparison methods that correctly handle 128-bit unsigned logic. This is significantly faster and safer than string sorting.

```javascript
const { generate, MicroShardUUID } = require('microshard-uuid');

const id1 = generate(1);
const id2 = generate(1);

// A. Relational Checks
if (id1.lt(id2)) {
  console.log("id1 is older than id2");
}

// B. Sorting Arrays (Chronological)
const list = [id2, id1, generate(1)];

// Pass the static compare function directly
list.sort(MicroShardUUID.compare);

// list is now sorted: [Oldest, ..., Newest]
```

### 4. Extracting Time (Metadata)
You can decode the creation time from any MicroShard UUID.

```javascript
const uid = generate(1);

// Option A: Get standard JS Date
// Warning: Microseconds are truncated to milliseconds.
const date = uid.getDate();
console.log(date.toISOString()); // 2025-12-12T10:00:00.123Z

// Option B: Get ISO String (Microseconds)
// Preserves full 54-bit precision (e.g., for audit logs).
const iso = uid.getIsoTimestamp();
console.log(iso); // 2025-12-12T10:00:00.123456Z
```

### 5. Binary Storage
For databases supporting `ByteA` or `Blob` (like Postgres or MySQL), storing raw bytes saves 50% space vs strings.

```javascript
const bytes = uid.toBytes(); // Returns Uint8Array(16)
// Save 'bytes' to DB...
```

---

## 💻 API Reference

### Top-Level Functions

| Function | Description |
| :--- | :--- |
| `generate(shardId)` | Generates a new `MicroShardUUID` using system time. |
| `fromTimestamp(ts, shardId)` | Generates a UUID for a specific past/future time. `ts` can be Date, ISO string, or BigInt micros. |

### Class: `MicroShardUUID`

Instances returned by `generate` or `parse` have the following methods:

| Method | Returns | Description |
| :--- | :--- | :--- |
| `.toString()` | `string` | Canonical 8-4-4-4-12 UUID string. |
| `.toBytes()` | `Uint8Array` | Raw 16-byte binary representation. |
| `.getShardId()` | `number` | Extracts the 32-bit Shard/Tenant ID. |
| `.getDate()` | `Date` | Extracts creation time (ms precision). |
| `.getIsoTimestamp()`| `string` | Extracts time as ISO 8601 string (us precision). |
| `.compare(other)` | `-1, 0, 1` | Compare two UUIDs (for sorting). |
| `.lt(other)` | `boolean` | Less Than. |
| `.gt(other)` | `boolean` | Greater Than. |
| `.equals(other)` | `boolean` | Equality check. |

### Static Methods

| Method | Description |
| :--- | :--- |
| `MicroShardUUID.parse(str)` | Validates and parses a string into a `MicroShardUUID`. |
| `MicroShardUUID.compare(a, b)` | Helper for `Array.sort()`. |

---

## 📐 Specification (54/32/36)

Total Size: **128 Bits**

| Component | Bits | Description |
| :--- | :--- | :--- |
| **Time** | **54** | Unix Microseconds (Valid until Year 2541) |
| **Ver** | 4 | Fixed (Version 8) |
| **Shard** | **32** | 4.29 Billion Shards / Tenants |
| **Var** | 2 | Fixed (Variant 2) |
| **Random** | **36** | 68.7 Billion random slots per microsecond |

---

## 🌐 Browser Support
This library automatically detects the environment (Zero-Config).
*   **Node.js:** Uses `crypto.randomBytes` and `process.hrtime`.
*   **Browser/Edge:** Uses `window.crypto` and `performance.now`.

**Note:** `BigInt` support is required (Available in all modern browsers and Node 10+).
