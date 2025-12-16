-- MicroShard UUID - MySQL Optimized Implementation
-- Architecture: 54-bit Time | 32-bit Shard | 36-bit Random
-- Output: BINARY(16)

DELIMITER $$

-- =============================================================================
-- 1. HELPER: Calculate High 64 Bits
-- Layout: [TimeHigh 48] [Ver 4] [TimeLow 6] [ShardHigh 6]
-- =============================================================================
DROP FUNCTION IF EXISTS `_microshard_calc_high`$$
CREATE FUNCTION `_microshard_calc_high`(p_micros BIGINT UNSIGNED, p_shard_id INT UNSIGNED)
RETURNS BIGINT UNSIGNED
DETERMINISTIC
NO SQL
BEGIN
    -- Version 8 (binary 1000) shifted to position 12
    DECLARE ver_bits BIGINT UNSIGNED DEFAULT 32768;

    RETURN (
        -- Time High: Shift right 6, then move to pos 16
        ((p_micros >> 6) << 16) |
        -- Version: Fixed 4 bits
        ver_bits |
        -- Time Low: Mask 6 bits, move to pos 6
        ((p_micros & 63) << 6) |
        -- Shard High: Shift right 26, Mask 6 bits, pos 0
        ((p_shard_id >> 26) & 63)
    );
END$$

-- =============================================================================
-- 2. HELPER: Calculate Low 64 Bits
-- Layout: [Var 2] [ShardLow 26] [Random 36]
-- =============================================================================
DROP FUNCTION IF EXISTS `_microshard_calc_low`$$
CREATE FUNCTION `_microshard_calc_low`(p_shard_id INT UNSIGNED)
RETURNS BIGINT UNSIGNED
NOT DETERMINISTIC
NO SQL
BEGIN
    -- Variant 2 (binary 10...) at top bit (0x8000000000000000)
    DECLARE var_bits BIGINT UNSIGNED DEFAULT 9223372036854775808;

    -- Max Positive BigInt (2^63 - 1) for random scaling
    DECLARE rand_cap BIGINT UNSIGNED DEFAULT 9223372036854775807;

    RETURN (
        -- 1. Variant bits
        var_bits |
        -- 2. Shard Low: Mask 26 bits, shift to pos 36
        ((p_shard_id & 67108863) << 36) |
        -- 3. Random: Generate 0..MaxBigInt -> Shift Right 27
        (CAST(FLOOR(RAND() * rand_cap) AS UNSIGNED) >> 27)
    );
END$$

-- =============================================================================
-- 3. GENERATION: From Explicit micros (Binary Optimized)
-- Returns BINARY(16)
-- =============================================================================
DROP FUNCTION IF EXISTS `microshard_from_micros`$$
CREATE FUNCTION `microshard_from_micros`(p_micros BIGINT, p_shard_id INT)
RETURNS BINARY(16)
NOT DETERMINISTIC
NO SQL
BEGIN
    DECLARE high_bits BIGINT UNSIGNED;
    DECLARE low_bits BIGINT UNSIGNED;

    -- 1. Calculate High 64 bits
    SET high_bits = `_microshard_calc_high`(CAST(p_micros AS UNSIGNED), CAST(p_shard_id AS UNSIGNED));

    -- 2. Calculate Low 64 bits
    SET low_bits = `_microshard_calc_low`(CAST(p_shard_id AS UNSIGNED));

    -- 3. Binary Construction
    -- Convert 64-bit integers to 16-character Hex strings, concatenate, and unhex to binary
    RETURN UNHEX(CONCAT(
        LPAD(HEX(high_bits), 16, '0'),
        LPAD(HEX(low_bits), 16, '0')
    ));
END$$

-- =============================================================================
-- 4. GENERATION: From Explicit Timestamp
-- =============================================================================
DROP FUNCTION IF EXISTS `microshard_from_timestamp`$$
CREATE FUNCTION `microshard_from_timestamp`(p_ts DATETIME(6), p_shard_id INT)
RETURNS BINARY(16)
NOT DETERMINISTIC
NO SQL
BEGIN
    DECLARE micros BIGINT;
    -- Convert DATETIME(6) to microseconds since epoch
    SET micros = CAST(FLOOR(UNIX_TIMESTAMP(p_ts) * 1000000) + MICROSECOND(p_ts) AS SIGNED);

    RETURN microshard_from_micros(micros, p_shard_id);
END$$

