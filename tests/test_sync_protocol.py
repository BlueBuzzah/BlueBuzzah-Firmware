"""
Sync Protocol Tests
===================

Comprehensive tests for the sync.py synchronization module.

Tests all serialization/deserialization functions, SyncCommand class,
helper functions, and SimpleSyncProtocol class.

Test Categories:
    - Serialization tests (_serialize_data)
    - Deserialization tests (_deserialize_data)
    - Roundtrip tests (serialization + deserialization)
    - SyncCommand tests (init, serialize, deserialize)
    - Helper function tests (send_execute_buzz, send_buzz_complete, etc.)
    - SimpleSyncProtocol tests (offset calculation, compensation)

Run with: python -m pytest tests/test_sync_protocol.py -v

Module: tests.test_sync_protocol
Version: 2.0.0
"""

import pytest
import sys
from pathlib import Path
from unittest.mock import MagicMock, patch

# Add src directory to Python path
src_path = Path(__file__).parent.parent / "src"
sys.path.insert(0, str(src_path))

# Mock micropython module before importing code that requires it
if 'micropython' not in sys.modules:
    micropython_mock = MagicMock()
    micropython_mock.const = lambda x: x
    sys.modules['micropython'] = micropython_mock

from sync import (
    _serialize_data,
    _deserialize_data,
    SyncCommand,
    get_precise_time,
    send_execute_buzz,
    send_buzz_complete,
    send_seed,
    receive_command,
    SimpleSyncProtocol,
)
from core.types import SyncCommandType


# ============================================================================
# Test Fixtures
# ============================================================================

@pytest.fixture
def mock_connection_with_write():
    """
    Provide a mock BLE connection with write method.

    Returns:
        MagicMock: Mock connection with write method
    """
    conn = MagicMock()
    conn.write = MagicMock()
    return conn


@pytest.fixture
def mock_connection_with_send():
    """
    Provide a mock BLE connection with send method (no write).

    Returns:
        MagicMock: Mock connection with send method only
    """
    conn = MagicMock(spec=['send'])
    conn.send = MagicMock()
    return conn


@pytest.fixture
def mock_connection_with_read():
    """
    Provide a mock BLE connection with read method.

    Returns:
        MagicMock: Mock connection with read method
    """
    conn = MagicMock()
    conn.read = MagicMock()
    return conn


@pytest.fixture
def mock_connection_with_receive():
    """
    Provide a mock BLE connection with receive method (no read).

    Returns:
        MagicMock: Mock connection with receive method only
    """
    conn = MagicMock(spec=['receive'])
    conn.receive = MagicMock()
    return conn


@pytest.fixture
def simple_sync_protocol():
    """
    Provide a fresh SimpleSyncProtocol instance.

    Returns:
        SimpleSyncProtocol: Fresh protocol instance
    """
    return SimpleSyncProtocol()


# ============================================================================
# Serialization Tests (_serialize_data)
# ============================================================================

class TestSerializeData:
    """Test _serialize_data function."""

    def test_empty_dict(self):
        """Test serializing an empty dictionary."""
        result = _serialize_data({})

        assert result == ""

    def test_none_input(self):
        """Test serializing None returns empty string."""
        result = _serialize_data(None)

        assert result == ""

    def test_single_key_value(self):
        """Test serializing a single key-value pair."""
        result = _serialize_data({"key": "value"})

        assert result == "key|value"

    def test_multiple_key_values(self):
        """Test serializing multiple key-value pairs."""
        result = _serialize_data({"a": "1", "b": "2"})

        # Order may vary in dict, so check both possibilities
        assert result in ["a|1|b|2", "b|2|a|1"]

    def test_integer_values(self):
        """Test serializing integer values."""
        result = _serialize_data({"finger": 0, "amplitude": 75})

        # Values should be converted to strings
        assert "finger|0" in result
        assert "amplitude|75" in result

    def test_string_values(self):
        """Test serializing string values."""
        result = _serialize_data({"name": "test", "type": "buzz"})

        assert "name|test" in result
        assert "type|buzz" in result

    def test_mixed_types(self):
        """Test serializing mixed integer and string values."""
        result = _serialize_data({"finger": 3, "pattern": "random"})

        assert "finger|3" in result
        assert "pattern|random" in result

    def test_pipe_delimiter_format(self):
        """Test that output uses pipe delimiter correctly."""
        result = _serialize_data({"key1": "val1"})

        # Should have exactly one pipe for single key-value
        assert result.count("|") == 1

    def test_three_key_values_has_five_pipes(self):
        """Test that three key-value pairs produces 5 pipes."""
        result = _serialize_data({"a": "1", "b": "2", "c": "3"})

        # 3 pairs = 6 elements = 5 pipes
        assert result.count("|") == 5


