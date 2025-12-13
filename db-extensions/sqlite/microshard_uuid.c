/*
** MicroShard UUID - SQLite Extension (Zero-Dependency Optimized)
**
** Architecture: 54-bit Time | 32-bit Shard | 36-bit Random
** Compliance:   IETF RFC 9562 (UUIDv8)
**
** Features:
** 1. Zero external dependencies (No OpenSSL/Windows Crypt API).
** 2. High-performance Xorshift64* PRNG seeded via Time + Memory Address (ASLR).
** 3. Thread-safe execution (Supports SQLite WAL mode).
** 4. Native BLOB and TEXT support.
**
** API Functions:
** - microshard_uuid_generate(shard_id) -> BLOB
** - microshard_uuid_generate_text(shard_id) -> TEXT
** - microshard_uuid_from_micros(micros, shard_id) -> BLOB
** - microshard_uuid_from_iso(iso_str, shard_id) -> BLOB
** - microshard_uuid_from_string(uuid_str) -> BLOB
** - microshard_uuid_to_string(blob) -> TEXT
** - microshard_uuid_get_shard_id(blob) -> INT
** - microshard_uuid_get_time(blob) -> INT
** - microshard_uuid_get_iso(blob) -> TEXT
*/

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// Platform specific includes for Thread Local Storage and Time
#if defined(_WIN32)
    #include <windows.h>
    #define THREAD_LOCAL __declspec(thread)
#else
    #include <sys/time.h>
    #define THREAD_LOCAL _Thread_local
#endif

// --- Constants ---
#define MAX_SHARD_ID 4294967295ULL          // 2^32 - 1
#define MAX_TIME_MICROS 18014398509481983ULL // 2^54 - 1
#define MAX_RANDOM 68719476735ULL           // 2^36 - 1
#define UUID_VERSION 8
#define UUID_VARIANT 2
// 719162 is the number of days from year 0 to 1970
#define DAYS_TILL_1970 719162

// =============================================================================
// 1. Zero-Dependency PRNG (Xorshift64*)
// =============================================================================
/*
** We implement a fast, lightweight PRNG to avoid the overhead of opening
** /dev/urandom or calling OS crypto APIs for every INSERT.
**
** We use Thread Local Storage to ensure safety in multi-threaded environments.
*/

typedef struct {
    uint64_t state;
    int initialized;
} RngState;

// Thread-local state prevents race conditions in WAL mode
static THREAD_LOCAL RngState rng_ctx = {0, 0};

