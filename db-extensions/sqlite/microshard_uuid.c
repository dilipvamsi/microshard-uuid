/*
** MicroShard UUID - SQLite Extension
**
** Provides:
** 1. microshard_uuid_generate(shard_id)
** 2. microshard_uuid_from_micros(micros, shard_id)
** 3. microshard_uuid_from_iso(iso_str, shard_id)
** 4. microshard_uuid_get_shard_id(blob)
** 5. microshard_uuid_get_time(blob)
** 6. microshard_uuid_get_iso(blob)
*/

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#if defined(_WIN32)
    #include <windows.h>
    #include <wincrypt.h>
#else
    #include <sys/time.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

// Constants
#define MAX_SHARD_ID 4294967295ULL
#define MAX_TIME_MICROS 18014398509481983ULL

// --- Helpers ---

static uint64_t to_big_endian(uint64_t v) {
    uint64_t ret;
    uint8_t *p = (uint8_t*)&ret;
    p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);  p[7] = (uint8_t)(v);
    return ret;
}

static uint64_t from_big_endian(uint64_t v) {
    return to_big_endian(v);
}

static uint64_t get_micros() {
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long tt = ft.dwHighDateTime;
    tt <<= 32;
    tt |= ft.dwLowDateTime;
    tt /= 10;
    return tt - 11644473600000000ULL;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
#endif
}

static uint64_t get_random_36() {
    uint64_t rnd = 0;
#if defined(_WIN32)
    HCRYPTPROV hProvider;
    if(CryptAcquireContext(&hProvider, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hProvider, 5, (BYTE*)&rnd);
        CryptReleaseContext(hProvider, 0);
    }
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        read(fd, &rnd, 5);
        close(fd);
    }
#endif
    return rnd & 0xFFFFFFFFFULL;
}

// --- Date Logic (Dependency Free) ---

static int is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Portable UTC conversion to avoid mktime timezone issues
static int64_t time_to_epoch(int year, int mon, int day, int hour, int min, int sec) {
    static const int days_before_month[] = {0,31,59,90,120,151,181,212,243,273,304,334};

    // Calculate days since 1970
    int y = year - 1900;
    int64_t days = (y - 70) * 365 + ((y - 69) / 4) - ((y - 1) / 100) + ((y + 299) / 400);

    // Add days for months in current year
    days += days_before_month[mon - 1];

    // Leap year adjustment
    if (mon > 2 && is_leap(year)) days++;

    // Add current day
    days += (day - 1);

    return (days * 86400) + (hour * 3600) + (min * 60) + sec;
}

// Parse ISO 8601 "YYYY-MM-DDTHH:MM:SS.mmmmmmZ"
static int parse_iso_to_micros(const char* str, uint64_t* out) {
    int Y, M, D, h, m, s;
    int micros = 0;
    int scanned = sscanf(str, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s);

    if (scanned < 6) return 0; // Fail

    // Find fractional part (look for '.')
    const char* dot = strchr(str, '.');
    if (dot) {
        const char* p = dot + 1;
        int multiplier = 100000; // Start at 100k for first digit
        while (*p && isdigit(*p) && multiplier >= 1) {
            micros += (*p - '0') * multiplier;
            multiplier /= 10;
            p++;
        }
    }

    int64_t epoch_sec = time_to_epoch(Y, M, D, h, m, s);
    if (epoch_sec < 0) return 0; // Pre-1970 not supported

    *out = (uint64_t)epoch_sec * 1000000ULL + micros;
    return 1; // Success
}

// --- Shared Builder Logic ---

static void build_and_return_blob(sqlite3_context *context, uint64_t micros, uint64_t shard_id) {
    if (shard_id > MAX_SHARD_ID) {
        sqlite3_result_error(context, "Shard ID out of range (0-4294967295)", -1);
        return;
    }
    if (micros > MAX_TIME_MICROS) {
        sqlite3_result_error(context, "Time overflow (Year > 2541)", -1);
        return;
    }

    uint64_t rnd = get_random_36();

    // Packing 54/32/36
    uint64_t time_high = (micros >> 6) & 0xFFFFFFFFFFFFULL;
    uint64_t time_low  = micros & 0x3FULL;
    uint64_t shard_high = (shard_id >> 26) & 0x3FULL;

    uint64_t high_u64 = (time_high << 16) | (0x8ULL << 12) | (time_low << 6) | shard_high;

    uint64_t shard_low = shard_id & 0x3FFFFFFULL;
    uint64_t low_u64 = (0x2ULL << 62) | (shard_low << 36) | rnd;

    uint64_t final_high = to_big_endian(high_u64);
    uint64_t final_low  = to_big_endian(low_u64);

    unsigned char blob[16];
    memcpy(blob, &final_high, 8);
    memcpy(blob + 8, &final_low, 8);

    sqlite3_result_blob(context, blob, 16, SQLITE_TRANSIENT);
}

// --- SQL Functions ---

/* microshard_uuid_generate(shard_id) */
static void uuid_generate(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1) return;
    int64_t shard_arg = sqlite3_value_int64(argv[0]);
    build_and_return_blob(context, get_micros(), (uint64_t)shard_arg);
}

/* microshard_uuid_from_micros(micros, shard_id) */
static void uuid_from_micros(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 2) return;
    int64_t micros_arg = sqlite3_value_int64(argv[0]);
    int64_t shard_arg  = sqlite3_value_int64(argv[1]);
    if (micros_arg < 0) {
        sqlite3_result_error(context, "Timestamp must be positive", -1);
        return;
    }
    build_and_return_blob(context, (uint64_t)micros_arg, (uint64_t)shard_arg);
}

