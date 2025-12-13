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

// MicroShardUUID represents a 128-bit UUIDv8.
// High contains the first 64 bits (Time, Version, Shard High).
// Low contains the last 64 bits (Variant, Shard Low, Random).
type MicroShardUUID struct {
	High uint64
	Low  uint64
}

// ==========================================
// 1. Generation
// ==========================================

// Generate creates a new MicroShardUUID using the current system time.
func Generate(shardID uint32) (MicroShardUUID, error) {
	if shardID > MaxShardID {
		return MicroShardUUID{}, fmt.Errorf("shard ID must be between 0 and %d", MaxShardID)
	}

	// 1. Time (Microseconds)
	now := uint64(time.Now().UnixMicro())

	// 2. Build
	return buildUUID(now, shardID)
}

// FromTime creates a MicroShardUUID for a specific timestamp.
// Useful for backfilling.
func FromTime(ts time.Time, shardID uint32) (MicroShardUUID, error) {
	if shardID > MaxShardID {
		return MicroShardUUID{}, fmt.Errorf("shard ID must be between 0 and %d", MaxShardID)
	}

	micros := uint64(ts.UnixMicro())
	return buildUUID(micros, shardID)
}

// ==========================================
// 2. Parsing & String Conversion
// ==========================================

// Parse converts a UUID string (standard 8-4-4-4-12 format) into a MicroShardUUID struct.
// It validates format, length, Version (8), and Variant (2).
func Parse(uuidStr string) (MicroShardUUID, error) {
	clean := strings.ReplaceAll(uuidStr, "-", "")
	if len(clean) != 32 {
		return MicroShardUUID{}, errors.New("invalid UUID length")
	}

	bytes, err := hex.DecodeString(clean)
	if err != nil {
		return MicroShardUUID{}, errors.New("invalid UUID hex")
	}

	high := binary.BigEndian.Uint64(bytes[0:8])
	low := binary.BigEndian.Uint64(bytes[8:16])

	// Validate Version (Bits 48-51 of High) => (High >> 12) & 0xF
	// Wait, bits are: [TimeHigh 48][Ver 4]...
	// High is 64 bits.
	// Layout: 0-47 (TimeHigh), 48-51 (Ver), 52-57 (TimeLow), 58-63 (ShardHigh) -- NO, Big Endian reads left to right.
	//
	// Let's look at the bit packing in buildUUID:
	// high64 := (timeHigh << 16) | (Version << 12) | ...
	//
	// Position 12 (from bottom) means bits 12-15.
	// So (High >> 12) & 0xF is correct.
	ver := (high >> 12) & 0xF
	if ver != Version {
		return MicroShardUUID{}, fmt.Errorf("invalid version: %d (expected %d)", ver, Version)
	}

	// Validate Variant (Top 2 bits of Low)
	// low64 := (Variant << 62) | ...
	// So (Low >> 62) & 0x3
	varnt := (low >> 62) & 0x3
	if varnt != Variant {
		return MicroShardUUID{}, fmt.Errorf("invalid variant: %d (expected %d)", varnt, Variant)
	}

	return MicroShardUUID{High: high, Low: low}, nil
}

// String returns the standard canonical UUID string representation.
// Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
func (u MicroShardUUID) String() string {
	return fmt.Sprintf("%08x-%04x-%04x-%04x-%012x",
		u.High>>32,           // First 8 bytes (32 bits)
		(u.High>>16)&0xFFFF,  // Next 2 bytes (16 bits)
		u.High&0xFFFF,        // Next 2 bytes (16 bits)
		u.Low>>48,            // Next 2 bytes (16 bits)
		u.Low&0xFFFFFFFFFFFF, // Last 6 bytes (48 bits)
	)
}

// Bytes returns the raw 16-byte slice (Big Endian).
func (u MicroShardUUID) Bytes() []byte {
	buf := make([]byte, 16)
	binary.BigEndian.PutUint64(buf[0:8], u.High)
	binary.BigEndian.PutUint64(buf[8:16], u.Low)
	return buf
}

// ==========================================
// 3. Extraction (Methods on Struct)
// ==========================================

// ShardID extracts the 32-bit Shard ID.
func (u MicroShardUUID) ShardID() uint32 {
	// Logic:
	// High[5:0] is Shard High (6 bits)
	// Low[63:36] is Shard Low (26 bits)

	shardHigh := u.High & 0x3F
	shardLow := (u.Low >> 36) & 0x3FFFFFF

	return uint32((shardHigh << 26) | shardLow)
}

// Time extracts the timestamp as a standard Go time.Time object (UTC).
func (u MicroShardUUID) Time() time.Time {
	// Logic:
	// High[63:16] is Time High (48 bits)
	// High[11:6]  is Time Low (6 bits)

	timeHigh := (u.High >> 16) & 0xFFFFFFFFFFFF
	timeLow := (u.High >> 6) & 0x3F

	micros := int64((timeHigh << 6) | timeLow)

	return time.UnixMicro(micros).UTC()
}

// ISOTime extracts the timestamp as an ISO 8601 string.
func (u MicroShardUUID) ISOTime() string {
	return u.Time().Format("2006-01-02T15:04:05.000000Z")
}

// ==========================================
// 4. Stateful Generator
// ==========================================

// Generator holds the configuration for a specific Shard ID.
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
func (g *Generator) NewID() (MicroShardUUID, error) {
	now := uint64(time.Now().UnixMicro())
	return buildUUID(now, g.shardID)
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
	fullBytes := append([]byte{0, 0, 0}, b...)
	val := binary.BigEndian.Uint64(fullBytes)

	// Mask to 36 bits
	return val & MaxRandom, nil
}

func buildUUID(micros uint64, shardID uint32) (MicroShardUUID, error) {
	if micros > MaxTime {
		return MicroShardUUID{}, errors.New("time overflow (Year > 2541)")
	}

	rnd, err := getRandom36()
	if err != nil {
		return MicroShardUUID{}, err
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

	return MicroShardUUID{High: high64, Low: low64}, nil
}

// ==========================================
// 5. Comparison
// ==========================================

// Compare returns an integer comparing two UUIDs lexicographically.
// The result will be 0 if u == other, -1 if u < other, and +1 if u > other.
// Since the timestamp is at the MSB, this effectively compares creation time.
func (u MicroShardUUID) Compare(other MicroShardUUID) int {
	if u.High < other.High {
		return -1
	}
	if u.High > other.High {
		return 1
	}
	if u.Low < other.Low {
		return -1
	}
	if u.Low > other.Low {
		return 1
	}
	return 0
}

// Equals checks if two UUIDs are identical.
// Note: You can also use standard `==` operator in Go for structs.
func (u MicroShardUUID) Equals(other MicroShardUUID) bool {
	return u.High == other.High && u.Low == other.Low
}

// Before returns true if u is strictly smaller (older) than other.
func (u MicroShardUUID) Before(other MicroShardUUID) bool {
	return u.Compare(other) < 0
}

// After returns true if u is strictly greater (newer) than other.
func (u MicroShardUUID) After(other MicroShardUUID) bool {
	return u.Compare(other) > 0
}

// ByTime implements sort.Interface for []MicroShardUUID.
// It sorts UUIDs chronologically.
type ByTime []MicroShardUUID

func (a ByTime) Len() int           { return len(a) }
func (a ByTime) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a ByTime) Less(i, j int) bool { return a[i].Compare(a[j]) < 0 }
