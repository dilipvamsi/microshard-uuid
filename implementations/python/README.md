# MicroShard UUID (Python Implementation)

A zero-dependency, **Partition-Aware UUIDv8** library for Python.

Unlike standard UUIDs (v4/v7), MicroShard embeds a **32-bit Shard ID** directly into the 128-bit identifier. This enables **Zero-Lookup Routing**, allowing your application to determine the database shard, tenant, or region of a record simply by decoding its Primary Key.

## 📦 Features

*   **Zero-Lookup Routing:** Extract Shard/Tenant IDs instantly from the UUID.
*   **Microsecond Precision:** 54-bit timestamp ensures strict chronological sorting.
*   **Massive Scale:** Supports **4.29 Billion** unique Shards/Tenants.
*   **Collision Resistant:** 36 bits of randomness *per microsecond* per shard.
*   **Stateless & Stateful:** Supports both functional usage and class-based generators.
*   **Standard Library Only:** No external dependencies (uses `os`, `struct`, `uuid`).

---

## 🛠 Installation

Requires **Python 3.6+**.

---

## 📐 Architecture: The 54/32/36 Split

MicroShard utilizes the **UUIDv8 (Custom)** format defined in IETF RFC 9562.

| Component | Bits | Description | Capacity |
| :--- | :--- | :--- | :--- |
| **Time** | **54** | Unix Microseconds | Valid until **Year 2541** |
| **Ver** | 4 | Fixed Version 8 | RFC Compliance |
| **Shard ID** | **32** | Logical Shard / Tenant | **4.29 Billion** IDs |
| **Var** | 2 | Fixed Variant 2 | RFC Compliance |
| **Random** | **36** | Entropy | **68.7 Billion** per microsecond |

---

## 🚀 Usage

### 1. Stateless Generation (Functional)
Best for simple scripts or when the Shard ID changes dynamically per request.

```python
from microshard_uuid import generate, get_shard_id

# 1. Generate an ID for Shard #500
uid = generate(shard_id=500)
print(f"Generated: {uid}")

# 2. Extract Shard ID (Routing)
target_shard = get_shard_id(uid)
assert target_shard == 500
```

### 2. Stateful Generation (Class-Based)
Best for application configuration or dependency injection where the Shard ID is fixed for the lifecycle of the service.

```python
from microshard_uuid import Generator

# Configure once at startup
id_gen = Generator(default_shard_id=101)

# Generate anywhere in your app without passing the ID
uid = id_gen.new_id()
```

### 3. Backfilling Data (Explicit Time)
If you are migrating legacy data, you can generate UUIDs for past timestamps while maintaining correct sorting order.

```python
from datetime import datetime, timezone
from microshard_uuid import from_timestamp

# Create an ID for a specific past event (e.g., Jan 1, 2023)
past_date = datetime(2023, 1, 1, 12, 0, 0, tzinfo=timezone.utc)

legacy_uid = from_timestamp(past_date, shard_id=50)

# Supports integer microseconds as well
legacy_uid_int = from_timestamp(1672574400000000, shard_id=50)
```

### 4. Extracting Metadata
You can decode any MicroShard UUID to retrieve its creation time and origin shard.

```python
from microshard_uuid import get_timestamp, get_shard_id

# Option A: Get Python Datetime object
dt = get_timestamp(uid)
# datetime.datetime(2025, 12, 12, 10, 0, 0, 123456, tzinfo=datetime.timezone.utc)

# Option B: Get ISO String (Strict format with Z)
iso = get_iso_timestamp(uid)
# "2025-12-12T10:00:00.123456Z"

# Get Origin Shard
shard = get_shard_id(uid)
print(f"Origin Shard: {shard}")
```

---

## 🛡️ Safety & Performance

### Collision Probability
Uniqueness is guaranteed by the combination of: `(Timestamp + Shard ID + Randomness)`.

*   **Global Uniqueness:** Guaranteed if Shard IDs are unique.
*   **Per-Shard Uniqueness:** A single shard has **36 bits** of randomness per microsecond.
*   **Risk:** A collision only occurs if a *single specific shard* generates billions of IDs within a *single microsecond*. This is physically impossible on current hardware.

### Performance
*   **Generation:** ~2-3 microseconds per ID (comparable to `uuid.uuid4()`).
*   **Sorting:** UUIDs are generated with the Timestamp in the highest bits, ensuring that standard string/byte sorting yields strict chronological order.

---

## 🧪 Running Tests

This library includes a comprehensive test suite using `unittest`.

```bash
# Run tests from the implementation folder
PYTHONPATH=src python3 -m unittest discover tests
```
