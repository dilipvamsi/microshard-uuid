/**
 * MicroShard UUID - Verification Test Suite
 *
 * Run with: bun test/test.ts
 */

import assert from "assert";
import {
  MicroShardUUID,
  generate,
  fromTimestamp,
  Generator,
} from "../src/index";

// Helper to print test headers
function test(name: string, fn: Function) {
  try {
    process.stdout.write(`🧪 Testing ${name}... `);
    fn();
    console.log("✅ PASS");
  } catch (e) {
    console.log("❌ FAIL");
    console.error(e);
    process.exit(1);
  }
}

console.log("=========================================");
console.log("   MicroShard UUID (JS) Test Suite      ");
console.log("   Architecture: Class-based (BigInt)   ");
console.log("=========================================\n");

// ==========================================
// 1. Shard Integrity Tests
// ==========================================
test("Shard ID Integrity", () => {
  const testCases = [0, 1, 500, 999999, 4294967295];

  testCases.forEach((shard) => {
    // Generate ID (returns MicroShardUUID object)
    const uid = generate(shard);

    // Extract ID using instance method
    const extracted = uid.getShardId();

    assert.strictEqual(extracted, shard, `Failed integrity for shard ${shard}`);
  });
});

// ==========================================
// 2. Timestamp Accuracy
// ==========================================
test("Current Time Accuracy", () => {
  const start = Date.now();
  const uid = generate(100);
  const end = Date.now();

  // Extract time (Date object) from instance
  const extractedDate = uid.getDate();
  const extractedTime = extractedDate.getTime();

  // Allow 200ms buffer for execution jitter
  assert.ok(extractedTime >= start - 200, "Timestamp too early");
  assert.ok(extractedTime <= end + 200, "Timestamp too late");
});

// ==========================================
// 3. ISO 8601 Microsecond Precision
// ==========================================
test("ISO 8601 Microsecond Preservation", () => {
  // Input: A specific time with Microseconds (.123456)
  const inputIso = "2025-12-12T01:35:00.123456Z";

  // Generate UUID object
  const uid = fromTimestamp(inputIso, 50);

  // 1. Check Standard Date extraction (Should be Lossy)
  const dateObj = uid.getDate();
  assert.strictEqual(
    dateObj.toISOString(),
    "2025-12-12T01:35:00.123Z",
    "Standard Date object should have truncated microseconds",
  );

  // 2. Check ISO Extraction (Should be Lossless)
  const outputIso = uid.getIsoTimestamp();

  assert.strictEqual(
    outputIso,
    inputIso,
    "getIsoTimestamp() should preserve full 6-digit microsecond precision",
  );
});

// ==========================================
// 4. Object & Parsing Roundtrip
// ==========================================
test("Parsing & Object Equality", () => {
  // 1. Generate Original
  const original = generate(123);
  const str = original.toString();

  // 2. Parse back from string
  const parsed = MicroShardUUID.parse(str);

  // 3. Verify Equality
  assert.ok(original.equals(parsed), "Parsed object should equal original");

  // 4. Verify Data Persisted
  assert.strictEqual(
    parsed.getShardId(),
    123,
    "Parsed object lost Shard ID data",
  );
});

// ==========================================
// 5. Binary Output (Uint8Array)
// ==========================================
test("Binary Output (toBytes)", () => {
  const uid = generate(1);
  const bytes = uid.toBytes();

  assert.ok(bytes instanceof Uint8Array, "Should return Uint8Array");
  assert.strictEqual(bytes.length, 16, "Should be exactly 16 bytes");

  // Verify reconstruction from bytes via parsing the string hex
  // (In a real app, you might just store the bytes directly)
  const hex = Buffer.from(bytes).toString("hex");
  // Insert dashes manually to test parse
  const uuidStr = [
    hex.slice(0, 8),
    hex.slice(8, 12),
    hex.slice(12, 16),
    hex.slice(16, 20),
    hex.slice(20),
  ].join("-");

  const reconstructed = MicroShardUUID.parse(uuidStr);
  assert.ok(uid.equals(reconstructed), "Binary roundtrip failed");
});

// ==========================================
// 6. JSON Serialization
// ==========================================
test("JSON.stringify() Behavior", () => {
  const uid = generate(99);
  const container = { id: uid, name: "test" };

  const json = JSON.stringify(container);
  const parsed = JSON.parse(json);

  assert.strictEqual(
    parsed.id,
    uid.toString(),
    "toJSON() should auto-convert to canonical string",
  );
});

// ==========================================
// 7. Input Types for Backfilling
// ==========================================
test("Input Types (Date vs Int vs String)", () => {
  const shard = 5;
  const targetIso = "2023-01-01T12:00:00.000000Z";
  const targetMs = 1672574400000;

  // Case A: Date Object
  const uidDate = fromTimestamp(new Date(targetMs), shard);
  assert.strictEqual(uidDate.getIsoTimestamp(), targetIso);

  // Case B: Number (Milliseconds)
  const uidNum = fromTimestamp(targetMs, shard);
  assert.strictEqual(uidNum.getIsoTimestamp(), targetIso);

  // Case C: BigInt (Microseconds)
  const targetMicros = BigInt(targetMs) * 1000n;
  const uidBigInt = fromTimestamp(targetMicros, shard);
  assert.strictEqual(uidBigInt.getIsoTimestamp(), targetIso);
});

// ==========================================
// 8. Stateful Generator Class
// ==========================================
test("Stateful Generator Class", () => {
  const gen = new Generator(777);

  // Test newId() returning object
  const uid1 = gen.newId();
  assert.strictEqual(
    uid1.getShardId(),
    777,
    "Generator should use configured shard ID",
  );
  assert.ok(uid1 instanceof MicroShardUUID);
});

