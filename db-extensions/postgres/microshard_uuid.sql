-- MicroShard UUID - PostgreSQL Optimized Implementation
-- Architecture: 54-bit Time | 32-bit Shard | 36-bit Random
-- Compliant with UUIDv8 (RFC 9562)

-- =============================================================================
-- 1. HELPER: Calculate High 64 Bits
-- Layout: [TimeHigh 48] [Ver 4] [TimeLow 6] [ShardHigh 6]
-- =============================================================================
CREATE OR REPLACE FUNCTION _microshard_calc_high(micros bigint, shard_id int)
RETURNS bigint AS $$
DECLARE
    -- Version 8 (binary 1000) shifted to position 12
    ver_bits bigint := 32768;
BEGIN
    RETURN (
        -- Time High: Shift right 6 (drop low bits), then move to pos 16
        ((micros >> 6) << 16) |
        -- Version: Fixed 4 bits
        ver_bits |
        -- Time Low: Mask 6 bits, move to pos 6
        ((micros & 63) << 6) |
        -- Shard High: Shift right 26, Mask 6 bits, pos 0
        ((shard_id >> 26) & 63)
    );
END;
$$ LANGUAGE plpgsql IMMUTABLE PARALLEL SAFE;

-- =============================================================================
-- 2. HELPER: Calculate Low 64 Bits
-- Layout: [Var 2] [ShardLow 26] [Random 36]
-- Logic: "Multiply Big -> Shift Down" for maximum entropy
-- =============================================================================
CREATE OR REPLACE FUNCTION _microshard_calc_low(shard_id int)
RETURNS bigint AS $$
DECLARE
    -- Variant 2 (binary 10...) at top bit (Signed Minimum)
    -- Hex: 0x8000000000000000
    var_bits bigint := -9223372036854775808;

    -- Max Positive BigInt (2^63 - 1)
    -- Scaling random() by this fills the entire 63-bit positive integer space
    rand_cap numeric := 9223372036854775807;
BEGIN
    RETURN (
        -- 1. Variant bits
        var_bits |

        -- 2. Shard Low: Mask 26 bits (0x3FFFFFF), shift to pos 36
        ((shard_id & 67108863)::bigint << 36) |

        -- 3. Random: High Entropy Extraction
        -- Logic: Generate 0..MaxBigInt (63 bits) -> Shift Right 27
        -- Result: The top 36 bits of the random generation are preserved
        --         and moved to positions 0-35.
        ((random() * rand_cap)::bigint >> 27)
    );
END;
$$ LANGUAGE plpgsql VOLATILE PARALLEL SAFE;

-- =============================================================================
-- 3. GENERATION: From Explicit micros (Binary Optimized)
-- Useful for backfilling data or maintaining historical sort order
-- =============================================================================
CREATE OR REPLACE FUNCTION microshard_from_micros(p_micros bigint, shard_id int)
RETURNS uuid AS $$
DECLARE
    high_bits bigint;
    low_bits bigint;
BEGIN
    -- 1. Calculate High 64 bits using the PROVIDED timestamp
    --    (Instead of clock_timestamp())
    high_bits := _microshard_calc_high(p_micros, shard_id);

    -- 2. Calculate Low 64 bits
    --    (Generates fresh randomness for the tail to ensure uniqueness)
    low_bits  := _microshard_calc_low(shard_id);

    -- 3. Binary Construction
    -- int8send: Returns raw 8-byte network representation (No '0' padding needed)
    -- encode:   Converts 16 raw bytes to hex string in one pass
    RETURN encode(int8send(high_bits) || int8send(low_bits), 'hex')::uuid;
END;
$$ LANGUAGE plpgsql VOLATILE PARALLEL SAFE;

-- =============================================================================
-- 4. GENERATION: From Explicit Timestamp (Binary Optimized)
-- Useful for backfilling data or maintaining historical sort order
-- =============================================================================
CREATE OR REPLACE FUNCTION microshard_from_timestamp(p_ts timestamptz, shard_id int)
RETURNS uuid AS $$
BEGIN
    RETURN microshard_from_micros(
        (EXTRACT(EPOCH FROM p_ts) * 1000000)::bigint,
        shard_id
    );
