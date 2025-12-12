import unittest
import time
from datetime import datetime, timezone, timedelta
from uuid import UUID

# Import everything from the package
from microshard_uuid import (
    generate,
    from_timestamp,
    get_shard_id,
    get_timestamp,
    get_iso_timestamp,
    Generator,
)


class TestMicroShardIntegrity(unittest.TestCase):
    """
    Tests ensuring data entered into the UUID can be retrieved accurately.
    """

    def test_shard_id_integrity(self):
        """
        Verify that Shard IDs are encoded and decoded correctly.
        Tests boundaries: 0, 1, arbitrary, and Max 32-bit.
        """
        test_cases = [0, 1, 500, 1024, 999999, 4294967295]
        for shard in test_cases:
            with self.subTest(shard=shard):
                uid = generate(shard)
                extracted = get_shard_id(uid)
                self.assertEqual(
                    extracted, shard, f"Failed integrity for shard {shard}"
                )

    def test_randomness_uniqueness(self):
        """
        Verify that two IDs generated for the same shard at the same time
        (simulated by tight loop) are unique due to random bits.
        """
        uid1 = generate(1)
        uid2 = generate(1)
        self.assertNotEqual(uid1, uid2)


class TestMicroShardTime(unittest.TestCase):
    """
    Tests focused on Timestamp accuracy, formats, and ISO 8601 compliance.
    """

    def setUp(self):
        # Allow 200ms delta for execution time jitter
        self.delta = timedelta(milliseconds=200)

    def test_current_time_accuracy(self):
        """
        Verify generate() captures the current system clock correctly.
        """
        start_dt = datetime.now(timezone.utc)
        uid = generate(shard_id=1)
        end_dt = datetime.now(timezone.utc)

        extracted_dt = get_timestamp(uid)

        # Check range: start - delta <= extracted <= end + delta
        self.assertGreaterEqual(extracted_dt, start_dt - self.delta)
        self.assertLessEqual(extracted_dt, end_dt + self.delta)

    def test_from_timestamp_datetime(self):
        """Test backfilling with an explicit datetime object."""
        # Date: 2023-01-01 12:00:00 UTC
        target_dt = datetime(2023, 1, 1, 12, 0, 0, tzinfo=timezone.utc)

        uid = from_timestamp(target_dt, shard_id=50)
        extracted_dt = get_timestamp(uid)

        self.assertEqual(extracted_dt, target_dt)

    def test_from_timestamp_int_micros(self):
        """Test backfilling with integer microseconds."""
        # 1672574400000000 is 2023-01-01 12:00:00 UTC
        micros = 1672574400000000

        uid = from_timestamp(micros, shard_id=99)
        extracted_dt = get_timestamp(uid)

        # Convert extracted datetime back to micros
        extracted_micros = int(extracted_dt.timestamp() * 1_000_000)
        self.assertEqual(extracted_micros, micros)

    def test_from_timestamp_float_seconds(self):
        """Test backfilling with float seconds (like time.time())."""
        # 1672574400.5 is 2023-01-01 12:00:00.500000 UTC
        seconds = 1672574400.5

        uid = from_timestamp(seconds, shard_id=99)
        extracted_dt = get_timestamp(uid)

        self.assertEqual(extracted_dt.timestamp(), seconds)

    def test_iso_timestamp_roundtrip(self):
        """
        Verify that an ISO string input results in the exact same ISO string output.
        Crucial for string-based systems.
        """
        input_iso = "2025-12-12T01:35:00.123456Z"

        uid = from_timestamp(input_iso, shard_id=1)
        output_iso = get_iso_timestamp(uid)

        self.assertEqual(output_iso, input_iso)

    def test_iso_timestamp_zero_padding(self):
        """
        Verify that microsecond precision is preserved even if it is .000000.
        Standard python libraries often strip trailing zeros; we must not.
        """
        input_iso = "2025-01-01T12:00:00.000000Z"

        uid = from_timestamp(input_iso, shard_id=1)
        output_iso = get_iso_timestamp(uid)

        self.assertEqual(output_iso, input_iso)


class TestGeneratorClass(unittest.TestCase):
    """
    Tests for the Stateful Generator class.
    """

    def setUp(self):
        self.shard_id = 777
        self.gen = Generator(shard_id=self.shard_id)

    def test_new_id(self):
        """Ensure new_id() uses the configured shard ID."""
        uid = self.gen.new_id()
        self.assertEqual(get_shard_id(uid), self.shard_id)

    def test_from_timestamp(self):
        """Ensure class from_timestamp() uses configured shard ID."""
        dt = datetime(2025, 1, 1, tzinfo=timezone.utc)
        uid = self.gen.from_timestamp(dt)

        self.assertEqual(get_shard_id(uid), self.shard_id)
        self.assertEqual(get_timestamp(uid), dt)

    def test_init_validation(self):
        """Generator should fail immediately if initialized with bad ID."""
        with self.assertRaises(ValueError):
            Generator(4294967296)  # Max 32-bit + 1


class TestRFCCompliance(unittest.TestCase):
    """
    Tests ensuring compliance with IETF RFC 9562 for UUIDv8.
    """

    def test_uuid_structure(self):
        """
        UUID Format: xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
        M (Version) must be 8.
        N (Variant) must be 8, 9, a, or b (binary 10xx).
        """
        uid = generate(1)
        hex_str = str(uid)

        # 1. Check Version (Index 14)
        version_char = hex_str[14]
        self.assertEqual(version_char, "8", f"Version must be 8, got {version_char}")

        # 2. Check Variant (Index 19)
        variant_char = hex_str[19]
        self.assertIn(
            variant_char, ["8", "9", "a", "b"], f"Variant must be 2, got {variant_char}"
        )

    def test_sorting_monotonicity(self):
        """
        Ensure that UUIDs generated sequentially sort correctly.
        UUIDv8 puts time in the highest bits, so string sort == time sort.
        """
        uid1 = generate(1)
        time.sleep(0.001)  # Sleep 1ms to ensure clock tick
        uid2 = generate(1)

        # Standard comparison
        self.assertLess(uid1, uid2)

        # Verify timestamp comparison matches
        self.assertLess(get_timestamp(uid1), get_timestamp(uid2))


class TestValidation(unittest.TestCase):
    """
    Tests for Error Handling and Boundary Conditions.
    """

    def test_invalid_shard_ids(self):
        # Negative
        with self.assertRaisesRegex(ValueError, "Shard ID"):
            generate(-1)

        # Overflow
        with self.assertRaisesRegex(ValueError, "Shard ID"):
            generate(4294967296)

    def test_time_overflow(self):
        """
        Test timestamp exceeding 54 bits (Approx Year 2541).
        """
        max_micros = (1 << 54) - 1
        future_micros = max_micros + 1

        with self.assertRaisesRegex(ValueError, "Time overflow"):
            from_timestamp(future_micros, 1)

    def test_invalid_iso_string(self):
        """Test garbage string input."""
        with self.assertRaisesRegex(ValueError, "Invalid ISO"):
            from_timestamp("not-a-date", 1)

    def test_invalid_input_types(self):
        """Test unsupported types."""
        with self.assertRaises(TypeError):
            from_timestamp([], 1)  # List is invalid


if __name__ == "__main__":
    unittest.main()
