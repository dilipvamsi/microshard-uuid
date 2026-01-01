use std::cell::RefCell;
use std::fmt;
use std::time::{SystemTime, UNIX_EPOCH};

// ==========================================
// Constants & Configuration
// ==========================================

const MAX_SHARD_ID: u32 = 4_294_967_295; // 2^32 - 1
const MAX_TIME_MICROS: u64 = 18_014_398_509_481_983; // 2^54 - 1
const MAX_RANDOM: u64 = 68_719_476_735; // 2^36 - 1

// ==========================================
// Error Handling
// ==========================================

/// Custom error type for MicroShard operations.
/// Designed to replace `thiserror` for zero-dependency environments.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MicroShardError {
    InvalidShardId(u32),
    TimeOverflow,
    InvalidIsoFormat,
    SystemTimeError,
    InvalidVersion(u8),
    InvalidVariant(u8),
}

impl fmt::Display for MicroShardError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidShardId(id) => write!(f, "Shard ID must be between 0 and {}", id),
            Self::TimeOverflow => write!(f, "Time overflow (Year > 2541)"),
            Self::InvalidIsoFormat => write!(f, "Invalid ISO 8601 timestamp format"),
            Self::SystemTimeError => write!(f, "System time went backwards"),
            Self::InvalidVersion(v) => write!(f, "Invalid UUID Version: {}, expected 8", v),
            Self::InvalidVariant(v) => write!(f, "Invalid UUID Variant: {}, expected 2", v),
        }
    }
}

impl std::error::Error for MicroShardError {}

// ==========================================
// Core Struct: MicroShardUUID
// ==========================================

/// A custom, sortable, sharded UUID (UUIDv8).
///
/// **Layout:**
/// - **High 64 bits:** `[Time (48 bits)] [Ver (4 bits)] [TimeLow (6 bits)] [ShardHigh (6 bits)]`
/// - **Low 64 bits:**  `[Var (2 bits)] [ShardLow (26 bits)] [Random (36 bits)]`
///
/// This structure derives `Ord` and `PartialOrd` based on the underlying `u128`,
/// ensuring that UUIDs sort chronologically by default.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug, PartialOrd, Ord)]
pub struct MicroShardUUID(u128);

impl MicroShardUUID {

    pub fn high(&self) -> u64 {
        (self.0 >> 64) as u64
    }

    pub fn low(&self) -> u64 {
        self.0 as u64
    }

    // -------------------------------------------------------------------------
    // Constructors
    // -------------------------------------------------------------------------

    /// Generates a new `MicroShardUUID` using the current system time.
    ///
    /// # Arguments
    /// * `shard_id` - A unique identifier for the machine/process generating the ID (max u32).
    pub fn generate(shard_id: u32) -> Result<Self, MicroShardError> {
        validate_shard(shard_id)?;

        let start = SystemTime::now();
        let since_epoch = start
            .duration_since(UNIX_EPOCH)
            .map_err(|_| MicroShardError::SystemTimeError)?;

        let micros = since_epoch.as_micros() as u64;

        Self::build(micros, shard_id)
    }

    /// Generates a `MicroShardUUID` from a specific timestamp in microseconds.
    pub fn from_micros(micros: u64, shard_id: u32) -> Result<Self, MicroShardError> {
        validate_shard(shard_id)?;
        Self::build(micros, shard_id)
    }

    /// Generates a `MicroShardUUID` from an ISO 8601 string.
    ///
    /// # Format
    /// Expected format: `YYYY-MM-DDTHH:MM:SS.mmmmmmZ`
    ///
    pub fn from_iso(iso_str: &str, shard_id: u32) -> Result<Self, MicroShardError> {
        validate_shard(shard_id)?;
        let micros = parse_iso_strict(iso_str)?;
        Self::build(micros, shard_id)
    }

