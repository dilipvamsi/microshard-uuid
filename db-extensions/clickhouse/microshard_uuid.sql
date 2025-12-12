-- MicroShard UUID - ClickHouse Optimized
-- Architecture: 54-bit Time | 32-bit Shard | 36-bit Random
-- Optimization: Computes High/Low Qwords as UInt64 separately.

-- =============================================================================
-- 1. CORE LOGIC FUNCTION
-- Accepts (micros, shard_id) -> Returns UUID
-- =============================================================================

CREATE OR REPLACE FUNCTION _microshard_core AS (micros, shard_id) ->
    reinterpretAsUUID(
        bitOr(
            -- STEP 1: Construct HIGH 64 Bits (as UInt128 shifted left)
            -- Layout relative to 0-63: [TimeHigh 48] [Ver 4] [TimeLow 6] [ShardHigh 6]
            bitShiftLeft(
                toUInt128(
                    bitOr(
                        bitOr(
                            -- A. Time High (Top 48 bits) -> Position 16
                            bitShiftLeft(bitShiftRight(micros, 6), 16),
                            -- B. Version 8 (4 bits) -> Position 12
                            32768 -- (8 << 12)
                        ),
                        bitOr(
                            -- C. Time Low (Bottom 6 bits) -> Position 6
                            -- 2^6-1
                            bitShiftLeft(bitAnd(micros, 63), 6),
                            -- D. Shard High (Top 6 bits) -> Position 0
                            bitShiftRight(shard_id, 26)
                            -- bitAnd(bitShiftRight(shard_id, 26), 63)
                        )
                    )
                ),
                64
            ),

            -- STEP 2: Construct LOW 64 Bits (as UInt128)
            -- Layout relative to 0-63: [Var 2] [ShardLow 26] [Random 36]
            toUInt128(
                bitOr(
                    bitOr(
                        -- E. Variant 2 (2 bits) -> Position 62
                        9223372036854775808, -- (2 << 62)
                        -- F. Shard Low (Bottom 26 bits) -> Position 36
                        -- 2^26-1
                        bitShiftLeft(bitAnd(toUInt64(shard_id), 67108863), 36)
                    ),
                    -- Random (36 bits) -> Position 0
                    -- Logic: Generate 64 bits, drop bottom 28 to keep top 36.
                    bitShiftRight(rand64(), 28)
                )
            )
        )
    );

-- =============================================================================
-- 2. PUBLIC GENERATORS
-- =============================================================================

-- Main Generator: Calculates 'now()' exactly once and passes it.
CREATE OR REPLACE FUNCTION microshard_generate AS (shard_id) ->
    _microshard_core(
        toUInt64(toUnixTimestamp64Micro(now64(6))),
        toUInt32(shard_id)
    );

-- Backfilling Generator: Accepts explicit timestamp.
CREATE OR REPLACE FUNCTION microshard_from_micros AS (micros, shard_id) ->
    _microshard_core(
        toUInt64(micros),
        toUInt32(shard_id)
    );

-- =============================================================================
-- 3. EXTRACTION FUNCTIONS
-- =============================================================================

CREATE OR REPLACE FUNCTION microshard_get_shard_id AS (uid) ->
    bitOr(
        -- Part A: Shard High (6 bits)
        -- 1. Shift Right 64: Moves High Qword to bottom.
        -- 2. toUInt32: Truncates top 96 bits (TimeHigh/Ver/TimeLow fall off or stay in upper 32, irrelevant).
        -- 3. Mask 63: Keeps bottom 6 bits.
        -- 4. Shift Left 26: Moves to top of Shard ID.
        bitShiftLeft(
            bitAnd(
                toUInt32(bitShiftRight(reinterpretAsUInt128(uid), 64)),
                63
            ),
            26
        ),

        -- Part B: Shard Low (26 bits)
        -- 1. Shift Right 36: Moves Shard Low to bottom.
        -- 2. toUInt32: Truncates top 96 bits (Variant bits stay, but TimeHigh etc fall off).
        -- 3. Mask 0x3FFFFFF: Removes Variant bits, keeps 26 Shard bits.
        bitAnd(
            toUInt32(bitShiftRight(reinterpretAsUInt128(uid), 36)),
            67108863
        )
    );

CREATE OR REPLACE FUNCTION microshard_get_timestamp AS (uid) ->
    toDateTime64(
        bitOr(
            -- Extract Time High (from High 64 bits)
            -- Shift Right 64 (to get High Qword). Shift Right 16 (remove Ver/LowTime/Shard). Shift Left 6.
            bitShiftLeft(
                bitShiftRight(toUInt64(bitShiftRight(reinterpretAsUInt128(uid), 64)), 16),
                6
            ),
            -- Extract Time Low (from High 64 bits)
            -- Shift Right 64. Shift Right 6. Mask 6 bits.
            bitAnd(bitShiftRight(toUInt64(bitShiftRight(reinterpretAsUInt128(uid), 64)), 6), 63)
        ) / 1000000.0,
        6
    );

CREATE OR REPLACE FUNCTION microshard_get_iso_timestamp AS (uid) ->
    formatDateTime(
        microshard_get_timestamp(uid),
        '%Y-%m-%dT%H:%i:%S.%fZ'
    );