// ==========================================
// 9. RFC Compliance
// ==========================================
test("RFC 9562 Compliance (Ver 8, Var 2)", () => {
  const uid = generate(1);
  const str = uid.toString();

  // Version at index 14
  assert.strictEqual(str.charAt(14), "8", "UUID Version must be 8");

  // Variant at index 19
  const variantChar = str.charAt(19);
  const validVariants = ["8", "9", "a", "b"];
  assert.ok(
    validVariants.includes(variantChar),
    `Variant must be 2 (Got '${variantChar}')`,
  );
});

// ==========================================
// 10. UUID String Sorting
// ==========================================
test("Chronological String Sorting", () => {
  const pastDate = new Date(Date.now() - 50000);
  const uidOld = fromTimestamp(pastDate, 1);
  const uidNew = generate(1);

  // Lexicographical String sort
  assert.ok(
    uidOld.toString() < uidNew.toString(),
    "Old UUID string should be strictly less than New UUID string",
  );

  // Numeric High-Bit sort (Simulating binary sort)
  assert.ok(
    uidOld.high < uidNew.high,
    "Old UUID High Bits should be less than New UUID High Bits",
  );
});

// ==========================================
// 11. Comparison & Sorting APIs (Updated)
// ==========================================
test("Comparison & Sorting APIs", () => {
  const t1 = fromTimestamp(1000, 1); // Oldest
  const t2 = fromTimestamp(2000, 1); // Middle

  // FIX: Create a clone of t2 (including random bits) to test equality
  const t3 = MicroShardUUID.parse(t2.toString());

  const t4 = fromTimestamp(3000, 1); // Newest

  // 1. Standard Compare (-1, 0, 1)
  assert.strictEqual(t1.compare(t2), -1, "t1 should be less than t2");
  assert.strictEqual(t4.compare(t2), 1, "t4 should be greater than t2");

  // Now t2 and t3 are bitwise identical, so compare must return 0
  assert.strictEqual(t2.compare(t3), 0, "t2 should equal t3 (clone)");

  // 2. Relational Helpers (.lt, .gt, .lte, .gte)
  assert.ok(t1.lt(t2), "t1.lt(t2) should be true");
  assert.ok(t4.gt(t2), "t4.gt(t2) should be true");

  assert.ok(t2.lte(t3), "t2.lte(t3) should be true (equal)");
  assert.ok(t2.gte(t3), "t2.gte(t3) should be true (equal)");
  assert.ok(t1.lte(t4), "t1.lte(t4) should be true (smaller)");

  // 3. Array Sorting
  const list = [t4, t2, t1]; // [New, Middle, Old]
  list.sort(MicroShardUUID.compare);

  assert.ok(list[0].equals(t1), "First element should be Oldest");
  assert.ok(list[1].equals(t2), "Second element should be Middle");
  assert.ok(list[2].equals(t4), "Last element should be Newest");
});

// ==========================================
// 12. Unsigned 64-bit Logic (Edge Case)
// ==========================================
test("Unsigned 64-bit Sorting Logic", () => {
  /**
   * IN JS BigInt, -1n is numerically less than 10n.
   * IN UUID (Unsigned 64-bit), 0xFF...FF (which looks like -1) is the LARGEST value.
   *
   * We manually construct a UUID with -1n in the high bits.
   * If .compare() uses standard signed logic, this will fail.
   * If .compare() uses asUintN() correctly, this will pass.
   */

  // -1n represents 0xFFFFFFFFFFFFFFFF in 64-bit representation
  const hugeUUID = new MicroShardUUID(-1n, 0n);
  const smallUUID = new MicroShardUUID(10n, 0n);

  // 1. Prove standard JS behavior is wrong for UUIDs
  assert.ok(hugeUUID.high < smallUUID.high, "Sanity check: JS sees -1n < 10n");

  // 2. Prove our .compare() handles it correctly (Unsigned view)
  // huge (MaxUint64) should be GREATER than small (10)
  assert.strictEqual(
    hugeUUID.compare(smallUUID),
    1,
    "Unsigned Compare failed: 0xFF... should be > 0x0A...",
  );

  assert.ok(hugeUUID.gt(smallUUID), ".gt() failed on unsigned check");
});

// ==========================================
// 13. Shard ID Sorting
// ==========================================
test("Same Timestamp, Different Shard Sorting", () => {
  const time = Date.now();

  // Shard 1 vs Shard 2 (Same Time)
  // Since Shard ID is embedded in High bits, Shard 1 < Shard 2
  const idShard1 = fromTimestamp(time, 1);
  const idShard2 = fromTimestamp(time, 2);

  assert.strictEqual(
    idShard1.compare(idShard2),
    -1,
    "Lower Shard ID should sort first",
  );
});

// ==========================================
// 14. Error Handling
// ==========================================
test("Error Handling", () => {
  // Invalid Shards
  assert.throws(() => generate(-1), /Shard ID/);
  assert.throws(() => generate(4294967296), /Shard ID/);

  // Parsing Errors
  assert.throws(
    () => MicroShardUUID.parse("invalid-uuid"),
    /Invalid UUID string length/,
  );

  // Create a UUID string with Wrong Version (e.g., Version 4)
  // xxxxxxxx-xxxx-4xxx...
  const ver4 = "018eb13c-d81b-4190-8000-64299b9514e2";
  assert.throws(() => MicroShardUUID.parse(ver4), /Invalid version/);
});

console.log("\n🎉 ALL TESTS PASSED SUCCESSFULLY");
