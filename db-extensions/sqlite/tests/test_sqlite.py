import unittest
import sqlite3
import os
import sys
import time

# ------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------

EXT_NAME = "microshard_uuid"
EXT_PATH = f"./{EXT_NAME}"

# Detect OS to select correct extension suffix
if sys.platform == "win32":
    EXT_PATH += ".dll"
elif sys.platform == "darwin":
    EXT_PATH += ".dylib"  # macOS shared library
else:
    EXT_PATH += ".so"  # Linux / Default

print(f"ℹ️  Target Extension: {EXT_PATH}")


class TestMicroShardSQLite(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        """Check if compiled extension exists before running tests"""
        if not os.path.exists(EXT_PATH):
            print(f"\n❌ Extension binary not found at: {EXT_PATH}")
            print("\nPLEASE COMPILE FIRST:")

            if sys.platform == "win32":
                print(f"  gcc -g -shared microshard_sqlite.c -o {EXT_NAME}.dll")
            elif sys.platform == "darwin":
                print(
                    f"  gcc -g -fPIC -dynamiclib microshard_sqlite.c -o {EXT_NAME}.dylib"
                )
            else:
                print(f"  gcc -g -fPIC -shared microshard_sqlite.c -o {EXT_NAME}.so")

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
            cur.execute("SELECT microshard_uuid_validate_iso(?)", (date_str,))
            result = cur.fetchone()[0]
            self.assertEqual(result, expected, f"Failed logic for {date_str}")

    def test_strict_errors(self):
        """Ensure strict SQL errors are thrown"""
        cur = self.conn.cursor()

        # 1. Invalid Format
        with self.assertRaises(sqlite3.OperationalError) as cm:
            cur.execute("SELECT microshard_uuid_from_iso('bad-date', 1)")
        self.assertIn("Invalid ISO", str(cm.exception))

        # 2. Logical Date Error (Feb 30)
        with self.assertRaises(sqlite3.OperationalError) as cm:
            cur.execute("SELECT microshard_uuid_from_iso('2023-02-30T00:00:00Z', 1)")
        self.assertIn("Invalid ISO", str(cm.exception))

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

        # from_iso(NULL) -> Error (Because we enforce Not Null in logic usually, or return NULL)
        # In our C implementation we check: if (value_type == NULL) -> Error.
        with self.assertRaises(sqlite3.OperationalError):
            cur.execute("SELECT microshard_uuid_from_iso(NULL, 1)")

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
        self.assertEqual(uuid_str[14], "8", "Must be Version 8")
        self.assertTrue(uuid_str[19] in ["8", "9", "a", "b"], "Must be Variant 2")

    def test_blob_string_roundtrip(self):
        """Test conversion between BLOB and String helpers"""
        cur = self.conn.cursor()
        uuid_str = "018eb13c-d81b-8190-8000-64299b9514e2"

        # String -> Blob -> String
        cur.execute(
            "SELECT microshard_uuid_to_string(microshard_uuid_from_string(?))",
            (uuid_str,),
        )
        self.assertEqual(cur.fetchone()[0], uuid_str)

        # Blob -> String -> Blob (Compare Bytes)
        cur.execute(
            """
            SELECT microshard_uuid_from_string(
                microshard_uuid_to_string(
                    microshard_uuid_from_string(?)
                )
            )
        """,
            (uuid_str,),
        )

        # To verify blob equality, we can hex it in python
        final_blob = cur.fetchone()[0]
        self.assertEqual(final_blob.hex(), uuid_str.replace("-", ""))

    # ==========================================
    # 4. Integration Patterns (Real Usage)
    # ==========================================

    def test_virtual_columns(self):
        """Test using Extraction in Generated Columns (Zero-Lookup Architecture)"""
        cur = self.conn.cursor()

        # SQLite supports generated columns since 3.31
        # Create a table where shard_id is automatically extracted from the UUID blob
        try:
            cur.execute(
                """
                CREATE TABLE users (
                    id BLOB PRIMARY KEY,
                    username TEXT,
                    shard_id INTEGER GENERATED ALWAYS AS (microshard_uuid_get_shard_id(id)) VIRTUAL
                )
            """
            )
        except sqlite3.OperationalError:
            print("⚠️ Skipped Generated Column test (SQLite version too old?)")
            return

        # Insert 3 users into different shards
        cur.execute(
            "INSERT INTO users (id, username) VALUES (microshard_uuid_generate(10), 'Alice')"
        )
        cur.execute(
            "INSERT INTO users (id, username) VALUES (microshard_uuid_generate(20), 'Bob')"
        )
        cur.execute(
            "INSERT INTO users (id, username) VALUES (microshard_uuid_generate(10), 'Charlie')"
        )

        # Query by computed shard_id (This is the Zero-Lookup Magic)
        cur.execute("SELECT username FROM users WHERE shard_id = 10 ORDER BY username")
        rows = cur.fetchall()

        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0][0], "Alice")
        self.assertEqual(rows[1][0], "Charlie")

    def test_collision_resistance(self):
        """Generate 10,000 IDs and ensure no collisions"""
        cur = self.conn.cursor()
        count = 10000

        # Generate bulk IDs in Python loop
        ids = set()
        for _ in range(count):
            cur.execute("SELECT microshard_uuid_generate(1)")
            blob = cur.fetchone()[0]
            ids.add(blob)

        self.assertEqual(len(ids), count, "Collision detected in 10k batch!")

    def test_sorting_order(self):
        """Test K-Sortable property (Chronological Sorting)"""
        cur = self.conn.cursor()
        cur.execute("CREATE TABLE log (id BLOB PRIMARY KEY, event TEXT)")

        # Insert Past, Present, Future
        times = [
            ("2020-01-01T00:00:00Z", "Past"),
            ("2025-01-01T00:00:00Z", "Present"),
            ("2030-01-01T00:00:00Z", "Future"),
        ]

        for iso, name in times:
            cur.execute(
                "INSERT INTO log VALUES (microshard_uuid_from_iso(?, 1), ?)",
                (iso, name),
            )

        # Select Order By ID
        cur.execute("SELECT event FROM log ORDER BY id ASC")
        rows = cur.fetchall()

        self.assertEqual(rows[0][0], "Past")
        self.assertEqual(rows[1][0], "Present")
        self.assertEqual(rows[2][0], "Future")


if __name__ == "__main__":
    unittest.main()