// Helper to get high-resolution nanoseconds for seeding
static uint64_t get_nanos_seed() {
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

// Xorshift64* Algorithm
static uint64_t rng_next_u64() {
    // Lazy Initialization
    if (!rng_ctx.initialized) {
        uint64_t now = get_nanos_seed();
        // ASLR Entropy: Mix time with the memory address of the stack variable
        uint64_t ptr = (uint64_t)(uintptr_t)&now;

        rng_ctx.state = now ^ ptr;

        // Safety: State cannot be 0 for Xorshift
        if (rng_ctx.state == 0) rng_ctx.state = 0xCAFEBABE;
        rng_ctx.initialized = 1;
    }

    uint64_t x = rng_ctx.state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_ctx.state = x;

    // The Star (*) step: Multiplication adds non-linearity
    return x * 0x2545F4914F6CDD1DULL;
}

static uint64_t get_random_36() {
    return rng_next_u64() & MAX_RANDOM;
}

// =============================================================================
// 2. Time & Date Logic
// =============================================================================

// Get current time in Microseconds (UTC)
static uint64_t get_micros() {
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long tt = ft.dwHighDateTime;
    tt <<= 32;
    tt |= ft.dwLowDateTime;
    tt /= 10; // Convert 100ns intervals to microseconds
    // Adjust from Windows Epoch (1601) to Unix Epoch (1970)
    return tt - 11644473600000000ULL;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
#endif
}

static int is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Helper: Calculate total days from year 0 to the start of the given year
static int64_t days_from_civil(int y) {
    y -= 1; // Don't count the current incomplete year
    return (int64_t)y * 365 + y / 4 - y / 100 + y / 400;
}

// Converts Y/M/D to Unix Epoch Days
static int64_t date_to_days(int year, int mon, int day) {
    static const int days_before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    int64_t days = days_from_civil(year);

    days -= DAYS_TILL_1970; // Subtract days from Year 0 to 1970
    days += days_before[mon - 1];

    if (mon > 2 && is_leap(year)) days++;

    return days + (day - 1);
}

// ==========================================
// INTERNAL: Strict ISO Parser
// ==========================================

static int parse_iso_to_micros(const char* str, uint64_t* out) {
    if (!str) return 0;

    // 1. Length Check
    if (strlen(str) < 20) return 0;

    // 2. Strict Separator Check (YYYY-MM-DDTHH:MM:SS)
    if (str[4] != '-' || str[7] != '-' || str[10] != 'T' || str[13] != ':' || str[16] != ':') {
        return 0;
    }

    int Y, M, D, h, m, s;
    if (sscanf(str, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) < 6) return 0;

    // 3. Logical Range Validation
    if (M < 1 || M > 12) return 0;
    if (h < 0 || h > 23) return 0;
    if (m < 0 || m > 59) return 0;
    if (s < 0 || s > 60) return 0;

    // 4. Days in Month Check (Strict Leap Year Support)
    int days_in_month = 31;
    switch (M) {
        case 4: case 6: case 9: case 11: days_in_month = 30; break;
        case 2: days_in_month = is_leap(Y) ? 29 : 28; break;
    }
    if (D < 1 || D > days_in_month) return 0;

    // 5. Fractional Seconds
    uint64_t micros = 0;
    const char* dot = strchr(str, '.');
    if (dot) {
        const char* p = dot + 1;
        int multiplier = 100000;
        while (*p && isdigit(*p) && multiplier >= 1) {
            micros += (*p - '0') * multiplier;
            multiplier /= 10;
            p++;
        }
    }

    // 6. Conversion
    int64_t days_since_epoch = date_to_days(Y, M, D);
    if (days_since_epoch < 0) return 0; // Pre-1970 check

    *out = (uint64_t)days_since_epoch * 86400000000ULL
           + (uint64_t)h * 3600000000ULL
           + (uint64_t)m * 60000000ULL
           + (uint64_t)s * 1000000ULL
           + micros;

    return 1; // Success
}

// =============================================================================
// 3. Bitwise & Binary Helpers
// =============================================================================

// Writes uint64 to buffer in Big-Endian (Network Byte Order)
static void write_be64(unsigned char* buf, uint64_t v) {
    buf[0] = (uint8_t)(v >> 56); buf[1] = (uint8_t)(v >> 48);
    buf[2] = (uint8_t)(v >> 40); buf[3] = (uint8_t)(v >> 32);
    buf[4] = (uint8_t)(v >> 24); buf[5] = (uint8_t)(v >> 16);
    buf[6] = (uint8_t)(v >> 8);  buf[7] = (uint8_t)(v);
}

// Reads uint64 from buffer assuming Big-Endian
static uint64_t read_be64(const unsigned char* buf) {
    return ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) |
           ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) |
           ((uint64_t)buf[6] << 8)  | (uint64_t)buf[7];
}

static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void blob_to_string(const unsigned char* blob, char* buffer) {
    sprintf(buffer,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        blob[0], blob[1], blob[2], blob[3],
        blob[4], blob[5],
        blob[6], blob[7],
        blob[8], blob[9],
        blob[10], blob[11], blob[12], blob[13], blob[14], blob[15]
    );
}

// =============================================================================
// 4. Core Logic (Bit Packing)
// =============================================================================

// Generates the 128-bit UUID and writes to `out_blob`
static int core_build(uint64_t micros, uint64_t shard_id, unsigned char* out_blob, char** err_msg) {
    if (shard_id > MAX_SHARD_ID) {
        *err_msg = "Shard ID out of range (0-4294967295)";
        return 0;
    }
    if (micros > MAX_TIME_MICROS) {
        *err_msg = "Time overflow (Year > 2541)";
        return 0;
    }

    uint64_t rnd = get_random_36();

    // --- High 64 Bits ---
    // [TimeHigh 48] [Ver 4] [TimeLow 6] [ShardHigh 6]
    uint64_t time_high = (micros >> 6) & 0xFFFFFFFFFFFFULL;
    uint64_t time_low  = micros & 0x3FULL;
    uint64_t shard_high = (shard_id >> 26) & 0x3FULL;

    uint64_t high_u64 = (time_high << 16) | ((uint64_t)UUID_VERSION << 12) | (time_low << 6) | shard_high;

    // --- Low 64 Bits ---
    // [Var 2] [ShardLow 26] [Random 36]
    uint64_t shard_low = shard_id & 0x3FFFFFFULL;
    uint64_t low_u64 = ((uint64_t)UUID_VARIANT << 62) | (shard_low << 36) | rnd;

    // Write to memory in Big Endian
    write_be64(out_blob, high_u64);
    write_be64(out_blob + 8, low_u64);
    return 1;
}