# ============================================================================
# Deserialization Tests (_deserialize_data)
# ============================================================================

class TestDeserializeData:
    """Test _deserialize_data function."""

    def test_empty_string(self):
        """Test deserializing an empty string."""
        result = _deserialize_data("")

        assert result == {}

    def test_none_input(self):
        """Test deserializing None returns empty dict."""
        result = _deserialize_data(None)

        assert result == {}

    def test_single_key_value(self):
        """Test deserializing a single key-value pair."""
        result = _deserialize_data("key|value")

        assert result == {"key": "value"}

    def test_multiple_key_values(self):
        """Test deserializing multiple key-value pairs."""
        result = _deserialize_data("a|1|b|2")

        assert result == {"a": 1, "b": 2}

    def test_converts_integers(self):
        """Test that integer strings are converted to int type."""
        result = _deserialize_data("count|42|value|100")

        assert result["count"] == 42
        assert isinstance(result["count"], int)
        assert result["value"] == 100
        assert isinstance(result["value"], int)

    def test_keeps_non_integers_as_strings(self):
        """Test that non-integer values remain as strings."""
        result = _deserialize_data("name|test|status|active")

        assert result["name"] == "test"
        assert isinstance(result["name"], str)
        assert result["status"] == "active"
        assert isinstance(result["status"], str)

    def test_mixed_integer_and_string_values(self):
        """Test mixed integer and string values."""
        result = _deserialize_data("finger|0|pattern|random")

        assert result["finger"] == 0
        assert isinstance(result["finger"], int)
        assert result["pattern"] == "random"
        assert isinstance(result["pattern"], str)

    def test_invalid_format_odd_parts_raises_error(self):
        """Test that odd number of parts raises ValueError."""
        with pytest.raises(ValueError, match="Invalid data format"):
            _deserialize_data("key|value|extra")

    def test_invalid_format_single_element(self):
        """Test that single element raises ValueError."""
        with pytest.raises(ValueError, match="Invalid data format"):
            _deserialize_data("single")

    def test_negative_integer(self):
        """Test deserializing negative integers."""
        result = _deserialize_data("offset|-500")

        assert result["offset"] == -500
        assert isinstance(result["offset"], int)

    def test_zero_value(self):
        """Test deserializing zero value."""
        result = _deserialize_data("index|0")

        assert result["index"] == 0


# ============================================================================
# Serialization Roundtrip Tests
# ============================================================================

class TestSerializationRoundtrip:
    """Test serialization + deserialization roundtrip."""

    def test_roundtrip_preserves_data(self):
        """Test that serialize then deserialize preserves original data."""
        original = {"finger": 3, "amplitude": 75}

        serialized = _serialize_data(original)
        result = _deserialize_data(serialized)

        assert result == original

    def test_roundtrip_empty_dict(self):
        """Test roundtrip with empty dictionary."""
        original = {}

        serialized = _serialize_data(original)
        result = _deserialize_data(serialized)

        assert result == original

    def test_roundtrip_single_pair(self):
        """Test roundtrip with single key-value pair."""
        original = {"seed": 12345}

        serialized = _serialize_data(original)
        result = _deserialize_data(serialized)

        assert result == original

    def test_roundtrip_multiple_pairs(self):
        """Test roundtrip with multiple key-value pairs."""
        original = {"a": 1, "b": 2, "c": 3}

        serialized = _serialize_data(original)
        result = _deserialize_data(serialized)

        assert result == original

    def test_roundtrip_with_string_numbers(self):
        """Test roundtrip converts string numbers to integers."""
        original = {"value": "100"}  # String "100"

        serialized = _serialize_data(original)
        result = _deserialize_data(serialized)

        # After roundtrip, string "100" becomes integer 100
        assert result["value"] == 100


# ============================================================================
# SyncCommand Tests
# ============================================================================

