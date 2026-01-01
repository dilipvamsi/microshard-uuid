/*
** ============================================================================
** MicroShard UUID (C++ Wrapper)
** Version: 1.0.0
** ============================================================================
**
** Description:
**   A type-safe C++11 wrapper for the MicroShard C library.
**   Provides std::string integration, exception handling, and operator overloads.
**
** Usage:
**   #include "microshard_uuid.hpp"
**   using namespace microshard;
**
**   try {
**       UUID id = UUID::generate(101);
**       std::cout << id << std::endl;
**   } catch (const std::exception& e) { ... }
**
** License: MIT
** ============================================================================
*/

#ifndef MICROSHARD_UUID_HPP
#define MICROSHARD_UUID_HPP

#include <string>
#include <array>
#include <iostream>
#include <stdexcept>
#include <iomanip>
#include <functional> // For std::hash

/* Include the pure C implementation */
#include "microshard_uuid.h"

namespace microshard {

class UUID {
private:
    ms_uuid_t _u;

public:
    /*
    ** ========================================================================
    ** Constructors
    ** ========================================================================
    */

    /* Default Constructor: Creates a nil (zero) UUID */
    UUID() : _u({0, 0}) {}

    /* Construct from raw C struct */
    explicit UUID(ms_uuid_t u) : _u(u) {}

    /* Construct by generating a new UUID for the given Shard ID */
    explicit UUID(uint32_t shard_id) {
        _u = ms_generate(shard_id);
    }

    /*
    ** ========================================================================
    ** Static Factories
    ** ========================================================================
    */

    /* Factory: Generate new UUID using system time */
    static UUID generate(uint32_t shard_id) {
        return UUID(ms_generate(shard_id));
    }

    /* Factory: Parse from string. Throws std::invalid_argument on failure. */
    static UUID fromString(const std::string& str) {
        ms_uuid_t temp;
        ms_status_t status = ms_from_string(str.c_str(), &temp);

        if (status != MS_OK) {
            throw std::invalid_argument(std::string("MicroShard Error: ") + ms_strerror(status));
        }
        return UUID(temp);
    }

    /* Factory: Deterministic build (e.g. for Backfilling or Testing) */
    static UUID build(uint64_t micros, uint32_t shard_id, uint64_t random_bits) {
        return UUID(ms_build(micros, shard_id, random_bits));
    }

    /*
    ** ========================================================================
    ** Converters & Accessors
    ** ========================================================================
    */

    /* Returns standard UUID string: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
    std::string toString() const {
        char buf[37];
        if (ms_to_string(_u, buf, sizeof(buf)) != MS_OK) {
             throw std::runtime_error("UUID internal buffer error");
        }
        return std::string(buf);
    }

    /* Returns ISO 8601 Timestamp: YYYY-MM-DDTHH:MM:SS.ssssssZ */
    std::string toIsoTime() const {
        char buf[40];
        if (ms_extract_iso(_u, buf, sizeof(buf)) != MS_OK) {
            throw std::runtime_error("Failed to extract ISO time from UUID");
        }
        return std::string(buf);
    }

    /* Returns 16-byte raw array (Stack allocated) */
    std::array<uint8_t, 16> toBytes() const {
        std::array<uint8_t, 16> arr;
        ms_to_bytes_be(_u, arr.data(), arr.size());
        return arr;
    }

    uint32_t getShardId() const { return ms_extract_shard(_u); }
    uint64_t getTime() const    { return ms_extract_time(_u); }
    ms_uuid_t raw() const       { return _u; }

    /*
    ** ========================================================================
    ** Operators
    ** ========================================================================
    */

    /* Equality Checks */
    bool operator==(const UUID& other) const {
        return _u.high == other._u.high && _u.low == other._u.low;
    }
    bool operator!=(const UUID& other) const {
        return !(*this == other);
    }

    /*
    ** Chronological Sorting
    ** Sorts by High bits (Time/Ver/ShardHigh) then Low bits.
    ** Guaranteed to sort by Time, then Shard, then Random.
    */
    bool operator<(const UUID& other) const {
        if (_u.high != other._u.high) return _u.high < other._u.high;
        return _u.low < other._u.low;
    }

    /* Output Stream Support */
    friend std::ostream& operator<<(std::ostream& os, const UUID& uuid) {
        return os << uuid.toString();
    }
};

} /* namespace microshard */

/*
** ============================================================================
** Standard Library Hash Support
** Allows UUID to be used as a key in std::unordered_map / std::unordered_set
** ============================================================================
*/
namespace std {
    template <>
    struct hash<microshard::UUID> {
        std::size_t operator()(const microshard::UUID& uuid) const {
            /*
             * Combine hashes using XOR (^).
             * We shift the Low part left by 1 bit (<< 1) to break symmetry.
             *
             * 1. Prevents Commutativity: Ensures Hash(A, B) != Hash(B, A).
             * 2. Prevents Self-Cancellation: Ensures that if High == Low,
             *    the result is not 0 (since X ^ X == 0).
             */
            auto raw = uuid.raw();
            return std::hash<uint64_t>{}(raw.high) ^ (std::hash<uint64_t>{}(raw.low) << 1);
        }
    };
}

#endif /* MICROSHARD_UUID_HPP */