    /// Constructs a UUID from a raw `u128` value with strict validation.
    /// Checks for RFC 9562 compliance (Version 8, Variant 2).
    pub fn from_u128(v: u128) -> Result<Self, MicroShardError> {
        // 1. Check Version (Must be 8)
        // Layout High 64: [TimeHigh 48] [Ver 4] [TimeLow 6] [ShardHigh 6]
        // Version is at bits 12-15 of the High 64-bit word.
        // Relative to u128: 64 + 12 = 76.
        let version = ((v >> 76) & 0xF) as u8;
        if version != 8 {
            return Err(MicroShardError::InvalidVersion(version));
        }

        // 2. Check Variant (Must be 2)
        // Layout Low 64: [Var 2] [ShardLow 26] [Random 36]
        // Variant is at bits 62-63 of the Low 64-bit word.
        // Relative to u128: 62.
        // Variant 2 is binary '10' (decimal 2).
        let variant = ((v >> 62) & 0x3) as u8;
        if variant != 2 {
            return Err(MicroShardError::InvalidVariant(variant));
        }

        Ok(Self(v))
    }

    /// Constructs a UUID from a 16-byte array (Big Endian).
    pub fn from_bytes(bytes: [u8; 16]) -> Result<Self, MicroShardError> {
        Self::from_u128(u128::from_be_bytes(bytes))
    }

    // -------------------------------------------------------------------------
    // Accessors & Converters
    // -------------------------------------------------------------------------

    /// Returns the raw `u128` value.
    /// This is the fastest way to pass the UUID around internally.
    #[inline(always)]
    pub fn as_u128(&self) -> u128 {
        self.0
    }

    /// Returns the UUID as a standard 16-byte array (Big Endian).
    /// Necessary for interoperability with other libraries or network/disk IO.
    pub fn as_bytes(&self) -> [u8; 16] {
        self.0.to_be_bytes()
    }

    /// Extracts the 32-bit Shard ID embedded in the UUID.
    pub fn shard_id(&self) -> u32 {
        let val = self.0; // Direct access to u128

        // Logic:
        // 1. Shift top 64 bits down to extract High part
        // 2. High[5:0] -> Shard High
        // 3. Low[63:36] -> Shard Low

        let high = (val >> 64) as u64;
        let low = val as u64;

        let shard_high = high & 0x3F;
        let shard_low = (low >> 36) & 0x3FFFFFF;

        ((shard_high << 26) | shard_low) as u32
    }

    /// Extracts the creation time as raw microseconds since Unix Epoch.
    pub fn timestamp_micros(&self) -> u64 {
        let val = self.0; // Direct access to u128
        let high = (val >> 64) as u64;

        let time_high = (high >> 16) & 0xFFFFFFFFFFFF;
        let time_low = (high >> 6) & 0x3F;

        (time_high << 6) | time_low
    }

    /// Extracts the creation time and formats it as an ISO 8601 string.
    /// Format: `YYYY-MM-DDTHH:MM:SS.mmmmmmZ`
    pub fn to_iso_string(&self) -> String {
        let total_micros = self.timestamp_micros();

        let seconds = total_micros / 1_000_000;
        let micros = total_micros % 1_000_000;

        let (year, month, day, hour, min, sec) = unix_to_civil(seconds);

        format!(
            "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:06}Z",
            year, month, day, hour, min, sec, micros
        )
    }

    // -------------------------------------------------------------------------
    // Internal Construction Helper
    // -------------------------------------------------------------------------

    /// Internal builder that composes the bits.
    fn build(micros: u64, shard_id: u32) -> Result<Self, MicroShardError> {
        if micros > MAX_TIME_MICROS {
            return Err(MicroShardError::TimeOverflow);
        }

        // Get 36 bits of randomness from Thread-Local Xoshiro256**
        let rnd_val = Xoshiro256StarStar::next_36();

        let shard_id_64 = shard_id as u64;

        // --- High 64 Bits ---
        let time_high = (micros >> 6) & 0xFFFFFFFFFFFF;
        let time_low = micros & 0x3F;
        let shard_high = (shard_id_64 >> 26) & 0x3F;

        // Version 8 at pos 12
        let high_64 = (time_high << 16) | (8 << 12) | (time_low << 6) | shard_high;

        // --- Low 64 Bits ---
        let shard_low = shard_id_64 & 0x3FFFFFF;
        // Variant 2 at pos 62
        let low_64 = (2 << 62) | (shard_low << 36) | rnd_val;

        // Combine into u128 directly
        Ok(Self(((high_64 as u128) << 64) | (low_64 as u128)))
    }
}