class TestSyncCommand:
    """Test SyncCommand class."""

    def test_init_with_data(self):
        """Test SyncCommand initialization with data."""
        data = {"finger": 2, "amplitude": 50}

        cmd = SyncCommand(
            sequence_id=42,
            timestamp=1000000,
            command_type=SyncCommandType.EXECUTE_BUZZ,
            data=data
        )

        assert cmd.sequence_id == 42
        assert cmd.timestamp == 1000000
        assert cmd.command_type == SyncCommandType.EXECUTE_BUZZ
        assert cmd.data == {"finger": 2, "amplitude": 50}

    def test_init_without_data(self):
        """Test SyncCommand initialization without data."""
        cmd = SyncCommand(
            sequence_id=1,
            timestamp=500000,
            command_type=SyncCommandType.BUZZ_COMPLETE
        )

        assert cmd.sequence_id == 1
        assert cmd.timestamp == 500000
        assert cmd.command_type == SyncCommandType.BUZZ_COMPLETE
        assert cmd.data == {}

    def test_init_with_none_data(self):
        """Test SyncCommand initialization with None data defaults to empty dict."""
        cmd = SyncCommand(
            sequence_id=1,
            timestamp=500000,
            command_type=SyncCommandType.HEARTBEAT,
            data=None
        )

        assert cmd.data == {}

    def test_serialize_format(self):
        """Test SyncCommand serialization format."""
        cmd = SyncCommand(
            sequence_id=10,
            timestamp=2000000,
            command_type=SyncCommandType.BUZZ_COMPLETE,
            data={}
        )

        result = cmd.serialize()

        # Should be bytes
        assert isinstance(result, bytes)

        # Decode and check format
        decoded = result.decode('utf-8')
        assert decoded == "BUZZ_COMPLETE:10:2000000\n"

    def test_serialize_with_data(self):
        """Test SyncCommand serialization with data payload."""
        cmd = SyncCommand(
            sequence_id=5,
            timestamp=1500000,
            command_type=SyncCommandType.EXECUTE_BUZZ,
            data={"finger": 0, "amplitude": 100}
        )

        result = cmd.serialize()
        decoded = result.decode('utf-8')

        # Check format components
        assert decoded.startswith("EXECUTE_BUZZ:5:1500000:")
        assert decoded.endswith("\n")
        assert "finger|0" in decoded
        assert "amplitude|100" in decoded

    def test_serialize_ends_with_newline(self):
        """Test that serialized command ends with newline."""
        cmd = SyncCommand(
            sequence_id=1,
            timestamp=100,
            command_type=SyncCommandType.HEARTBEAT
        )

        result = cmd.serialize()

        assert result.endswith(b"\n")

    def test_deserialize_basic(self):
        """Test SyncCommand deserialization of basic command."""
        data = b"BUZZ_COMPLETE:42:3000000\n"

        cmd = SyncCommand.deserialize(data)

        assert cmd.sequence_id == 42
        assert cmd.timestamp == 3000000
        assert cmd.command_type == SyncCommandType.BUZZ_COMPLETE
        assert cmd.data == {}

    def test_deserialize_with_data(self):
        """Test SyncCommand deserialization with data payload."""
        data = b"EXECUTE_BUZZ:99:4000000:finger|3|amplitude|80\n"

        cmd = SyncCommand.deserialize(data)

        assert cmd.sequence_id == 99
        assert cmd.timestamp == 4000000
        assert cmd.command_type == SyncCommandType.EXECUTE_BUZZ
        assert cmd.data == {"finger": 3, "amplitude": 80}

    def test_deserialize_roundtrip(self):
        """Test serialize then deserialize produces equivalent command."""
        original = SyncCommand(
            sequence_id=123,
            timestamp=5000000,
            command_type=SyncCommandType.EXECUTE_BUZZ,
            data={"finger": 4, "amplitude": 65}
        )

        serialized = original.serialize()
        restored = SyncCommand.deserialize(serialized)

        assert restored.sequence_id == original.sequence_id
        assert restored.timestamp == original.timestamp
        assert restored.command_type == original.command_type
        assert restored.data == original.data

    def test_deserialize_invalid_format_raises_error(self):
        """Test that invalid format raises ValueError."""
        # Only two parts (missing timestamp)
        data = b"EXECUTE_BUZZ:42\n"

        with pytest.raises(ValueError, match="Invalid command format"):
            SyncCommand.deserialize(data)

    def test_deserialize_invalid_utf8_raises_error(self):
        """Test that invalid UTF-8 raises ValueError."""
        # Invalid UTF-8 bytes
        data = b"\xff\xfe\x00"

        with pytest.raises(ValueError, match="Failed to deserialize"):
            SyncCommand.deserialize(data)

    def test_deserialize_strips_whitespace(self):
        """Test that deserialization strips whitespace."""
        data = b"  HEARTBEAT:1:100  \n"

        cmd = SyncCommand.deserialize(data)

        assert cmd.command_type == SyncCommandType.HEARTBEAT

    def test_deserialize_all_command_types(self):
        """Test deserialization works for all SyncCommandType values."""
        command_types = [
            SyncCommandType.SYNC_ADJ,
            SyncCommandType.SYNC_ADJ_START,
            SyncCommandType.EXECUTE_BUZZ,
            SyncCommandType.BUZZ_COMPLETE,
            SyncCommandType.FIRST_SYNC,
            SyncCommandType.ACK_SYNC_ADJ,
            SyncCommandType.START_SESSION,
            SyncCommandType.PAUSE_SESSION,
            SyncCommandType.RESUME_SESSION,
            SyncCommandType.STOP_SESSION,
            SyncCommandType.DEACTIVATE,
            SyncCommandType.HEARTBEAT,
        ]

        for cmd_type in command_types:
            cmd = SyncCommand(
                sequence_id=1,
                timestamp=100,
                command_type=cmd_type
            )
            serialized = cmd.serialize()
            restored = SyncCommand.deserialize(serialized)

            assert restored.command_type == cmd_type


