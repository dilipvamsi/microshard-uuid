// File: tests/integration_tests.rs

use microshard_uuid::{MicroShardUUID, MicroShardError};
use std::time::{SystemTime, UNIX_EPOCH};

// Constant for 2^54 - 1 (Max supported microsecond timestamp)
const MAX_TIME_MICROS: u64 = 18_014_398_509_481_983;

#[test]
fn test_generation_and_extraction() {
    let shard = 12345;
    // API Change: generate is now a method of MicroShardUUID
    let uuid = MicroShardUUID::generate(shard).unwrap();

    // Validate string format
    let s = uuid.to_string();
    assert_eq!(s.len(), 36);
    assert_eq!(s.chars().nth(14), Some('8')); // Version 8

    // Validate extraction
    assert_eq!(uuid.shard_id(), shard);

    // Validate time (within 1 second)
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_micros() as u64;
    let extracted = uuid.timestamp_micros();
    
    // Allow for small clock skew/execution time
    assert!((now as i64 - extracted as i64).abs() < 1_000_000);
}

#[test]
fn test_backfill() {
    let ts = 1000_000_000_000; // Arbitrary time
    let uuid = MicroShardUUID::from_micros(ts, 99).unwrap();
    assert_eq!(uuid.timestamp_micros(), ts);
    assert_eq!(uuid.shard_id(), 99);
}

#[test]
fn test_shard_integrity() {
    // Edge cases: 0, 1, mid-range, and Max u32
    let shards = vec![0, 1, 500, 1024, 4_294_967_295];
    for shard in shards {
        let uuid = MicroShardUUID::generate(shard).expect("Generation failed");
        let extracted = uuid.shard_id();
        assert_eq!(extracted, shard, "Shard mismatch for ID: {}", uuid);
    }
}

#[test]
fn test_time_accuracy() {
    // 1. Get current system time
    let start = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_micros() as u64;

    // 2. Generate ID
    let uuid = MicroShardUUID::generate(100).unwrap();

    // 3. Get time immediately after
    let end = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_micros() as u64;

    // 4. Extract time from ID
    let extracted = uuid.timestamp_micros();

    // 5. Assert: start <= extracted <= end
    assert!(
        extracted >= start,
        "ID time {} is older than start time {}",
        extracted,
        start
    );
    assert!(
        extracted <= end,
        "ID time {} is newer than end time {}",
        extracted,
        end
    );
}

#[test]
fn test_micros_roundtrip() {
    // Arbitrary timestamp: 2025-12-12 01:35:00 UTC (approx)
    let input_micros: u64 = 1_765_503_300_123_456;

    // Backfill
    let uuid = MicroShardUUID::from_micros(input_micros, 55).unwrap();

    // Extract
    let output_micros = uuid.timestamp_micros();

    assert_eq!(output_micros, input_micros);
}

#[test]
fn test_rfc_compliance() {
    let uuid = MicroShardUUID::generate(1).unwrap();
    let bytes = uuid.as_bytes();

    // Byte 6: Version (High nibble) -> xxxx Mxxx
    let version_nibble = bytes[6] >> 4;
    assert_eq!(version_nibble, 8, "Version must be 8 (UUIDv8)");

    // Byte 8: Variant (Top 2 bits) -> Nxxx xxxx
    // Variant 2 is '10' binary (decimal 2)
    let variant_bits = bytes[8] >> 6;
    assert_eq!(variant_bits, 0b10, "Variant must be 2 (RFC 9562)");
}

#[test]
fn test_sorting() {
    // 1. Create an ID from the past (Year 2023)
    let past_micros = 1_672_531_200_000_000;
    let uuid_old = MicroShardUUID::from_micros(past_micros, 1).unwrap();

    // 2. Create an ID from now
    let uuid_new = MicroShardUUID::generate(1).unwrap();

    // 3. Compare String representations (Lexical Sort)
    assert!(
        uuid_old.to_string() < uuid_new.to_string(),
        "Lexical sort failed: {} should be less than {}",
        uuid_old,
        uuid_new
    );

    // 4. Compare Native Types (Ord trait)
    assert!(
        uuid_old < uuid_new,
        "Native sort failed: old UUID should be less than new UUID"
    );
}

#[test]
fn test_error_handling() {
    // Test Time Overflow (Year > 2541)
    let overflow_micros = MAX_TIME_MICROS + 1000;
    let res = MicroShardUUID::from_micros(overflow_micros, 1);

    assert!(res.is_err(), "Should catch time overflow");
    
    // Verify specific error formatting
    let err = res.unwrap_err();
    assert_eq!(format!("{}", err), "Time overflow (Year > 2541)");
}

