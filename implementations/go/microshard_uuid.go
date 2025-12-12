package microsharduuid

import (
	"crypto/rand"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"strings"
	"time"
)

// Constants defining the bit layout
const (
	MaxShardID uint32 = 4294967295        // 2^32 - 1
	MaxTime    uint64 = 18014398509481983 // 2^54 - 1
	MaxRandom  uint64 = 68719476735       // 2^36 - 1
	Version    uint64 = 8
	Variant    uint64 = 2
)

// ==========================================
// Stateless Functions
// ==========================================

// Generate creates a new UUIDv8 using the current system time for a specific shard.
func Generate(shardID uint32) (string, error) {
	if shardID > MaxShardID {
		return "", fmt.Errorf("shard ID must be between 0 and %d", MaxShardID)
	}

	// 1. Time (Microseconds)
	now := uint64(time.Now().UnixMicro())

	// 2. Build
	return buildUUID(now, shardID)
}

// FromTime creates a UUIDv8 for a specific timestamp.
// Useful for backfilling.
func FromTime(ts time.Time, shardID uint32) (string, error) {
	if shardID > MaxShardID {
		return "", fmt.Errorf("shard ID must be between 0 and %d", MaxShardID)
	}

	micros := uint64(ts.UnixMicro())
	return buildUUID(micros, shardID)
}

// GetShardID extracts the 32-bit Shard ID from a UUID string.
func GetShardID(uuidStr string) (uint32, error) {
	high, low, err := parseUUID(uuidStr)
	if err != nil {
		return 0, err
	}

	// Logic:
	// High[5:0] is Shard High (6 bits)
	// Low[63:36] is Shard Low (26 bits)

	shardHigh := high & 0x3F
	shardLow := (low >> 36) & 0x3FFFFFF

	return uint32((shardHigh << 26) | shardLow), nil
}

// GetTime extracts the timestamp as a standard Go time.Time object.
func GetTime(uuidStr string) (time.Time, error) {
	high, _, err := parseUUID(uuidStr)
	if err != nil {
		return time.Time{}, err
	}

	// Logic:
	// High[63:16] is Time High (48 bits)
	// High[11:6]  is Time Low (6 bits)

	timeHigh := (high >> 16) & 0xFFFFFFFFFFFF
	timeLow := (high >> 6) & 0x3F

	micros := int64((timeHigh << 6) | timeLow)

	return time.UnixMicro(micros).UTC(), nil
}

// GetISOTime extracts the timestamp as an ISO 8601 string with microsecond precision.
// Example: "2025-12-12T10:00:00.123456Z"
func GetISOTime(uuidStr string) (string, error) {
	t, err := GetTime(uuidStr)
	if err != nil {
		return "", err
	}
	// Go format string for ISO 8601 with 6-digit microseconds
	return t.Format("2006-01-02T15:04:05.000000Z"), nil
}

// ==========================================
// Stateful Generator Struct
// ==========================================

// Generator holds the configuration for a specific Shard ID.
// Best used for Dependency Injection.
type Generator struct {
	shardID uint32
}

// NewGenerator creates a new Generator instance.
func NewGenerator(defaultShardID uint32) (*Generator, error) {
	if defaultShardID > MaxShardID {
		return nil, fmt.Errorf("shard ID must be between 0 and %d", MaxShardID)
	}
	return &Generator{shardID: defaultShardID}, nil
}

// NewID generates a UUID using the configured Shard ID.
func (g *Generator) NewID() (string, error) {
	now := uint64(time.Now().UnixMicro())
	return buildUUID(now, g.shardID)
}

// FromTime generates a UUID using the configured Shard ID and specific time.
func (g *Generator) FromTime(ts time.Time) (string, error) {
	micros := uint64(ts.UnixMicro())
	return buildUUID(micros, g.shardID)
}

// ==========================================
// Internal Helpers
// ==========================================

func getRandom36() (uint64, error) {
	// Read 5 bytes (40 bits)
	b := make([]byte, 5)
	_, err := rand.Read(b)
	if err != nil {
		return 0, err
	}

	// Convert to uint64
	// Pad with 3 leading zero bytes to make 8 bytes
	fullBytes := append([]byte{0, 0, 0}, b...)
	val := binary.BigEndian.Uint64(fullBytes)

	// Mask to 36 bits
	return val & MaxRandom, nil
}

func buildUUID(micros uint64, shardID uint32) (string, error) {
	if micros > MaxTime {
		return "", errors.New("time overflow (Year > 2541)")
	}

	rnd, err := getRandom36()
	if err != nil {
		return "", err
	}

	shardID64 := uint64(shardID)

	// --- High 64 Bits ---
	// Layout: [Time High 48] [Ver 4] [Time Low 6] [Shard High 6]

	timeHigh := (micros >> 6) & 0xFFFFFFFFFFFF
	timeLow := micros & 0x3F
	shardHigh := (shardID64 >> 26) & 0x3F

	high64 := (timeHigh << 16) | (Version << 12) | (timeLow << 6) | shardHigh

	// --- Low 64 Bits ---
	// Layout: [Var 2] [Shard Low 26] [Random 36]

	shardLow := shardID64 & 0x3FFFFFF
	low64 := (Variant << 62) | (shardLow << 36) | rnd

	// Format as UUID string: 8-4-4-4-12
	return fmt.Sprintf("%08x-%04x-%04x-%04x-%012x",
		high64>>32,           // First 8
		(high64>>16)&0xFFFF,  // Next 4
		high64&0xFFFF,        // Next 4
		low64>>48,            // Next 4
		low64&0xFFFFFFFFFFFF, // Last 12
	), nil
}

func parseUUID(uuidStr string) (uint64, uint64, error) {
	clean := strings.ReplaceAll(uuidStr, "-", "")
	if len(clean) != 32 {
		return 0, 0, errors.New("invalid UUID length")
	}

	bytes, err := hex.DecodeString(clean)
	if err != nil {
		return 0, 0, errors.New("invalid UUID hex")
	}

	high := binary.BigEndian.Uint64(bytes[0:8])
	low := binary.BigEndian.Uint64(bytes[8:16])

	return high, low, nil
}