// -----------------------------------------------------------------------------
// Trait Implementations
// -----------------------------------------------------------------------------

// Implements standard 8-4-4-4-12 hex string formatting
impl fmt::Display for MicroShardUUID {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // We convert to bytes for formatting to ensure Big Endian (Network) order
        // regardless of the host machine's endianness.
        let b = self.as_bytes();
        write!(
            f,
            "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
            b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]
        )
    }
}


// ==========================================
// Internal: PRNG (Xoshiro256**)
// ==========================================

/// Internal State for Xoshiro256**
struct XoshiroState {
    s: [u64; 4],
    init: bool,
}

impl XoshiroState {
    const fn new() -> Self {
        Self {
            s: [0; 4],
            init: false,
        }
    }
}

// Thread-Local Storage for the RNG state.
// This acts like `static MS_TLS` in C.
thread_local! {
    static RNG_STATE: RefCell<XoshiroState> = RefCell::new(XoshiroState::new());
}

struct Xoshiro256StarStar;

impl Xoshiro256StarStar {
    /// Internal: Rotate Left
    #[inline(always)]
    fn rotl(x: u64, k: i32) -> u64 {
        (x << k) | (x >> (64 - k))
    }

    /// Internal: SplitMix64 (Used for bootstrapping seed)
    fn splitmix64(x: &mut u64) -> u64 {
        *x = x.wrapping_add(0x9e3779b97f4a7c15);
        let mut z = *x;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d049bb133111eb);
        z ^ (z >> 31)
    }

    /// Internal: Get High-Res Nanoseconds for Seeding
    fn get_nanos_seed() -> u64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos() as u64
    }

    /// Public: Get next 36 bits of randomness.
    /// Handles lazy initialization.
    fn next_36() -> u64 {
        RNG_STATE.with(|cell| {
            let mut ctx = cell.borrow_mut();

            // 1. Auto-Seed if not initialized
            if !ctx.init {
                let now = Self::get_nanos_seed();

                // ASLR Entropy: XOR time with the address of the state variable on stack/heap
                let ptr = &*ctx as *const _ as u64;
                let mut seed_val = now ^ ptr;

                ctx.s[0] = Self::splitmix64(&mut seed_val);
                ctx.s[1] = Self::splitmix64(&mut seed_val);
                ctx.s[2] = Self::splitmix64(&mut seed_val);
                ctx.s[3] = Self::splitmix64(&mut seed_val);
                ctx.init = true;
            }

            // 2. Xoshiro256** Algorithm
            let result = Self::rotl(ctx.s[1].wrapping_mul(5), 7).wrapping_mul(9);
            let t = ctx.s[1] << 17;

            ctx.s[2] ^= ctx.s[0];
            ctx.s[3] ^= ctx.s[1];
            ctx.s[1] ^= ctx.s[2];
            ctx.s[0] ^= ctx.s[3];

            ctx.s[2] ^= t;
            ctx.s[3] = Self::rotl(ctx.s[3], 45);

            // 3. Return truncated to 36 bits
            result & MAX_RANDOM
        })
    }
}

// ==========================================
// Internal: Helpers & Utilities
// ==========================================

#[inline(always)]
fn validate_shard(shard_id: u32) -> Result<(), MicroShardError> {
    if shard_id > MAX_SHARD_ID {
        return Err(MicroShardError::InvalidShardId(MAX_SHARD_ID));
    }
    Ok(())
}

