/**
 * MicroShard UUID (JavaScript/Universal)
 *
 * A zero-lookup, partition-aware UUIDv8 implementation.
 * Layout: 54-bit Time | 32-bit Shard ID | 36-bit Random
 *
 * @module microshard-uuid
 */

// ==========================================
// 1. Environment Detection & Polyfills
// ==========================================

/**
 * Detects if running in Node.js environment.
 * @type {boolean}
 */
const isNode =
  typeof process !== "undefined" &&
  process.versions != null &&
  process.versions.node != null;

// --- A. Randomness Adapter ---

/**
 * Generates cryptographically strong random bytes.
 * @type {(n: number) => Uint8Array|Buffer}
 */
let getRandomBytes;

if (isNode) {
  // Node.js: Use native crypto module
  const nodeCrypto = require("crypto");
  getRandomBytes = (n) => nodeCrypto.randomBytes(n);
} else {
  // Browser: Use Web Crypto API
  const webCrypto =
    (typeof globalThis !== "undefined" && globalThis.crypto) ||
    (typeof window !== "undefined" && window.crypto) ||
    (typeof self !== "undefined" && self.crypto);

  if (!webCrypto) {
    throw new Error(
      "MicroShard: Secure Web Crypto API is not available in this environment."
    );
  }

  getRandomBytes = (n) => {
    /** @type {Uint8Array} */
    const buf = new Uint8Array(n);
    webCrypto.getRandomValues(buf);
    return buf;
  };
}

// --- B. High-Resolution Time Adapter ---

/**
 * Returns the current time in Microseconds since Unix Epoch.
 * @type {() => bigint}
 */
let getNowMicros;

if (isNode) {
  // Node.js: Use process.hrtime.bigint() anchored to Date.now()
  const START_TIME_MS = BigInt(Date.now());
  const START_HR_NS = process.hrtime.bigint();

  getNowMicros = () => {
    const currentHrNs = process.hrtime.bigint();
    const diffNs = currentHrNs - START_HR_NS;
    // Formula: (StartMS * 1000) + (DeltaNS / 1000)
    return START_TIME_MS * 1000n + diffNs / 1000n;
  };
} else {
  // Browser: Use performance.now() anchored to timeOrigin
  const perf =
    (typeof globalThis !== "undefined" && globalThis.performance) ||
    (typeof window !== "undefined" && window.performance);

  if (perf) {
    const origin = BigInt(Math.floor(perf.timeOrigin || Date.now()));
    getNowMicros = () => {
      /** @type {number} Floating point milliseconds (e.g. 100.123) */
      const now = perf.now();
      const micros = BigInt(Math.floor(now * 1000));
      return origin * 1000n + micros;
    };
  } else {
    // Fallback: Standard Date.now()
    getNowMicros = () => BigInt(Date.now()) * 1000n;
  }
}

// ==========================================
// 2. Constants
// ==========================================

/** @type {bigint} Max 32-bit integer (4,294,967,295) */
const MAX_SHARD_ID = 4294967295n;

/** @type {bigint} Max 54-bit integer (Year ~2541) */
const MAX_TIME_MICROS = 18014398509481983n;

/** @type {bigint} Max 36-bit integer (68.7 Billion) */
const MAX_RANDOM = 68719476735n;

// ==========================================
// 3. Public Stateless Functions
// ==========================================

/**
 * Generates a new UUIDv8 using the current system time (High-Res) for a specific shard.
 *
 * @param {number|bigint} shardId - The 32-bit Shard/Tenant ID (0 - 4,294,967,295).
 * @returns {string} The formatted UUID string (e.g. "018e...").
 * @throws {Error} If shardId is out of the 32-bit range.
 *
 * @example
 * const uid = generate(101);
 */
function generate(shardId) {
  const sId = _validateShard(shardId);
  const nowMicros = getNowMicros();
  return _buildUUID(nowMicros, sId);
}

/**
 * Generates a UUIDv8 for a SPECIFIC timestamp.
 * Supports ISO 8601 strings with microsecond precision.
 *
 * @param {Date|string|number|bigint} timestamp - The time source.
 *   - `Date`: JS Date object.
 *   - `string`: ISO 8601 string (e.g., "2025-12-12T10:00:00.123456Z").
 *   - `number`: Milliseconds (Unix Epoch).
 *   - `bigint`: Microseconds (Unix Epoch).
 * @param {number|bigint} shardId - The 32-bit Shard/Tenant ID.
 * @returns {string} The formatted UUID string.
 * @throws {Error} If timestamp format is invalid or shardId is out of range.
 *
 * @example
 * const uid = fromTimestamp("2025-01-01T12:00:00.123456Z", 55);
 */