// =============================================================================
// 5. SQLite Function Implementations
// =============================================================================

/*
** microshard_uuid_generate(shard_id) -> BLOB
** Generates a new binary UUID (16 bytes).
*/
static void fn_generate_blob(sqlite3_context *context, int argc, sqlite3_value **argv) {
    int64_t shard_arg = sqlite3_value_int64(argv[0]);
    unsigned char blob[16];
    char* err = NULL;

    if (!core_build(get_micros(), (uint64_t)shard_arg, blob, &err)) {
        sqlite3_result_error(context, err, -1);
        return;
    }
    sqlite3_result_blob(context, blob, 16, SQLITE_TRANSIENT);
}

/*
** microshard_uuid_generate_text(shard_id) -> TEXT
** Generates a new UUID as a standard string.
*/
static void fn_generate_text(sqlite3_context *context, int argc, sqlite3_value **argv) {
    int64_t shard_arg = sqlite3_value_int64(argv[0]);
    unsigned char blob[16];
    char buffer[37];
    char* err = NULL;

    if (!core_build(get_micros(), (uint64_t)shard_arg, blob, &err)) {
        sqlite3_result_error(context, err, -1);
        return;
    }

    blob_to_string(blob, buffer);
    sqlite3_result_text(context, buffer, 36, SQLITE_TRANSIENT);
}

/*
** microshard_uuid_from_micros(micros, shard_id) -> BLOB
*/
static void fn_from_micros(sqlite3_context *context, int argc, sqlite3_value **argv) {
    int64_t micros_arg = sqlite3_value_int64(argv[0]);
    int64_t shard_arg  = sqlite3_value_int64(argv[1]);
    unsigned char blob[16];
    char* err = NULL;

    if (micros_arg < 0) {
        sqlite3_result_error(context, "Timestamp must be positive", -1);
        return;
    }

    if (!core_build((uint64_t)micros_arg, (uint64_t)shard_arg, blob, &err)) {
        sqlite3_result_error(context, err, -1);
        return;
    }
    sqlite3_result_blob(context, blob, 16, SQLITE_TRANSIENT);
}

/*
** microshard_uuid_from_iso(iso_string, shard_id) -> BLOB
**
** Throws a SQLite Error if the string format is invalid.
*/
static void fn_from_iso(sqlite3_context *context, int argc, sqlite3_value **argv) {
    // 1. Check for NULL
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_error(context, "ISO string cannot be NULL", -1);
        return;
    }

    const unsigned char* iso_text = sqlite3_value_text(argv[0]);
    int64_t shard_arg = sqlite3_value_int64(argv[1]);
    unsigned char blob[16];
    char* err = NULL;
    uint64_t micros;

    // 2. Strict Parse
    // If this returns 0, we abort the query immediately.
    if (!parse_iso_to_micros((const char*)iso_text, &micros)) {
        sqlite3_result_error(context, "Invalid ISO 8601 timestamp format or range (e.g. 2023-02-30)", -1);
        return;
    }

    // 3. Generate
    if (!core_build(micros, (uint64_t)shard_arg, blob, &err)) {
        sqlite3_result_error(context, err, -1);
        return;
    }

    sqlite3_result_blob(context, blob, 16, SQLITE_TRANSIENT);
}

/*
** microshard_uuid_from_string(uuid_string) -> BLOB
** Parses hex string (with or without dashes) to binary.
*/
static void fn_from_string(sqlite3_context *context, int argc, sqlite3_value **argv) {
    const unsigned char *text = sqlite3_value_text(argv[0]);
    if (!text) return;

    unsigned char blob[16];
    int blob_idx = 0;
    int high_nibble = -1;

    for (const char *p = (const char*)text; *p; p++) {
        if (*p == '-') continue;
        int val = hex_to_int(*p);
        if (val < 0) {
            sqlite3_result_error(context, "Invalid UUID hex character", -1);
            return;
        }

        if (high_nibble == -1) {
            high_nibble = val;
        } else {
            if (blob_idx >= 16) {
                sqlite3_result_error(context, "UUID string too long", -1);
                return;
            }
            blob[blob_idx++] = (high_nibble << 4) | val;
            high_nibble = -1;
        }
    }

    if (blob_idx != 16 || high_nibble != -1) {
        sqlite3_result_error(context, "Invalid UUID length", -1);
        return;
    }

    sqlite3_result_blob(context, blob, 16, SQLITE_TRANSIENT);
}

