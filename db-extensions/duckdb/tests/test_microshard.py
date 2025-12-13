import unittest
import duckdb
import uuid
import datetime
import os
import random
from datetime import timezone, timedelta


class TestMicroShard(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        """
        Initializes an in-memory DuckDB connection and loads the MicroShard SQL macros.
        """
        # Determine path to SQL file relative to this test file
        base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        sql_file_path = os.path.join(base_dir, "microshard_uuid.sql")

        if not os.path.exists(sql_file_path):
            raise FileNotFoundError(f"Could not find {sql_file_path}")

        cls.con = duckdb.connect(":memory:")

        with open(sql_file_path, "r") as f:
            sql_script = f.read()
            cls.con.execute(sql_script)

    @classmethod
    def tearDownClass(cls):
        cls.con.close()

    # =========================================================================
    # 1. ARCHITECTURE & CONSTANTS
    # =========================================================================

    def test_macros_existence(self):
        """Ensure all defined macros are loaded into the catalog."""
        result = self.con.execute(
            "SELECT function_name FROM duckdb_functions() WHERE function_name LIKE 'microshard_%'"
        ).fetchall()
        macros = [r[0] for r in result]

        required = [
            "microshard_generate",
            "microshard_generate_int",
            "microshard_from_micros",
            "microshard_from_micros_int",
            "microshard_get_shard_id",
            "microshard_get_shard_id_int",
            "microshard_get_timestamp",
            "microshard_get_timestamp_int",
            "microshard_uuid_to_int",
            "microshard_int_to_uuid",
        ]

        for r in required:
            self.assertIn(r, macros, f"Macro {r} is missing from the database.")

    def test_architectural_layout(self):
        """
        Bitwise verification of the constructed integer.
        Layout: [Time:54] [Ver:4] [TimeLow:6] [ShardHigh:6] ... [Var:2] ...
        """
        micros = 0
        shard_id = 0

        # Generate raw integer from zero inputs
        val = self.con.execute(
            f"SELECT microshard_from_micros_int({micros}, {shard_id})"
        ).fetchone()[0]

        # Version Check (Bits 79-76 = 8)
        version = (val >> 76) & 0xF
        self.assertEqual(version, 8, "Incorrect UUID Version (Should be 8)")

        # Variant Check (Bits 63-62 = 2)
        variant = (val >> 62) & 0x3
        self.assertEqual(variant, 2, "Incorrect UUID Variant (Should be 2)")

    # =========================================================================
    # 2. BASIC FUNCTIONALITY & TYPES
    # =========================================================================

    def test_basic_generation(self):
        """Test basic UUID generation and format."""
        shard_id = 101
        uuid_val = self.con.execute(
            f"SELECT microshard_generate({shard_id})"
        ).fetchone()[0]

        try:
            uid_obj = uuid.UUID(str(uuid_val))
        except ValueError:
            self.fail("Generated value is not a valid UUID string")

        self.assertEqual(uid_obj.version, 8)
        self.assertEqual(uid_obj.variant, uuid.RFC_4122)

    def test_type_conversions(self):
        """Verify seamless conversion between UUID String and UHUGEINT format."""
        shard_id = 55
        original_uuid = self.con.execute(
            f"SELECT microshard_generate({shard_id})"
        ).fetchone()[0]

        # UUID -> INT
        as_int = self.con.execute(
            f"SELECT microshard_uuid_to_int('{original_uuid}')"
        ).fetchone()[0]
        self.assertIsInstance(as_int, int)

        # INT -> UUID
        back_to_uuid = self.con.execute(
            f"SELECT microshard_int_to_uuid({as_int}::UHUGEINT)"
        ).fetchone()[0]

        self.assertEqual(str(original_uuid), str(back_to_uuid))

    def test_upper_lower_case_handling(self):
        """Verify that hex parsing handles upper and lower case UUID strings equally."""
        uuid_str = "1822bf01-7b20-800e-ade6-8b17384be644"
        upper_str = uuid_str.upper()

        res_lower = self.con.execute(
            f"SELECT microshard_uuid_to_int('{uuid_str}')"
        ).fetchone()[0]
        res_upper = self.con.execute(
            f"SELECT microshard_uuid_to_int('{upper_str}')"
        ).fetchone()[0]

        self.assertEqual(res_lower, res_upper)

    # =========================================================================
    # 3. DATA INTEGRITY & LIMITS
    # =========================================================================

    def test_shard_id_uinteger_limits(self):
        """
        Ensures that Shard IDs > 2,147,483,647 (Max Signed Int) are handled correctly.
        """
        micros = 1698393600000000

        test_cases = [
            (0, "Min Shard"),
            (2147483647, "Max Signed Int32"),
            (4294967295, "Max Unsigned Int32"),
        ]

        for shard_val, label in test_cases:
            res = self.con.execute(
                f"SELECT microshard_get_shard_id(microshard_from_micros({micros}, {shard_val}))"
            ).fetchone()[0]
            self.assertEqual(res, shard_val, f"Failed at {label}")

    def test_time_boundaries_epoch(self):
        """Test Epoch 0 (1970-01-01)."""
        dt = datetime.datetime(1970, 1, 1, 0, 0, 0, tzinfo=timezone.utc)
        micros = int(dt.timestamp() * 1_000_000)  # Should be 0
        shard_id = 123

        uuid_val = self.con.execute(
            f"SELECT microshard_from_micros({micros}, {shard_id})"
        ).fetchone()[0]
        extracted_ts = self.con.execute(
            f"SELECT microshard_get_timestamp('{uuid_val}')"
        ).fetchone()[0]

        self.assertEqual(extracted_ts.timestamp(), dt.timestamp())

    def test_round_trip_deterministic(self):
        """Verify exact round-tripping of timestamp and shard ID."""
        dt = datetime.datetime(2023, 10, 27, 10, 0, 0, tzinfo=timezone.utc)
        micros = int(dt.timestamp() * 1_000_000)
        shard_id = 987654321

        # 1. UUID String Path
        uuid_str = self.con.execute(
            f"SELECT microshard_from_micros({micros}, {shard_id})"
        ).fetchone()[0]
        s_shard = self.con.execute(
            f"SELECT microshard_get_shard_id('{uuid_str}')"
        ).fetchone()[0]
        s_ts = self.con.execute(
            f"SELECT microshard_get_timestamp('{uuid_str}')"
        ).fetchone()[0]

        self.assertEqual(s_shard, shard_id)
        self.assertEqual(int(s_ts.timestamp() * 1_000_000), micros)

        # 2. UHUGEINT Path
        huge_int = self.con.execute(
            f"SELECT microshard_from_micros_int({micros}, {shard_id})"
        ).fetchone()[0]
        i_shard = self.con.execute(
            f"SELECT microshard_get_shard_id_int({huge_int})"
        ).fetchone()[0]
        i_ts = self.con.execute(
            f"SELECT microshard_get_timestamp_int({huge_int})"
        ).fetchone()[0]

        self.assertEqual(i_shard, shard_id)
        self.assertEqual(int(i_ts.timestamp() * 1_000_000), micros)

    # =========================================================================
    # 4. VECTORIZED & BULK TESTS
    # =========================================================================

    def test_vectorized_uniqueness(self):
        """
        Generate 10,000 UUIDs in a single query.
        """
        shard_id = 50
        count = 10000

        query = f"""
            SELECT 
                microshard_generate({shard_id}) as uid 
            FROM generate_series(1, {count})
        """
        results = self.con.execute(query).fetchall()

        self.assertEqual(len(results), count)
        unique_ids = set([r[0] for r in results])
        self.assertEqual(len(unique_ids), count, "Collision detected")

    def test_ordering_monotony(self):
        """
        Ensure that UUIDs generated with increasing timestamps
        sort correctly lexicographically.
        """
        base_micros = 1700000000000000
        shard_id = 1

        u1 = self.con.execute(
            f"SELECT microshard_from_micros({base_micros}, {shard_id})"
        ).fetchone()[0]
        u2 = self.con.execute(
            f"SELECT microshard_from_micros({base_micros + 1000000}, {shard_id})"
        ).fetchone()[0]
        u3 = self.con.execute(
            f"SELECT microshard_from_micros({base_micros + 2000000}, {shard_id})"
        ).fetchone()[0]

        self.assertTrue(
            str(u1) < str(u2) < str(u3), "String UUIDs are not time-ordered"
        )

        i1 = self.con.execute(f"SELECT microshard_uuid_to_int('{u1}')").fetchone()[0]
        i2 = self.con.execute(f"SELECT microshard_uuid_to_int('{u2}')").fetchone()[0]
        i3 = self.con.execute(f"SELECT microshard_uuid_to_int('{u3}')").fetchone()[0]

        self.assertTrue(i1 < i2 < i3, "Integer UUIDs are not time-ordered")

    def test_randomness_distribution(self):
        """
        Generate multiple UUIDs for the EXACT same timestamp and shard.
        Verify they differ.
        """
        micros = 100000
        shard = 1

        u1 = self.con.execute(
            f"SELECT microshard_from_micros({micros}, {shard})"
        ).fetchone()[0]
        u2 = self.con.execute(
            f"SELECT microshard_from_micros({micros}, {shard})"
        ).fetchone()[0]
        u3 = self.con.execute(
            f"SELECT microshard_from_micros({micros}, {shard})"
        ).fetchone()[0]

        self.assertNotEqual(u1, u2)
        self.assertNotEqual(u2, u3)
        self.assertNotEqual(u1, u3)

    # =========================================================================
    # 5. NULL & ERROR HANDLING
    # =========================================================================

    def test_null_handling(self):
        """Pass NULLs to functions."""
        res = self.con.execute("SELECT microshard_generate(NULL)").fetchone()[0]
        self.assertIsNone(res)

        res = self.con.execute("SELECT microshard_from_micros(NULL, 1)").fetchone()[0]
        self.assertIsNone(res)

        res = self.con.execute("SELECT microshard_get_shard_id(NULL)").fetchone()[0]
        self.assertIsNone(res)

    def test_invalid_uuid_parsing(self):
        """Pass malformed strings to unpackers."""
        bad_uuid = "not-a-uuid"
        with self.assertRaises(duckdb.Error):
            self.con.execute(f"SELECT microshard_get_shard_id('{bad_uuid}')").fetchone()


if __name__ == "__main__":
    unittest.main()
