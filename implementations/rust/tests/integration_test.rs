use chrono::{TimeZone, Utc};
use microshard_uuid::{
    from_iso, generate, get_iso_timestamp, get_shard_id, get_timestamp, Generator,
};

#[test]
fn test_shard_integrity() {
    let shards = vec![0, 1, 500, 1024, 4294967295];
    for shard in shards {
        let uuid = generate(shard).unwrap();
        let extracted = get_shard_id(&uuid);
        assert_eq!(extracted, shard, "Shard mismatch for {}", shard);
    }
}

#[test]
fn test_time_accuracy() {
    let start = Utc::now();
    let uuid = generate(100).unwrap();
    let end = Utc::now();

    let extracted = get_timestamp(&uuid);

    // Allow 100ms delta
    let delta = chrono::Duration::milliseconds(100);

    assert!(extracted >= start - delta);
    assert!(extracted <= end + delta);
}

#[test]
fn test_iso_roundtrip() {
    // 2025-12-12 01:35:00.123456 UTC
    let input_iso = "2025-12-12T01:35:00.123456Z";

    // Backfill
    let uuid = from_iso(input_iso, 55).unwrap();

    // Extract
    let output_iso = get_iso_timestamp(&uuid);

    assert_eq!(output_iso, input_iso);
}

#[test]
fn test_rfc_compliance() {
    let uuid = generate(1).unwrap();

    // Get Version (bits 48-51 of high 64)
    let _ver = uuid.get_version_num();
    // uuid crate helper returns standard version logic.
    // However, for custom v8, we manually packed '8'.
    // Let's verify manually via bytes to be sure.
    let bytes = uuid.as_bytes();

    // Byte 6: xxxx Mxxx (M is version)
    let version_nibble = bytes[6] >> 4;
    assert_eq!(version_nibble, 8, "Version must be 8");

    // Byte 8: Nxxx xxxx (N is variant)
    // Variant 2 is 10xx binary (8, 9, A, B hex)
    let variant_nibble = bytes[8] >> 6; // Top 2 bits
    assert_eq!(variant_nibble, 0b10, "Variant must be 2 (RFC)");
}

#[test]
fn test_generator_struct() {
    let gen = Generator::new(777).unwrap();
    let uuid = gen.new_id().unwrap();
    assert_eq!(get_shard_id(&uuid), 777);
}

#[test]
fn test_sorting() {
    // Old ID
    let _old_time = Utc.timestamp_opt(1672531200, 0).unwrap(); // Jan 1 2023
                                                               // Manually construct using from_micros logic or string
    let uuid_old = from_iso("2023-01-01T00:00:00.000000Z", 1).unwrap();

    // New ID
    let uuid_new = generate(1).unwrap();

    assert!(uuid_old < uuid_new, "Lexical sort failed");
}

#[test]
fn test_error_handling() {
    // Time overflow (Year > 2541)
    // 2^54 is the limit
    // Valid year: 2540. Let's try year 3000.
    let future_iso = "3000-01-01T00:00:00.000000Z";
    let res = from_iso(future_iso, 1);
    assert!(res.is_err(), "Should catch time overflow");
}