// ==========================================
// Internal: Zero-Dependency Date/Time Logic
// ==========================================

/// Internal helper: Parses ISO string to microseconds.
/// Contains all the strict validation logic (Zero-Dep).
fn parse_iso_strict(iso_str: &str) -> Result<u64, MicroShardError> {
    // 1. Basic Length Check
    // minimal: "2023-01-01T00:00:00Z" (20 chars)
    if iso_str.len() < 20 {
        return Err(MicroShardError::InvalidIsoFormat);
    }

    // 2. Separator Check (Strict ISO 8601)
    // Expect: YYYY-MM-DDTHH:MM:SS...
    let b = iso_str.as_bytes();
    if b[4] != b'-' || b[7] != b'-' || b[10] != b'T' || b[13] != b':' || b[16] != b':' {
        return Err(MicroShardError::InvalidIsoFormat);
    }

    // 3. Parse Numbers
    let parse_chunk = |s: &str| -> Result<u32, MicroShardError> {
        s.parse::<u32>().map_err(|_| MicroShardError::InvalidIsoFormat)
    };

    let year = iso_str[0..4].parse::<i32>().map_err(|_| MicroShardError::InvalidIsoFormat)?;
    let month = parse_chunk(&iso_str[5..7])?;
    let day = parse_chunk(&iso_str[8..10])?;
    let hour = parse_chunk(&iso_str[11..13])?;
    let min = parse_chunk(&iso_str[14..16])?;
    let sec = parse_chunk(&iso_str[17..19])?;

    // 4. Logical Range Validation
    if month < 1 || month > 12 {
        return Err(MicroShardError::InvalidIsoFormat);
    }
    if hour > 23 || min > 59 || sec > 60 {
        // 60 allowed for leap seconds
        return Err(MicroShardError::InvalidIsoFormat);
    }

    // Days in Month Check (Handles Feb 29)
    let days_in_month = match month {
        4 | 6 | 9 | 11 => 30,
        2 => {
            if is_leap(year) {
                29
            } else {
                28
            }
        }
        _ => 31,
    };

    if day < 1 || day > days_in_month {
        return Err(MicroShardError::InvalidIsoFormat);
    }

    // 5. Parse Microseconds (Optional)
    let mut micros = 0;
    if iso_str.len() > 20 {
        // Must start with dot
        if b[19] != b'.' {
            return Err(MicroShardError::InvalidIsoFormat);
        }

        let end = iso_str.find('Z').unwrap_or(iso_str.len());
        let frac_str = &iso_str[20..end];

        let mut multiplier = 100_000;
        for c in frac_str.chars() {
            if let Some(digit) = c.to_digit(10) {
                if multiplier >= 1 {
                    micros += digit * multiplier;
                    multiplier /= 10;
                }
            } else {
                return Err(MicroShardError::InvalidIsoFormat);
            }
        }
    }

    // 6. Convert to Unix Epoch
    let days_since_epoch = date_to_days(year, month, day);
    if days_since_epoch < 0 {
        return Err(MicroShardError::InvalidIsoFormat);
    }

    let seconds = (days_since_epoch as u64 * 86400)
        + (hour as u64 * 3600)
        + (min as u64 * 60)
        + sec as u64;

    Ok(seconds * 1_000_000 + micros as u64)
}

/// Calculates the number of days from Year 0000 to the start of the given year.
/// Formula: 365*y + y/4 - y/100 + y/400
fn days_from_civil(y: i32) -> i64 {
    // We calculate completed years, so subtract 1 from input
    let y = y as i64 - 1;
    y * 365 + y / 4 - y / 100 + y / 400
}

