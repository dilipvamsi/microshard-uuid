/**
 * MicroShard UUID - Verification Test Suite
 *
 * Run with: node test/test.js
 */

const assert = require("assert");
const {
  generate,
  fromTimestamp,
  getShardId,
  getDate,
  getIsoTimestamp,
  Generator,
} = require("../src/index");

// Helper to print test headers
function test(name, fn) {
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
console.log("=========================================\n");

// ==========================================
// 1. Shard Integrity Tests
// ==========================================
test("Shard ID Integrity", () => {
  // Edge cases for 32-bit unsigned integers
  const testCases = [0, 1, 500, 999999, 4294967295];

  testCases.forEach((shard) => {
    // Generate ID
    const uid = generate(shard);

    // Extract ID
    const extracted = getShardId(uid);

    // Verify they match
    assert.strictEqual(extracted, shard, `Failed integrity for shard ${shard}`);
  });
});

// ==========================================
// 2. Timestamp Accuracy (Stateless)
// ==========================================
test("Current Time Accuracy", () => {
  const start = Date.now();
  const uid = generate(100);
  const end = Date.now();

  // Extract time (Date object)
  const extractedDate = getDate(uid);
  const extractedTime = extractedDate.getTime();

  // Check if extracted time is within the start-end execution window
  // Allow 200ms buffer for execution jitter
  assert.ok(extractedTime >= start - 200, "Timestamp too early");
  assert.ok(extractedTime <= end + 200, "Timestamp too late");
});

// ==========================================
// 3. ISO 8601 Microsecond Precision
// ==========================================
test("ISO 8601 Microsecond Preservation", () => {
  // Input: A specific time with Microseconds (.123456)
  // 2025-12-12 01:35:00.123456 UTC
  const inputIso = "2025-12-12T01:35:00.123456Z";

  // Generate UUID from this specific time
  const uid = fromTimestamp(inputIso, 50);

  // 1. Check Standard Date extraction (Should be Lossy)
  // Date objects in JS only hold milliseconds. It should truncate "456".
  const dateObj = getDate(uid);
  assert.strictEqual(
    dateObj.toISOString(),
    "2025-12-12T01:35:00.123Z",
    "Standard Date object should have truncated microseconds"
  );

  // 2. Check ISO Extraction (Should be Lossless)
  // This function manually parses the 128-bit ID to get the full 54-bit time.
  const outputIso = getIsoTimestamp(uid);

  assert.strictEqual(
    outputIso,
    inputIso,
    "getIsoTimestamp should preserve full 6-digit microsecond precision"
  );
});

// ==========================================
// 4. Input Types for Backfilling
// ==========================================
test("Input Types (Date vs Int vs String)", () => {
  const shard = 5;
  const targetIso = "2023-01-01T12:00:00.000000Z";
  const targetMs = 1672574400000; // 2023-01-01 12:00:00 UTC in ms

  // Case A: Date Object
  const uidDate = fromTimestamp(new Date(targetMs), shard);
  assert.strictEqual(getIsoTimestamp(uidDate), targetIso);

  // Case B: Number (Milliseconds)
  const uidNum = fromTimestamp(targetMs, shard);
  assert.strictEqual(getIsoTimestamp(uidNum), targetIso);

  // Case C: BigInt (Microseconds)
  const targetMicros = BigInt(targetMs) * 1000n;
  const uidBigInt = fromTimestamp(targetMicros, shard);
  assert.strictEqual(getIsoTimestamp(uidBigInt), targetIso);
});

// ==========================================
// 5. Stateful Generator Class
// ==========================================
test("Stateful Generator Class", () => {
  // Configure generator for Tenant #777
  const gen = new Generator(777);

  // Test A: newId()
  const uid1 = gen.newId();
  assert.strictEqual(
    getShardId(uid1),
    777,
    "Generator should use configured shard ID"
  );

  // Test B: fromTimestamp()
  const target = "2025-01-01T00:00:00.000000Z";
  const uid2 = gen.fromTimestamp(target);
  assert.strictEqual(getIsoTimestamp(uid2), target);
  assert.strictEqual(getShardId(uid2), 777);
});

// ==========================================
// 6. RFC Compliance (Bits & Bytes)
// ==========================================
test("RFC 9562 Compliance (Ver 8, Var 2)", () => {
  const uid = generate(1);

  // UUID format: xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
  // M = Version (must be 8)
  // N = Variant (must be 8, 9, a, or b for Variant 2)

  // Check Version (Index 14)
  assert.strictEqual(uid.charAt(14), "8", "UUID Version must be 8");

  // Check Variant (Index 19)
  const variantChar = uid.charAt(19);
  const validVariants = ["8", "9", "a", "b"];
  assert.ok(
    validVariants.includes(variantChar),
    `Variant must be 2 (Got '${variantChar}')`
  );
});

// ==========================================
// 7. Sorting & Monotonicity
// ==========================================
test("Chronological Sorting", () => {
  // Create an ID from the past
  const pastDate = new Date(Date.now() - 50000); // 50 seconds ago
  const uidOld = fromTimestamp(pastDate, 1);

  // Create an ID now
  const uidNew = generate(1);

  // Lexicographical String sort (standard string comparison)
  // should match Chronological sort because Time is in the High bits.
  assert.ok(uidOld < uidNew, "Old UUID should be strictly less than New UUID");
});

// ==========================================
// 8. Error Handling / Validation
// ==========================================
test("Error Handling", () => {
  // Case A: Negative Shard
  assert.throws(
    () => {
      generate(-1);
    },
    /Shard ID/,
    "Should throw on negative shard"
  );

  // Case B: Shard Overflow (> 32 bits)
  assert.throws(
    () => {
      generate(4294967296); // 2^32
    },
    /Shard ID/,
    "Should throw on 32-bit overflow"
  );

  // Case C: Invalid Timestamp String
  assert.throws(
    () => {
      fromTimestamp("Not a date", 1);
    },
    /Invalid ISO/,
    "Should throw on bad date string"
  );
});

console.log("\n🎉 ALL TESTS PASSED SUCCESSFULLY");
