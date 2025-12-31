#include <iostream>
#include <vector>
#include <algorithm> // sort
#include <unordered_set>
#include <sstream>
#include <cstring>
#include <cassert>

// Include the C++ Wrapper
#include "microshard_uuid.hpp"

using namespace microshard;
using std::cout;
using std::cerr;
using std::endl;
using std::string;

// ====================================================================
// TEST HARNESS
// ====================================================================
int g_passed = 0;
int g_failed = 0;

void pass(const string& name) {
    cout << "[\033[0;32mPASS\033[0m] " << name << endl;
    g_passed++;
}

void fail(const string& name, const string& reason) {
    cout << "[\033[0;31mFAIL\033[0m] " << name << ": " << reason << endl;
    g_failed++;
    exit(1);
}

template <typename T>
void assert_eq(T actual, T expected, const string& name) {
    if (actual != expected) {
        std::stringstream ss;
        ss << "Expected " << expected << ", got " << actual;
        fail(name, ss.str());
    }
}

// ====================================================================
// TEST CASES
// ====================================================================

void test_basic_generation() {
    uint32_t shard = 555;
    UUID u = UUID::generate(shard);

    if (u.getShardId() != shard) fail("Generation", "Shard ID mismatch");
    if (u.toString().length() != 36) fail("Generation", "Invalid string length");

    pass("Basic Generation & Accessors");
}

void test_parsing_roundtrip() {
    UUID original = UUID::generate(123);
    string s = original.toString();

    try {
        UUID parsed = UUID::fromString(s);

        // Test Equality Operator
        if (parsed != original) fail("Parsing", "Round trip equality check failed");
        if (!(parsed == original)) fail("Parsing", "Operator== failed");

    } catch (const std::exception& e) {
        fail("Parsing", string("Exception thrown: ") + e.what());
    }

    pass("String Parsing & Round Trip");
}

void test_exceptions() {
    bool caught = false;
    try {
        // Invalid Hex
        UUID::fromString("z18e65c9-3a10-0400-8000-a4f1d3b8e1a1");
    } catch (const std::invalid_argument& e) {
        caught = true;
        // Verify the C error message bubbled up
        if (string(e.what()).find("Invalid hex") == string::npos) {
            fail("Exceptions", "Wrong error message received");
        }
    }

    if (!caught) fail("Exceptions", "Failed to throw on invalid input");

    caught = false;
    try {
        // Invalid Length
        UUID::fromString("123");
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    if (!caught) fail("Exceptions", "Failed to throw on bad length");

    pass("Exception Handling (std::invalid_argument)");
}

void test_binary_conversion() {
    UUID u1 = UUID::generate(999);
    auto bytes = u1.toBytes(); // std::array<uint8_t, 16>

    if (bytes.size() != 16) fail("Binary", "Array size incorrect");

    // Manually reconstruct using C struct to verify byte order
    ms_uuid_t raw;
    std::memcpy(&raw, bytes.data(), 16); // Assuming little-endian host for test simple copy, 
    // actually we need to use the C helper to be safe, but let's trust the class methods:

    // The class doesn't expose a fromBytes constructor yet, so we verify via raw()
    // Convert class raw to bytes manually
    ms_uuid_t internal = u1.raw();
    uint8_t check[16];
    ms_to_bytes_be(internal, check, 16);

    if (std::memcmp(bytes.data(), check, 16) != 0) {
        fail("Binary", "Byte serialization mismatch");
    }

    pass("Binary std::array Conversion");
}

void test_stl_sorting() {
    // Deterministic Build
    // T1: 1000, T2: 2000
    UUID u1 = UUID::build(1000, 1, 0);
    UUID u2 = UUID::build(2000, 1, 0);
    UUID u3 = UUID::build(3000, 1, 0);

    // Vector in wrong order
    std::vector<UUID> list = {u3, u1, u2};

    // Sort using operator<
    std::sort(list.begin(), list.end());

    if (list[0] != u1) fail("Sorting", "Item 0 should be u1 (Oldest)");
    if (list[1] != u2) fail("Sorting", "Item 1 should be u2");
    if (list[2] != u3) fail("Sorting", "Item 2 should be u3 (Newest)");

    pass("STL Sorting (std::sort with operator<)");
}

void test_stl_hashing() {
    std::unordered_set<UUID> set;

    UUID u1 = UUID::generate(1);
    UUID u2 = UUID::generate(2);
    UUID u3 = u1; // Copy

    set.insert(u1);
    set.insert(u2);

    if (set.find(u1) == set.end()) fail("Hashing", "Could not find u1");
    if (set.find(u3) == set.end()) fail("Hashing", "Could not find u3 (copy)");
    if (set.size() != 2) fail("Hashing", "Set size should be 2");

    pass("STL Hashing (std::unordered_set)");
}

void test_iso_time() {
    // 2023-01-01 00:00:00 UTC
    uint64_t micros = 1672531200000000ULL;
    UUID u = UUID::build(micros, 1, 0);

    string iso = u.toIsoTime();
    if (iso.find("2023-01-01") == string::npos) {
        fail("ISO Time", "Date mismatch: " + iso);
    }
    pass("ISO 8601 Formatting");
}

void test_stream_operator() {
    UUID u = UUID::generate(5);
    std::stringstream ss;
    ss << u; // Should call operator<<

    if (ss.str() != u.toString()) {
        fail("Stream Operator", "Stream output does not match toString()");
    }
    pass("Stream Operator (<<)");
}

int main() {
    cout << "==========================================" << endl;
    cout << "TEST SUITE: MicroShard UUID (C++ Wrapper) " << endl;
    cout << "==========================================" << endl;

    test_basic_generation();
    test_parsing_roundtrip();
    test_exceptions();
    test_binary_conversion();
    test_stl_sorting();
    test_stl_hashing();
    test_iso_time();
    test_stream_operator();

    cout << "==========================================" << endl;
    if (g_failed == 0) {
        cout << "\033[0;32mALL C++ TESTS PASSED.\033[0m" << endl;
        return 0;
    } else {
        cout << "\033[0;31m" << g_failed << " TESTS FAILED.\033[0m" << endl;
        return 1;
    }
}