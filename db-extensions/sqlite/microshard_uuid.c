/*
** MicroShard UUID - SQLite Extension (Zero-Dependency Optimized)
**
** Architecture: 54-bit Time | 32-bit Shard | 36-bit Random
** Compliance:   IETF RFC 9562 (UUIDv8)
**
** Description:
**   A high-performance SQLite extension exposing MicroShard UUID functionality.
**   It maps SQL functions to the core C library logic.
**
** Dependencies:
**   - microshard_uuid.h (Header-Only Library)
**   - sqlite3ext.h      (SQLite Interface)
**
** Thread Safety:
**   This extension is safe to use in SQLite WAL mode and multi-threaded apps.
**   It relies on the 'microshard_uuid.h' Thread Local Storage (TLS) for RNG.
**
** Features:
**   - Zero external dependencies (No OpenSSL/Windows Crypt API).
**   - Native BLOB and TEXT support.
**
** Compilation:
**   gcc -O2 -fPIC -shared sqlite_extension.c -o microshard_uuid.so
**   (Add -I/path/to/headers if necessary)
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

#include "microshard_uuid.h"

/*
** ============================================================================
** Helper Functions
** ============================================================================
*/

/*
** Checks the status code from the library.
** If it's an error, sets the SQLite result error message and returns 1.
** Returns 0 on Success.
*/
static int check_error(sqlite3_context *ctx, ms_status_t status) {
    if (status != MS_OK) {
        sqlite3_result_error(ctx, ms_strerror(status), -1);
        return 1;
    }
    return 0;
}

/*
** ============================================================================
** SQL Function Implementations (Generators)
** ============================================================================
*/

/*
** SQL: microshard_uuid_generate(shard_id INTEGER) -> BLOB
** Desc: Generates a new 16-byte UUID using current system time.
*/
static void fn_generate_blob(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    int64_t shard = sqlite3_value_int64(argv[0]);

    /* Input Validation */
    if (shard < 0 || shard > MS_MAX_SHARD_ID) {
        sqlite3_result_error(ctx, "Shard ID out of range (0 - 4,294,967,295)", -1);
        return;
    }

    /* Core Logic: Generate UUID struct */
    ms_uuid_t u = ms_generate((uint32_t)shard);

    /* Serialize to BLOB */
    unsigned char blob[16];
    if (check_error(ctx, ms_to_bytes_be(u, blob, sizeof(blob)))) return;

    sqlite3_result_blob(ctx, blob, 16, SQLITE_TRANSIENT);
}

/*
** SQL: microshard_uuid_generate_text(shard_id INTEGER) -> TEXT
** Desc: Generates a new 36-char UUID string using current system time.
*/
static void fn_generate_text(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    int64_t shard = sqlite3_value_int64(argv[0]);

    if (shard < 0 || shard > MS_MAX_SHARD_ID) {
        sqlite3_result_error(ctx, "Shard ID out of range", -1);
        return;
    }

    ms_uuid_t u = ms_generate((uint32_t)shard);

    char buf[37];
    if (check_error(ctx, ms_to_string(u, buf, sizeof(buf)))) return;

    sqlite3_result_text(ctx, buf, 36, SQLITE_TRANSIENT);
}

/*
** SQL: microshard_uuid_from_micros(micros INTEGER, shard_id INTEGER) -> BLOB
** Desc: Backfills a UUID for a specific timestamp.
** Note: Uses internal RNG for the random component to ensure collision resistance.
*/
static void fn_from_micros(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    int64_t mic = sqlite3_value_int64(argv[0]);
    int64_t shard = sqlite3_value_int64(argv[1]);

    if (mic < 0 || shard < 0 || shard > MS_MAX_SHARD_ID) {
        sqlite3_result_error(ctx, "Invalid timestamp or shard ID", -1);
        return;
    }

    /*
    ** Use ms_build with specific time.
    ** We access _ms_next_36() from the header to fill the random bits.
    */
    ms_uuid_t u = ms_build((uint64_t)mic, (uint32_t)shard, _ms_next_36());

    unsigned char blob[16];
    if (check_error(ctx, ms_to_bytes_be(u, blob, sizeof(blob)))) return;

    sqlite3_result_blob(ctx, blob, 16, SQLITE_TRANSIENT);
}

/*
** SQL: microshard_uuid_from_iso(iso_str TEXT, shard_id INTEGER) -> BLOB
** Desc: Parses an ISO 8601 string and generates a UUID for that time.
*/
static void fn_from_iso(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    const char* str = (const char*)sqlite3_value_text(argv[0]);
    int64_t shard = sqlite3_value_int64(argv[1]);

    if (shard < 0 || shard > MS_MAX_SHARD_ID) {
        sqlite3_result_error(ctx, "Invalid shard ID", -1);
        return;
    }

    /* Parse ISO Time */
    uint64_t mic;
    if (check_error(ctx, ms_parse_iso(str, &mic))) return;

    ms_uuid_t u = ms_build(mic, (uint32_t)shard, _ms_next_36());

    unsigned char blob[16];
    if (check_error(ctx, ms_to_bytes_be(u, blob, sizeof(blob)))) return;

    sqlite3_result_blob(ctx, blob, 16, SQLITE_TRANSIENT);
}

/*
** ============================================================================
** SQL Function Implementations (Converters & Extractors)
** ============================================================================
*/

