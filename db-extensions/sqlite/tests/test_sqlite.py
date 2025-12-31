import unittest
import sqlite3
import os
import sys
import platform

# ------------------------------------------------------------------
# Configuration & Setup
# ------------------------------------------------------------------

EXT_NAME = "microshard_uuid"

# Determine extension suffix
system = platform.system()
if system == "Windows":
    SUFFIX = ".dll"
elif system == "Darwin":
    SUFFIX = ".dylib"
else:
    SUFFIX = ".so"

# Construct absolute path (SQLite often requires absolute paths for security)
EXT_FILENAME = f"{EXT_NAME}{SUFFIX}"
EXT_PATH = os.path.abspath(EXT_FILENAME)

print(f"ℹ️  Target Extension Path: {EXT_PATH}")


class TestMicroShardSQLite(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        """Check if compiled extension exists before running tests"""
        if not os.path.exists(EXT_PATH):
            print(f"\n❌ Extension binary not found at: {EXT_PATH}")
            print("\nPLEASE COMPILE FIRST:")
            if system == "Windows":
                print(
                    f"  gcc -O2 -shared -I../../implementations/c_cpp sqlite_extension.c -o {EXT_NAME}.dll"
                )
            elif system == "Darwin":
                print(
                    f"  gcc -O2 -fPIC -dynamiclib -I../../implementations/c_cpp sqlite_extension.c -o {EXT_NAME}.dylib"
                )
            else:
                print(
                    f"  gcc -O2 -fPIC -shared -I../../implementations/c_cpp sqlite_extension.c -o {EXT_NAME}.so"
                )
            sys.exit(1)

    def setUp(self):
        """Create a fresh in-memory database for every test"""
        self.conn = sqlite3.connect(":memory:")
        self.conn.enable_load_extension(True)
        try:
            self.conn.load_extension(EXT_PATH)
        except sqlite3.OperationalError as e:
            self.fail(f"Failed to load extension: {e}")

    def tearDown(self):
        self.conn.close()

    # ==========================================
    # 1. Core Logic & Math
    # ==========================================

    def test_basic_generation(self):
        """Test basic generation and shard extraction"""
        shard_id = 123
        cur = self.conn.cursor()

        # 1. Generate BLOB
        cur.execute("SELECT microshard_uuid_generate(?)", (shard_id,))
        blob = cur.fetchone()[0]
        self.assertEqual(len(blob), 16)

        # 2. Extract Shard ID
        cur.execute("SELECT microshard_uuid_get_shard_id(?)", (blob,))
        extracted_shard = cur.fetchone()[0]
        self.assertEqual(extracted_shard, shard_id)

    def test_boundary_shards(self):
        """Test Min (0) and Max (uint32) Shard IDs"""
        cur = self.conn.cursor()

        # Min
        cur.execute("SELECT microshard_uuid_get_shard_id(microshard_uuid_generate(0))")
        self.assertEqual(cur.fetchone()[0], 0)

        # Max (2^32 - 1)
        max_shard = 4294967295
        cur.execute(
            "SELECT microshard_uuid_get_shard_id(microshard_uuid_generate(?))",
            (max_shard,),
        )
        self.assertEqual(cur.fetchone()[0], max_shard)

    def test_time_extraction_micros(self):
        """Test precise microsecond roundtrip via integer API"""
        # 2023-01-01 00:00:00 UTC = 1672531200000000 micros
        target_micros = 1672531200000000
        shard_id = 99

        cur = self.conn.cursor()
        cur.execute(
            "SELECT microshard_uuid_get_time(microshard_uuid_from_micros(?, ?))",
            (target_micros, shard_id),
        )

        extracted = cur.fetchone()[0]
        self.assertEqual(extracted, target_micros)

    def test_iso_roundtrip(self):
        """Test ISO string -> UUID -> ISO string consistency"""
        # Note: Input allows arbitrary fractional precision, output standardizes to 6 digits
        input_iso = "2025-12-13T14:00:00.123456Z"
        shard_id = 55
        cur = self.conn.cursor()

        cur.execute(
            "SELECT microshard_uuid_get_iso(microshard_uuid_from_iso(?, ?))",
            (input_iso, shard_id),
        )
        output_iso = cur.fetchone()[0]

        self.assertEqual(output_iso, input_iso)

    # ==========================================
    # 2. Validation & Edge Cases
    # ==========================================

    def test_leap_year_complexity(self):
        """Test detailed leap year logic (Century rules)"""
        cur = self.conn.cursor()

        cases = [
            ("2024-02-29T12:00:00Z", 1),  # Valid Leap
            ("2023-02-29T12:00:00Z", 0),  # Invalid (Normal year)
            ("2000-02-29T12:00:00Z", 1),  # Valid (Divisible by 400)
            ("2100-02-29T12:00:00Z", 0),  # Invalid (Divisible by 100 but not 400)
        ]

        for date_str, expected in cases:
            # Note: validate_iso returns 1 (success) or 0 (failure)
            cur.execute("SELECT microshard_uuid_validate_iso(?)", (date_str,))
            result = cur.fetchone()[0]
            self.assertEqual(result, expected, f"Failed logic for {date_str}")

    def test_strict_errors(self):
        """Ensure strict SQL errors are thrown"""
        cur = self.conn.cursor()

        # 1. Invalid Format
        with self.assertRaises(sqlite3.OperationalError) as cm:
            cur.execute("SELECT microshard_uuid_from_iso('bad-date', 1)")
        self.assertIn("Invalid string length", str(cm.exception))

        # 2. Logical Date Error (Feb 30)
        with self.assertRaises(sqlite3.OperationalError) as cm:
            cur.execute("SELECT microshard_uuid_from_iso('2023-02-30T00:00:00Z', 1)")
        self.assertIn("Date/Time values out of logical range", str(cm.exception))

        # 3. Shard Overflow
        with self.assertRaises(sqlite3.OperationalError) as cm:
            cur.execute("SELECT microshard_uuid_generate(4294967296)")
        self.assertIn("Shard ID out of range", str(cm.exception))

    def test_null_handling(self):
        """Functions should handle NULL inputs gracefully"""
        cur = self.conn.cursor()

        # validate_iso(NULL) -> NULL
        cur.execute("SELECT microshard_uuid_validate_iso(NULL)")
        self.assertIsNone(cur.fetchone()[0])

        # from_string(NULL) -> NULL (Empty Result)
        cur.execute("SELECT microshard_uuid_from_string(NULL)")
        self.assertIsNone(cur.fetchone()[0])

        # from_iso(NULL) -> NULL (Graceful return)
        cur.execute("SELECT microshard_uuid_from_iso(NULL, 1)")
        self.assertIsNone(cur.fetchone()[0])

    # ==========================================
    # 3. Formats & Conversion
    # ==========================================

    def test_text_generation(self):
        """Test text generation format"""
        shard_id = 999
        cur = self.conn.cursor()

        cur.execute("SELECT microshard_uuid_generate_text(?)", (shard_id,))
        uuid_str = cur.fetchone()[0]

        self.assertIsInstance(uuid_str, str)
        self.assertEqual(len(uuid_str), 36)
        # 8-4-4-4-12 format. Version at index 14 ('8'), Variant at 19 ('8','9','a','b')
        self.assertEqual(uuid_str[14], "8", "Must be Version 8")