#[test]
fn test_iso_parsing() {
    // 1. Standard
    let iso = "2023-01-01T12:00:00.123456Z";
    let uuid = MicroShardUUID::from_iso(iso, 1).expect("Failed to parse valid ISO");
    let out = uuid.to_iso_string();
    assert_eq!(iso, out);

    // 2. Leap Year (Feb 29)
    let iso_leap = "2024-02-29T10:00:00.000000Z";
    let uuid_leap = MicroShardUUID::from_iso(iso_leap, 1).expect("Failed to parse leap year");
    let out_leap = uuid_leap.to_iso_string();
    assert_eq!(iso_leap, out_leap);

    // 3. No fractional part (should auto-pad to .000000 in output)
    let iso_short = "2023-01-01T12:00:00Z";
    let uuid_short = MicroShardUUID::from_iso(iso_short, 1).unwrap();
    let out_short = uuid_short.to_iso_string();
    assert_eq!(out_short, "2023-01-01T12:00:00.000000Z");
}

#[test]
fn test_iso_errors() {
    // Malformed string
    assert!(matches!(
        MicroShardUUID::from_iso("bad-string", 1),
        Err(MicroShardError::InvalidIsoFormat)
    ));
    // Invalid month
    assert!(MicroShardUUID::from_iso("2023-99-01T00:00:00Z", 1).is_err());
}

#[test]
fn test_iso_validation() {
    // We validate by attempting to parse via the public API
    let is_valid = |s| MicroShardUUID::from_iso(s, 0).is_ok();

    // Valid cases
    assert!(is_valid("2023-01-01T12:00:00Z"));
    assert!(is_valid("2024-02-29T12:00:00.123456Z")); // Leap year
    assert!(is_valid("2025-12-31T23:59:59.999Z"));

    // Invalid cases
    assert!(!is_valid("not-a-date"));
    assert!(!is_valid("2023/01/01T12:00:00Z")); // Wrong separators
    assert!(!is_valid("2023-02-30T12:00:00Z")); // Feb 30 doesn't exist
    assert!(!is_valid("2023-01-01T25:00:00Z")); // Hour 25
    assert!(!is_valid("2023-13-01T12:00:00Z")); // Month 13
}

#[test]
fn test_civil_calendar_logic() {
    // Tests the internal date-math logic by checking if specific
    // ISO inputs result in the exact same ISO outputs (round-trip).
    // This verifies the conversion from String -> Micros -> String.

    let check = |expected_iso: &str| {
        let uuid = MicroShardUUID::from_iso(expected_iso, 0).unwrap();
        assert_eq!(uuid.to_iso_string(), expected_iso);
    };

    // Case 1: Unix Epoch (1970-01-01 00:00:00)
    check("1970-01-01T00:00:00.000000Z");

    // Case 2: Leap Year 2000 (Divisible by 400 - LEAP)
    check("2000-02-29T12:30:45.000000Z");

    // Case 3: Non-Leap Year 2100 (Divisible by 100 but not 400 - NOT LEAP)
    // Feb 28th
    check("2100-02-28T23:59:59.000000Z");
    // March 1st (Should not be Feb 29)
    check("2100-03-01T00:00:00.000000Z");

    // Case 4: Standard Leap Year 2024 (Divisible by 4)
    check("2024-02-29T15:00:00.000000Z");

    // Case 5: End of Year Rollover
    check("2023-12-31T23:59:59.000000Z");
    check("2024-01-01T00:00:00.000000Z");

    // Case 6: Microsecond Precision
    // Ensures min and max microseconds are preserved and don't roll over incorrectly
    check("2025-12-13T18:15:30.000001Z"); // Min positive micros
    check("2025-12-13T18:15:30.500000Z"); // Half second
    check("2025-12-13T18:15:30.999999Z"); // Max micros before next second
}

#[test]
fn test_iso_roundtrip_consistency() {
    // 1. Full precision roundtrip
    let input_full = "2025-12-13T14:05:00.123456Z";
    let uuid_full = MicroShardUUID::from_iso(input_full, 1).expect("Failed to parse full ISO");
    let output_full = uuid_full.to_iso_string();
    assert_eq!(input_full, output_full, "Full precision roundtrip failed");

    // 2. Normalization Check (Input short -> Output long)
    let input_short = "2023-01-01T02:11:10Z";
    let expected_normalized = "2023-01-01T02:11:10.000000Z";

    let uuid_short = MicroShardUUID::from_iso(input_short, 1).expect("Failed to parse short ISO");
    let output_short = uuid_short.to_iso_string();

    assert_eq!(
        output_short, expected_normalized,
        "Normalization roundtrip failed"
    );
}