-- =============================================================================
-- 5. GENERATION: Main Function
-- Usage: INSERT INTO t (id) VALUES (microshard_generate(1));
-- =============================================================================
DROP FUNCTION IF EXISTS `microshard_generate`$$
CREATE FUNCTION `microshard_generate`(p_shard_id INT)
RETURNS BINARY(16)
NOT DETERMINISTIC
NO SQL
BEGIN
    DECLARE micros BIGINT;
    -- Get current time in microseconds
    SET micros = CAST(FLOOR(UNIX_TIMESTAMP(NOW(6)) * 1000000) AS SIGNED);
    -- Handle case where microsecond extraction might miss (though UNIX_TIMESTAMP(NOW(6)) is usually safe)
    IF MICROSECOND(NOW(6)) > 0 THEN
         SET micros = micros + (MICROSECOND(NOW(6)) % 1000000);
    END IF;

    -- Clean up slightly inaccurate float math from UNIX_TIMESTAMP if necessary,
    -- but usually standard NOW(6) flow above is sufficient.
    SET micros = CAST(FLOOR(UNIX_TIMESTAMP(NOW(6)) * 1000000) AS SIGNED);

    RETURN microshard_from_micros(micros, p_shard_id);
END$$

-- =============================================================================
-- 6. EXTRACTION: Get Shard ID (Binary Optimized)
-- =============================================================================
DROP FUNCTION IF EXISTS `microshard_get_shard_id`$$
CREATE FUNCTION `microshard_get_shard_id`(p_uid BINARY(16))
RETURNS INT
DETERMINISTIC
NO SQL
BEGIN
    DECLARE shard_high INT;
    DECLARE shard_low INT;

    -- NOTE: MySQL Strings/BLOBs are 1-indexed.

    -- 1. Shard High (6 bits) -> Lower 6 bits of Byte 8 (Index 8)
    -- Postgres Byte 7 is MySQL Byte 8
    SET shard_high = (ORD(SUBSTRING(p_uid, 8, 1)) & 63) << 26;

    -- 2. Shard Low (26 bits) -> Distributed across Bytes 9, 10, 11, 12
    SET shard_low =
          ((ORD(SUBSTRING(p_uid, 9, 1))  & 63) << 20) -- Byte 9: Mask Variant
        |  (ORD(SUBSTRING(p_uid, 10, 1))       << 12) -- Byte 10
        |  (ORD(SUBSTRING(p_uid, 11, 1))       << 4)  -- Byte 11
        |  (ORD(SUBSTRING(p_uid, 12, 1))       >> 4); -- Byte 12: Top 4 bits

    RETURN shard_high | shard_low;
END$$

-- =============================================================================
-- 7. EXTRACTION: Get Timestamp
-- Fix: Uses integer arithmetic (DIV/MOD) and interval addition to avoid
-- floating-point precision loss associated with FROM_UNIXTIME(bigint_div_result).
-- =============================================================================
DROP FUNCTION IF EXISTS `microshard_get_timestamp`$$
CREATE FUNCTION `microshard_get_timestamp`(p_uid BINARY(16))
RETURNS DATETIME(6)
DETERMINISTIC
NO SQL
BEGIN
    DECLARE time_high BIGINT UNSIGNED;
    DECLARE time_low BIGINT UNSIGNED;
    DECLARE total_micros BIGINT UNSIGNED;

    -- 1. Time High (48 bits) -> Bytes 1-6
    SET time_high =
          (CAST(ORD(SUBSTRING(p_uid, 1, 1)) AS UNSIGNED) << 40)
        | (CAST(ORD(SUBSTRING(p_uid, 2, 1)) AS UNSIGNED) << 32)
        | (CAST(ORD(SUBSTRING(p_uid, 3, 1)) AS UNSIGNED) << 24)
        | (CAST(ORD(SUBSTRING(p_uid, 4, 1)) AS UNSIGNED) << 16)
        | (CAST(ORD(SUBSTRING(p_uid, 5, 1)) AS UNSIGNED) << 8)
        |  CAST(ORD(SUBSTRING(p_uid, 6, 1)) AS UNSIGNED);

    -- 2. Time Low (6 bits) -> Spread across Byte 7 and 8
    SET time_low =
          ((ORD(SUBSTRING(p_uid, 7, 1)) & 15) << 2)
        |  (ORD(SUBSTRING(p_uid, 8, 1)) >> 6);

    -- 3. Combine
    SET total_micros = (time_high << 6) | time_low;

    -- 4. Convert to DATETIME using Integer Arithmetic
    -- Start at Epoch (1970-01-01), add seconds (Integer DIV), add remaining micros (Integer MOD)
    RETURN DATE_ADD(
        DATE_ADD(FROM_UNIXTIME(0), INTERVAL (total_micros DIV 1000000) SECOND),
        INTERVAL (total_micros MOD 1000000) MICROSECOND
    );
END$$

DELIMITER ;