# ============================================================================
# get_precise_time Tests
# ============================================================================

class TestGetPreciseTime:
    """Test get_precise_time function."""

    def test_returns_integer(self):
        """Test that get_precise_time returns an integer."""
        result = get_precise_time()

        assert isinstance(result, int)

    def test_returns_positive_value(self):
        """Test that get_precise_time returns positive value."""
        result = get_precise_time()

        assert result > 0

    def test_increases_over_time(self):
        """Test that successive calls return increasing values."""
        time1 = get_precise_time()
        time2 = get_precise_time()

        assert time2 >= time1

    def test_returns_microseconds(self):
        """Test that value is in microsecond range (large number)."""
        result = get_precise_time()

        # Should be at least 1000 (1ms in microseconds)
        # In practice will be much larger (seconds since boot * 1000000)
        assert result >= 1000

    @patch('sync.time.monotonic')
    def test_multiplies_by_million(self, mock_monotonic):
        """Test that monotonic time is multiplied by 1,000,000."""
        mock_monotonic.return_value = 1.5  # 1.5 seconds

        result = get_precise_time()

        assert result == 1500000  # 1.5 * 1,000,000


# ============================================================================
# send_execute_buzz Tests
# ============================================================================

class TestSendExecuteBuzz:
    """Test send_execute_buzz function."""

    def test_sends_command(self, mock_connection_with_write):
        """Test that send_execute_buzz sends a command."""
        send_execute_buzz(mock_connection_with_write, sequence_id=1, finger=0, amplitude=75)

        mock_connection_with_write.write.assert_called_once()

    def test_includes_finger_and_amplitude(self, mock_connection_with_write):
        """Test that command includes finger and amplitude data."""
        send_execute_buzz(mock_connection_with_write, sequence_id=10, finger=3, amplitude=50)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert "finger|3" in decoded
        assert "amplitude|50" in decoded

    def test_uses_execute_buzz_command_type(self, mock_connection_with_write):
        """Test that command type is EXECUTE_BUZZ."""
        send_execute_buzz(mock_connection_with_write, sequence_id=1, finger=0, amplitude=100)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert decoded.startswith("EXECUTE_BUZZ:")

    def test_includes_sequence_id(self, mock_connection_with_write):
        """Test that command includes sequence_id."""
        send_execute_buzz(mock_connection_with_write, sequence_id=42, finger=0, amplitude=100)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert ":42:" in decoded

    def test_uses_write_method(self, mock_connection_with_write):
        """Test that write method is preferred."""
        send_execute_buzz(mock_connection_with_write, sequence_id=1, finger=0, amplitude=50)

        mock_connection_with_write.write.assert_called_once()

    def test_fallback_to_send_method(self, mock_connection_with_send):
        """Test fallback to send method when no write."""
        send_execute_buzz(mock_connection_with_send, sequence_id=1, finger=0, amplitude=50)

        mock_connection_with_send.send.assert_called_once()

    def test_finger_range_0(self, mock_connection_with_write):
        """Test with finger index 0."""
        send_execute_buzz(mock_connection_with_write, sequence_id=1, finger=0, amplitude=100)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert "finger|0" in decoded

    def test_finger_range_7(self, mock_connection_with_write):
        """Test with finger index 7 (maximum)."""
        send_execute_buzz(mock_connection_with_write, sequence_id=1, finger=7, amplitude=100)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert "finger|7" in decoded

    def test_amplitude_range_0(self, mock_connection_with_write):
        """Test with amplitude 0 (minimum)."""
        send_execute_buzz(mock_connection_with_write, sequence_id=1, finger=0, amplitude=0)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert "amplitude|0" in decoded

    def test_amplitude_range_100(self, mock_connection_with_write):
        """Test with amplitude 100 (maximum)."""
        send_execute_buzz(mock_connection_with_write, sequence_id=1, finger=0, amplitude=100)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert "amplitude|100" in decoded


