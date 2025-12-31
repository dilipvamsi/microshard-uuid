# MicroShard UUID (C/C++ Library)

**A high-performance, header-only C/C++ library for generating and parsing partition-aware UUIDs.**

`microshard-uuid` is a zero-dependency implementation of **RFC 9562 (UUIDv8)**. Unlike standard opaque UUIDs (v4/v7), MicroShard embeds a **32-bit Shard ID** directly into the 128-bit identifier.

This enables **Zero-Lookup Routing**: your application, load balancer, or database router can determine the tenant, region, or shard of a record simply by parsing the ID, without a secondary lookup table.

---

## 📐 Architecture (54 / 32 / 36)

MicroShard packs specific metadata into the 128-bit space while maintaining strict UUID compatibility.

| Component | Bits | Description | Capacity / Range |
| :--- | :--- | :--- | :--- |
| **Time** | **54** | Unix Microseconds | Valid until **Year 2541** |
| **Ver** | 4 | Version 8 | Fixed (RFC Compliance) |
| **Shard** | **32** | Logical Partition / Tenant | **4.29 Billion** Unique IDs |
| **Var** | 2 | Variant 2 | Fixed (RFC Compliance) |
| **Random** | **36** | Entropy | **68.7 Billion** per microsecond |

---

## 📦 Features

*   **100% Header-Only:** No library linking required. Just drop the headers into your project.
*   **Dual API:** Pure C99 API (`microshard_uuid.h`) + Modern C++11 Wrapper (`microshard_uuid.hpp`).
*   **Thread-Safe:** Internal PRNG uses **Thread Local Storage (TLS)** to ensure safety in multi-threaded apps.
*   **Portable:** Automatically detects and uses the correct APIs for **Windows** (Win32) and **Linux/macOS** (POSIX).
*   **High Performance:** Uses **Xoshiro256\*\*** PRNG (sub-nanosecond generation) and optimized 2x `uint64_t` struct layout.

---

## 🛠 Installation

Since this is a header-only library, there is no build step.

1.  Download `microshard_uuid.h` (Required).
2.  Download `microshard_uuid.hpp` (Optional, for C++).
3.  Copy them to your project's include directory.

---

## 🚀 Usage (Pure C)

Designed for embedded systems, kernels, or legacy codebases.

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

---

## 🚀 Usage (C++11)

Designed for modern applications. Provides `std::string` integration, exceptions, and operators.

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

## 📚 API Reference

### Core Functions (C API)

| Function | Description |
| :--- | :--- |
| **`ms_generate(shard_id)`** | Generates a new `ms_uuid_t` using the current system time. |
| **`ms_build(time, shard, rnd)`** | Deterministically constructs a UUID. |
| **`ms_to_string(u, buf, len)`** | Formats UUID into buffer. Returns `MS_OK` on success. |
| **`ms_from_string(str, &u)`** | Parses string into struct. Returns `MS_OK` on success. |
| **`ms_extract_shard(u)`** | Returns the 32-bit Shard ID. |
| **`ms_extract_iso(u, buf, len)`** | Formats the timestamp as ISO 8601. |

### Class Methods (C++ API)

| Method | Description |
| :--- | :--- |
| **`UUID::generate(shard)`** | Static factory. Returns `UUID` object. |
| **`UUID::fromString(str)`** | Static factory. Throws `std::invalid_argument` on error. |
| **`toString()`** | Returns `std::string`. |
| **`getShardId()`** | Returns `uint32_t`. |
| **`operator==`, `operator<`** | Supports sorting (`std::sort`) and equality checks. |

---

## 🔒 Thread Safety & RNG

This library is designed for high-concurrency environments (Web Servers, SQLite Extensions).

1.  **Thread Local Storage (TLS):**
    The internal random state uses `__declspec(thread)` (Windows) or `_Thread_local` (POSIX). Each thread maintains its own independent seed.

2.  **Auto-Seeding Strategy:**
    The RNG is automatically seeded using: `Current Time (nanoseconds) XOR Stack Address (ASLR entropy)`. This ensures unique streams even if threads start at the exact same nanosecond.

3.  **Xoshiro256\*\*:**
    We use the Xoshiro256\*\* algorithm, which is significantly faster and statistically superior to the standard C `rand()`.

---

## 🧪 Running Tests

A complete test suite is included for both C and C++.

**Prerequisites:** GCC or Clang.

```bash
# Run both C and C++ tests
make test
```

---

## 📄 License

MIT License. See the LICENSE file in the root directory.
