#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

// Include the robust library
#include "microshard_uuid.h"

// ====================================================================
// TEST HARNESS
// ====================================================================
int g_tests_passed = 0;
int g_tests_failed = 0;

void test_pass(const char* name) {
    printf("[\033[0;32mPASS\033[0m] %s\n", name);
    g_tests_passed++;
}

void test_fail(const char* name, const char* reason) {
    printf("[\033[0;31mFAIL\033[0m] %s: %s\n", name, reason);
    g_tests_failed++;
    exit(1);
}

void assert_ok(ms_status_t status, const char* name) {
    if (status != MS_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Expected MS_OK, got Error: %s (%d)", ms_strerror(status), status);
        test_fail(name, buf);
    }
}

void assert_err(ms_status_t status, ms_status_t expected, const char* name) {
    if (status != expected) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Expected Error %d, got %d (%s)", expected, status, ms_strerror(status));
        test_fail(name, buf);
    }
}

void assert_eq_u64(uint64_t actual, uint64_t expected, const char* name) {
    if (actual != expected) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Expected %" PRIu64 ", got %" PRIu64, expected, actual);
        test_fail(name, buf);
    }
}

void assert_eq_u32(uint32_t actual, uint32_t expected, const char* name) {
    if (actual != expected) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Expected %u, got %u", expected, actual);
        test_fail(name, buf);
    }
}

// ====================================================================
// TEST CASES
// ====================================================================

void test_bitwise_integrity() {
    uint64_t time_in  = 0x3FFFFFFFFFFFFFULL; // Max 54-bit time
    uint32_t shard_in = 0xFFFFFFFF;          // Max 32-bit shard
    uint64_t rand_in  = 0xFFFFFFFFF;         // Max 36-bit random

    // Deterministic Build
    ms_uuid_t u = ms_build(time_in, shard_in, rand_in);

    // Verify Extractions
    assert_eq_u32(ms_extract_shard(u), shard_in, "Bitwise: Max Shard Extraction");
    assert_eq_u64(ms_extract_time(u), time_in, "Bitwise: Max Time Extraction");

    // Version 8 Check (High 48-51)
    assert_eq_u64((u.high >> 12) & 0xF, MS_VERSION, "Bitwise: Version 8 Check");
    // Variant 2 Check (Low 62-63)
    assert_eq_u64((u.low >> 62) & 0x3, MS_VARIANT, "Bitwise: Variant 2 Check");

    test_pass("Bitwise Integrity & Packing");
}

void test_string_conversion() {
    ms_uuid_t u1 = ms_generate(12345);
    char str[37];

    // 1. To String (Safe)
    assert_ok(ms_to_string(u1, str, sizeof(str)), "String: Serialize");

    // Check length
    if (strlen(str) != 36) test_fail("String", "Invalid length output");

    // 2. From String
    ms_uuid_t u2;
    assert_ok(ms_from_string(str, &u2), "String: Parse Standard");

    // 3. Round Trip Check
    if (u1.high != u2.high || u1.low != u2.low) test_fail("String", "Round trip mismatch");

    // 4. Robustness (Hyphenless)
    char no_hyphen[] = "018e65c93a1004008000a4f1d3b8e1a1";
    assert_ok(ms_from_string(no_hyphen, &u2), "String: Parse Hyphenless");

    test_pass("String Serialization (Safe & Robust)");
}

void test_binary_conversion() {
    ms_uuid_t u1 = ms_generate(999);
    unsigned char blob[16];

    // 1. To Bytes (Safe)
    assert_ok(ms_to_bytes_be(u1, blob, sizeof(blob)), "Binary: Serialize");

    // 2. From Bytes
    ms_uuid_t u2 = ms_from_bytes_be(blob);

    // 3. Round Trip
    if (u1.high != u2.high || u1.low != u2.low) test_fail("Binary", "Round trip mismatch");

    test_pass("Binary Serialization (Big Endian)");
}

