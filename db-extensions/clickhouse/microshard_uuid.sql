-- =============================================================================
-- MICROSHARD UUID v8 - CLICKHOUSE IMPLEMENTATION
-- =============================================================================
--
-- WHAT IS THIS?
-- This is a custom UUID generator designed for distributed systems.
-- It combines Time, a Shard ID, and Randomness into a single 128-bit identifier.
--
-- WHY USE IT?
-- 1. Sortable: UUIDs generated later will be alphabetically "larger".
-- 2. Sharded: You can extract the Shard ID (e.g., Tenant ID) directly from the UUID.
-- 3. Unique: Includes random bits to prevent collisions.
-- 4. Fast: Uses low-level CPU instructions (bitwise math) instead of slow string parsing.
--
-- THE STRUCTURE (128 Bits Total):
-- We split the UUID into two 64-bit halves (High and Low).
--
-- [ UUID START (High 64 Bits) ]  --------------------------------------------+
-- | Time (High) | Version | Time (Low) | Shard (High) |                      |
-- | 48 bits     | 4 bits  | 6 bits     | 6 bits       |                      |
-- +---------------------------------------------------+                      |
--                                                                            |
-- [ UUID END (Low 64 Bits) ]     --------------------------------------------+
-- | Variant | Shard (Low) | Randomness |                                     |
-- | 2 bits  | 26 bits     | 36 bits    |                                     |
-- +------------------------------------+
--
-- THE "LITTLE ENDIAN" TRICK:
-- ClickHouse runs on x86 CPUs, which store numbers "backwards" in memory.
-- - The "Low" bits of a number are stored at the START of memory.
-- - The "High" bits of a number are stored at the END of memory.
--
-- To make the UUID string print correctly (Time first, Random last), we do this:
-- 1. Put the "Time" logic into the LOW bits of the 128-bit integer.
-- 2. Put the "Random" logic into the HIGH bits of the 128-bit integer.
-- =============================================================================


-- =============================================================================
-- 1. CORE LOGIC FUNCTION
-- This performs the heavy lifting. It takes numbers and turns them into a UUID.
-- =============================================================================

CREATE OR REPLACE FUNCTION _microshard_core AS (micros, shard_id) ->
    reinterpretAsUUID(
        bitOr(
            -- =================================================================
            -- PART 1: THE BEGINNING OF THE UUID (Time + Version + Shard High)
            -- =================================================================
            -- We place this in the LOWest 64 bits of the UInt128.
            -- Because ClickHouse is Little Endian, the "Low" bits are written
            -- to the START of the memory block, making them the START of the UUID string.
            toUInt128(
                toUInt64(
                    bitOr(
                        bitOr(
                            -- A. TIME HIGH (Top 48 bits)
                            -- We take the microsecond timestamp.
                            -- 1. Shift Right 6: Discard the bottom 6 bits (saved for later).
                            -- 2. Shift Left 16: Move it up to make room for Version (4) + TimeLow (6) + ShardHigh (6).
                            bitShiftLeft(bitShiftRight(micros, 6), 16),

                            -- B. VERSION (4 bits)
                            -- Standard UUIDs need a version number. We use 8 (Binary 1000).
                            -- We shift it left 12 positions to sit right below Time High.
                            32768 -- Equal to (8 << 12)
                        ),
                        bitOr(
                            -- C. TIME LOW (Bottom 6 bits)
                            -- We take the microsecond timestamp again.
                            -- 1. 'bitAnd' with 63 keeps only the bottom 6 bits.
                            -- 2. Shift Left 6: Move it up to make room for Shard High.
                            bitShiftLeft(bitAnd(micros, 63), 6),

                            -- D. SHARD HIGH (Top 6 bits)
                            -- The Shard ID is 32 bits, but we can only fit 6 bits here.
                            -- We take the TOP 6 bits of the Shard ID.
                            bitShiftRight(shard_id, 26)
                        )
                    )
                )
            ),

            -- =================================================================
            -- PART 2: THE END OF THE UUID (Variant + Shard Low + Random)
            -- =================================================================
            -- We place this in the HIGHEST 64 bits of the UInt128.
            -- In Little Endian memory, these bits come LAST.
            bitShiftLeft(
                toUInt128(
                    toUInt64(
                        bitOr(
                            bitOr(
                                -- E. VARIANT (2 bits)
                                -- UUID standard requires a "Variant" marker. We use 2 (Binary 10).
                                -- This goes at the very top of this 64-bit segment (Position 62).
                                9223372036854775808, -- Equal to (2 << 62)

                                -- F. SHARD LOW (Bottom 26 bits)
                                -- We put the remaining 26 bits of the Shard ID here.
                                -- 1. 'bitAnd' 67108863 keeps the bottom 26 bits.
                                -- 2. Shift Left 36: Move it up to sit right below the Variant.
                                bitShiftLeft(bitAnd(toUInt64(shard_id), 67108863), 36)
                            ),
                            -- G. RANDOM (36 bits)
                            -- We fill the remaining space with random noise to ensure uniqueness.
                            -- rand64() generates 64 bits. We Shift Right 28 to keep only the top 36 bits.
                            bitShiftRight(rand64(), 28)
                        )
                    )
                ),
                64 -- Move this whole block 64 bits to the left, into the "High" position.
            )
        )
    );