# ============================================================================
# send_buzz_complete Tests
# ============================================================================

class TestSendBuzzComplete:
    """Test send_buzz_complete function."""

    def test_sends_acknowledgment(self, mock_connection_with_write):
        """Test that send_buzz_complete sends acknowledgment."""
        send_buzz_complete(mock_connection_with_write, sequence_id=5)

        mock_connection_with_write.write.assert_called_once()

    def test_uses_buzz_complete_command_type(self, mock_connection_with_write):
        """Test that command type is BUZZ_COMPLETE."""
        send_buzz_complete(mock_connection_with_write, sequence_id=10)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert decoded.startswith("BUZZ_COMPLETE:")

    def test_includes_sequence_id(self, mock_connection_with_write):
        """Test that sequence_id matches EXECUTE_BUZZ."""
        send_buzz_complete(mock_connection_with_write, sequence_id=99)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert ":99:" in decoded

    def test_has_empty_data(self, mock_connection_with_write):
        """Test that BUZZ_COMPLETE has no data payload."""
        send_buzz_complete(mock_connection_with_write, sequence_id=1)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8').strip()

        # Should only have 3 parts (type:seq:timestamp)
        parts = decoded.split(':')
        assert len(parts) == 3

    def test_uses_write_method(self, mock_connection_with_write):
        """Test that write method is preferred."""
        send_buzz_complete(mock_connection_with_write, sequence_id=1)

        mock_connection_with_write.write.assert_called_once()

    def test_fallback_to_send_method(self, mock_connection_with_send):
        """Test fallback to send method when no write."""
        send_buzz_complete(mock_connection_with_send, sequence_id=1)

        mock_connection_with_send.send.assert_called_once()


# ============================================================================
# send_seed Tests
# ============================================================================

class TestSendSeed:
    """Test send_seed function."""

    def test_sends_seed_command(self, mock_connection_with_write):
        """Test that send_seed sends a command."""
        send_seed(mock_connection_with_write, seed=12345)

        mock_connection_with_write.write.assert_called_once()

    def test_includes_seed_value(self, mock_connection_with_write):
        """Test that command includes the seed value."""
        send_seed(mock_connection_with_write, seed=98765)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert "seed|98765" in decoded

    def test_uses_sync_adj_command_type(self, mock_connection_with_write):
        """Test that SYNC_ADJ is reused for seed sharing."""
        send_seed(mock_connection_with_write, seed=100)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        assert decoded.startswith("SYNC_ADJ:")

    def test_uses_sequence_zero(self, mock_connection_with_write):
        """Test that seed sharing uses sequence_id 0."""
        send_seed(mock_connection_with_write, seed=50000)

        call_args = mock_connection_with_write.write.call_args[0][0]
        decoded = call_args.decode('utf-8')

        # Format: SYNC_ADJ:0:timestamp:seed|value
        parts = decoded.split(':')
        assert parts[1] == "0"

    def test_uses_write_method(self, mock_connection_with_write):
        """Test that write method is preferred."""
        send_seed(mock_connection_with_write, seed=1)

        mock_connection_with_write.write.assert_called_once()

    def test_fallback_to_send_method(self, mock_connection_with_send):
        """Test fallback to send method when no write."""
        send_seed(mock_connection_with_send, seed=1)

        mock_connection_with_send.send.assert_called_once()


# ============================================================================
# receive_command Tests
# ============================================================================