/*
** microshard_uuid_to_string(blob) -> TEXT
*/
static void fn_to_string(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (sqlite3_value_bytes(argv[0]) != 16) return;
    const unsigned char *blob = sqlite3_value_blob(argv[0]);
    char buffer[37];
    blob_to_string(blob, buffer);
    sqlite3_result_text(context, buffer, 36, SQLITE_TRANSIENT);
}

/*
** microshard_uuid_get_shard_id(blob) -> INT
*/
static void fn_get_shard_id(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (sqlite3_value_bytes(argv[0]) != 16) return; // Silent return for null/bad input
    const unsigned char *blob = sqlite3_value_blob(argv[0]);

    uint64_t high = read_be64(blob);
    uint64_t low  = read_be64(blob + 8);

    // Extraction Logic:
    // Shard High: High[5:0]
    // Shard Low:  Low[61:36]
    uint64_t shard_high = high & 0x3FULL;
    uint64_t shard_low = (low >> 36) & 0x3FFFFFFULL;

    uint64_t shard_id = (shard_high << 26) | shard_low;
    sqlite3_result_int64(context, (sqlite3_int64)shard_id);
}

/*
** microshard_uuid_get_time(blob) -> INT (Microseconds)
*/
static void fn_get_time(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (sqlite3_value_bytes(argv[0]) != 16) return;
    const unsigned char *blob = sqlite3_value_blob(argv[0]);

    uint64_t high = read_be64(blob);

    // Time High: High[63:16]
    // Time Low:  High[11:6]
    uint64_t time_high = (high >> 16) & 0xFFFFFFFFFFFFULL;
    uint64_t time_low  = (high >> 6)  & 0x3FULL;

    sqlite3_result_int64(context, (sqlite3_int64)((time_high << 6) | time_low));
}

/*
** microshard_uuid_get_iso(blob) -> TEXT
*/
static void fn_get_iso(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (sqlite3_value_bytes(argv[0]) != 16) return;
    const unsigned char *blob = sqlite3_value_blob(argv[0]);

    uint64_t high = read_be64(blob);
    uint64_t high_time = (high >> 16) & 0xFFFFFFFFFFFFULL;
    uint64_t low_time  = (high >> 6)  & 0x3FULL;
    uint64_t micros = (high_time << 6) | low_time;

    time_t seconds = (time_t)(micros / 1000000ULL);
    int frac = (int)(micros % 1000000ULL);

    // Use gmtime for UTC
    struct tm *tm_info = gmtime(&seconds);
    char buffer[40];
    strftime(buffer, 26, "%Y-%m-%dT%H:%M:%S", tm_info);

    char final_buf[40];
    snprintf(final_buf, 40, "%s.%06dZ", buffer, frac);

    sqlite3_result_text(context, final_buf, -1, SQLITE_TRANSIENT);
}

/*
** microshard_uuid_validate_iso(text) -> INT (Boolean)
** Returns 1 if string is a valid ISO 8601 timestamp, 0 otherwise.
*/
static void fn_validate_iso(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }

    const unsigned char* iso_text = sqlite3_value_text(argv[0]);
    uint64_t dummy; // We don't need the result, just the success flag

    int valid = parse_iso_to_micros((const char*)iso_text, &dummy);
    sqlite3_result_int(context, valid);
}

// =============================================================================
// 6. Extension Registration
// =============================================================================

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_microsharduuid_init(
  sqlite3 *db,
  char **pzErrMsg,
  const sqlite3_api_routines *pApi
) {
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);

  // --- Generators (Volatile) ---
  // SQLITE_INNOCUOUS: Allowed in schemas/triggers, but result changes every call.
  int f_gen = SQLITE_UTF8 | SQLITE_INNOCUOUS;

  sqlite3_create_function(db, "microshard_uuid_generate", 1, f_gen, 0, fn_generate_blob, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_generate_text", 1, f_gen, 0, fn_generate_text, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_from_micros", 2, f_gen, 0, fn_from_micros, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_from_iso", 2, f_gen, 0, fn_from_iso, 0, 0);

  // --- Extractors / Converters (Deterministic) ---
  // SQLITE_DETERMINISTIC: Safe for indexes.
  int f_det = SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS;

  sqlite3_create_function(db, "microshard_uuid_from_string", 1, f_det, 0, fn_from_string, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_to_string", 1, f_det, 0, fn_to_string, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_get_shard_id", 1, f_det, 0, fn_get_shard_id, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_get_time", 1, f_det, 0, fn_get_time, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_get_iso", 1, f_det, 0, fn_get_iso, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_validate_iso", 1, f_det, 0, fn_validate_iso, 0, 0);

  return rc;
}
