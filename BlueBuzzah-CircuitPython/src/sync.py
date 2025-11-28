"""
Simplified Synchronization Module
==================================
Essential PRIMARY-SECONDARY synchronization for bilateral coordination.

This module provides simplified synchronization functionality including:
- EXECUTE_BUZZ and BUZZ_COMPLETE command transmission
- Basic timestamp-based synchronization
- RNG seed sharing for coordinated pattern generation
- Direct BLE send/receive (no coordinator abstraction)

Module: sync
Version: 2.0.0
"""

import time

from core.types import SyncCommandType

# ============================================================================
# Manual Serialization Functions (replaces JSON - saves ~2KB RAM)
# ============================================================================

def _serialize_data(data):
    """
    Manually serialize data dictionary to pipe-delimited format.

    Format: key1|value1|key2|value2|...

    Args:
        data: Dictionary to serialize

    Returns:
        Pipe-delimited string, or empty string if no data
    """
    if not data:
        return ""

    parts = []
    for key, value in data.items():
        parts.append(str(key))
        parts.append(str(value))

    return '|'.join(parts)


def _deserialize_data(data_str):
    """
    Manually deserialize pipe-delimited data to dictionary.

    Args:
        data_str: Pipe-delimited string

    Returns:
        Dictionary with key-value pairs

    Raises:
        ValueError: If format is invalid (odd number of parts)
    """
    if not data_str:
        return {}

    parts = data_str.split('|')

    if len(parts) % 2 != 0:
        raise ValueError("Invalid data format: odd number of parts ({})".format(len(parts)))

    data = {}
    for i in range(0, len(parts), 2):
        key = parts[i]
        value = parts[i + 1]

        # Try to convert to int (most values are integers)
        try:
            value = int(value)
        except ValueError:
            # Keep as string if not an integer
            pass

        data[key] = value

    return data


class SyncCommand:
    """
    Simple synchronization command for BLE transmission.

    Attributes:
        sequence_id: Unique sequence identifier
        timestamp: Command timestamp in microseconds
        command_type: Type of sync command
        data: Optional payload data
    """
    def __init__(self, sequence_id, timestamp, command_type, data=None):
        self.sequence_id = sequence_id
        self.timestamp = timestamp
        self.command_type = command_type
        self.data = data if data is not None else {}

    def serialize(self):
        """
        Serialize command for BLE transmission.

        Format: COMMAND_TYPE:sequence_id:timestamp[:data_pipe_delimited]\n

        Example: EXECUTE_BUZZ:42:1000000:finger|0|amplitude|50\n

        Returns:
            Serialized command as UTF-8 encoded bytes
        """
        parts = [
            str(self.command_type.value),
            str(self.sequence_id),
            str(self.timestamp)
        ]

        if self.data:
            data_str = _serialize_data(self.data)
            parts.append(data_str)

        message = ':'.join(parts) + '\n'
        return message.encode('utf-8')

    @classmethod
    def deserialize(cls, data):
        """
        Deserialize command from BLE transmission.

        Args:
            data: Received BLE data (UTF-8 encoded bytes)

        Returns:
            Deserialized SyncCommand

        Raises:
            ValueError: If data format is invalid
        """
        try:
            message = data.decode('utf-8').strip()
            parts = message.split(':', 3)

            if len(parts) < 3:
                raise ValueError("Invalid command format: {}".format(message))

            command_type = SyncCommandType(parts[0])
            sequence_id = int(parts[1])
            timestamp = int(parts[2])

            data_dict = {}
            if len(parts) == 4:
                data_dict = _deserialize_data(parts[3])

            return cls(
                sequence_id=sequence_id,
                timestamp=timestamp,
                command_type=command_type,
                data=data_dict
            )

        except (UnicodeDecodeError, ValueError) as e:
            raise ValueError("Failed to deserialize command: {}".format(e))