function fromTimestamp(timestamp, shardId) {
  const sId = _validateShard(shardId);
  const micros = _normalizeTimestamp(timestamp);
  return _buildUUID(micros, sId);
}

/**
 * Extracts the 32-bit Shard ID from a UUID string.
 *
 * @param {string} uuid - The MicroShard UUIDv8 string.
 * @returns {number} The extracted Shard ID.
 * @throws {Error} If the UUID string is malformed.
 */
function getShardId(uuid) {
  const { high, low } = _parseUUID(uuid);

  // Logic:
  // High[5:0] is Shard High (6 bits)
  // Low[63:36] is Shard Low (26 bits)

  /** @type {bigint} Mask bottom 6 bits */
  const shardHigh = high & 0x3fn;

  /** @type {bigint} Shift right 36, Mask 26 bits */
  const shardLow = (low >> 36n) & 0x3ffffffn;

  return Number((shardHigh << 26n) | shardLow);
}

/**
 * Extracts the creation time from a UUID string as an ISO 8601 string.
 * **Preserves Microsecond Precision** (e.g., ".123456Z").
 *
 * @param {string} uuid - The MicroShard UUIDv8 string.
 * @returns {string} ISO string.
 */
function getIsoTimestamp(uuid) {
  const { high } = _parseUUID(uuid);

  /** @type {bigint} Top 48 bits of time */
  const timeHigh = (high >> 16n) & 0xffffffffffffn;

  /** @type {bigint} Bottom 6 bits of time */
  const timeLow = (high >> 6n) & 0x3fn;

  /** @type {bigint} Recombined 54-bit timestamp */
  const totalMicros = (timeHigh << 6n) | timeLow;

  const seconds = Number(totalMicros / 1000000n);
  const microsFraction = Number(totalMicros % 1000000n);

  const dt = new Date(seconds * 1000);
  const baseIso = dt.toISOString().split(".")[0]; // YYYY-MM-DDTHH:mm:ss
  const microStr = microsFraction.toString().padStart(6, "0");

  return `${baseIso}.${microStr}Z`;
}

/**
 * Extracts the creation time from a UUID string as a JS Date object.
 * **Note:** Truncates microseconds to milliseconds.
 *
 * @param {string} uuid - The UUID string.
 * @returns {Date}
 */
function getDate(uuid) {
  const { high } = _parseUUID(uuid);
  const timeHigh = (high >> 16n) & 0xffffffffffffn;
  const timeLow = (high >> 6n) & 0x3fn;
  const micros = (timeHigh << 6n) | timeLow;
  return new Date(Number(micros / 1000n));
}

// ==========================================
// 4. Stateful Generator Class
// ==========================================

/**
 * A stateful generator that holds the Shard ID configuration.
 * Best used for Dependency Injection or Application Singleton patterns.
 */
class Generator {
  /**
   * Creates a new Generator instance.
   * @param {number|bigint} defaultShardId - The default 32-bit Shard ID.
   */
  constructor(defaultShardId) {
    this.shardId = _validateShard(defaultShardId);
  }

  /**
   * Generate using stored shard ID and current time.
   * @returns {string}
   */
  newId() {
    return _buildUUID(getNowMicros(), this.shardId);
  }

  /**
   * Generate using stored shard ID and specific time.
   * @param {Date|string|number|bigint} timestamp
   * @returns {string}
   */
  fromTimestamp(timestamp) {
    const micros = _normalizeTimestamp(timestamp);
    return _buildUUID(micros, this.shardId);
  }
}

// ==========================================
// 5. Internal Helpers (Private)
// ==========================================

/**
 * Validates and converts Shard ID to BigInt.
 * @private
 * @param {number|bigint} shardId
 * @returns {bigint} Validated Shard ID
 */
function _validateShard(shardId) {
  const sId = BigInt(shardId);
  if (sId < 0n || sId > MAX_SHARD_ID) {
    throw new Error(`Shard ID must be between 0 and ${MAX_SHARD_ID}`);
  }
  return sId;
}