/* microshard_uuid_from_iso(iso_str, shard_id) */
static void uuid_from_iso(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 2) return;
    const unsigned char* iso_text = sqlite3_value_text(argv[0]);
    int64_t shard_arg = sqlite3_value_int64(argv[1]);

    uint64_t micros;
    if (!parse_iso_to_micros((const char*)iso_text, &micros)) {
        sqlite3_result_error(context, "Invalid ISO 8601 timestamp format", -1);
        return;
    }

    build_and_return_blob(context, micros, (uint64_t)shard_arg);
}

/* microshard_uuid_get_shard_id(blob) */
static void uuid_get_shard_id(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1 || sqlite3_value_bytes(argv[0]) != 16) return;
    const unsigned char *blob = sqlite3_value_blob(argv[0]);

    uint64_t high = from_big_endian(*(uint64_t*)blob);
    uint64_t low  = from_big_endian(*(uint64_t*)(blob + 8));

    uint64_t shard_id = ((high & 0x3FULL) << 26) | ((low >> 36) & 0x3FFFFFFULL);
    sqlite3_result_int64(context, (sqlite3_int64)shard_id);
}

/* microshard_uuid_get_time(blob) */
static void uuid_get_time(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1 || sqlite3_value_bytes(argv[0]) != 16) return;
    const unsigned char *blob = sqlite3_value_blob(argv[0]);

    uint64_t high = from_big_endian(*(uint64_t*)blob);
    uint64_t time_high = (high >> 16) & 0xFFFFFFFFFFFFULL;
    uint64_t time_low  = (high >> 6)  & 0x3FULL;

    sqlite3_result_int64(context, (sqlite3_int64)((time_high << 6) | time_low));
}

/* microshard_uuid_get_iso(blob) */
static void uuid_get_iso(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1 || sqlite3_value_bytes(argv[0]) != 16) return;
    const unsigned char *blob = sqlite3_value_blob(argv[0]);

    uint64_t high = from_big_endian(*(uint64_t*)blob);
    uint64_t high_time = (high >> 16) & 0xFFFFFFFFFFFFULL;
    uint64_t low_time  = (high >> 6)  & 0x3FULL;
    uint64_t micros = (high_time << 6) | low_time;

    time_t seconds = (time_t)(micros / 1000000ULL);
    int frac = (int)(micros % 1000000ULL);

    struct tm *tm_info = gmtime(&seconds);
    char buffer[40];
    strftime(buffer, 26, "%Y-%m-%dT%H:%M:%S", tm_info);

    char final_buf[40];
    snprintf(final_buf, 40, "%s.%06dZ", buffer, frac);

    sqlite3_result_text(context, final_buf, -1, SQLITE_TRANSIENT);
}

/*
** microshard_uuid_to_string(blob) -> text
** Converts 16-byte BLOB to "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
*/
static void uuid_to_string(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1 || sqlite3_value_bytes(argv[0]) != 16) return;

    const unsigned char *blob = sqlite3_value_blob(argv[0]);
    char buffer[37]; // 36 chars + null terminator

    // Format as standard UUID
    snprintf(buffer, sizeof(buffer),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        blob[0], blob[1], blob[2], blob[3],
        blob[4], blob[5],
        blob[6], blob[7],
        blob[8], blob[9],
        blob[10], blob[11], blob[12], blob[13], blob[14], blob[15]
    );

    sqlite3_result_text(context, buffer, 36, SQLITE_TRANSIENT);
}

// --- Helper: Hex Char to Int ---
static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
** microshard_uuid_from_string(text) -> blob
** Parses "018e..." or "018e...-..." into 16-byte BLOB
*/
static void uuid_from_string(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1) return;
    const unsigned char *text = sqlite3_value_text(argv[0]);
    if (!text) return;

    unsigned char blob[16];
    int blob_idx = 0;
    int high_nibble = -1;

    // Loop through string, skipping dashes, parsing hex
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
            if (blob_idx >= 16) break; // Should not happen for valid UUID
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

// --- Registration ---

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

  // 1. Generators: Mark as INNOCUOUS
  // This allows them to be used in DEFAULT clauses and Triggers.
  // They are NOT deterministic (volatile), so we don't add SQLITE_DETERMINISTIC.
  int flags_gen = SQLITE_UTF8 | SQLITE_INNOCUOUS;

  sqlite3_create_function(db, "microshard_uuid_generate", 1, flags_gen, 0, uuid_generate, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_from_micros", 2, flags_gen, 0, uuid_from_micros, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_from_iso", 2, flags_gen, 0, uuid_from_iso, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_from_string", 1, flags_gen, 0, uuid_from_string, 0, 0);

  // 2. Accessors: Mark as DETERMINISTIC + INNOCUOUS
  // These always return the same output for the same input.
  int flags_acc = SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS;

  sqlite3_create_function(db, "microshard_uuid_get_shard_id", 1, flags_acc, 0, uuid_get_shard_id, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_get_time", 1, flags_acc, 0, uuid_get_time, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_get_iso", 1, flags_acc, 0, uuid_get_iso, 0, 0);
  sqlite3_create_function(db, "microshard_uuid_to_string", 1, flags_acc, 0, uuid_to_string, 0, 0);

  return rc;
}
