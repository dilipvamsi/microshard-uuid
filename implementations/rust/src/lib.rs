use chrono::{DateTime, SecondsFormat, TimeZone, Utc};
use rand::RngCore;
use std::time::{SystemTime, UNIX_EPOCH};
use thiserror::Error;
use uuid::Uuid;

// --- Constants ---
const MAX_SHARD_ID: u32 = 4_294_967_295; // 2^32 - 1
const MAX_TIME_MICROS: u64 = 18_014_398_509_481_983; // 2^54 - 1
const MAX_RANDOM: u64 = 68_719_476_735; // 2^36 - 1

// --- Error Type ---
#[derive(Error, Debug)]
pub enum MicroShardError {
    #[error("Shard ID must be between 0 and {0}")]
    InvalidShardId(u32),
    #[error("Time overflow (Year > 2541)")]
    TimeOverflow,
    #[error("Invalid ISO 8601 timestamp format")]
    InvalidIsoFormat,
}

// ==========================================
// Stateless Functions
// ==========================================

/// Generates a new UUIDv8 using the current system time for a specific shard.
pub fn generate(shard_id: u32) -> Result<Uuid, MicroShardError> {
    validate_shard(shard_id)?;

    let start = SystemTime::now();
    let since_epoch = start
        .duration_since(UNIX_EPOCH)
        .expect("Time went backwards");
    let micros = since_epoch.as_micros() as u64;

    build_uuid(micros, shard_id)
}

/// Generates a UUIDv8 for a specific timestamp (Microseconds).
/// Useful for backfilling using integer timestamps.
pub fn from_micros(micros: u64, shard_id: u32) -> Result<Uuid, MicroShardError> {
    validate_shard(shard_id)?;
    build_uuid(micros, shard_id)
}

/// Generates a UUIDv8 for a specific ISO 8601 timestamp string.
/// Example: "2025-12-12T10:00:00.123456Z"
pub fn from_iso(iso_str: &str, shard_id: u32) -> Result<Uuid, MicroShardError> {
    validate_shard(shard_id)?;

    // Parse ISO string using Chrono
    let dt = DateTime::parse_from_rfc3339(iso_str)
        .map_err(|_| MicroShardError::InvalidIsoFormat)?
        .with_timezone(&Utc);

    // Convert to microseconds
    // timestamp_micros() returns i64, cast to u64
    let micros = dt.timestamp_micros();
    if micros < 0 {
        return Err(MicroShardError::InvalidIsoFormat); // No negative time supported
    }

    build_uuid(micros as u64, shard_id)
}

/// Extracts the 32-bit Shard ID from a UUID.
pub fn get_shard_id(uuid: &Uuid) -> u32 {
    let val = uuid.as_u128();
    let high = (val >> 64) as u64;
    let low = val as u64;

    // High[5:0] -> Shard High (6 bits)
    let shard_high = high & 0x3F;
    // Low[63:36] -> Shard Low (26 bits)
    let shard_low = (low >> 36) & 0x3FFFFFF;

    ((shard_high << 26) | shard_low) as u32
}

/// Extracts the creation time from a UUID as a Chrono DateTime<Utc>.
pub fn get_timestamp(uuid: &Uuid) -> DateTime<Utc> {
    let val = uuid.as_u128();
    let high = (val >> 64) as u64;

    // High[63:16] -> Time High (48 bits)
    let time_high = (high >> 16) & 0xFFFFFFFFFFFF;
    // High[11:6] -> Time Low (6 bits)
    let time_low = (high >> 6) & 0x3F;

    let micros = (time_high << 6) | time_low;

    // Convert micros to Seconds + Nanos for Chrono
    let seconds = (micros / 1_000_000) as i64;
    let nanos = ((micros % 1_000_000) * 1_000) as u32;

    Utc.timestamp_opt(seconds, nanos).unwrap()
}

/// Extracts the creation time as an ISO 8601 string.
/// Preserves microsecond precision.
pub fn get_iso_timestamp(uuid: &Uuid) -> String {
    let dt = get_timestamp(uuid);
    // Use RFC3339 with microseconds specifically
    dt.to_rfc3339_opts(SecondsFormat::Micros, true)
}

// ==========================================
// Stateful Generator Struct
// ==========================================

pub struct Generator {
    shard_id: u32,
}

impl Generator {
    pub fn new(default_shard_id: u32) -> Result<Self, MicroShardError> {
        validate_shard(default_shard_id)?;
        Ok(Self {
            shard_id: default_shard_id,
        })
    }

    pub fn new_id(&self) -> Result<Uuid, MicroShardError> {
        generate(self.shard_id)
    }

    pub fn from_iso(&self, iso_str: &str) -> Result<Uuid, MicroShardError> {
        from_iso(iso_str, self.shard_id)
    }
}

// ==========================================
// Internal Helpers
// ==========================================

fn validate_shard(shard_id: u32) -> Result<(), MicroShardError> {
    if shard_id > MAX_SHARD_ID {
        return Err(MicroShardError::InvalidShardId(MAX_SHARD_ID));
    }
    Ok(())
}

fn build_uuid(micros: u64, shard_id: u32) -> Result<Uuid, MicroShardError> {
    if micros > MAX_TIME_MICROS {
        return Err(MicroShardError::TimeOverflow);
    }

    // Randomness (36 bits)
    let mut rng = rand::thread_rng();
    let mut rnd_bytes = [0u8; 8];
    rng.fill_bytes(&mut rnd_bytes);
    let rnd_val = u64::from_be_bytes(rnd_bytes) & MAX_RANDOM;

    let shard_id_64 = shard_id as u64;

    // --- High 64 Bits ---
    let time_high = (micros >> 6) & 0xFFFFFFFFFFFF;
    let time_low = micros & 0x3F;
    let shard_high = (shard_id_64 >> 26) & 0x3F;

    let high_64 = (time_high << 16) | (8 << 12) | (time_low << 6) | shard_high;

    // --- Low 64 Bits ---
    let shard_low = shard_id_64 & 0x3FFFFFF;
    let low_64 = (2 << 62) | (shard_low << 36) | rnd_val;

    Ok(Uuid::from_u128(
        ((high_64 as u128) << 64) | (low_64 as u128),
    ))
}
