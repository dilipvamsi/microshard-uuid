# MicroShard UUID (JavaScript / TypeScript)

**A zero-lookup, partition-aware UUIDv8 implementation.**

`microshard-uuid` is a 128-bit identifier compliant with IETF RFC 9562. Unlike standard UUIDs (v4/v7), MicroShard embeds a **32-bit Shard ID** directly into the ID.

This enables **Zero-Lookup Routing**: your application can determine the database shard, tenant, or region of a record simply by looking at its Primary Key.

## 📦 Features

*   **Universal:** Works in **Node.js** (`crypto`, `hrtime`) and **Browsers** (`window.crypto`, `performance.now`).
*   **Zero-Lookup Routing:** Extract Shard/Tenant IDs instantly from the UUID.
*   **Microsecond Precision:** 54-bit timestamp ensures strict chronological sorting.
*   **ISO 8601 Support:** Parse and format timestamps with full microsecond precision.
*   **TypeScript:** Native `.d.ts` definitions included.
*   **Zero Dependencies:** Lightweight and fast.

---

## 🛠 Installation

```bash
npm install microshard-uuid
```

---

## 🚀 Usage

### 1. Basic Generation (Stateless)
Ideal for simple scripts or when the Shard ID changes per request.

```javascript
const { generate, getShardId } = require('microshard-uuid');

// 1. Generate an ID for Shard #101
const uid = generate(101);
console.log(uid);
// Output: 018e65c9-3a10-0400-8000-a4f1d3b8e1a1

// 2. Extract Shard ID (Routing)
const targetShard = getShardId(uid);

if (targetShard === 101) {
    console.log("Routing to Shard 101...");
}
```

### 2. Extracting Time (Metadata)
You can decode the creation time from any MicroShard UUID.

```javascript
const { getDate, getIsoTimestamp } = require('microshard-uuid');

const uid = "018e65c9-3a10-0400-8000-a4f1d3b8e1a1";

// Option A: Get standard JS Date (Milliseconds)
// Warning: Microseconds are truncated.
const date = getDate(uid);
console.log(date.toISOString());
// Output: 2025-12-12T10:00:00.123Z

// Option B: Get ISO String (Microseconds)
// Preserves full 54-bit precision.
const iso = getIsoTimestamp(uid);
console.log(iso);
// Output: 2025-12-12T10:00:00.123456Z
```

### 3. Backfilling (Explicit Time)
Generate UUIDs for past events while maintaining correct sort order.

```javascript
const { fromTimestamp } = require('microshard-uuid');

// Create ID for a specific time with Microseconds
// Input can be Date, Number (ms), BigInt (micros), or ISO String
const pastEvent = "2023-01-01T12:00:00.654321Z";

const legacyUid = fromTimestamp(pastEvent, 55);
```

### 4. Stateful Generator (Class)
Best for Dependency Injection or Singleton patterns where the Shard ID is configured once at startup.

```javascript
const { Generator } = require('microshard-uuid');

// Configure once
const idGen = new Generator(500);

// Generate anywhere
const uid = idGen.newId();
```

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

## 💻 API Reference

### `generate(shardId)`
Generates a UUID using the current system time.
*   **shardId**: `number | bigint` (0 - 4,294,967,295)

### `fromTimestamp(timestamp, shardId)`
Generates a UUID for a specific time.
*   **timestamp**: `Date | string (ISO) | number (ms) | bigint (micros)`

### `getShardId(uuid)`
Returns the embedded 32-bit Shard ID.
*   **Returns**: `number`

### `getDate(uuid)`
Returns the creation time as a JavaScript `Date` object.
*   **Returns**: `Date`
*   *Note: Microseconds are lost due to JS Date limitations.*

### `getIsoTimestamp(uuid)`
Returns the creation time as an ISO 8601 string.
*   **Returns**: `string` (e.g., `"2025-01-01T12:00:00.123456Z"`)
*   *Note: Preserves full microsecond precision.*

---

## 🌐 Browser Support
This library automatically detects the environment.
*   **Node.js:** Uses `crypto.randomBytes` and `process.hrtime`.
*   **Browser:** Uses `window.crypto.getRandomValues` and `performance.now`.

No configuration is required. Works with React, Vue, Next.js, and Vanilla JS.
