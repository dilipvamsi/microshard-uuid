/*
** ============================================================================
** MicroShard UUID (Pure C Header-Only Library)
** Version: 1.0.0
** ============================================================================
**
** Description:
**   A zero-dependency, header-only C library for generating and parsing
**   UUIDv8 identifiers compliant with IETF RFC 9562.
**
**   Unlike opaque UUIDv4/v7, MicroShard embeds a 32-bit partition ID directly
**   into the identifier, enabling zero-lookup routing for sharded systems.
**
** Architecture (128-bit Layout):
**   [ Time: 54 bits ] [ Ver: 4 ] [ Shard: 32 bits ] [ Var: 2 ] [ Rand: 36 bits ]
**
**   - Time:  Unix Microseconds (Valid until Year 2541).
**   - Ver:   Version 8 (Custom).
**   - Shard: User-defined 32-bit Integer (Tenant/Region/Partition ID).
**   - Var:   Variant 2 (RFC 9562).
**   - Rand:  36 bits of entropy (Xoshiro256**) -> ~68 Billion per microsecond.
**
** Thread Safety:
**   - Uses Thread Local Storage (TLS) for the Random Number Generator state.
**   - Safe for high-concurrency environments (Web Servers, SQLite WAL).
**
** License: MIT
** ============================================================================
*/

#ifndef MICROSHARD_UUID_H
#define MICROSHARD_UUID_H