/// Converts Y/M/D to Unix Epoch Days (Days since 1970-01-01)
fn date_to_days(y: i32, m: u32, d: u32) -> i64 {
    const DAYS_BEFORE: [i64; 12] = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334];

    // 1. Calculate absolute days from Year 0
    let mut days = days_from_civil(y);

    // 2. Subtract the number of days from Year 0 to 1970-01-01
    //    (This constant is derived from days_from_civil(1970))
    days -= 719162;

    // 3. Add days for months passed in current year
    days += DAYS_BEFORE[(m - 1) as usize];

    // 4. Leap year adjustment for current year
    //    If it's a leap year AND we are past February, add 1 day
    if m > 2 && is_leap(y) {
        days += 1;
    }

    // 5. Add days in current month (1-based to 0-based)
    days + (d as i64 - 1)
}

fn is_leap(year: i32) -> bool {
    (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)
}

/// Converts a Unix Timestamp (u64 seconds) into civil date components:
/// (Year, Month, Day, Hour, Minute, Second).
///
/// Algorithm: Howard Hinnant's "civil_from_days".
/// It shifts the epoch to 0000-03-01 to simplify leap year logic,
/// effectively moving Feb 29 to the very end of the year cycle.
fn unix_to_civil(ts: u64) -> (i32, u32, u32, u32, u32, u32) {
    // --------------------------------------------------------
    // Part 1: Time Extraction (HH:MM:SS)
    // --------------------------------------------------------
    // 86400 seconds in a day. Integer division gives total full days.
    let days = ts / 86400;
    // Modulo gives the seconds elapsed in the current partial day.
    let rem_sec = ts % 86400;

    let hour = (rem_sec / 3600) as u32;
    let min = ((rem_sec % 3600) / 60) as u32;
    let sec = (rem_sec % 60) as u32;

    // --------------------------------------------------------
    // Part 2: Date Conversion (Hinnant's Algorithm)
    // --------------------------------------------------------

    // 1. Shift Epoch
    // Unix Epoch is 1970-01-01. The algorithm requires an epoch of 0000-03-01.
    // 719468 is the exact number of days between 0000-03-01 and 1970-01-01.
    // 'z' is now the number of days since March 1st, year 0.
    let z = days as i64 + 719468;

    // 2. Calculate Era
    // The Gregorian calendar repeats exactly every 400 years.
    // One "Era" = 400 years = 146,097 days.
    // We calculate which 400-year block we are in.
    let era = (if z >= 0 { z } else { z - 146096 }) / 146097;

    // 3. Day of Era (doe)
    // How many days into this specific 400-year cycle are we? (0 to 146096)
    let doe = (z - era * 146097) as u32;

    // 4. Year of Era (yoe)
    // Calculate which year (0-399) within the era we are in.
    // This magic formula accounts for the leap year rules:
    // - 1460   = days in 4 years
    // - 36524  = days in 100 years
    // - 146096 = days in 400 years
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;

    // 5. Absolute Year (y)
    // Reconstruct the actual year number.
    let y = (yoe as i64) + era * 400;

    // 6. Day of Year (doy)
    // How many days into this specific year are we? (0 to 365)
    // This removes the days contributed by previous years in the era.
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);

    // 7. Month Position (mp)
    // Map the Day of Year to a Month Index.
    // Because we shifted the start to March, the months are:
    // 0=March, 1=April ... 10=Jan, 11=Feb.
    // The linear formula (5*doy + 2)/153 maps the uneven month lengths (30/31 days) perfectly.
    let mp = (5 * doy + 2) / 153;

    // 8. Day of Month (d)
    // Inverse the logic above to get the specific day (1-31).
    let d = doy - (153 * mp + 2) / 5 + 1;

    // 9. Convert Month Index back to Civil Month (1-12)
    // If mp < 10 (March...Dec), add 3 to get standard index.
    // If mp >= 10 (Jan...Feb), subtract 9 to wrap around.
    let m = if mp < 10 { mp + 3 } else { mp - 9 };

    // 10. Adjust Year
    // Since we treated March as the start of the year, Jan and Feb
    // actually belong to the *next* civil year.
    // Example: "Month 11" in our math is actually Feb of (Year + 1).
    let y = y + if m <= 2 { 1 } else { 0 };

    (y as i32, m as u32, d as u32, hour, min, sec)
}
