import unittest
import psycopg2
import uuid
import os
import struct
import time
from datetime import datetime, timezone, timedelta

# Configuration
DB_DSN = os.environ.get(
    "TEST_DATABASE_URL", "postgresql://postgres:postgres@localhost:5432/postgres"
)
SQL_FILE_PATH = os.path.join(os.path.dirname(__file__), "../microshard_uuid.sql")


class TestMicroShardUUID(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.conn = psycopg2.connect(DB_DSN)
            cls.conn.autocommit = False
            cls.cursor = cls.conn.cursor()

            # 1. Load the user's original SQL definition
            with open(SQL_FILE_PATH, "r") as f:
                cls.cursor.execute(f.read())

            cls.conn.commit()

            # Verify the patch applied
            cls.cursor.execute(
                "SELECT prosrc FROM pg_proc WHERE proname = 'microshard_get_timestamp'"
            )
            src = cls.cursor.fetchone()[0]
            if "INTERVAL" not in src:
                raise Exception(
                    "Failed to patch microshard_get_timestamp: Function source does not contain INTERVAL fix."
                )

        except Exception as e:
            print(f"Failed to connect or load SQL: {e}")
            raise

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "cursor"):
            cls.cursor.close()
        if hasattr(cls, "conn"):
            cls.conn.close()

    def to_signed_32bit(self, n):
        """Helper: Python unsigned int -> Postgres signed int"""
        return struct.unpack("i", struct.pack("I", n & 0xFFFFFFFF))[0]

    def test_01_uuid_structure_compliance(self):
        shard_id = 123
        self.cursor.execute("SELECT microshard_generate(%s)", (shard_id,))
        uid_str = self.cursor.fetchone()[0]
        py_uuid = uuid.UUID(str(uid_str))
        self.assertEqual(py_uuid.version, 8)
        self.assertEqual(py_uuid.variant, uuid.RFC_4122)

    def test_02_shard_extraction(self):
        test_shards = [0, 1, 4096, 2147483647, -1]
        for shard in test_shards:
            with self.subTest(shard=shard):
                self.cursor.execute("SELECT microshard_generate(%s)", (shard,))
                generated_uuid = self.cursor.fetchone()[0]
                self.cursor.execute(
                    "SELECT microshard_get_shard_id(%s)", (generated_uuid,)
                )
                extracted_shard = self.cursor.fetchone()[0]
                self.assertEqual(extracted_shard, shard)

    def test_03_a_verify_storage_bytes_python(self):
        """
        Crucial Test: Verifies that the UUID actually contains the 54-bit timestamp
        by decoding the bytes in Python. This proves STORAGE is correct,
        bypassing any SQL extraction precision issues.
        """
        # Max 54-bit value: 2^54 - 1
        max_micros = (1 << 54) - 1
        shard_id = 0

        # Generate via SQL
        self.cursor.execute(
            "SELECT microshard_from_micros(%s, %s)", (max_micros, shard_id)
        )
        uid_str = str(self.cursor.fetchone()[0])
        u = uuid.UUID(uid_str)
        b = u.bytes

        # Manually Extract Timestamp from Bytes 0-7 (High 64)
        # Layout: [TimeHigh 48] [Ver 4] [TimeLow 6] [ShardHigh 6]

        # 1. Time High (Bytes 0-5)
        time_high_extracted = int.from_bytes(b[0:6], byteorder="big")

        # 2. Time Low (Byte 6 lower 4 bits, Byte 7 upper 2 bits)
        byte6 = b[6]
        byte7 = b[7]
        time_low_extracted = ((byte6 & 0x0F) << 2) | ((byte7 & 0xC0) >> 6)

        # 3. Recombine
        total_extracted_micros = (time_high_extracted << 6) | time_low_extracted

        self.assertEqual(
            total_extracted_micros,
            max_micros,
            "Python Byte Decoding: The UUID does not contain the exact 54-bit timestamp.",
        )

    def test_03_b_verify_sql_extraction_patch(self):
        """
        Verifies that the patched SQL function 'microshard_get_timestamp'
        can extract the large timestamp without float rounding errors.
        """
        max_micros = (1 << 54) - 1
        shard_id = 100

        self.cursor.execute(
            "SELECT microshard_from_micros(%s, %s)", (max_micros, shard_id)
        )
        uid = self.cursor.fetchone()[0]

        # Extract using the patched SQL function
        self.cursor.execute("SELECT microshard_get_timestamp(%s)", (uid,))
        result_dt = self.cursor.fetchone()[0]

        # Calculate micros using Integer Math in Python
        epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
        if result_dt.tzinfo is None:
            result_dt = result_dt.replace(tzinfo=timezone.utc)

        delta = result_dt - epoch

        extracted_micros = (
            (delta.days * 86400 * 1000000)
            + (delta.seconds * 1000000)
            + delta.microseconds
        )

        self.assertEqual(
            extracted_micros,
            max_micros,
            "SQL Extraction: Precision loss detected. The SQL patch (INTERVAL) may not have applied.",
        )

    def test_04_micros_roundtrip(self):
        input_micros = 1672531200 * 1000000 + 999999
        shard_id = 99
        self.cursor.execute(
            "SELECT microshard_from_micros(%s, %s)", (input_micros, shard_id)
        )
        generated_uuid = self.cursor.fetchone()[0]

        self.cursor.execute("SELECT microshard_get_timestamp(%s)", (generated_uuid,))
        result_dt = self.cursor.fetchone()[0]
        epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
        delta = result_dt - epoch
        extracted_micros = (
            (delta.days * 86400 * 1000000)
            + (delta.seconds * 1000000)
            + delta.microseconds
        )

        self.assertEqual(extracted_micros, input_micros)

    def test_05_monotonicity(self):
        shard_id = 1
        self.cursor.execute("SELECT microshard_generate(%s)", (shard_id,))
        uuid1 = str(self.cursor.fetchone()[0])
        time.sleep(0.01)
        self.cursor.execute("SELECT microshard_generate(%s)", (shard_id,))
        uuid2 = str(self.cursor.fetchone()[0])
        self.assertLess(uuid1, uuid2)

    def test_06_collision_resistance(self):
        shard_id = 1
        count = 1000
        self.cursor.execute(
            f"SELECT microshard_generate({shard_id}) FROM generate_series(1, {count})"
        )
        uuids = [r[0] for r in self.cursor.fetchall()]
        self.assertEqual(len(set(uuids)), count)

    def test_07_shard_distribution_in_bytes(self):
        micros = 1000000
        self.cursor.execute("SELECT _microshard_calc_high(%s, %s)", (micros, 0))
        high_0 = self.cursor.fetchone()[0]

        raw_shard_val = 63 << 26
        shard_high_val = self.to_signed_32bit(raw_shard_val)

        self.cursor.execute(
            "SELECT _microshard_calc_high(%s, %s)", (micros, shard_high_val)
        )
        high_shard = self.cursor.fetchone()[0]
        self.assertNotEqual(high_0, high_shard)

    def test_08_epoch_timestamp(self):
        """Verify handling of the 0 timestamp (1970-01-01 00:00:00 UTC)."""
        shard_id = 55
        self.cursor.execute("SELECT microshard_from_micros(0, %s)", (shard_id,))
        uid = self.cursor.fetchone()[0]

        self.cursor.execute("SELECT microshard_get_timestamp(%s)", (uid,))
        result_dt = self.cursor.fetchone()[0]

        expected_dt = datetime(1970, 1, 1, tzinfo=timezone.utc)
        self.assertEqual(result_dt, expected_dt)

        # Verify Shard extraction still works at time 0
        self.cursor.execute("SELECT microshard_get_shard_id(%s)", (uid,))
        self.assertEqual(self.cursor.fetchone()[0], shard_id)

    def test_09_entropy_at_same_timestamp(self):
        """Verify that two UUIDs generated with identical args are distinct (Randomness check)."""
        micros = 1600000000000000
        shard_id = 101

        self.cursor.execute("SELECT microshard_from_micros(%s, %s)", (micros, shard_id))
        uid1 = self.cursor.fetchone()[0]

        self.cursor.execute("SELECT microshard_from_micros(%s, %s)", (micros, shard_id))
        uid2 = self.cursor.fetchone()[0]

        self.assertNotEqual(
            uid1, uid2, "UUIDs with same inputs should differ due to random tail"
        )
        self.assertEqual(
            uid1[:13], uid2[:13], "High bits (Time+Ver) should be identical"
        )

    def test_10_sorting_chronological(self):
        """Verify that UUIDs sort correctly by time (Time Dominance)."""
        shard_id = 1

        # t1 < t2 < t3
        t1 = 1000000
        t2 = 2000000
        t3 = 3000000

        self.cursor.execute("SELECT microshard_from_micros(%s, %s)", (t1, shard_id))
        u1 = str(self.cursor.fetchone()[0])

        self.cursor.execute("SELECT microshard_from_micros(%s, %s)", (t2, shard_id))
        u2 = str(self.cursor.fetchone()[0])

        self.cursor.execute("SELECT microshard_from_micros(%s, %s)", (t3, shard_id))
        u3 = str(self.cursor.fetchone()[0])

        # Python string comparison of UUIDs matches Postgres byte sorting for standard layouts
        self.assertLess(u1, u2)
        self.assertLess(u2, u3)

    def test_11_database_persistence(self):
        """Verify UUIDs work as Primary Keys in a real table."""
        try:
            self.cursor.execute(
                "CREATE TEMP TABLE test_uuids (id uuid PRIMARY KEY, info text)"
            )

            # Insert a few
            shard_id = 7
            ids = []
            for i in range(5):
                self.cursor.execute(
                    "INSERT INTO test_uuids (id, info) VALUES (microshard_generate(%s), %s) RETURNING id",
                    (shard_id, f"row_{i}"),
                )
                ids.append(self.cursor.fetchone()[0])

            # Select back by ID
            target_id = ids[2]
            self.cursor.execute(
                "SELECT info FROM test_uuids WHERE id = %s", (target_id,)
            )
            row = self.cursor.fetchone()
            self.assertEqual(row[0], "row_2")

            # Verify Shard Extraction from table data
            self.cursor.execute(
                "SELECT microshard_get_shard_id(id) FROM test_uuids WHERE info = 'row_0'"
            )
            extracted_shard = self.cursor.fetchone()[0]
            self.assertEqual(extracted_shard, shard_id)

        except Exception as e:
            self.fail(f"Database persistence failed: {e}")
        finally:
            self.conn.rollback()  # Clean up temp table

    def test_12_max_timestamp_boundary(self):
        """Verify the absolute maximum 54-bit timestamp."""
        # 2^54 - 1
        max_micros = 18014398509481983
        shard_id = 999

        self.cursor.execute(
            "SELECT microshard_from_micros(%s, %s)", (max_micros, shard_id)
        )
        uid = self.cursor.fetchone()[0]

        self.cursor.execute("SELECT microshard_get_timestamp(%s)", (uid,))
        res = self.cursor.fetchone()[0]

        # Calculate expected year in Python to sanity check
        # 18014398509481983 / 1000000 / 60 / 60 / 24 / 365.25 ≈ 571 years after 1970
        # Should be roughly year 2540
        self.assertEqual(res.year, 2540)

        # Exact microsecond check
        epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
        delta = res - epoch
        calc_micros = (
            (delta.days * 86400 * 1000000)
            + (delta.seconds * 1000000)
            + delta.microseconds
        )
        self.assertEqual(calc_micros, max_micros)

    def test_13_bulk_generation(self):
        """Smoke test: Generate 10k UUIDs to ensure stability."""
        self.cursor.execute(
            """
                SELECT count(DISTINCT id), count(DISTINCT shard)
                FROM (
                    SELECT microshard_generate(5) as id,
                        microshard_get_shard_id(microshard_generate(5)) as shard
                    FROM generate_series(1, 10000)
                ) sub
            """
        )
        counts = self.cursor.fetchone()
        distinct_ids = counts[0]
        distinct_shards = counts[1]

        self.assertEqual(distinct_ids, 10000, "Should generate 10k unique IDs")
        self.assertEqual(distinct_shards, 1, "Should all belong to shard 5")


if __name__ == "__main__":
    unittest.main()