/**
 * Normalizes various timestamp inputs to Microseconds (BigInt).
 * @private
 * @param {Date|string|number|bigint} timestamp
 * @returns {bigint} Microseconds since Epoch
 */
function _normalizeTimestamp(timestamp) {
  if (timestamp instanceof Date) return BigInt(timestamp.getTime()) * 1000n;

  // ISO String Manual Parsing
  if (typeof timestamp === "string") {
    const match = timestamp.match(
      /^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(?:\.(\d+))?Z?$/
    );
    if (!match) throw new Error("Invalid ISO 8601 timestamp");

    const millisBase = BigInt(Date.parse(match[1] + "Z"));
    // Pad fractional part to 6 digits (microseconds)
    let fraction = (match[2] || "0").padEnd(6, "0").substring(0, 6);
    return millisBase * 1000n + BigInt(fraction);
  }

  if (typeof timestamp === "number") return BigInt(timestamp) * 1000n;

  if (typeof timestamp === "bigint") {
    // Heuristic: If > Year 3000 in seconds, assume it's already micros.
    return timestamp > 1000000000000n ? timestamp : timestamp * 1000n;
  }

  throw new Error(
    "Timestamp must be Date, ISO String, number (ms), or bigint (micros)"
  );
}

/**
 * Generates 36 bits of cryptographically strong randomness.
 * @private
 * @returns {bigint} Random value (0 - 68 Billion)
 */
function _getRandom36() {
  /** @type {Uint8Array|Buffer} 5 bytes = 40 bits */
  const buf = getRandomBytes(5);

  // Convert buffer to hex manually for browser compatibility
  let hex = "";
  for (let i = 0; i < buf.length; i++) {
    // padStart ensures '5' becomes '05'
    hex += (buf[i] < 16 ? "0" : "") + buf[i].toString(16);
  }

  // Mask to 36 bits
  return BigInt("0x" + hex) & MAX_RANDOM;
}

/**
 * Core Logic: Packs bits into the 128-bit UUIDv8 layout.
 * @private
 * @param {bigint} micros - 54-bit timestamp
 * @param {bigint} shardId - 32-bit shard ID
 * @returns {string} Formatted UUID
 */
function _buildUUID(micros, shardId) {
  if (micros > MAX_TIME_MICROS) throw new Error("Time overflow (Year > 2541)");

  const rndVal = _getRandom36();

  // --- High 64-Bit Integer Construction ---
  // Layout: [Time High 48] [Ver 4] [Time Low 6] [Shard High 6]

  /** @type {bigint} Top 48 bits of timestamp */
  const timeHigh = (micros >> 6n) & 0xffffffffffffn;

  /** @type {bigint} Bottom 6 bits of timestamp */
  const timeLow = micros & 0x3fn;

  /** @type {bigint} Top 6 bits of Shard ID */
  const shardHigh = (shardId >> 26n) & 0x3fn;

  const high64 =
    (timeHigh << 16n) | (0x8n << 12n) | (timeLow << 6n) | shardHigh;

  // --- Low 64-Bit Integer Construction ---
  // Layout: [Var 2] [Shard Low 26] [Random 36]

  /** @type {bigint} Bottom 26 bits of Shard ID */
  const shardLow = shardId & 0x3ffffffn;

  const low64 = (0x2n << 62n) | (shardLow << 36n) | rndVal;

  // Format to Hex String
  const h = high64.toString(16).padStart(16, "0");
  const l = low64.toString(16).padStart(16, "0");

  // Return standard UUID format: 8-4-4-4-12
  return `${h.substring(0, 8)}-${h.substring(8, 12)}-${h.substring(
    12,
    16
  )}-${l.substring(0, 4)}-${l.substring(4)}`;
}

/**
 * Parses UUID string into High/Low BigInts.
 * @private
 * @param {string} uuid
 * @returns {{high: bigint, low: bigint}}
 */
function _parseUUID(uuid) {
  if (!uuid) throw new Error("UUID string required");
  const hex = uuid.replace(/-/g, "");
  if (hex.length !== 32) throw new Error("Invalid UUID string");

  const high = BigInt("0x" + hex.substring(0, 16));
  const low = BigInt("0x" + hex.substring(16));
  return { high, low };
}

// Exports
module.exports = {
  generate,
  fromTimestamp,
  getShardId,
  getDate,
  getIsoTimestamp,
  Generator,
};