void test_iso_parsing() {
    uint64_t mic;

    // 1. Valid Standard (Long)
    assert_ok(ms_parse_iso("2023-01-01T00:00:00.000000", &mic), "ISO: Standard Full");

    // 2. Valid Short (No fractions)
    assert_ok(ms_parse_iso("2024-02-29T12:00:00", &mic), "ISO: Short Leap Day");

    // 3. Invalid Logic
    assert_err(ms_parse_iso("2023-02-29T12:00:00", &mic), MS_ERR_ISO_RANGE, "ISO: Invalid Leap Day");
    assert_err(ms_parse_iso("2023-13-01T00:00:00", &mic), MS_ERR_ISO_RANGE, "ISO: Month 13");

    // 4. Invalid Format
    assert_err(ms_parse_iso("2023/01/01", &mic), MS_ERR_BAD_LENGTH, "ISO: Too Short");
    assert_err(ms_parse_iso("2023/01/01T00:00:00", &mic), MS_ERR_ISO_FORMAT, "ISO: Bad Separators");

    test_pass("ISO 8601 Parsing Logic");
}

void test_error_handling() {
    ms_uuid_t u = ms_generate(1);
    char small_buf[10];
    unsigned char small_blob[5];

    // 1. Buffer Too Small (String)
    assert_err(ms_to_string(u, small_buf, sizeof(small_buf)), MS_ERR_BUFFER_TOO_SMALL, "Error: String buf too small");

    // 2. Buffer Too Small (Binary)
    assert_err(ms_to_bytes_be(u, small_blob, sizeof(small_blob)), MS_ERR_BUFFER_TOO_SMALL, "Error: Blob buf too small");

    // 3. Invalid Hex in Input
    ms_uuid_t u_out;
    assert_err(ms_from_string("z18e65c9-3a10-0400-8000-a4f1d3b8e1a1", &u_out), MS_ERR_INVALID_HEX, "Error: Invalid Hex");

    // 4. Bad Length Input
    assert_err(ms_from_string("018e65c9", &u_out), MS_ERR_BAD_LENGTH, "Error: Bad Length");

    // 5. NULL Checks
    assert_err(ms_to_string(u, NULL, 37), MS_ERR_INVALID_INPUT, "Error: NULL output");
    assert_err(ms_from_string(NULL, &u_out), MS_ERR_INVALID_INPUT, "Error: NULL input");

    test_pass("Error Handling & Validation");
}

void test_iso_extraction() {
    uint64_t target_micros = 1672531200000000ULL; // 2023-01-01 00:00:00 UTC
    ms_uuid_t u = ms_build(target_micros, 1, 1);

    char buf[40];
    assert_ok(ms_extract_iso(u, buf, sizeof(buf)), "ISO: Extract");

    // Check Prefix
    if (strncmp(buf, "2023-01-01", 10) != 0) {
        printf("Got: %s\n", buf);
        test_fail("ISO Extract", "Date mismatch");
    }

    // Check buffer safety
    char small[5];
    assert_err(ms_extract_iso(u, small, sizeof(small)), MS_ERR_BUFFER_TOO_SMALL, "ISO Extract: Buffer check");

    test_pass("ISO Extraction & Formatting");
}

void test_sorting_hierarchy() {
    // Newer Time (1000) vs Older Time (2000)
    ms_uuid_t u_old = ms_build(1000, 0xFFFFFFFF, 0);
    ms_uuid_t u_new = ms_build(2000, 0, 0);

    char s_old[37], s_new[37];
    ms_to_string(u_old, s_old, 37);
    ms_to_string(u_new, s_new, 37);

    // Lexicographical check
    if (strcmp(s_old, s_new) >= 0) test_fail("Sort", "Time priority failed (Old >= New string)");

    // Binary check
    if (u_old.high >= u_new.high) test_fail("Sort", "Time priority failed (High bits)");

    test_pass("Sorting Hierarchy (Time > Shard)");
}

int main() {
    printf("==========================================\n");
    printf("TEST SUITE: MicroShard UUID (Robust C)\n");
    printf("==========================================\n");

    test_bitwise_integrity();
    test_string_conversion();
    test_binary_conversion();
    test_iso_parsing();
    test_error_handling();
    test_iso_extraction();
    test_sorting_hierarchy();

    printf("==========================================\n");
    if (g_tests_failed == 0) {
        printf("\033[0;32mALL TESTS PASSED.\033[0m\n");
        return 0;
    } else {
        printf("\033[0;31m%d TESTS FAILED.\033[0m\n", g_tests_failed);
        return 1;
    }
}