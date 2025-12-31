/**
 * Represents a 128-bit MicroShard UUID (UUIDv8).
 * Stores the value as two 64-bit BigInts for performance and precision.
 */
export class MicroShardUUID {
  /**
   * The High 64 bits (Time, Version, Shard High).
   */
  readonly high: bigint;

  /**
   * The Low 64 bits (Variant, Shard Low, Random).
   */
  readonly low: bigint;

  /**
   * Creates a new instance.
   * @param high The high 64 bits.
   * @param low The low 64 bits.
   */
  constructor(high: bigint, low: bigint);

  /**
   * Returns the canonical UUID string representation.
   * Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
   */
  toString(): string;

  /**
   * Helper for JSON serialization. Returns the UUID string.
   */
  toJSON(): string;

  /**
   * Returns the UUID as a raw 16-byte Uint8Array (Big Endian).
   * Useful for binary database storage (ByteA/Blob).
   */
  toBytes(): Uint8Array;

  /**
   * Extracts the embedded 32-bit Shard ID.
   * Performs a bitwise extraction (O(1)).
   */
  getShardId(): number;

  /**
   * Extracts the creation time as a standard JS Date object.
   * @note Truncates microseconds to milliseconds.
   */
  getDate(): Date;

  /**
   * Extracts the creation time as an ISO 8601 string.
   * Preserves full **microsecond** precision (e.g., `.123456Z`).
   */
  getIsoTimestamp(): string;

  /**
   * Checks equality with another MicroShardUUID instance.
   * @param other The other UUID to compare.
   */
  equals(other: MicroShardUUID): boolean;

  /**
   * Compares this UUID with another using strictly Unsigned 64-bit arithmetic.
   * This is useful for chronological sorting.
   *
   * @param other The other UUID to compare.
   * @returns -1 if this < other, 1 if this > other, 0 if equal.
   */
  compare(other: MicroShardUUID): number;

  /**
   * Less Than (<).
   * Checks if this UUID is chronologically/lexicographically older (smaller) than the other.
   */
  lt(other: MicroShardUUID): boolean;

  /**
   * Greater Than (>).
   * Checks if this UUID is chronologically/lexicographically newer (larger) than the other.
   */
  gt(other: MicroShardUUID): boolean;

  /**
   * Less Than or Equal (<=).
   */
  lte(other: MicroShardUUID): boolean;

  /**
   * Greater Than or Equal (>=).
   */
  gte(other: MicroShardUUID): boolean;

  /**
   * Parses a standard UUID string into a MicroShardUUID object.
   * Validates Version 8 and Variant 2 compliance.
   * @param uuidStr The UUID string to parse.
   * @throws Error if the string is invalid or not a MicroShard UUID.
   */
  static parse(uuidStr: string): MicroShardUUID;

  /**
   * Static comparison helper for array sorting.
   * Usage: `items.sort(MicroShardUUID.compare)`
   *
   * @returns -1 if a < b, 1 if a > b, 0 if equal.
   */
  static compare(a: MicroShardUUID, b: MicroShardUUID): number;
}

/**
 * Generates a new MicroShardUUID using the current system time.
 * @param shardId The 32-bit Shard/Tenant ID (0 - 4,294,967,295).
 */
export function generate(shardId: number | bigint): MicroShardUUID;

/**
 * Generates a MicroShardUUID from a specific timestamp.
 * Useful for migrations or backfilling data.
 *
 * @param timestamp Date object, ISO string, milliseconds (number), or microseconds (bigint).
 * @param shardId The 32-bit Shard/Tenant ID.
 */
export function fromTimestamp(
  timestamp: Date | string | number | bigint,
  shardId: number | bigint,
): MicroShardUUID;

/**
 * A stateful generator wrapper.
 * Useful for Dependency Injection where the Shard ID is configured once at startup.
 */
export class Generator {
  /**
   * Creates a generator fixed to a specific Shard ID.
   * @param defaultShardId The 32-bit Shard ID.
   */
  constructor(defaultShardId: number | bigint);

  /**
   * Generates a new MicroShardUUID using the configured Shard ID.
   */
  newId(): MicroShardUUID;
}

/**
 * UMD Namespace Declaration.
 * This allows the library to be used as a global variable `MicroShard`
 * when loaded via a <script> tag in non-module environments.
 */
export as namespace MicroShard;
