import unittest
import mysql.connector
import uuid
import os
import struct
import time
from datetime import datetime, timezone, timedelta

# Configuration
# Expects a DSN or separate env vars.
# Defaults match the Docker command: user=root, pass=root, db=microshard_test
DB_HOST = os.environ.get("DB_HOST", "127.0.0.1")
DB_USER = os.environ.get("DB_USER", "root")
DB_PASS = os.environ.get("DB_PASS", "root")
DB_NAME = os.environ.get("DB_NAME", "microshard_test")
DB_PORT = int(os.environ.get("DB_PORT", "3306"))

SQL_FILE_PATH = os.path.join(os.path.dirname(__file__), "../microshard_uuid.sql")


class TestMicroShardUUID(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        try:
            # Connect to MySQL
            cls.conn = mysql.connector.connect(
                host=DB_HOST,
                user=DB_USER,
                password=DB_PASS,
                database=DB_NAME,
                port=DB_PORT,
            )
            cls.conn.autocommit = False
            cls.cursor = cls.conn.cursor()

            # 0. Set Session Timezone to UTC for deterministic timestamp testing
            cls.cursor.execute("SET time_zone = '+00:00'")

            # 1. Load the SQL definition
            with open(SQL_FILE_PATH, "r") as f:
                sql_content = f.read()

            # 2. CLEANUP: Remove client-side 'DELIMITER' commands.
            # The driver executes one statement at a time; it doesn't need to know about delimiters.
            # We replace them with empty strings so they don't cause syntax errors.
            sql_clean = sql_content.replace("DELIMITER $$", "").replace(
                "DELIMITER ;", ""
            )

            # 3. Split by the custom separator '$$' used in the file
            # This separates the function definitions.
            statements = sql_clean.split("$$")

            for statement in statements:
                stmt = statement.strip()
                # Skip empty blocks (often resulting from the split or trailing newlines)
                if not stmt:
                    continue

                # Execute the clean SQL block
                cls.cursor.execute(stmt)

            cls.conn.commit()

            # Verify the function exists
            cls.cursor.execute("SHOW CREATE FUNCTION microshard_get_timestamp")
            res = cls.cursor.fetchone()
            src = res[2]

            if "FROM_UNIXTIME" not in src and "interval" not in src.lower():
                print("Warning: Source verification weak, but continuing.")

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
        """Helper: Python unsigned int -> Signed int (simulating 32-bit overflow)"""
        return struct.unpack("i", struct.pack("I", n & 0xFFFFFFFF))[0]

    def test_01_uuid_structure_compliance(self):
        shard_id = 123
        self.cursor.execute("SELECT microshard_generate(%s)", (shard_id,))
        # MySQL returns bytes (BINARY 16)
        uid_bytes = self.cursor.fetchone()[0]

        py_uuid = uuid.UUID(bytes=uid_bytes)

        self.assertEqual(py_uuid.version, 8)
        self.assertEqual(py_uuid.variant, uuid.RFC_4122)

    def test_02_shard_extraction(self):
        # Note: MySQL integer limits apply, but logic handles standard int range
        test_shards = [0, 1, 4096, 2147483647]
        for shard in test_shards:
            with self.subTest(shard=shard):
                self.cursor.execute("SELECT microshard_generate(%s)", (shard,))
                generated_uuid_bytes = self.cursor.fetchone()[0]

                self.cursor.execute(
                    "SELECT microshard_get_shard_id(%s)", (generated_uuid_bytes,)
                )
                extracted_shard = self.cursor.fetchone()[0]
                self.assertEqual(extracted_shard, shard)

    def test_03_a_verify_storage_bytes_python(self):
        """
        Crucial Test: Verifies that the UUID actually contains the 54-bit timestamp
        by decoding the bytes in Python.
        """
        # Max 54-bit value: 2^54 - 1
        max_micros = (1 << 54) - 1
        shard_id = 0

        # Generate via SQL
        self.cursor.execute(
            "SELECT microshard_from_micros(%s, %s)", (max_micros, shard_id)
        )
        uid_bytes = self.cursor.fetchone()[0]

        # Verify length
        self.assertEqual(len(uid_bytes), 16)

        # Use Python UUID to handle byte parsing if preferred, or raw bytes
        # Layout: [TimeHigh 48] [Ver 4] [TimeLow 6] [ShardHigh 6]

        # 1. Time High (Bytes 0-5)
        time_high_extracted = int.from_bytes(uid_bytes[0:6], byteorder="big")

        # 2. Time Low (Byte 6 lower 4 bits, Byte 7 upper 2 bits)
        byte6 = uid_bytes[6]
        byte7 = uid_bytes[7]
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
        Verifies that the SQL function 'microshard_get_timestamp' extracts correctly.
        """
        max_micros = (1 << 54) - 1
        shard_id = 100

        self.cursor.execute(
            "SELECT microshard_from_micros(%s, %s)", (max_micros, shard_id)
        )
        uid_bytes = self.cursor.fetchone()[0]

        # Extract using the SQL function
        self.cursor.execute("SELECT microshard_get_timestamp(%s)", (uid_bytes,))
        result_dt = self.cursor.fetchone()[0]

        # Ensure timezone awareness (MySQL returns naive datetime usually, assuming session TZ)
        # We set session to UTC in setUpClass, so we treat this as UTC.
        if result_dt.tzinfo is None:
            result_dt = result_dt.replace(tzinfo=timezone.utc)

        epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
        delta = result_dt - epoch

        extracted_micros = (
            (delta.days * 86400 * 1000000)
            + (delta.seconds * 1000000)
            + delta.microseconds
        )

        # Allow off-by-one due to float/double precision in older MySQL versions?
        # The new implementation uses math to avoid float, so it should be exact.
        self.assertEqual(
            extracted_micros,
            max_micros,
            f"SQL Extraction: Micros mismatch. Got {extracted_micros}, Expected {max_micros}",
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

        if result_dt.tzinfo is None:
            result_dt = result_dt.replace(tzinfo=timezone.utc)

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
        uuid1_bytes = self.cursor.fetchone()[0]
        time.sleep(0.01)
        self.cursor.execute("SELECT microshard_generate(%s)", (shard_id,))
        uuid2_bytes = self.cursor.fetchone()[0]

        # Lexicographical comparison of bytes works for Time-based UUIDs
        self.assertLess(uuid1_bytes, uuid2_bytes)

    def test_06_collision_resistance(self):
        shard_id = 1
        count = 1000

        # MySQL 8.0 support CTEs (Recursive), but to support older MySQL and simplicity:
        # We will do generation in Python loop.
        uuids = set()
        for _ in range(count):
            self.cursor.execute("SELECT microshard_generate(%s)", (shard_id,))
            uuids.add(self.cursor.fetchone()[0])

        self.assertEqual(len(uuids), count)

    def test_07_shard_distribution_in_bytes(self):
        micros = 1000000
        self.cursor.execute("SELECT _microshard_calc_high(%s, %s)", (micros, 0))
        high_0 = self.cursor.fetchone()[0]

        raw_shard_val = 63 << 26
        # MySQL handles unsigned, so we pass positive large int
        shard_high_val = raw_shard_val

        self.cursor.execute(
            "SELECT _microshard_calc_high(%s, %s)", (micros, shard_high_val)
        )
        high_shard = self.cursor.fetchone()[0]
        self.assertNotEqual(high_0, high_shard)

    def test_08_epoch_timestamp(self):
        """Verify handling of the 0 timestamp."""
        shard_id = 55
        self.cursor.execute("SELECT microshard_from_micros(0, %s)", (shard_id,))
        uid = self.cursor.fetchone()[0]

        self.cursor.execute("SELECT microshard_get_timestamp(%s)", (uid,))
        result_dt = self.cursor.fetchone()[0]

        # In UTC
        expected_dt = datetime(1970, 1, 1, tzinfo=timezone.utc)

        if result_dt.tzinfo is None:
            result_dt = result_dt.replace(tzinfo=timezone.utc)

        self.assertEqual(result_dt, expected_dt)

        self.cursor.execute("SELECT microshard_get_shard_id(%s)", (uid,))
        self.assertEqual(self.cursor.fetchone()[0], shard_id)

    def test_09_entropy_at_same_timestamp(self):
        micros = 1600000000000000
        shard_id = 101

        self.cursor.execute("SELECT microshard_from_micros(%s, %s)", (micros, shard_id))
        uid1 = self.cursor.fetchone()[0]

        self.cursor.execute("SELECT microshard_from_micros(%s, %s)", (micros, shard_id))
        uid2 = self.cursor.fetchone()[0]

        self.assertNotEqual(
            uid1, uid2, "UUIDs with same inputs should differ due to random tail"
        )
        self.assertEqual(uid1[:7], uid2[:7], "High bits (Time+Ver) should be identical")

    def test_10_sorting_chronological(self):
        shard_id = 1
        t1 = 1000000
        t2 = 2000000
        t3 = 3000000

        self.cursor.execute("SELECT microshard_from_micros(%s, %s)", (t1, shard_id))
        u1 = self.cursor.fetchone()[0]

        self.cursor.execute("SELECT microshard_from_micros(%s, %s)", (t2, shard_id))
        u2 = self.cursor.fetchone()[0]

        self.cursor.execute("SELECT microshard_from_micros(%s, %s)", (t3, shard_id))
        u3 = self.cursor.fetchone()[0]

        self.assertLess(u1, u2)
        self.assertLess(u2, u3)

    def test_11_database_persistence(self):
        """Verify UUIDs work as Primary Keys in a real table."""
        try:
            self.cursor.execute(
                "CREATE TEMPORARY TABLE test_uuids (id BINARY(16) PRIMARY KEY, info text)"
            )

            shard_id = 7
            ids = []
            for i in range(5):
                # MySQL does not support RETURNING. We must Generate -> Insert.
                self.cursor.execute("SELECT microshard_generate(%s)", (shard_id,))
                gen_id = self.cursor.fetchone()[0]

                self.cursor.execute(
                    "INSERT INTO test_uuids (id, info) VALUES (%s, %s)",
                    (gen_id, f"row_{i}"),
                )
                ids.append(gen_id)

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
            self.conn.rollback()

    def test_12_max_timestamp_boundary(self):
        # 2^54 - 1
        max_micros = 18014398509481983
        shard_id = 999

        self.cursor.execute(
            "SELECT microshard_from_micros(%s, %s)", (max_micros, shard_id)
        )
        uid = self.cursor.fetchone()[0]

        self.cursor.execute("SELECT microshard_get_timestamp(%s)", (uid,))
        res = self.cursor.fetchone()[0]

        self.assertEqual(res.year, 2540)

        if res.tzinfo is None:
            res = res.replace(tzinfo=timezone.utc)

        epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
        delta = res - epoch
        calc_micros = (
            (delta.days * 86400 * 1000000)
            + (delta.seconds * 1000000)
            + delta.microseconds
        )
        self.assertEqual(calc_micros, max_micros)


def test_13_bulk_generation(self):
    """Smoke test: Generate 1000 UUIDs (Max default recursion depth) to ensure stability."""
    try:
        # MySQL default cte_max_recursion_depth is 1000.
        # We limit the test to 1000 to avoid needing to set global config.
        sql = """
                WITH RECURSIVE seq AS (
                    SELECT 1 AS n
                    UNION ALL
                    SELECT n + 1 FROM seq WHERE n < 1000
                )
                SELECT
                    COUNT(DISTINCT id),
                    COUNT(DISTINCT shard)
                FROM (
                    SELECT
                        microshard_generate(5) as id,
                        microshard_get_shard_id(microshard_generate(5)) as shard
                    FROM seq
                ) sub
            """
        self.cursor.execute(sql)
        counts = self.cursor.fetchone()
        distinct_ids = counts[0]
        distinct_shards = counts[1]

        self.assertEqual(distinct_ids, 1000, "Should generate 1000 unique IDs")
        self.assertEqual(distinct_shards, 1, "Should all belong to shard 5")

    except mysql.connector.Error as err:
        # ... existing error handling ...
        if err.errno == 1146 or "syntax" in str(err).lower():
            print("Skipping CTE bulk test...")
        else:
            raise


if __name__ == "__main__":
    unittest.main()
