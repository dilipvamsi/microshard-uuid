/**
 * @module microshard-uuid
 * @description
 * **MicroShard UUIDv8 Implementation (Zero-Dependency)**
 *
 * A partition-aware, sortable unique identifier generator compliant with RFC 9562 (UUIDv8).
 * Unlike standard UUIDs, MicroShard embeds a **32-bit Shard ID** directly into the 128-bit space.
 *
 * **Bit Layout Architecture:**
 * - **High 64 Bits:**
 *   - Bits 63-16: Timestamp High (48 bits)
 *   - Bits 15-12: Version (4 bits, fixed to 8)
 *   - Bits 11-06: Timestamp Low (6 bits)
 *   - Bits 05-00: Shard ID High (6 bits)
 *
 * - **Low 64 Bits:**
 *   - Bits 63-62: Variant (2 bits, fixed to 2)
 *   - Bits 61-36: Shard ID Low (26 bits)
 *   - Bits 35-00: Randomness (36 bits)
 *
 *  Universal Module Definition (UMD) - Works in Node.js and Browser (<script>).
 */

(function (root, factory) {
  if (typeof module === "object" && typeof module.exports === "object") {
    // Node.js / CommonJS
    module.exports = factory();
  } else {
    // Browser Globals (root is window)
    // Exposes a global variable "MicroShard"
    root.MicroShard = factory();
  }
})(
  typeof globalThis !== "undefined"
    ? globalThis
    : typeof self !== "undefined"
      ? self
      : this,
  function () {
    "use strict";
    // ==========================================
    // 1. Environment Detection & Polyfills
    // ==========================================
    // We detect the environment to choose the best available Random Number Generator (RNG)
    // and High-Resolution Timer without adding external dependencies (npm packages).

    const isNode =
      typeof process !== "undefined" &&
      process.versions != null &&
      process.versions.node != null;

    /**
     * Generates cryptographically strong random bytes.
     * @type {(n: number) => Uint8Array|Buffer}
     */
    let getRandomBytes;

    /**
     * Returns the current time in Microseconds since Unix Epoch.
     * @type {() => bigint}
     */
    let getNowMicros;

    // --- A. Randomness Adapter ---
    if (isNode) {
      // Node.js: Use native crypto module (Zero Dep)
      const nodeCrypto = require("crypto");
      getRandomBytes = (n) => nodeCrypto.randomBytes(n);
    } else {
      // Browser/Edge: Use Web Crypto API
      const webCrypto =
        (typeof globalThis !== "undefined" && globalThis.crypto) ||
        (typeof window !== "undefined" && window.crypto) ||
        (typeof self !== "undefined" && self.crypto);

      if (!webCrypto) {
        throw new Error(
          "MicroShard: Secure Web Crypto API is not available in this environment.",
        );
      }

      getRandomBytes = (n) => {
        const buf = new Uint8Array(n);
        webCrypto.getRandomValues(buf);
        return buf;
      };
    }

    // --- B. High-Resolution Time Adapter ---
    if (isNode) {
      // Node.js: Use process.hrtime.bigint() for monotonic microsecond precision
      const START_TIME_MS = BigInt(Date.now());
      const START_HR_NS = process.hrtime.bigint();
      getNowMicros = () => {
        const currentHrNs = process.hrtime.bigint();
        const diffNs = currentHrNs - START_HR_NS;
        return START_TIME_MS * 1000n + diffNs / 1000n;
      };
    } else {
      // Browser: Use performance.now() if available, else fallback to Date.now()
      const perf =
        (typeof globalThis !== "undefined" && globalThis.performance) ||
        (typeof window !== "undefined" && window.performance);

      if (perf) {
        const origin = BigInt(Math.floor(perf.timeOrigin || Date.now()));
        getNowMicros = () => {
          // performance.now() returns fractional milliseconds
          return origin * 1000n + BigInt(Math.floor(perf.now() * 1000));
        };
      } else {
        getNowMicros = () => BigInt(Date.now()) * 1000n;
      }
    }

    // ==========================================
    // 2. Constants
    // ==========================================

    const MAX_SHARD_ID = 4294967295n; // 2^32 - 1
    const MAX_TIME_MICROS = 18014398509481983n; // 2^54 - 1 (Year ~2541)
    const MAX_RANDOM = 68719476735n; // 2^36 - 1
    const VERSION = 8n;
    const VARIANT = 2n;

    // ==========================================
    // 3. The Core Class
    // ==========================================

    /**
     * Represents a parsed or generated 128-bit MicroShard UUID.
     *
     * Instead of storing the UUID as a string (which requires parsing for every operation),
     * this class stores the ID as two 64-bit BigInts (`high` and `low`).
     * This makes extraction and manipulation significantly faster.
     */
    class MicroShardUUID {
      /**
       * @param {bigint} high - The high 64 bits (Time, Version, Shard High)
       * @param {bigint} low - The low 64 bits (Variant, Shard Low, Random)
       */
      constructor(high, low) {
        this.high = BigInt(high);
        this.low = BigInt(low);
      }

      /**
       * Returns the canonical UUID string representation.
       * Format: 8-4-4-4-12 (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)
       * @returns {string}
       */
      toString() {
        // Pad with zeros to ensure full 64-bit width (16 hex chars)
        const h = this.high.toString(16).padStart(16, "0");
        const l = this.low.toString(16).padStart(16, "0");

        return (
          h.substring(0, 8) +
          "-" +
          h.substring(8, 12) +
          "-" +
          h.substring(12, 16) +
          "-" +
          l.substring(0, 4) +
          "-" +
          l.substring(4)
        );
      }

      /**
       * Helper for JSON.stringify() to automatically serialize as a string.
       * @returns {string}
       */
      toJSON() {
        return this.toString();
      }

      /**
       * Returns the UUID as a Uint8Array (16 bytes).
       * Useful for storing in databases as raw binary (Blob/ByteA).
       * @returns {Uint8Array}
       */
      toBytes() {
        const buf = new Uint8Array(16);
        const view = new DataView(buf.buffer);
        // Use setBigUint64 to write bits in Big Endian (Network Order)
        view.setBigUint64(0, this.high, false);
        view.setBigUint64(8, this.low, false);
        return buf;
      }

      /**
       * Extracts the embedded 32-bit Shard ID.
       * This operation is strictly bitwise and requires no string parsing.
       *
       * @returns {number} The Shard ID (0 - 4,294,967,295)
       */
      getShardId() {
        // 1. Shard High (6 bits) -> Stored in High[5:0]
        const shardHigh = this.high & 0x3fn;

        // 2. Shard Low (26 bits) -> Stored in Low[61:36]
        // shift right 36 to bring them to bottom, mask 26 bits
        const shardLow = (this.low >> 36n) & 0x3ffffffn;

        // 3. Combine: (High << 26) | Low
        return Number((shardHigh << 26n) | shardLow);
      }

      /**
       * Extracts the creation time as a standard JS Date object.
       * **Note:** JS Date only supports millisecond precision, so microseconds are truncated.
       * Use `getIsoTimestamp()` if you need full precision.
       * @returns {Date}
       */
      getDate() {
        const micros = this._getTimestampMicros();
        return new Date(Number(micros / 1000n));
      }

      /**
       * Extracts the creation time as an ISO 8601 string with full **microsecond precision**.
       * @returns {string} e.g. "2025-12-12T10:00:00.123456Z"
       */
      getIsoTimestamp() {
        const totalMicros = this._getTimestampMicros();
        const seconds = Number(totalMicros / 1000000n);
        const microsFraction = Number(totalMicros % 1000000n);

        const dt = new Date(seconds * 1000);
        const baseIso = dt.toISOString().split(".")[0];
        const microStr = microsFraction.toString().padStart(6, "0");

        return `${baseIso}.${microStr}Z`;
      }

      /**
       * Checks equality with another MicroShardUUID.
       * @param {MicroShardUUID} other
       * @returns {boolean}
       */
      equals(other) {
        if (!(other instanceof MicroShardUUID)) return false;
        return this.high === other.high && this.low === other.low;
      }

      /**
       * Internal helper to reconstruct the 54-bit timestamp from split bits.
       * @private
       * @returns {bigint}
       */
      _getTimestampMicros() {
        // Time High (48 bits) -> High[63:16]
        const timeHigh = (this.high >> 16n) & 0xffffffffffffn;
        // Time Low (6 bits) -> High[11:6]
        const timeLow = (this.high >> 6n) & 0x3fn;

        // Combine: (TimeHigh << 6) | TimeLow
        return (timeHigh << 6n) | timeLow;
      }

      /**
       * Parses a UUID string into a MicroShardUUID object.
       * Validates Version (8) and Variant (2) compliance.
       *
       * @param {string} uuidStr - standard UUID string
       * @returns {MicroShardUUID}
       * @throws {Error} If format, version, or variant is invalid.
       */
      static parse(uuidStr) {
        if (!uuidStr) throw new Error("UUID string required");
        // Remove dashes for raw hex parsing
        const hex = uuidStr.replace(/-/g, "");
        if (hex.length !== 32) throw new Error("Invalid UUID string length");

        const high = BigInt("0x" + hex.substring(0, 16));
        const low = BigInt("0x" + hex.substring(16));

        // Validate Version (Bits 12-15 of High must be 8)
        const ver = (high >> 12n) & 0xfn;
        if (ver !== VERSION) {
          throw new Error(`Invalid version: ${ver} (expected 8)`);
        }

        // Validate Variant (Bits 62-63 of Low must be 2)
        const variant = (low >> 62n) & 0x3n;
        if (variant !== VARIANT) {
          throw new Error(`Invalid variant: ${variant} (expected 2)`);
        }

        return new MicroShardUUID(high, low);
      }

      /**
       * Compares this UUID with another using strictly Unsigned 64-bit arithmetic.
       * Uses BigInt.asUintN to correctly interpret the bit patterns.
       *
       * @param {MicroShardUUID} other
       * @returns {number} -1 if this < other, 1 if this > other, 0 if equal
       */
      compare(other) {
        if (!(other instanceof MicroShardUUID)) {
          throw new TypeError("Comparison requires a MicroShardUUID instance");
        }

        // 1. Compare High 64 bits (Timestamp + Version + Shard High)
        // We treat them as unsigned 64-bit integers.
        const aHigh = BigInt.asUintN(64, this.high);
        const bHigh = BigInt.asUintN(64, other.high);

        if (aHigh < bHigh) return -1;
        if (aHigh > bHigh) return 1;

        // 2. High bits are equal, compare Low 64 bits (Variant + Shard Low + Random)
        const aLow = BigInt.asUintN(64, this.low);
        const bLow = BigInt.asUintN(64, other.low);

        if (aLow < bLow) return -1;
        if (aLow > bLow) return 1;

        return 0;
      }

      /**
       * Less Than (<)
       * @param {MicroShardUUID} other
       * @returns {boolean}
       */
      lt(other) {
        return this.compare(other) === -1;
      }

      /**
       * Greater Than (>)
       * @param {MicroShardUUID} other
       * @returns {boolean}
       */
      gt(other) {
        return this.compare(other) === 1;
      }

      /**
       * Less Than or Equal (<=)
       * @param {MicroShardUUID} other
       * @returns {boolean}
       */
      lte(other) {
        return this.compare(other) <= 0;
      }

      /**
       * Greater Than or Equal (>=)
       * @param {MicroShardUUID} other
       * @returns {boolean}
       */
      gte(other) {
        return this.compare(other) >= 0;
      }

      /**
       * Static comparison helper for Array.sort()
       * @param {MicroShardUUID} a
       * @param {MicroShardUUID} b
       * @returns {number}
       */
      static compare(a, b) {
        return a.compare(b);
      }
    }

    // ==========================================
    // 4. Factory Functions
    // ==========================================

    /**
     * Generates a new MicroShardUUID using the current system time.
     * This is the primary entry point for the library.
     *
     * @param {number|bigint} shardId - The 32-bit Shard ID (0 - 4.29 Billion).
     * @returns {MicroShardUUID} A new UUID object.
     * @throws {Error} If shardId is invalid.
     * @example
     * const id = generate(100);
     * console.log(id.toString());
     */
    function generate(shardId) {
      const sId = _validateShard(shardId);
      const nowMicros = getNowMicros();
      return _build(nowMicros, sId);
    }

    /**
     * Generates a MicroShardUUID from a specific past or future timestamp.
     * Useful for data migration or backfilling.
     *
     * @param {Date|string|number|bigint} timestamp - Date object, ISO string, or ms/us number.
     * @param {number|bigint} shardId - The 32-bit Shard ID.
     * @returns {MicroShardUUID}
     * @example
     * const id = fromTimestamp("2023-01-01T00:00:00Z", 55);
     */
    function fromTimestamp(timestamp, shardId) {
      const sId = _validateShard(shardId);
      const micros = _normalizeTimestamp(timestamp);
      return _build(micros, sId);
    }

    // ==========================================
    // 5. Stateful Generator Class
    // ==========================================

    /**
     * A stateful wrapper for the generator.
     * Useful for Dependency Injection where the Shard ID is configured once at startup.
     */
    class Generator {
      /**
       * @param {number|bigint} defaultShardId
       */
      constructor(defaultShardId) {
        this.shardId = _validateShard(defaultShardId);
      }

      /**
       * Generate a new ID using the internal Shard ID.
       * @returns {MicroShardUUID}
       */
      newId() {
        return _build(getNowMicros(), this.shardId);
      }
    }

    // ==========================================
    // 6. Internal Helpers (Private)
    // ==========================================

    function _validateShard(shardId) {
      const sId = BigInt(shardId);
      if (sId < 0n || sId > MAX_SHARD_ID) {
        throw new Error(`Shard ID must be between 0 and ${MAX_SHARD_ID}`);
      }
      return sId;
    }

    function _normalizeTimestamp(timestamp) {
      if (timestamp instanceof Date) return BigInt(timestamp.getTime()) * 1000n;
      if (typeof timestamp === "string") {
        // Regex matches ISO format and extracts seconds + optional microseconds
        const match = timestamp.match(
          /^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(?:\.(\d+))?Z?$/,
        );
        if (!match) throw new Error("Invalid ISO 8601 timestamp");
        const millisBase = BigInt(Date.parse(match[1] + "Z"));
        // Pad fractional part to 6 digits to ensure microsecond interpretation
        let fraction = (match[2] || "0").padEnd(6, "0").substring(0, 6);
        return millisBase * 1000n + BigInt(fraction);
      }
      if (typeof timestamp === "number") return BigInt(timestamp) * 1000n;
      if (typeof timestamp === "bigint")
        return timestamp > 1000000000000n ? timestamp : timestamp * 1000n;
      throw new Error("Invalid timestamp format");
    }

    function _getRandom36() {
      // We need 36 bits. We fetch 5 bytes (40 bits) and mask.
      const buf = getRandomBytes(5);
      let hex = "";
      for (let i = 0; i < buf.length; i++) {
        // Convert bytes to hex string manually to avoid Buffer dependency
        hex += (buf[i] < 16 ? "0" : "") + buf[i].toString(16);
      }
      return BigInt("0x" + hex) & MAX_RANDOM;
    }

    /**
     * Packs the components into the 128-bit structure.
     *
     * @param {bigint} micros
     * @param {bigint} shardId
     * @returns {MicroShardUUID}
     */
    function _build(micros, shardId) {
      if (micros > MAX_TIME_MICROS)
        throw new Error("Time overflow (Year > 2541)");

      const rnd = _getRandom36();

      // --- High 64-Bit Construction ---
      // [TimeHigh 48] [Ver 4] [TimeLow 6] [ShardHigh 6]
      const timeHigh = (micros >> 6n) & 0xffffffffffffn;
      const timeLow = micros & 0x3fn;
      const shardHigh = (shardId >> 26n) & 0x3fn;

      const high64 =
        (timeHigh << 16n) | (VERSION << 12n) | (timeLow << 6n) | shardHigh;

      // --- Low 64-Bit Construction ---
      // [Var 2] [ShardLow 26] [Random 36]
      const shardLow = shardId & 0x3ffffffn;
      const low64 = (VARIANT << 62n) | (shardLow << 36n) | rnd;

      return new MicroShardUUID(high64, low64);
    }

    // Public API
    return {
      MicroShardUUID,
      generate,
      fromTimestamp,
      Generator,
    };
  },
);
