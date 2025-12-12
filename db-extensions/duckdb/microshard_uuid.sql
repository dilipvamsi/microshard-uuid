-- MicroShard UUID - DuckDB Implementation
-- Architecture: 54-bit Time | 32-bit Shard | 36-bit Random
--
-- Constants:
-- Version 8 (at 76) | Variant 2 (at 62) = 604472133179351442128896

-- =============================================================================
-- 1. HELPER MACROS (Bit Packing Logic)
-- =============================================================================

-- Calculate HIGH 64 Bits (UBIGINT)
-- Layout: [TimeHigh 48] [Ver 4] [TimeLow 6] [ShardHigh 6]
-- Offsets relative to 64-bit word: 16, 12, 6, 0
CREATE OR REPLACE MACRO _microshard_calc_high(micros, shard_id) AS (
    -- Time High: Shift right 6 (drop low bits), then move to pos 16
    ((micros >> 6) << 16) |
    -- Version: 8 << 12
    32768::UBIGINT |
    -- Time Low: Mask 6 bits, move to pos 6
    ((micros & 63) << 6) |
    -- Shard High: Shift right 26, Mask 6 bits, pos 0
    ((shard_id >> 26) & 63)
);

-- Calculate LOW 64 Bits (UBIGINT)
-- Layout: [Var 2] [ShardLow 26] [Random 36]
-- Offsets relative to 64-bit word: 62, 36, 0
CREATE OR REPLACE MACRO _microshard_calc_low(shard_id) AS (
    -- Variant: 2 << 62
    9223372036854775808::UBIGINT |
    -- Shard Low: Mask 26 bits, move to pos 36
    ((shard_id & 67108863) << 36) |
    -- 3. Random: Generate 64-bit Int -> Shift Down
    (
        CAST(
            -- Scale 0..1 to 0..MaxUint64
            random() * 18446744073709551615
        AS UBIGINT)
        -- Shift Right 28 bits
        -- (Moves the Top 36 bits down to positions 0-35)
        >> 28
    )
);


-- Helper: Unpack Shard from HUGEINT (Pure Bitwise)
-- Logic: Extracts the 32-bit Shard ID from its split positions in the 128-bit integer.
--        Shard High (6 bits) is at bits 69-64.
--        Shard Low (26 bits) is at bits 61-36.
CREATE OR REPLACE MACRO _microshard_unpack_shard_int(hid) AS (
    CAST (
        (
            -- 1. Extract Shard High (Top 6 bits)
            -- Source: Bits 69 to 64.
            -- Logic: Shift Right 64 to move them to position 5-0.
            --        Mask 63 (0x3F) to isolate them.
            --        Shift Left 26 to move them to the top of the 32-bit Shard ID.
            ((hid >> 64) & 63) << 26
        ) | (
            -- 2. Extract Shard Low (Bottom 26 bits)
            -- Source: Bits 61 to 36.
            -- Logic: Shift Right 36 to move them to position 25-0.
            --        Mask 67108863 (0x3FFFFFF) to isolate them (removes Variant bits above).
            (hid >> 36) & 67108863
        )
        AS INTEGER
    )
);


-- Helper: Unpack Time from HUGEINT (Pure Bitwise)
-- Logic: Reconstructs the 54-bit timestamp from its split positions.
CREATE OR REPLACE MACRO _microshard_unpack_time_int(hid) AS (
    (
        -- 1. Extract Time High (Top 48 bits)
        -- Source: Bits 127 to 80.
        -- Logic: Shift Right 80 to move them to the bottom.
        --        Shift Left 6 to make room for the Time Low bits.
        (hid >> 80) << 6
    ) | (
        -- 2. Extract Time Low (Bottom 6 bits)
        -- Source: Bits 75 to 70 (sitting below Version).
        -- Logic: Shift Right 70 to move them to the bottom.
        --        Mask 63 (0x3F) to clean off the Version bits above.
        (hid >> 70) & 63::HUGEINT
    )
);


-- Helper: Unpack Shard from UUID String (Hex Parsing)
-- Logic: Parses specific Hex nibbles to reconstruct the Shard ID.
--        Necessary because casting full UUID strings to HUGEINT is unstable in some DuckDB versions.
CREATE OR REPLACE MACRO _microshard_unpack_shard_str(uid_text) AS (
    CAST (
        (
            -- 1. Extract Shard High (6 bits)
            -- Source: Group 3 (Chars 15-18). Layout: [Ver 4][TimeLow 6][ShardHigh 6]
            -- Logic: Parse 4 chars. Mask 63 (0x3F) to get bottom 6 bits. Shift Left 26.
            (CAST('0x' || substr(uid_text, 15, 4) AS BIGINT) & 63) << 26
        ) | (
            -- 2. Extract Shard Low - Top Part (14 bits)
            -- Source: Group 4 (Chars 20-23). Layout: [Var 2][ShardLowTop 14]
            -- Logic: Parse 4 chars. Mask 16383 (0x3FFF) to remove Variant bits. Shift Left 12.
            (CAST('0x' || substr(uid_text, 20, 4) AS BIGINT) & 16383) << 12
        ) | (
            -- 3. Extract Shard Low - Bottom Part (12 bits)
            -- Source: Group 5, First 3 chars (Chars 25-27). Layout: [ShardLowBottom 12][...]
            -- Logic: Parse 3 chars (12 bits). No shift needed.
            CAST('0x' || substr(uid_text, 25, 3) AS BIGINT)
        )
        AS INTEGER
    )
);