-- =============================================================================
-- 2. PUBLIC GENERATORS
-- Use these functions in your INSERT statements.
-- =============================================================================

-- USAGE: microshard_generate(101)
-- Generates a UUID using the CURRENT system time.
CREATE OR REPLACE FUNCTION microshard_generate AS (shard_id) ->
    _microshard_core(
        toUInt64(toUnixTimestamp64Micro(now64(6))), -- Get current time in microseconds
        toUInt32(shard_id)                          -- Ensure Shard ID is 32-bit
    );

-- USAGE: microshard_from_micros(1698000000000, 101)
-- Generates a UUID using a PAST or FUTURE timestamp (for backfilling data).
CREATE OR REPLACE FUNCTION microshard_from_micros AS (micros, shard_id) ->
    _microshard_core(
        toUInt64(micros),
        toUInt32(shard_id)
    );

-- =============================================================================
-- 3. EXTRACTION FUNCTIONS
-- Use these to read data BACK out of a UUID column.
-- =============================================================================

-- EXTRACT SHARD ID
-- We have to stitch the Shard ID back together from two different places.
CREATE OR REPLACE FUNCTION _microshard_get_shard_id AS (iid) ->
    bitOr(
        -- Part A: Get the Top 6 bits
        -- They are hidden in the "Time" section (Low 64 bits of the integer).
        bitShiftLeft(
            bitAnd(
                toUInt32(iid), -- Read the bottom 32 bits
                63 -- Mask to get only the very last 6 bits
            ),
            26 -- Shift them back up to the top position (26)
        ),
        -- Part B: Get the Bottom 26 bits
        -- They are hidden in the "Random" section (High 64 bits of the integer).
        bitAnd(
            toUInt32(
                bitShiftRight(
                    toUInt64(bitShiftRight(iid, 64)), -- Read the top 64 bits
                    36 -- Shift down past the Random bits to find the Shard bits
                )
            ),
            67108863 -- Mask to clean up any bits above the 26 we want
        )
    );

-- EXTRACT SHARD ID
CREATE OR REPLACE FUNCTION microshard_get_shard_id AS (uid) ->
    _microshard_get_shard_id(reinterpretAsUInt128(uid));

-- Extract Raw Microseconds (Bypasses DateTime64 Year 2299 limit)
CREATE OR REPLACE FUNCTION _microshard_get_micros AS (iid) ->
    bitOr(
        -- Part A: Get the Top 48 bits of Time
        -- Found in the "Time" section (Low 64 bits of the integer).
        bitShiftLeft(
            bitShiftRight(
                iid,
                16 -- Shift right to skip Version/ShardHigh/TimeLow
            ),
            6 -- Shift left to restore original position
        ),
        -- Part B: Get the Bottom 6 bits of Time
        -- Found in the "Time" section (Low 64 bits of the integer).
        bitAnd(
            bitShiftRight(
                iid,
                6 -- Shift right to skip ShardHigh
            ),
            63 -- Mask to keep only the 6 bits we want
        )
    );

-- EXTRACT micros
CREATE OR REPLACE FUNCTION microshard_get_micros AS (uid) ->
    _microshard_get_micros(toUInt64(reinterpretAsUInt128(uid)));

-- EXTRACT TIMESTAMP
-- The maximum supported value is 2262-04-11 23:47:16 in UTC from official documentation
-- We reconstruct the microsecond timestamp from its split parts.
CREATE OR REPLACE FUNCTION microshard_get_timestamp AS (uid) ->
    toDateTime64(
        microshard_get_micros(uid) / 1000000.0, -- Convert microseconds back to seconds (float)
        6 -- Precision for DateTime64
    );

-- EXTRACT ISO STRING
-- Helper to get a human-readable date string (e.g., '2023-10-27T10:00:00.123456Z')
CREATE OR REPLACE FUNCTION microshard_get_iso_timestamp AS (uid) ->
    formatDateTime(
        microshard_get_timestamp(uid),
        '%Y-%m-%dT%H:%i:%S.%fZ'
    );

-- =============================================================================
-- 5. SORTING UTILITY (Crucial for Performance)
-- Flips High/Low bits so Time becomes the most significant part of the integer.
-- =============================================================================

-- Helper to get sort key of the id by shifting them the high 64 bits to low and low to high
CREATE OR REPLACE FUNCTION _microshard_sort_key AS (id) ->
    bitOr(
        bitShiftLeft(id, 64),
        bitShiftRight(id, 64)
    );

-- Creating the sort key from the id as data is stored in LITTLE ENDIAN
CREATE OR REPLACE FUNCTION microshard_sort_key AS (uid) ->
    _microshard_sort_key(reinterpretAsUInt128(uid));
