/**
 * Generates a new UUIDv8 using the current system time (High-Res) for a specific shard.
 *
 * @param shardId - The 32-bit Shard/Tenant ID (0 - 4,294,967,295).
 * @returns The formatted UUID string (e.g. "018e...").
 * @throws {Error} If shardId is out of the 32-bit range.
 */
export function generate(shardId: number | bigint): string;

/**
 * Generates a UUIDv8 for a SPECIFIC timestamp.
 * Supports ISO 8601 strings with microsecond precision.
 *
 * @param timestamp - Date object, ISO string, number (ms), or bigint (micros).
 * @param shardId - The 32-bit Shard/Tenant ID.
 * @returns The formatted UUID string.
 * @throws {Error} If timestamp format is invalid.
 */
export function fromTimestamp(
  timestamp: Date | string | number | bigint,
  shardId: number | bigint
): string;

/**
 * Extracts the 32-bit Shard ID from a UUID string.
 *
 * @param uuid - The MicroShard UUIDv8 string.
 * @returns The extracted Shard ID as a number.
 */
export function getShardId(uuid: string): number;

/**
 * Extracts the creation time from a UUID string as an ISO 8601 string.
 * **Preserves Microsecond Precision** (e.g., ".123456Z").
 *
 * @param uuid - The MicroShard UUIDv8 string.
 * @returns The ISO 8601 timestamp string.
 */
export function getIsoTimestamp(uuid: string): string;

/**
 * Extracts the creation time from a UUID string as a JS Date object.
 * **Warning:** Truncates microseconds to milliseconds.
 *
 * @param uuid - The MicroShard UUIDv8 string.
 * @returns The creation time as a Date object.
 */
export function getDate(uuid: string): Date;

/**
 * A stateful generator that holds the Shard ID configuration.
 * Best used for Dependency Injection or Singleton patterns.
 */
export class Generator {
  /**
   * Creates a new Generator instance.
   * @param defaultShardId - The default 32-bit Shard ID.
   */
  constructor(defaultShardId: number | bigint);

  /**
   * Generate a new UUID using the configured Shard ID.
   * @returns The formatted UUID string.
   */
  newId(): string;

  /**
   * Generate a UUID using the configured Shard ID and specific time.
   * @param timestamp - Date object, ISO String, or number.
   * @returns The formatted UUID string.
   */
  fromTimestamp(timestamp: Date | string | number | bigint): string;
}