/*
** SQL: microshard_uuid_from_string(uuid_str TEXT) -> BLOB
** Desc: Converts a standard UUID string representation to binary.
*/
static void fn_from_string(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    const char* str = (const char*)sqlite3_value_text(argv[0]);

    ms_uuid_t u;
    if (check_error(ctx, ms_from_string(str, &u))) return;

    unsigned char blob[16];
    if (check_error(ctx, ms_to_bytes_be(u, blob, sizeof(blob)))) return;

    sqlite3_result_blob(ctx, blob, 16, SQLITE_TRANSIENT);
}

/*
** SQL: microshard_uuid_to_string(uuid_blob BLOB) -> TEXT
** Desc: Converts binary UUID to canonical string format.
*/
static void fn_to_string(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (sqlite3_value_bytes(argv[0]) != 16) {
        /* Return NULL if blob size is wrong to avoid garbage output */
        sqlite3_result_null(ctx);
        return;
    }

    const unsigned char* blob = sqlite3_value_blob(argv[0]);
    ms_uuid_t u = ms_from_bytes_be(blob);

    char buf[37];
    if (check_error(ctx, ms_to_string(u, buf, sizeof(buf)))) return;

    sqlite3_result_text(ctx, buf, 36, SQLITE_TRANSIENT);
}

/*
** SQL: microshard_uuid_get_shard_id(uuid_blob BLOB) -> INTEGER
** Desc: Extracts the Shard ID (Zero-Lookup) from the binary UUID.
*/
static void fn_get_shard_id(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (sqlite3_value_bytes(argv[0]) != 16) return;

    const unsigned char* blob = sqlite3_value_blob(argv[0]);
    ms_uuid_t u = ms_from_bytes_be(blob);

    sqlite3_result_int64(ctx, (sqlite3_int64)ms_extract_shard(u));
}

/*
** SQL: microshard_uuid_get_time(uuid_blob BLOB) -> INTEGER
** Desc: Extracts the timestamp in microseconds from the binary UUID.
*/
static void fn_get_time(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (sqlite3_value_bytes(argv[0]) != 16) return;

    const unsigned char* blob = sqlite3_value_blob(argv[0]);
    ms_uuid_t u = ms_from_bytes_be(blob);

    sqlite3_result_int64(ctx, (sqlite3_int64)ms_extract_time(u));
}

/*
** SQL: microshard_uuid_get_iso(uuid_blob BLOB) -> TEXT
** Desc: Extracts timestamp and formats as ISO 8601 string.
*/
static void fn_get_iso(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (sqlite3_value_bytes(argv[0]) != 16) return;

    const unsigned char* blob = sqlite3_value_blob(argv[0]);
    ms_uuid_t u = ms_from_bytes_be(blob);

    char buf[40];
    if (check_error(ctx, ms_extract_iso(u, buf, sizeof(buf)))) return;

    sqlite3_result_text(ctx, buf, -1, SQLITE_TRANSIENT);
}

/*
** SQL: microshard_uuid_validate_iso(iso_str TEXT) -> INTEGER
** Desc: Returns MS_OK (0) if valid, or an error code if invalid.
**       Useful for CHECK constraints.
*/
static void fn_validate_iso(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    const char* str = (const char*)sqlite3_value_text(argv[0]);
    uint64_t dummy;

    // Convert MS_OK (0) -> 1 (True), Errors -> 0 (False)
    ms_status_t status = ms_parse_iso(str, &dummy);
    sqlite3_result_int(ctx, (status == MS_OK) ? 1 : 0);
}

/*
** ============================================================================
** Extension Registration
** ============================================================================
*/

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

    /*
    ** SQLITE_INNOCUOUS:
    ** Functions are safe to be used in views, triggers, and schemas
    ** even if strict security is enabled.
    */

    /* Generators (Volatile: results change per call) */
    int f_gen = SQLITE_UTF8 | SQLITE_INNOCUOUS;

    /* Converters (Deterministic: same input = same output) */
    int f_det = SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS;

    /* Register Functions */
    sqlite3_create_function(db, "microshard_uuid_generate", 1, f_gen, 0, fn_generate_blob, 0, 0);
    sqlite3_create_function(db, "microshard_uuid_generate_text", 1, f_gen, 0, fn_generate_text, 0, 0);
    sqlite3_create_function(db, "microshard_uuid_from_micros", 2, f_gen, 0, fn_from_micros, 0, 0);
    sqlite3_create_function(db, "microshard_uuid_from_iso", 2, f_gen, 0, fn_from_iso, 0, 0);

    sqlite3_create_function(db, "microshard_uuid_from_string", 1, f_det, 0, fn_from_string, 0, 0);
    sqlite3_create_function(db, "microshard_uuid_to_string", 1, f_det, 0, fn_to_string, 0, 0);
    sqlite3_create_function(db, "microshard_uuid_get_shard_id", 1, f_det, 0, fn_get_shard_id, 0, 0);
    sqlite3_create_function(db, "microshard_uuid_get_time", 1, f_det, 0, fn_get_time, 0, 0);
    sqlite3_create_function(db, "microshard_uuid_get_iso", 1, f_det, 0, fn_get_iso, 0, 0);
    sqlite3_create_function(db, "microshard_uuid_validate_iso", 1, f_det, 0, fn_validate_iso, 0, 0);

    return rc;
}