#ifdef __cplusplus
extern "C" {
#endif

/* Standard C99 Includes */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/*
** ============================================================================
** SECTION 1: Error Handling & Status Codes
** ============================================================================
*/

/* Status codes returned by parsing and serialization functions */
typedef enum {
    MS_OK                   =  0,
    MS_ERR_INVALID_INPUT    = -1, /* NULL pointer passed */
    MS_ERR_BUFFER_TOO_SMALL = -2, /* Destination buffer too small */
    MS_ERR_INVALID_HEX      = -3, /* Non-hex character in UUID string */
    MS_ERR_BAD_LENGTH       = -4, /* String length incorrect (must be 32 or 36) */
    MS_ERR_ISO_FORMAT       = -5, /* Malformed ISO 8601 string syntax */
    MS_ERR_ISO_RANGE        = -6  /* Logical date error (e.g., Month 13, Feb 30) */
} ms_status_t;

/*
** Helper: ms_strerror
** Converts a status code into a human-readable error message.
*/
static inline const char* ms_strerror(ms_status_t status) {
    switch (status) {
        case MS_OK:                   return "Success";
        case MS_ERR_INVALID_INPUT:    return "Invalid input (NULL pointer)";
        case MS_ERR_BUFFER_TOO_SMALL: return "Destination buffer too small";
        case MS_ERR_INVALID_HEX:      return "Invalid hex character";
        case MS_ERR_BAD_LENGTH:       return "Invalid string length";
        case MS_ERR_ISO_FORMAT:       return "Invalid ISO 8601 syntax";
        case MS_ERR_ISO_RANGE:        return "Date/Time values out of logical range";
        default:                      return "Unknown error";
    }
}

/*
** ============================================================================
** SECTION 2: Platform Macros & Constants
** ============================================================================
*/

/* Constants defining the bit layout limits */
#define MS_MAX_SHARD_ID 4294967295ULL          /* 2^32 - 1 */
#define MS_VERSION      8                      /* UUIDv8 */
#define MS_VARIANT      2                      /* Variant 10xx */
#define MS_MAX_RANDOM   68719476735ULL         /* 2^36 - 1 */

/*
** Thread Local Storage (TLS) Detection
** We use TLS to ensure each thread has its own RNG seed, preventing race conditions
** without the performance penalty of Mutex locks.
*/
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define MS_TLS __declspec(thread)
#else
    #include <sys/time.h>
    /* Detect C11 standard or GCC/Clang extensions */
    #if defined(__STDC_NO_THREADS__) || (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L)
        #define MS_TLS __thread
    #else
        #define MS_TLS _Thread_local
    #endif
#endif

/*
** Struct: ms_uuid_t
** Represents the 128-bit UUID as two 64-bit unsigned integers.
** Optimized for 64-bit CPU registers.
*/
typedef struct {
    uint64_t high; /* Time, Version, Shard High */
    uint64_t low;  /* Variant, Shard Low, Random */
} ms_uuid_t;

/*
** ============================================================================
** SECTION 3: Internal PRNG (Xoshiro256**)
** ============================================================================
** We implement Xoshiro256** (XOR/Shift/Rotate). It is extremely fast and
** passes rigorous statistical tests (BigCrush).
*/

typedef struct {
    uint64_t s[4];
    int init;
} _ms_rng_state_t;

/* Thread-local state instance. Zero-initialized. */
static MS_TLS _ms_rng_state_t _ms_ctx = {{0,0,0,0}, 0};

/* Internal: Rotate Left */
static inline uint64_t _ms_rotl(const uint64_t x, int k) {
	return (x << k) | (x >> (64 - k));
}

/* Internal: SplitMix64 (Used only for bootstrapping the seed) */
static inline uint64_t _ms_splitmix64(uint64_t *x) {
	uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

/* Internal: Get High-Res Nanoseconds for Seeding */
static inline uint64_t _ms_get_nanos_seed() {
#if defined(_WIN32) || defined(_WIN64)
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
#else
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

/*
** Internal: Get next 36 bits of randomness.
** Handles lazy initialization (auto-seeding) on first call per thread.
*/
static inline uint64_t _ms_next_36() {
    /* 1. Auto-Seed if not initialized */
    if (!_ms_ctx.init) {
        uint64_t now = _ms_get_nanos_seed();
        /* ASLR Entropy: XOR time with the stack address of the state variable.
           This ensures unique seeds even if processes spawn at exact same time. */
        uint64_t seed_val = now ^ (uint64_t)(uintptr_t)&_ms_ctx;

        _ms_ctx.s[0] = _ms_splitmix64(&seed_val);
        _ms_ctx.s[1] = _ms_splitmix64(&seed_val);
        _ms_ctx.s[2] = _ms_splitmix64(&seed_val);
        _ms_ctx.s[3] = _ms_splitmix64(&seed_val);
        _ms_ctx.init = 1;
    }

    /* 2. Xoshiro256** Algorithm */
    const uint64_t result = _ms_rotl(_ms_ctx.s[1] * 5, 7) * 9;
    const uint64_t t = _ms_ctx.s[1] << 17;

    _ms_ctx.s[2] ^= _ms_ctx.s[0];
    _ms_ctx.s[3] ^= _ms_ctx.s[1];
    _ms_ctx.s[1] ^= _ms_ctx.s[2];
    _ms_ctx.s[0] ^= _ms_ctx.s[3];

    _ms_ctx.s[2] ^= t;
    _ms_ctx.s[3] = _ms_rotl(_ms_ctx.s[3], 45);

    /* 3. Return truncated to 36 bits */
    return result & MS_MAX_RANDOM;
}

/*
** ============================================================================
** SECTION 4: Time Utilities
** ============================================================================
*/

/*
** Internal: Get Unix Timestamp in Microseconds.
** Handles Windows/POSIX epoch differences.
*/
static inline uint64_t ms_get_micros() {
    uint64_t micros = 0;
#if defined(_WIN32) || defined(_WIN64)
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    unsigned long long tt = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    /* Divide by 10 (100ns -> 1us), subtract 1601-1970 delta */
    micros = (tt / 10) - 11644473600000000ULL;
#else
    struct timeval tv; gettimeofday(&tv, NULL);
    micros = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
#endif
    return micros;
}

/*
** ============================================================================
** SECTION 5: ISO 8601 Parser
** ============================================================================
*/

static inline int _ms_is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static inline int64_t _ms_date_to_days(int year, int mon, int day) {
    static const int days_before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int y = year - 1;
    int64_t days = (int64_t)y * 365 + y / 4 - y / 100 + y / 400;
    days -= 719162; /* Days from year 0 to 1970 */
    days += days_before[mon - 1];
    if (mon > 2 && _ms_is_leap(year)) days++;
    return days + (day - 1);
}

/*
** ms_parse_iso
** Parses "YYYY-MM-DDTHH:MM:SS.ssssss" into microseconds.
** Validates leap years, month lengths, and separators.
*/
static inline ms_status_t ms_parse_iso(const char* str, uint64_t* out_micros) {
    if (!str || !out_micros) return MS_ERR_INVALID_INPUT;
    if (strlen(str) < 19) return MS_ERR_BAD_LENGTH;

    /* Strict separator check */
    if (str[4] != '-' || str[7] != '-' || str[10] != 'T' || str[13] != ':' || str[16] != ':')
        return MS_ERR_ISO_FORMAT;

    int Y, M, D, h, m, s;
    if (sscanf(str, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) < 6)
        return MS_ERR_ISO_FORMAT;

    /* Logical Range Validation */
    int days_in_month = 31;
    if (M == 4 || M == 6 || M == 9 || M == 11) days_in_month = 30;
    else if (M == 2) days_in_month = _ms_is_leap(Y) ? 29 : 28;

    if (M < 1 || M > 12 || D < 1 || D > days_in_month ||
        h > 23 || m > 59 || s > 60) return MS_ERR_ISO_RANGE;

    /* Fractional seconds parsing */
    uint64_t frac = 0;
    const char* dot = strchr(str, '.');
    if (dot) {
        const char* p = dot + 1; int mul = 100000;
        while (*p && isdigit(*p) && mul >= 1) { frac += (*p - '0') * mul; mul /= 10; p++; }
    }

    int64_t days = _ms_date_to_days(Y, M, D);
    if (days < 0) return MS_ERR_ISO_RANGE;

    *out_micros = (uint64_t)days * 86400000000ULL +
                  (uint64_t)h * 3600000000ULL +
                  (uint64_t)m * 60000000ULL +
                  (uint64_t)s * 1000000ULL + frac;
    return MS_OK;
}

/*
** ============================================================================
** SECTION 6: Public API
** ============================================================================
*/

/*
** ms_build (Deterministic Builder)
** Constructs a UUID from specific components.
**
** @param micros      Timestamp in microseconds
** @param shard_id    32-bit Shard ID
** @param random_bits Lower 36 bits will be used for entropy
** @return            Constructed UUID struct
*/
static inline ms_uuid_t ms_build(uint64_t micros, uint32_t shard_id, uint64_t random_bits) {
    ms_uuid_t u = {0, 0};

    /* Implicitly cap Shard ID to 32 bits via casting */
    uint64_t shard64 = (uint64_t)shard_id;

    /* --- High 64 Bits ---
       Layout: [TimeHigh 48] [Ver 4] [TimeLow 6] [ShardHigh 6] */
    uint64_t time_high = (micros >> 6) & 0xFFFFFFFFFFFFULL;
    uint64_t time_low  = micros & 0x3FULL;
    uint64_t shard_high = (shard64 >> 26) & 0x3FULL;

    u.high = (time_high << 16) |
             ((uint64_t)MS_VERSION << 12) |
             (time_low << 6) |
             shard_high;

    /* --- Low 64 Bits ---
       Layout: [Var 2] [ShardLow 26] [Random 36] */
    uint64_t shard_low = shard64 & 0x3FFFFFFULL;

    u.low = ((uint64_t)MS_VARIANT << 62) |
            (shard_low << 36) |
            (random_bits & MS_MAX_RANDOM);

    return u;
}

/*
** ms_generate (High-Level Generator)
** Generates a new UUID using system time and thread-safe RNG.
**
** @param shard_id  32-bit Shard ID (0 - 4,294,967,295)
** @return          Initialized UUID struct
*/
static inline ms_uuid_t ms_generate(uint32_t shard_id) {
    return ms_build(ms_get_micros(), shard_id, _ms_next_36());
}

/*
** ms_to_string
** Serializes UUID to canonical string format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
**
** @param u         UUID struct
** @param dest      Destination buffer
** @param dest_len  Size of buffer (Must be >= 37)
** @return          MS_OK or error status
*/
static inline ms_status_t ms_to_string(ms_uuid_t u, char* dest, size_t dest_len) {
    if (!dest) return MS_ERR_INVALID_INPUT;
    if (dest_len < 37) return MS_ERR_BUFFER_TOO_SMALL;

    /* Cast to (unsigned long long) to prevent warnings on LP64 systems */
    snprintf(dest, dest_len, "%08llx-%04llx-%04llx-%04llx-%012llx",
        (unsigned long long)(u.high >> 32),
        (unsigned long long)((u.high >> 16) & 0xFFFF),
        (unsigned long long)(u.high & 0xFFFF),
        (unsigned long long)(u.low >> 48),
        (unsigned long long)(u.low & 0xFFFFFFFFFFFFULL));
    return MS_OK;
}

/*
** ms_from_string
** Parses a UUID string (36 chars with hyphens or 32 chars without).
*/
static inline ms_status_t ms_from_string(const char* str, ms_uuid_t* out) {
    if (!str || !out) return MS_ERR_INVALID_INPUT;

    size_t len = strlen(str);
    if (len != 36 && len != 32) return MS_ERR_BAD_LENGTH;

    unsigned char blob[16]; int idx = 0, hi = -1;
    for (const char* p = str; *p; p++) {
        if (*p == '-') continue;
        int val = -1;
        if (*p >= '0' && *p <= '9') val = *p - '0';
        else if (*p >= 'a' && *p <= 'f') val = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') val = *p - 'A' + 10;

        if (val < 0) return MS_ERR_INVALID_HEX;

        if (hi == -1) hi = val;
        else {
            if (idx >= 16) return MS_ERR_BAD_LENGTH;
            blob[idx++] = (hi << 4) | val;
            hi = -1;
        }
    }
    if (idx != 16) return MS_ERR_BAD_LENGTH;

    /* Deserialize bytes to struct */
    out->high = 0; out->low = 0;
    for (int i = 0; i < 8; i++) out->high = (out->high << 8) | blob[i];
    for (int i = 8; i < 16; i++) out->low = (out->low << 8) | blob[i];

    return MS_OK;
}

/*
** ms_to_bytes_be
** Serializes struct to 16-byte Big Endian array (Network Byte Order).
*/
static inline ms_status_t ms_to_bytes_be(ms_uuid_t u, unsigned char* out, size_t len) {
    if (!out) return MS_ERR_INVALID_INPUT;
    if (len < 16) return MS_ERR_BUFFER_TOO_SMALL;
    for (int i = 7; i >= 0; i--) { out[i] = (uint8_t)(u.high & 0xFF); u.high >>= 8; }
    for (int i = 15; i >= 8; i--) { out[i] = (uint8_t)(u.low & 0xFF); u.low >>= 8; }
    return MS_OK;
}

/*
** ms_from_bytes_be
** Deserializes 16-byte Big Endian array to struct.
*/
static inline ms_uuid_t ms_from_bytes_be(const unsigned char* in) {
    ms_uuid_t u = {0, 0};
    if (!in) return u;
    for (int i = 0; i < 8; i++) u.high = (u.high << 8) | in[i];
    for (int i = 8; i < 16; i++) u.low = (u.low << 8) | in[i];
    return u;
}

/* Extracts 32-bit Shard ID */
static inline uint32_t ms_extract_shard(ms_uuid_t u) {
    return (uint32_t)(((u.high & 0x3FULL) << 26) | ((u.low >> 36) & 0x3FFFFFFULL));
}

/* Extracts 54-bit Timestamp (Microseconds) */
static inline uint64_t ms_extract_time(ms_uuid_t u) {
    return ((u.high >> 16) & 0xFFFFFFFFFFFFULL) << 6 | ((u.high >> 6) & 0x3FULL);
}

/* Extracts Timestamp as ISO 8601 String */
static inline ms_status_t ms_extract_iso(ms_uuid_t u, char* out_buf, size_t dest_len) {
    if (!out_buf) return MS_ERR_INVALID_INPUT;
    if (dest_len < 30) return MS_ERR_BUFFER_TOO_SMALL;

    uint64_t mic = ms_extract_time(u);
    time_t sec = (time_t)(mic / 1000000ULL);
    struct tm* t = gmtime(&sec);
    if (!t) return MS_ERR_ISO_RANGE;

    char tmp[30];
    strftime(tmp, 30, "%Y-%m-%dT%H:%M:%S", t);
    snprintf(out_buf, dest_len, "%s.%06dZ", tmp, (int)(mic % 1000000ULL));
    return MS_OK;
}

#ifdef __cplusplus
}
#endif
#endif /* MICROSHARD_UUID_H */
