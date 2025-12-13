package microsharduuid

import (
	"sort"
	"strings"
	"testing"
	"time"
)

func TestShardIntegrity(t *testing.T) {
	shards := []uint32{0, 1, 500, 1024, 4294967295}

	for _, shard := range shards {
		uuid, err := Generate(shard)
		if err != nil {
			t.Fatalf("Generate failed: %v", err)
		}

		// Use the struct method .ShardID()
		extracted := uuid.ShardID()

		if extracted != shard {
			t.Errorf("Shard mismatch. Expected %d, got %d", shard, extracted)
		}
	}
}

func TestTimeAccuracy(t *testing.T) {
	start := time.Now()
	uuid, _ := Generate(100)
	end := time.Now()

	// Use the struct method .Time()
	extracted := uuid.Time()

	// Check delta (allow 100ms jitter)
	// Truncate comparison to Microseconds to match precision
	if extracted.Before(start.Add(-100 * time.Millisecond)) {
		t.Error("Extracted time is too early")
	}
	if extracted.After(end.Add(100 * time.Millisecond)) {
		t.Error("Extracted time is too late")
	}
}

func TestISOTime(t *testing.T) {
	// 2025-12-12 01:35:00.123456 UTC
	targetStr := "2025-12-12T01:35:00.123456Z"
	targetTime, _ := time.Parse(time.RFC3339Nano, targetStr)

	// Backfill
	uuid, _ := FromTime(targetTime, 55)

	// Extract using struct method
	isoStr := uuid.ISOTime()

	if isoStr != targetStr {
		t.Errorf("ISO Mismatch. Expected %s, got %s", targetStr, isoStr)
	}
}

func TestGeneratorStruct(t *testing.T) {
	shard := uint32(777)
	gen, err := NewGenerator(shard)
	if err != nil {
		t.Fatalf("Failed to init generator: %v", err)
	}

	uuid, _ := gen.NewID()

	if uuid.ShardID() != shard {
		t.Errorf("Generator struct used wrong shard. Expected %d, got %d", shard, uuid.ShardID())
	}
}

func TestRFCCompliance(t *testing.T) {
	uuid, _ := Generate(1)
	str := uuid.String()

	// xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
	// Version at index 14
	if str[14] != '8' {
		t.Errorf("Version must be 8, got %c", str[14])
	}

	// Variant at index 19 (8, 9, a, b)
	variant := str[19]
	if !strings.ContainsRune("89ab", rune(variant)) {
		t.Errorf("Variant must be 2 (8,9,a,b), got %c", variant)
	}
}

func TestParsing(t *testing.T) {
	original, _ := Generate(123)
	str := original.String()

	parsed, err := Parse(str)
	if err != nil {
		t.Fatalf("Failed to parse valid UUID: %v", err)
	}

	if original != parsed {
		t.Errorf("Roundtrip failed. Original %v != Parsed %v", original, parsed)
	}

	if parsed.ShardID() != 123 {
		t.Errorf("Parsed UUID lost shard ID data")
	}
}

func TestSorting(t *testing.T) {
	// Create old ID
	oldTime := time.Now().Add(-10 * time.Second)
	uidOld, _ := FromTime(oldTime, 1)

	// Create new ID
	uidNew, _ := Generate(1)

	// Compare strings (Lexical Sort)
	if uidOld.String() >= uidNew.String() {
		t.Error("Chronological sorting failed. Old ID should be lexically smaller than New ID.")
	}

	// Compare raw High bits (Native numeric sort for High part)
	if uidOld.High >= uidNew.High {
		t.Error("High-bit numeric sorting failed.")
	}
}

func TestErrors(t *testing.T) {
	// Overflow shard
	_, err := Generate(4294967295) // Max valid
	if err != nil {
		t.Error("Max valid shard should not error")
	}

	// Check Time Overflow (Future)
	// MaxTime = 18014398509481983 (Year ~2541)
	// Let's manually create a time far in future
	future := time.UnixMicro(int64(18014398509481983 + 1000))
	_, err = FromTime(future, 1)
	if err == nil {
		t.Error("Should have errored on time overflow")
	}

	// Parse Invalid Hex
	_, err = Parse("zzzzzzzz-xxxx-xxxx-xxxx-xxxxxxxxxxxx")
	if err == nil {
		t.Error("Should have errored on invalid hex")
	}

	// Parse Invalid Length
	_, err = Parse("123")
	if err == nil {
		t.Error("Should have errored on invalid length")
	}
}

func TestComparison(t *testing.T) {
	// 1. Different Times
	t1 := time.Date(2023, 1, 1, 10, 0, 0, 0, time.UTC)
	t2 := t1.Add(1 * time.Second)

	idOld, _ := FromTime(t1, 1)
	idNew, _ := FromTime(t2, 1)

	if !idOld.Before(idNew) {
		t.Error("Before() failed: Old ID should be before New ID")
	}
	if !idNew.After(idOld) {
		t.Error("After() failed: New ID should be after Old ID")
	}
	if idOld.Compare(idNew) != -1 {
		t.Errorf("Compare() failed: Expected -1, got %d", idOld.Compare(idNew))
	}
	if idNew.Compare(idOld) != 1 {
		t.Errorf("Compare() failed: Expected 1, got %d", idNew.Compare(idOld))
	}

	// 2. Equality
	idCopy := idOld
	if !idOld.Equals(idCopy) {
		t.Error("Equals() failed: ID should equal its copy")
	}
	if idOld.Compare(idCopy) != 0 {
		t.Errorf("Compare() failed: Expected 0 for equal IDs, got %d", idOld.Compare(idCopy))
	}

	// 3. Same Time, Different Random/Shard (Manual Construction to avoid randomness)
	// If High bits (Time) are equal, it should compare Low bits
	u1 := MicroShardUUID{High: 100, Low: 500}
	u2 := MicroShardUUID{High: 100, Low: 501}

	if !u1.Before(u2) {
		t.Error("Comparison failed on Low bits (same High bits)")
	}
}

func TestSliceSorting(t *testing.T) {
	// Create 3 IDs with distinct timestamps
	t1 := time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC)
	t2 := time.Date(2021, 1, 1, 0, 0, 0, 0, time.UTC)
	t3 := time.Date(2022, 1, 1, 0, 0, 0, 0, time.UTC)

	id1, _ := FromTime(t1, 1)
	id2, _ := FromTime(t2, 1)
	id3, _ := FromTime(t3, 1)

	// Create an unsorted slice: [3, 1, 2]
	list := ByTime{id3, id1, id2}

	// Sort using standard library
	sort.Sort(list)

	// Verify order: [1, 2, 3]
	if !list[0].Equals(id1) {
		t.Errorf("Sort failed index 0. Expected %s, got %s", id1, list[0])
	}
	if !list[1].Equals(id2) {
		t.Errorf("Sort failed index 1. Expected %s, got %s", id2, list[1])
	}
	if !list[2].Equals(id3) {
		t.Errorf("Sort failed index 2. Expected %s, got %s", id3, list[2])
	}
}

// Update existing TestSorting to focus on String Lexical sorting
func TestLexicalSorting(t *testing.T) {
	// Create old ID
	oldTime := time.Now().Add(-10 * time.Second)
	uidOld, _ := FromTime(oldTime, 1)

	// Create new ID
	uidNew, _ := Generate(1)

	// Compare strings (Lexical Sort)
	// UUIDv8 is designed so string sorting matches binary sorting
	if uidOld.String() >= uidNew.String() {
		t.Error("Chronological string sorting failed. Old ID string should be lexically smaller.")
	}
}