class TestReceiveCommand:
    """Test receive_command function."""

    def test_receives_and_deserializes(self, mock_connection_with_read):
        """Test that receive_command reads and deserializes data."""
        mock_connection_with_read.read.return_value = b"HEARTBEAT:1:100000\n"

        cmd = receive_command(mock_connection_with_read)

        assert cmd is not None
        assert cmd.command_type == SyncCommandType.HEARTBEAT
        assert cmd.sequence_id == 1

    def test_returns_none_on_no_data(self, mock_connection_with_read):
        """Test that None is returned when no data available."""
        mock_connection_with_read.read.return_value = None

        cmd = receive_command(mock_connection_with_read)

        assert cmd is None

    def test_returns_none_on_empty_data(self, mock_connection_with_read):
        """Test that None is returned for empty data."""
        mock_connection_with_read.read.return_value = b""

        cmd = receive_command(mock_connection_with_read)

        assert cmd is None

    def test_returns_none_on_error(self, mock_connection_with_read):
        """Test that None is returned on deserialization error."""
        mock_connection_with_read.read.return_value = b"invalid data"

        cmd = receive_command(mock_connection_with_read)

        assert cmd is None

    def test_uses_read_method(self, mock_connection_with_read):
        """Test that read method is preferred."""
        mock_connection_with_read.read.return_value = b"HEARTBEAT:1:100\n"

        receive_command(mock_connection_with_read)

        mock_connection_with_read.read.assert_called_once()

    def test_fallback_to_receive_method(self, mock_connection_with_receive):
        """Test fallback to receive method when no read."""
        mock_connection_with_receive.receive.return_value = b"HEARTBEAT:1:100\n"

        cmd = receive_command(mock_connection_with_receive)

        mock_connection_with_receive.receive.assert_called_once()
        assert cmd is not None

    def test_passes_timeout_to_read(self, mock_connection_with_read):
        """Test that timeout is passed to read method."""
        mock_connection_with_read.read.return_value = None

        receive_command(mock_connection_with_read, timeout=0.5)

        mock_connection_with_read.read.assert_called_with(timeout=0.5)

    def test_default_timeout(self, mock_connection_with_read):
        """Test that default timeout is 0.01 seconds."""
        mock_connection_with_read.read.return_value = None

        receive_command(mock_connection_with_read)

        mock_connection_with_read.read.assert_called_with(timeout=0.01)

    def test_returns_none_for_connection_without_read_or_receive(self):
        """Test that None is returned for connection without read/receive."""
        conn = MagicMock(spec=['write'])  # No read or receive

        cmd = receive_command(conn)

        assert cmd is None

    def test_returns_none_on_read_exception(self, mock_connection_with_read):
        """Test that None is returned when read raises exception."""
        mock_connection_with_read.read.side_effect = Exception("Read error")

        cmd = receive_command(mock_connection_with_read)

        assert cmd is None


# ============================================================================
# SimpleSyncProtocol Tests
# ============================================================================