END;
$$ LANGUAGE plpgsql VOLATILE PARALLEL SAFE;

-- =============================================================================
-- 5. GENERATION
-- Uses int8send to avoid expensive string padding/parsing
-- =============================================================================
CREATE OR REPLACE FUNCTION microshard_generate(shard_id int)
RETURNS uuid AS $$
DECLARE
    micros bigint;
BEGIN
    -- Get Time
    micros := (EXTRACT(EPOCH FROM clock_timestamp()) * 1000000)::bigint;

    return microshard_from_micros(micros, shard_id);
END;
$$ LANGUAGE plpgsql VOLATILE PARALLEL SAFE;

-- =============================================================================
-- 6. EXTRACTION: Get Shard ID (Binary Optimized)
-- Uses byte access to avoid string conversion and substring allocs
-- =============================================================================
CREATE OR REPLACE FUNCTION microshard_get_shard_id(uid uuid)
RETURNS int AS $$
DECLARE
    -- Get raw 16 bytes
    b bytea := uuid_send(uid);
    shard_high int;
    shard_low int;
BEGIN
    -- 1. Shard High (6 bits) -> Lower 6 bits of Byte 7
    shard_high := (get_byte(b, 7) & 63) << 26;

    -- 2. Shard Low (26 bits) -> Distributed across Bytes 8, 9, 10, 11
    shard_low :=
          ((get_byte(b, 8)  & 63) << 20) -- Byte 8: Mask Variant, Shift
        |  (get_byte(b, 9)        << 12) -- Byte 9: All bits
        |  (get_byte(b, 10)       << 4)  -- Byte 10: All bits
        |  (get_byte(b, 11)       >> 4); -- Byte 11: Top 4 bits only

    RETURN shard_high | shard_low;
END;
$$ LANGUAGE plpgsql IMMUTABLE PARALLEL SAFE;

-- =============================================================================
-- 7. EXTRACTION: Get Timestamp (Binary Optimized)
-- Uses byte access to reconstruct the 54-bit integer
-- =============================================================================
CREATE OR REPLACE FUNCTION microshard_get_timestamp(uid uuid)
RETURNS timestamptz AS $$
DECLARE
    b bytea := uuid_send(uid);
    time_high bigint;
    time_low bigint;
    micros bigint;
    seconds bigint;
    sub_micros bigint;
BEGIN
    -- 1. Time High (48 bits) -> Bytes 0-5
    time_high :=
          (get_byte(b, 0)::bigint << 40)
        | (get_byte(b, 1)::bigint << 32)
        | (get_byte(b, 2)::bigint << 24)
        | (get_byte(b, 3)::bigint << 16)
        | (get_byte(b, 4)::bigint << 8)
        |  get_byte(b, 5)::bigint;

    -- 2. Time Low (6 bits) -> Spread across Byte 6 and 7
    -- Byte 6: Mask top 4 (Version), Shift Left 2
    -- Byte 7: Top 2 bits, Shift Right 6
    time_low :=
          ((get_byte(b, 6) & 15) << 2)
        |  (get_byte(b, 7) >> 6);

    -- 3. Combine: Shift high part up by 6 to make room for low part
    micros := (time_high << 6) | time_low;

    -- 4. Calculation: Split components to avoid Float53 rounding errors.
    --    Direct multiplication (micros * INTERVAL '1 microsecond') casts to double.
    --    Since 2^54 > 2^53 (double precision limit), odd numbers at the high
    --    end of the range will be rounded, causing the off-by-one error.

    seconds := micros / 1000000;      -- Integer Division
    sub_micros := micros % 1000000;   -- Integer Modulo

    -- Both 'seconds' (~1.8e10) and 'sub_micros' (<1e6) fit easily
    -- within the 15-digit precision of standard floats used in interval math.
    RETURN '1970-01-01 00:00:00+00'::timestamptz
         + (seconds * INTERVAL '1 second')
         + (sub_micros * INTERVAL '1 microsecond');
END;
$$ LANGUAGE plpgsql IMMUTABLE PARALLEL SAFE;
