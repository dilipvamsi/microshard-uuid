import time
import os
import struct
from uuid import UUID
from datetime import datetime, timezone
from typing import Union

# --- Constants ---
MAX_SHARD_ID = (1 << 32) - 1
MAX_TIME_MICROS = (1 << 54) - 1
MAX_RANDOM = (1 << 36) - 1

MASK_TIME_HIGH_48 = 0xFFFFFFFFFFFF
MASK_TIME_LOW_6 = 0x3F
MASK_SHARD_HIGH_6 = 0x3F
MASK_SHARD_LOW_26 = 0x3FFFFFF

# --- Stateless Module Functions ---


def generate(shard_id: int) -> UUID:
    """
    Stateless generation: Create a UUID for a specific shard using current time.
    """
    _validate_shard(shard_id)
    current_micros = int(time.time() * 1_000_000)
    return _build_uuid(current_micros, shard_id)


def from_timestamp(timestamp: Union[datetime, str, int, float], shard_id: int) -> UUID:
    """
    Stateless generation: Create a UUID for a specific shard and specific time.
    """
    _validate_shard(shard_id)
    micros = _normalize_timestamp(timestamp)
    return _build_uuid(micros, shard_id)


def get_shard_id(uid: UUID) -> int:
    """Extracts the 32-bit Shard ID from a UUID."""
    val = uid.int
    high_64 = val >> 64
    low_64 = val & 0xFFFFFFFFFFFFFFFF

    s_high = high_64 & 0x3F
    s_low = (low_64 >> 36) & 0x3FFFFFF

    return (s_high << 26) | s_low


def get_timestamp(uid: UUID) -> datetime:
    """Extracts the timestamp as a timezone-aware (UTC) datetime."""
    val = uid.int
    high_64 = val >> 64

    t_high = (high_64 >> 16) & 0xFFFFFFFFFFFF
    t_low = (high_64 >> 6) & 0x3F

    micros = (t_high << 6) | t_low
    return datetime.fromtimestamp(micros / 1_000_000.0, tz=timezone.utc)


def get_iso_timestamp(uid: UUID) -> str:
    """
    Extracts the timestamp as a strict ISO 8601 string with microsecond precision.
    Format: YYYY-MM-DDTHH:MM:SS.mmmmmmZ
    """
    dt = get_timestamp(uid)
    # timespec='microseconds' ensures we don't drop .000000
    # replace('+00:00', 'Z') ensures strict Zulu format matching JS/Specs
    return dt.isoformat(timespec="microseconds").replace("+00:00", "Z")


# --- Stateful Generator Class ---


class Generator:
    """
    A stateful generator that holds the Shard ID.
    Useful for Dependency Injection or Singleton configuration.
    """

    def __init__(self, shard_id: int):
        """
        Initialize the generator with a fixed Shard ID.
        :param shard_id: 32-bit Integer (0 - 4,294,967,295)
        """
        _validate_shard(shard_id)
        self.shard_id = shard_id

    def new_id(self) -> UUID:
        """
        Generate a new UUID using the configured Shard ID and current time.
        """
        current_micros = int(time.time() * 1_000_000)
        return _build_uuid(current_micros, self.shard_id)

    def from_timestamp(self, timestamp: Union[datetime, int, float]) -> UUID:
        """
        Generate a UUID using the configured Shard ID and a specific time.
        """
        micros = _normalize_timestamp(timestamp)
        return _build_uuid(micros, self.shard_id)


# --- Internal Helpers (Private) ---


def _validate_shard(shard_id: int):
    if not (0 <= shard_id <= MAX_SHARD_ID):
        raise ValueError(f"Shard ID must be 0 - {MAX_SHARD_ID}")


# --- Helper to normalize input to Microseconds ---
def _normalize_timestamp(timestamp: Union[datetime, str, int, float]) -> int:
    if isinstance(timestamp, datetime):
        return int(timestamp.timestamp() * 1_000_000)

    elif isinstance(timestamp, str):
        # Handle ISO String with potential Microseconds
        # Example: "2025-12-12T01:35:00.123456"
        try:
            # Python 3.7+ supports fromisoformat
            dt = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
            return int(dt.timestamp() * 1_000_000)
        except ValueError:
            raise ValueError("Invalid ISO 8601 timestamp format")

    elif isinstance(timestamp, float):
        return int(timestamp * 1_000_000)

    elif isinstance(timestamp, int):
        if timestamp > 1_000_000_000_000:
            return timestamp  # Already micros
        else:
            return timestamp * 1_000_000  # Seconds -> Micros

    else:
        raise TypeError("Timestamp must be datetime, ISO string, int, or float")


def _build_uuid(micros: int, shard_id: int) -> UUID:
    if micros > MAX_TIME_MICROS:
        raise ValueError("Time overflow (Year > 2541)")

    rand_bytes = os.urandom(5)
    rand_int = struct.unpack(">Q", rand_bytes.rjust(8, b"\x00"))[0]
    random_36 = rand_int & MAX_RANDOM

    time_high = (micros >> 6) & MASK_TIME_HIGH_48
    time_low = micros & MASK_TIME_LOW_6
    shard_high = (shard_id >> 26) & MASK_SHARD_HIGH_6

    high_64 = (time_high << 16) | (0x8 << 12) | (time_low << 6) | shard_high

    shard_low = shard_id & MASK_SHARD_LOW_26
    low_64 = (0x2 << 62) | (shard_low << 36) | random_36

    return UUID(int=(high_64 << 64) | low_64)