class TestSimpleSyncProtocol:
    """Test SimpleSyncProtocol class."""

    def test_initial_offset_is_zero(self, simple_sync_protocol):
        """Test that initial offset is zero."""
        assert simple_sync_protocol.current_offset == 0

    def test_initial_last_sync_time_is_none(self, simple_sync_protocol):
        """Test that initial last_sync_time is None."""
        assert simple_sync_protocol.last_sync_time is None

    def test_calculate_offset(self, simple_sync_protocol):
        """Test clock offset calculation."""
        primary_time = 1000000
        secondary_time = 1000500  # 500 microseconds ahead

        offset = simple_sync_protocol.calculate_offset(primary_time, secondary_time)

        assert offset == 500  # secondary_time - primary_time

    def test_calculate_offset_negative(self, simple_sync_protocol):
        """Test negative offset when SECONDARY is behind."""
        primary_time = 1000500
        secondary_time = 1000000  # 500 microseconds behind

        offset = simple_sync_protocol.calculate_offset(primary_time, secondary_time)

        assert offset == -500

    def test_calculate_offset_updates_current_offset(self, simple_sync_protocol):
        """Test that calculate_offset updates current_offset."""
        simple_sync_protocol.calculate_offset(1000, 1100)

        assert simple_sync_protocol.current_offset == 100

    def test_calculate_offset_updates_last_sync_time(self, simple_sync_protocol):
        """Test that calculate_offset updates last_sync_time."""
        assert simple_sync_protocol.last_sync_time is None

        simple_sync_protocol.calculate_offset(1000, 1100)

        assert simple_sync_protocol.last_sync_time is not None

    def test_apply_compensation(self, simple_sync_protocol):
        """Test timestamp compensation."""
        simple_sync_protocol.current_offset = 500  # SECONDARY ahead by 500

        compensated = simple_sync_protocol.apply_compensation(1000000)

        assert compensated == 999500  # 1000000 - 500

    def test_apply_compensation_with_negative_offset(self, simple_sync_protocol):
        """Test compensation with negative offset."""
        simple_sync_protocol.current_offset = -500  # SECONDARY behind by 500

        compensated = simple_sync_protocol.apply_compensation(1000000)

        assert compensated == 1000500  # 1000000 - (-500)

    def test_apply_compensation_zero_offset(self, simple_sync_protocol):
        """Test compensation with zero offset."""
        simple_sync_protocol.current_offset = 0

        compensated = simple_sync_protocol.apply_compensation(1000000)

        assert compensated == 1000000  # No change

    def test_reset(self, simple_sync_protocol):
        """Test reset clears state."""
        # Set some state
        simple_sync_protocol.current_offset = 1000
        simple_sync_protocol.last_sync_time = 12345

        simple_sync_protocol.reset()

        assert simple_sync_protocol.current_offset == 0
        assert simple_sync_protocol.last_sync_time is None

    def test_multiple_offset_calculations(self, simple_sync_protocol):
        """Test that subsequent calculations update offset."""
        simple_sync_protocol.calculate_offset(1000, 1100)
        assert simple_sync_protocol.current_offset == 100

        simple_sync_protocol.calculate_offset(2000, 2200)
        assert simple_sync_protocol.current_offset == 200

    def test_reset_after_calculations(self, simple_sync_protocol):
        """Test reset after multiple calculations."""
        simple_sync_protocol.calculate_offset(1000, 1500)
        simple_sync_protocol.calculate_offset(2000, 2300)

        simple_sync_protocol.reset()

        assert simple_sync_protocol.current_offset == 0
        assert simple_sync_protocol.last_sync_time is None


# ============================================================================
# Integration Tests
# ============================================================================

class TestSyncProtocolIntegration:
    """Integration tests for sync protocol components."""

    def test_execute_buzz_flow(self, mock_connection_with_write, mock_connection_with_read):
        """Test complete EXECUTE_BUZZ -> BUZZ_COMPLETE flow."""
        # PRIMARY sends EXECUTE_BUZZ
        send_execute_buzz(mock_connection_with_write, sequence_id=100, finger=2, amplitude=75)

        # Get the sent command
        sent_data = mock_connection_with_write.write.call_args[0][0]

        # Simulate SECONDARY receiving and parsing it
        cmd = SyncCommand.deserialize(sent_data)

        assert cmd.command_type == SyncCommandType.EXECUTE_BUZZ
        assert cmd.sequence_id == 100
        assert cmd.data["finger"] == 2
        assert cmd.data["amplitude"] == 75

    def test_sync_protocol_with_commands(self, simple_sync_protocol):
        """Test SimpleSyncProtocol with typical sync scenario."""
        # Initial state
        assert simple_sync_protocol.current_offset == 0

        # PRIMARY sends time 1000000, SECONDARY has time 1000250
        offset = simple_sync_protocol.calculate_offset(1000000, 1000250)
        assert offset == 250

        # Apply compensation to schedule execution
        target_time = 2000000
        compensated = simple_sync_protocol.apply_compensation(target_time)
        assert compensated == 1999750  # Account for being 250us ahead

    def test_command_serialization_consistency(self):
        """Test that all command types serialize and deserialize consistently."""
        test_cases = [
            (SyncCommandType.EXECUTE_BUZZ, {"finger": 0, "amplitude": 100}),
            (SyncCommandType.BUZZ_COMPLETE, {}),
            (SyncCommandType.SYNC_ADJ, {"seed": 12345}),
            (SyncCommandType.START_SESSION, {"duration": 3600}),
            (SyncCommandType.HEARTBEAT, {}),
        ]

        for cmd_type, data in test_cases:
            original = SyncCommand(
                sequence_id=42,
                timestamp=1000000,
                command_type=cmd_type,
                data=data
            )

            serialized = original.serialize()
            restored = SyncCommand.deserialize(serialized)

            assert restored.command_type == original.command_type
            assert restored.sequence_id == original.sequence_id
            assert restored.data == original.data


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