def get_precise_time():
    """
    Get precise timestamp in microseconds.

    Returns:
        Current time in microseconds since boot
    """
    return int(time.monotonic() * 1_000_000)


def send_execute_buzz(connection, sequence_id, finger, amplitude):
    """
    Send EXECUTE_BUZZ command to paired device.

    Args:
        connection: BLE connection object
        sequence_id: Unique sequence identifier
        finger: Finger index (0-7)
        amplitude: Haptic amplitude (0-100)
    """
    cmd = SyncCommand(
        sequence_id=sequence_id,
        timestamp=get_precise_time(),
        command_type=SyncCommandType.EXECUTE_BUZZ,
        data={
            "finger": finger,
            "amplitude": amplitude
        }
    )

    # Direct BLE send (assumes connection has write method)
    if hasattr(connection, 'write'):
        connection.write(cmd.serialize())
    else:
        # Fallback for different BLE implementations
        connection.send(cmd.serialize())


def send_buzz_complete(connection, sequence_id):
    """
    Send BUZZ_COMPLETE acknowledgment to paired device.

    Args:
        connection: BLE connection object
        sequence_id: Sequence identifier matching EXECUTE_BUZZ
    """
    cmd = SyncCommand(
        sequence_id=sequence_id,
        timestamp=get_precise_time(),
        command_type=SyncCommandType.BUZZ_COMPLETE,
        data={}
    )

    if hasattr(connection, 'write'):
        connection.write(cmd.serialize())
    else:
        connection.send(cmd.serialize())


def send_seed(connection, seed):
    """
    Share RNG seed with paired device for synchronized pattern generation.

    Args:
        connection: BLE connection object
        seed: Random seed value
    """
    cmd = SyncCommand(
        sequence_id=0,  # Seed sharing uses sequence 0
        timestamp=get_precise_time(),
        command_type=SyncCommandType.SYNC_ADJ,  # Reuse SYNC_ADJ for seed
        data={"seed": seed}
    )

    if hasattr(connection, 'write'):
        connection.write(cmd.serialize())
    else:
        connection.send(cmd.serialize())


def receive_command(connection, timeout= 0.01):
    """
    Receive and deserialize sync command from BLE connection.

    Args:
        connection: BLE connection object
        timeout: Receive timeout in seconds

    Returns:
        Deserialized SyncCommand, or None if no data available
    """
    try:
        # Try to receive data
        if hasattr(connection, 'read'):
            data = connection.read(timeout=timeout)
        elif hasattr(connection, 'receive'):
            data = connection.receive(timeout=timeout)
        else:
            return None

        if not data:
            return None

        return SyncCommand.deserialize(data)

    except Exception:
        return None


class SimpleSyncProtocol:
    """
    Simple synchronization protocol for timestamp-based coordination.

    This is a lightweight alternative to the full NTP-style protocol,
    suitable for memory-constrained devices with basic sync requirements.

    Attributes:
        current_offset: Current clock offset in microseconds
        last_sync_time: Time of last synchronization
    """

    def __init__(self):
        """Initialize simple sync protocol."""
        self.current_offset= 0
        self.last_sync_time= None

    def calculate_offset(self, primary_time, secondary_time):
        """
        Calculate simple clock offset between PRIMARY and SECONDARY.

        Args:
            primary_time: PRIMARY device timestamp (microseconds)
            secondary_time: SECONDARY device timestamp (microseconds)

        Returns:
            Clock offset in microseconds (positive = SECONDARY ahead)
        """
        offset = secondary_time - primary_time
        self.current_offset = offset
        self.last_sync_time = time.monotonic()
        return offset

    def apply_compensation(self, timestamp):
        """
        Apply time compensation to a timestamp.

        Args:
            timestamp: Original timestamp (microseconds)

        Returns:
            Compensated timestamp (microseconds)
        """
        return timestamp - self.current_offset

    def reset(self):
        """Reset synchronization state."""
        self.current_offset = 0
        self.last_sync_time = None
