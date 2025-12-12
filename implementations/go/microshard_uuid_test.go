package microsharduuid

import (
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

		extracted, err := GetShardID(uuid)
		if err != nil {
			t.Fatalf("Extraction failed: %v", err)
		}

		if extracted != shard {
			t.Errorf("Shard mismatch. Expected %d, got %d", shard, extracted)
		}
	}
}

func TestTimeAccuracy(t *testing.T) {
	start := time.Now()
	uuid, _ := Generate(100)
	end := time.Now()

	extracted, _ := GetTime(uuid)

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

	// Extract
	isoStr, err := GetISOTime(uuid)
	if err != nil {
		t.Fatalf("Failed to get ISO time: %v", err)
	}

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
	extracted, _ := GetShardID(uuid)

	if extracted != shard {
		t.Errorf("Generator struct used wrong shard. Expected %d, got %d", shard, extracted)
	}
}

func TestRFCCompliance(t *testing.T) {
	uuid, _ := Generate(1)

	// xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
	// Version at index 14
	if uuid[14] != '8' {
		t.Errorf("Version must be 8, got %c", uuid[14])
	}

	// Variant at index 19 (8, 9, a, b)
	variant := uuid[19]
	if !strings.ContainsRune("89ab", rune(variant)) {
		t.Errorf("Variant must be 2 (8,9,a,b), got %c", variant)
	}
}

func TestSorting(t *testing.T) {
	// Create old ID
	oldTime := time.Now().Add(-10 * time.Second)
	uidOld, _ := FromTime(oldTime, 1)

	// Create new ID
	uidNew, _ := Generate(1)

	if uidOld >= uidNew {
		t.Error("Chronological sorting failed. Old ID should be lexically smaller than New ID.")
	}
}

func TestErrors(t *testing.T) {
	// Overflow shard
	_, err := Generate(4294967296 - 1) // Max valid
	if err != nil {
		t.Error("Max valid shard should not error")
	}

	// Note: In Go, uint32 cannot inherently overflow 4294967295 via function args type-checking,
	// but logically we check boundaries.

	// Check Time Overflow (Future)
	// MaxTime = 18014398509481983
	// Let's manually create a time far in future
	future := time.UnixMicro(int64(18014398509481983 + 1000))
	_, err = FromTime(future, 1)
	if err == nil {
		t.Error("Should have errored on time overflow")
	}
}