-- Helper: Unpack Time from UUID String (Hex Parsing)
-- Logic: Reconstructs the 54-bit timestamp from 3 separate hex chunks.
CREATE OR REPLACE MACRO _microshard_unpack_time_str(uid_text) AS (
    (
        -- 1. Time High - Part A (32 bits)
        -- Source: Group 1 (Chars 1-8).
        -- Logic: Parse 8 chars. Shift Left 22 (54 total - 32 = 22).
        CAST('0x' || substr(uid_text, 1, 8) AS BIGINT) << 22
    ) | (
        -- 2. Time High - Part B (16 bits)
        -- Source: Group 2 (Chars 10-13).
        -- Logic: Parse 4 chars. Shift Left 6 (22 remaining - 16 = 6).
        CAST('0x' || substr(uid_text, 10, 4) AS BIGINT) << 6
    ) | (
        -- 3. Time Low (6 bits)
        -- Source: Group 3 (Chars 15-18). Layout: [Ver 4][TimeLow 6][ShardHigh 6]
        -- Logic: Parse 4 chars. Shift Right 6 to drop ShardHigh. Mask 63 to isolate TimeLow.
        (CAST('0x' || substr(uid_text, 15, 4) AS BIGINT) >> 6) & 63
    )
);

-- =============================================================================
-- 2. CONVERTERS
-- =============================================================================

-- UUID -> INT128
-- Logic: Parses Hex into UBIGINT (Unsigned 64-bit) first, then casts to HUGEINT.
CREATE OR REPLACE MACRO microshard_uuid_to_int(uid) AS (
    (
        -- 1. High 64 Bits
        -- Cast Hex String -> UBIGINT (Safe Hex Parsing) -> HUGEINT (For Shifting)
        CAST(
            CAST('0x' ||
                substr(CAST(uid AS TEXT), 1, 8) ||
                substr(CAST(uid AS TEXT), 10, 4) ||
                substr(CAST(uid AS TEXT), 15, 4)
            AS UBIGINT)
        AS HUGEINT) << 64
    ) | (
        -- 2. Low 64 Bits
        -- Cast Hex String -> UBIGINT -> HUGEINT
        CAST(
            CAST('0x' ||
                substr(CAST(uid AS TEXT), 20, 4) ||
                substr(CAST(uid AS TEXT), 25, 12)
            AS UBIGINT)
        AS HUGEINT)
    )
);

-- INT128 -> UUID
-- Formats hugeint as hex and casts to uuid
CREATE OR REPLACE MACRO microshard_int_to_uuid(hid) AS (
    CAST(printf('%032x', hid) AS UUID)
);

-- =============================================================================
-- 3. INT128 TYPE MACROS (High Performance)
-- Output: HUGEINT (Raw 128-bit integer)
-- ~50x Faster extraction than UUID macros because they avoid string parsing.
-- =============================================================================

CREATE OR REPLACE MACRO microshard_from_micros_int(macros, shard_id) AS (
    (
        CAST(_microshard_calc_high(CAST(macros AS UBIGINT), CAST(shard_id AS UBIGINT)) AS HUGEINT) << 64
    ) | (
        CAST(_microshard_calc_low(CAST(shard_id AS UBIGINT)) AS HUGEINT)
    )
);

CREATE OR REPLACE MACRO microshard_generate_int(shard_id) AS (
    microshard_from_micros_int(epoch(now()) * 1000000, shard_id)
);

CREATE OR REPLACE MACRO microshard_get_shard_id_int(hid) AS (
    _microshard_unpack_shard_int(hid)
);

CREATE OR REPLACE MACRO microshard_get_timestamp_int(hid) AS (
    to_timestamp(CAST(_microshard_unpack_time_int(hid) AS DOUBLE) / 1000000)
);


-- =============================================================================
-- 4. UUID TYPE MACROS (Standard)
-- Output: UUID (String formatted)
-- =============================================================================

CREATE OR REPLACE MACRO microshard_from_micros(macros, shard_id) AS (
    microshard_int_to_uuid(microshard_from_micros_int(macros, shard_id))
);

CREATE OR REPLACE MACRO microshard_generate(shard_id) AS (
    microshard_int_to_uuid(microshard_generate_int(shard_id))
);

CREATE OR REPLACE MACRO microshard_get_shard_id(uid) AS (
    _microshard_unpack_shard_str(CAST(uid AS TEXT))
);

CREATE OR REPLACE MACRO microshard_get_timestamp(uid) AS (
    to_timestamp(CAST(_microshard_unpack_time_str(CAST(uid AS TEXT)) AS DOUBLE) / 1000000)
);
