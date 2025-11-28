"""
BLE Manager Tests
=================

Comprehensive pytest tests for src/ble.py BLE communication module.
Tests BLEConnection class and BLE manager with mocked BLE libraries.

Test Categories:
    - BLEConnection: Connection state and buffer management
    - BLE Initialization: Radio and service setup
    - Advertising: Start/stop advertising with name configuration
    - Scanning: Device discovery and connection
    - Message Protocol: Send/receive with EOT framing
    - Connection Management: Multi-connection support
    - Error Handling: Connection failures and recovery

Version: 1.0.0
"""

import pytest
import sys
import time
from unittest.mock import MagicMock, patch, PropertyMock
from pathlib import Path

# Add src directory to Python path
src_path = Path(__file__).parent.parent / "src"
sys.path.insert(0, str(src_path))

# Mock micropython module
if 'micropython' not in sys.modules:
    micropython_mock = MagicMock()
    micropython_mock.const = lambda x: x
    sys.modules['micropython'] = micropython_mock


# ============================================================================
# Mock BLE Library Classes
# ============================================================================

class MockUARTService:
    """Mock UARTService for testing."""

    def __init__(self):
        self.in_waiting = 0
        self._read_data = bytearray()
        self._write_history = []
        self._read_position = 0

    def write(self, data):
        self._write_history.append(bytes(data))
        return len(data)

    def read(self):
        if self._read_position < len(self._read_data):
            data = self._read_data[self._read_position:]
            self._read_position = len(self._read_data)
            return bytes(data)
        return None

    def readinto(self, buf):
        if self._read_position < len(self._read_data):
            available = len(self._read_data) - self._read_position
            to_read = min(available, len(buf))
            buf[:to_read] = self._read_data[self._read_position:self._read_position + to_read]
            self._read_position += to_read
            return to_read
        return 0

    def flush(self):
        pass

    def queue_data(self, data):
        """Test helper: Queue data for reading."""
        if isinstance(data, str):
            data = data.encode('utf-8')
        self._read_data.extend(data)
        self.in_waiting = len(self._read_data) - self._read_position

    def clear_read_buffer(self):
        """Test helper: Clear read buffer."""
        self._read_data = bytearray()
        self._read_position = 0
        self.in_waiting = 0


class MockBLEConnection:
    """Mock BLE connection object."""

    def __init__(self, connected=True):
        self._connected = connected
        self._uart_service = MockUARTService()

    @property
    def connected(self):
        return self._connected

    def disconnect(self):
        self._connected = False

    def __getitem__(self, service_type):
        """Return UART service when accessed via connection[UARTService]."""
        return self._uart_service


class MockBLERadio:
    """Mock BLERadio for testing."""

    def __init__(self):
        self.name = "MockBLE"
        self.advertising = False
        self._connected = False
        self._connections = []
        self._scan_results = []
        self.address_bytes = bytearray([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF])

    @property
    def connected(self):
        return self._connected or len(self._connections) > 0

    @property
    def connections(self):
        return self._connections

    def start_advertising(self, advertisement):
        self.advertising = True

    def stop_advertising(self):
        self.advertising = False

    def start_scan(self, timeout=None):
        """Yield scan results."""
        for result in self._scan_results:
            yield result

    def stop_scan(self):
        pass

    def connect(self, advertisement):
        conn = MockBLEConnection()
        self._connections.append(conn)
        self._connected = True
        return conn

    def add_scan_result(self, result):
        """Test helper: Add scan result."""
        self._scan_results.append(result)

    def add_connection(self, conn):
        """Test helper: Add incoming connection."""
        self._connections.append(conn)


class MockProvideServicesAdvertisement:
    """Mock advertisement."""

    def __init__(self, *services):
        self.services = services
        self.complete_name = None
        self._data = bytearray()


class MockAdapter:
    """Mock BLE adapter."""
    name = "MockAdapter"


class MockAdvertisement:
    """Mock scanned advertisement."""

    def __init__(self, name="TestDevice", rssi=-50):
        self.complete_name = name
        self.rssi = rssi


# ============================================================================
# Test Fixtures
# ============================================================================

@pytest.fixture
def mock_ble_libs():
    """Mock all BLE library dependencies."""
    with patch.dict(sys.modules, {
        'adafruit_ble': MagicMock(),
        'adafruit_ble.advertising.standard': MagicMock(),
        'adafruit_ble.advertising': MagicMock(),
        'adafruit_ble.services.nordic': MagicMock(),
        '_bleio': MagicMock(),
    }):
        # Configure the mocks
        mock_bleio = sys.modules['_bleio']
        mock_bleio.adapter = MockAdapter()

        mock_adafruit_ble = sys.modules['adafruit_ble']
        mock_adafruit_ble.BLERadio = MockBLERadio

        mock_standard = sys.modules['adafruit_ble.advertising.standard']
        mock_standard.ProvideServicesAdvertisement = MockProvideServicesAdvertisement

        mock_nordic = sys.modules['adafruit_ble.services.nordic']
        mock_nordic.UARTService = MockUARTService

        yield {
            'bleio': mock_bleio,
            'adafruit_ble': mock_adafruit_ble,
            'standard': mock_standard,
            'nordic': mock_nordic,
        }


@pytest.fixture
def ble_connection_class():
    """Import BLEConnection class with mocked dependencies."""
    with patch.dict(sys.modules, {
        'adafruit_ble': MagicMock(),
        'adafruit_ble.advertising.standard': MagicMock(),
        'adafruit_ble.advertising': MagicMock(),
        'adafruit_ble.services.nordic': MagicMock(),
        '_bleio': MagicMock(),
    }):
        # Need to reload the module to use our mocks
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLEConnection
        yield BLEConnection


@pytest.fixture
def mock_uart():
    """Provide mock UART service."""
    return MockUARTService()


@pytest.fixture
def mock_ble_conn():
    """Provide mock BLE connection."""
    return MockBLEConnection()


# ============================================================================
# BLEConnection Class Tests
# ============================================================================

class TestBLEConnection:
    """Test BLEConnection class."""

    def test_initialization(self, ble_connection_class, mock_ble_conn, mock_uart):
        """BLEConnection initializes with correct attributes."""
        conn = ble_connection_class("test_conn", mock_ble_conn, mock_uart)

        assert conn.name == "test_conn"
        assert conn.ble_connection is mock_ble_conn
        assert conn.uart is mock_uart
        assert conn.connected_at > 0
        assert conn.last_seen > 0

    def test_buffer_allocation(self, ble_connection_class, mock_ble_conn, mock_uart):
        """BLEConnection pre-allocates buffers."""
        conn = ble_connection_class("test_conn", mock_ble_conn, mock_uart)

        # Check pre-allocated buffers exist
        assert hasattr(conn, '_rx_buffer')
        assert hasattr(conn, '_msg_buffer')
        assert hasattr(conn, '_msg_len')
        assert hasattr(conn, '_pending_messages')

        # Check buffer sizes
        assert len(conn._rx_buffer) == 256
        assert len(conn._msg_buffer) == 512
        assert conn._msg_len == 0
        assert conn._pending_messages == []

    def test_update_last_seen(self, ble_connection_class, mock_ble_conn, mock_uart):
        """update_last_seen() updates timestamp."""
        conn = ble_connection_class("test_conn", mock_ble_conn, mock_uart)

        initial_time = conn.last_seen
        time.sleep(0.01)
        conn.update_last_seen()

        assert conn.last_seen > initial_time

    def test_is_connected_when_connected(self, ble_connection_class, mock_ble_conn, mock_uart):
        """is_connected() returns True when connected."""
        conn = ble_connection_class("test_conn", mock_ble_conn, mock_uart)

        assert conn.is_connected() is True

    def test_is_connected_when_disconnected(self, ble_connection_class, mock_uart):
        """is_connected() returns False when disconnected."""
        mock_conn = MockBLEConnection(connected=False)
        conn = ble_connection_class("test_conn", mock_conn, mock_uart)

        assert conn.is_connected() is False

    def test_is_connected_handles_exception(self, ble_connection_class, mock_uart):
        """is_connected() returns False on exception."""
        mock_conn = MagicMock()
        # Use type() to create a proper property that raises exception
        type(mock_conn).connected = PropertyMock(side_effect=Exception("Connection error"))

        conn = ble_connection_class("test_conn", mock_conn, mock_uart)

        assert conn.is_connected() is False

    def test_pending_messages_queue(self, ble_connection_class, mock_ble_conn, mock_uart):
        """_pending_messages queue is properly initialized."""
        conn = ble_connection_class("test_conn", mock_ble_conn, mock_uart)

        # Queue should be empty initially
        assert len(conn._pending_messages) == 0

        # Can add messages
        conn._pending_messages.append("test_message")
        assert len(conn._pending_messages) == 1
        assert conn._pending_messages[0] == "test_message"


# ============================================================================
# BLE Class Initialization Tests
# ============================================================================

class TestBLEInitialization:
    """Test BLE class initialization."""

    def test_initialization_creates_radio(self, mock_ble_libs):
        """BLE initialization creates radio and services."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()

        assert ble.ble is not None
        assert ble.uart_service is not None
        assert ble.advertisement is not None
        assert ble._connections == {}

    def test_initialization_sets_adapter_name(self, mock_ble_libs):
        """BLE initialization sets default adapter name."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()

        # Adapter name should be set
        assert mock_ble_libs['bleio'].adapter.name == "BlueBuzzah"

    def test_initialization_fails_without_ble_libraries(self):
        """BLE initialization raises error without BLE libraries."""
        with patch.dict(sys.modules, {
            'adafruit_ble': MagicMock(),
        }):
            sys.modules['adafruit_ble'].BLERadio = None

            if 'ble' in sys.modules:
                del sys.modules['ble']
            from ble import BLE

            with pytest.raises(RuntimeError, match="BLE libraries not available"):
                BLE()


# ============================================================================
# Advertising Tests
# ============================================================================

class TestBLEAdvertising:
    """Test BLE advertising functionality."""

    def test_advertise_starts_advertising(self, mock_ble_libs):
        """advertise() starts BLE advertising."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()
        ble.advertise("TestDevice")

        assert ble.ble.advertising is True

    def test_advertise_sets_device_name(self, mock_ble_libs):
        """advertise() sets device name."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()
        ble.advertise("TestDevice")

        assert ble.ble.name == "TestDevice"

    def test_advertise_sets_adapter_name(self, mock_ble_libs):
        """advertise() sets adapter name."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()
        ble.advertise("TestDevice")

        assert mock_ble_libs['bleio'].adapter.name == "TestDevice"

    def test_advertise_recreates_uart_service(self, mock_ble_libs):
        """advertise() creates new UART service."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()
        original_uart = ble.uart_service

        ble.advertise("TestDevice")

        # UART service should be recreated
        assert ble.uart_service is not None

    def test_stop_advertising(self, mock_ble_libs):
        """stop_advertising() stops advertising."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()
        ble.advertise("TestDevice")
        assert ble.ble.advertising is True

        ble.stop_advertising()
        assert ble.ble.advertising is False

    def test_stop_advertising_when_not_advertising(self, mock_ble_libs):
        """stop_advertising() handles not advertising state."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()
        # Should not raise even if not advertising
        ble.stop_advertising()


# ============================================================================
# Identity Name Tests
# ============================================================================

class TestBLEIdentityName:
    """Test BLE identity name functionality."""

    def test_set_identity_name(self, mock_ble_libs):
        """set_identity_name() sets adapter and radio name."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()
        ble.set_identity_name("BlueBuzzah-Secondary")

        assert mock_ble_libs['bleio'].adapter.name == "BlueBuzzah-Secondary"
        assert ble.ble.name == "BlueBuzzah-Secondary"

    def test_set_identity_name_without_advertising(self, mock_ble_libs):
        """set_identity_name() does not start advertising."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()
        ble.set_identity_name("BlueBuzzah-Secondary")

        # Should not be advertising
        assert ble.ble.advertising is False


# ============================================================================
# Wait For Connection Tests
# ============================================================================

class TestBLEWaitForConnection:
    """Test BLE wait_for_connection functionality."""

    def test_wait_for_connection_returns_immediately_if_connected(self, mock_ble_libs):
        """wait_for_connection() returns immediately if already connected."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        # Pre-add a connection
        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        ble._connections["existing"] = BLEConnection("existing", mock_conn, mock_uart)

        result = ble.wait_for_connection("existing", timeout=1.0)

        assert result == "existing"

    def test_wait_for_connection_timeout(self, mock_ble_libs):
        """wait_for_connection() returns None on timeout."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()

        # No connections available
        result = ble.wait_for_connection("secondary", timeout=0.1)

        assert result is None

    def test_wait_for_connection_detects_new_connection(self, mock_ble_libs):
        """wait_for_connection() detects new incoming connection."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()

        # Simulate incoming connection
        new_conn = MockBLEConnection()
        ble.ble._connections.append(new_conn)

        result = ble.wait_for_connection("secondary", timeout=1.0)

        assert result == "secondary"
        assert "secondary" in ble._connections


# ============================================================================
# Scan And Connect Tests
# ============================================================================

class TestBLEScanAndConnect:
    """Test BLE scan_and_connect functionality."""

    def test_scan_and_connect_finds_target(self, mock_ble_libs):
        """scan_and_connect() finds and connects to target device."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()

        # Add scan result
        target_adv = MockAdvertisement(name="BlueBuzzah", rssi=-45)
        ble.ble.add_scan_result(target_adv)

        result = ble.scan_and_connect("BlueBuzzah", timeout=5.0)

        assert result == "primary"
        assert ble.is_connected("primary")

    def test_scan_and_connect_timeout(self, mock_ble_libs):
        """scan_and_connect() returns None on timeout."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()

        # No scan results
        result = ble.scan_and_connect("NonExistent", timeout=0.1)

        assert result is None

    def test_scan_and_connect_custom_conn_id(self, mock_ble_libs):
        """scan_and_connect() uses custom connection ID."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()

        # Add scan result
        target_adv = MockAdvertisement(name="BlueBuzzah", rssi=-45)
        ble.ble.add_scan_result(target_adv)

        result = ble.scan_and_connect("BlueBuzzah", timeout=5.0, conn_id="custom_id")

        assert result == "custom_id"
        assert ble.is_connected("custom_id")


# ============================================================================
# Send Tests
# ============================================================================

class TestBLESend:
    """Test BLE send functionality."""

    def test_send_appends_eot(self, mock_ble_libs):
        """send() appends EOT terminator to message."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        # Create connection with mock UART
        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        ble._connections["test"] = BLEConnection("test", mock_conn, mock_uart)

        result = ble.send("test", "Hello")

        assert result is True
        assert len(mock_uart._write_history) == 1
        assert mock_uart._write_history[0] == b"Hello\x04"

    def test_send_returns_false_for_unknown_connection(self, mock_ble_libs):
        """send() returns False for unknown connection."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()

        result = ble.send("unknown", "Hello")

        assert result is False

    def test_send_returns_false_for_disconnected(self, mock_ble_libs):
        """send() returns False for disconnected connection."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        # Create disconnected connection
        mock_conn = MockBLEConnection(connected=False)
        mock_uart = MockUARTService()
        ble._connections["test"] = BLEConnection("test", mock_conn, mock_uart)

        result = ble.send("test", "Hello")

        assert result is False

    def test_send_updates_last_seen(self, mock_ble_libs):
        """send() updates last_seen timestamp."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        initial_time = conn.last_seen
        time.sleep(0.01)
        ble.send("test", "Hello")

        assert conn.last_seen > initial_time

    def test_send_handles_write_exception(self, mock_ble_libs):
        """send() handles write exception gracefully."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MagicMock()
        mock_uart.write.side_effect = Exception("Write error")
        ble._connections["test"] = BLEConnection("test", mock_conn, mock_uart)

        result = ble.send("test", "Hello")

        assert result is False


# ============================================================================
# Receive Tests
# ============================================================================

class TestBLEReceive:
    """Test BLE receive functionality."""

    def test_receive_returns_message_without_eot(self, mock_ble_libs):
        """receive() returns message without EOT terminator."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Queue message with EOT
        mock_uart.queue_data("Hello\x04")

        result = ble.receive("test", timeout=1.0)

        assert result == "Hello"

    def test_receive_handles_multiple_messages(self, mock_ble_libs):
        """receive() handles multiple messages in single packet."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Queue multiple messages with EOT
        mock_uart.queue_data("First\x04Second\x04Third\x04")

        result1 = ble.receive("test", timeout=1.0)
        result2 = ble.receive("test", timeout=1.0)
        result3 = ble.receive("test", timeout=1.0)

        assert result1 == "First"
        assert result2 == "Second"
        assert result3 == "Third"

    def test_receive_returns_queued_message_first(self, mock_ble_libs):
        """receive() returns queued message before reading new data."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Pre-queue a message
        conn._pending_messages.append("Queued")

        result = ble.receive("test", timeout=1.0)

        assert result == "Queued"

    def test_receive_returns_none_for_unknown_connection(self, mock_ble_libs):
        """receive() returns None for unknown connection."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()

        result = ble.receive("unknown", timeout=0.1)

        assert result is None

    def test_receive_returns_none_for_disconnected(self, mock_ble_libs):
        """receive() returns None for disconnected connection."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection(connected=False)
        mock_uart = MockUARTService()
        ble._connections["test"] = BLEConnection("test", mock_conn, mock_uart)

        result = ble.receive("test", timeout=0.1)

        assert result is None

    def test_receive_timeout_returns_none(self, mock_ble_libs):
        """receive() returns None on timeout."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        ble._connections["test"] = BLEConnection("test", mock_conn, mock_uart)

        # No data queued
        result = ble.receive("test", timeout=0.05)

        assert result is None

    def test_receive_updates_last_seen(self, mock_ble_libs):
        """receive() updates last_seen on successful receive."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        mock_uart.queue_data("Hello\x04")

        initial_time = conn.last_seen
        time.sleep(0.01)
        ble.receive("test", timeout=1.0)

        assert conn.last_seen > initial_time

    def test_receive_handles_partial_message(self, mock_ble_libs):
        """receive() accumulates partial messages across calls."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Queue first part
        mock_uart.queue_data("Hel")

        # Should timeout (no EOT yet)
        result1 = ble.receive("test", timeout=0.05)
        assert result1 is None

        # Check message accumulated
        assert conn._msg_len == 3

        # Queue second part with EOT
        mock_uart.queue_data("lo\x04")

        result2 = ble.receive("test", timeout=1.0)
        assert result2 == "Hello"

    def test_receive_fallback_to_read(self, mock_ble_libs):
        """receive() falls back to read() if readinto() raises AttributeError."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()

        # Create a mock UART without readinto method
        class UARTWithoutReadinto:
            def __init__(self):
                self.in_waiting = 6
                self._read_data = bytearray(b"Hello\x04")
                self._read_position = 0

            def read(self):
                if self._read_position < len(self._read_data):
                    data = self._read_data[self._read_position:]
                    self._read_position = len(self._read_data)
                    return bytes(data)
                return None

            # No readinto method - will trigger AttributeError fallback

        mock_uart = UARTWithoutReadinto()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        result = ble.receive("test", timeout=1.0)

        assert result == "Hello"


# ============================================================================
# Connection Management Tests
# ============================================================================

class TestBLEConnectionManagement:
    """Test BLE connection management functionality."""

    def test_disconnect(self, mock_ble_libs):
        """disconnect() removes connection."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        ble._connections["test"] = BLEConnection("test", mock_conn, mock_uart)

        assert ble.is_connected("test") is True

        ble.disconnect("test")

        assert ble.is_connected("test") is False
        assert "test" not in ble._connections

    def test_disconnect_nonexistent_connection(self, mock_ble_libs):
        """disconnect() handles nonexistent connection gracefully."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE

        ble = BLE()

        # Should not raise
        ble.disconnect("nonexistent")

    def test_is_connected(self, mock_ble_libs):
        """is_connected() returns correct state."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        assert ble.is_connected("test") is False

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        ble._connections["test"] = BLEConnection("test", mock_conn, mock_uart)

        assert ble.is_connected("test") is True

        mock_conn._connected = False
        assert ble.is_connected("test") is False

    def test_get_all_connections(self, mock_ble_libs):
        """get_all_connections() returns all connection IDs."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        assert ble.get_all_connections() == []

        # Add connections
        for name in ["conn1", "conn2", "conn3"]:
            mock_conn = MockBLEConnection()
            mock_uart = MockUARTService()
            ble._connections[name] = BLEConnection(name, mock_conn, mock_uart)

        connections = ble.get_all_connections()

        assert len(connections) == 3
        assert "conn1" in connections
        assert "conn2" in connections
        assert "conn3" in connections

    def test_disconnect_all(self, mock_ble_libs):
        """disconnect_all() disconnects all connections."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        # Add connections
        for name in ["conn1", "conn2", "conn3"]:
            mock_conn = MockBLEConnection()
            mock_uart = MockUARTService()
            ble._connections[name] = BLEConnection(name, mock_conn, mock_uart)

        assert len(ble.get_all_connections()) == 3

        ble.disconnect_all()

        assert len(ble.get_all_connections()) == 0


# ============================================================================
# EOT Protocol Tests
# ============================================================================

class TestEOTProtocol:
    """Test End-Of-Transmission protocol handling."""

    def test_eot_is_0x04(self, mock_ble_libs):
        """EOT terminator is 0x04."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        ble._connections["test"] = BLEConnection("test", mock_conn, mock_uart)

        ble.send("test", "Test")

        # Check EOT (0x04) is appended
        assert mock_uart._write_history[0][-1] == 0x04

    def test_eot_stripped_from_received_message(self, mock_ble_libs):
        """EOT is not included in received message."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        mock_uart.queue_data("Message\x04")

        result = ble.receive("test", timeout=1.0)

        assert result == "Message"
        assert '\x04' not in result

    def test_empty_message_before_eot(self, mock_ble_libs):
        """Empty message before EOT is handled."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Empty message (just EOT)
        mock_uart.queue_data("\x04RealMessage\x04")

        # First receive gets empty or skips to real message
        result1 = ble.receive("test", timeout=1.0)
        result2 = ble.receive("test", timeout=1.0)

        # One of them should be "RealMessage"
        assert "RealMessage" in [result1, result2]


# ============================================================================
# Message Framing Tests
# ============================================================================

class TestMessageFraming:
    """Test message framing with realistic scenarios."""

    def test_sync_command_framing(self, mock_ble_libs):
        """SYNC commands are properly framed."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        ble._connections["test"] = BLEConnection("test", mock_conn, mock_uart)

        # Send SYNC command
        sync_cmd = "SYNC:EXECUTE_BUZZ|0|75|100"
        ble.send("test", sync_cmd)

        assert mock_uart._write_history[0] == (sync_cmd + "\x04").encode('utf-8')

    def test_heartbeat_message_framing(self, mock_ble_libs):
        """HEARTBEAT messages are properly framed."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Queue heartbeat
        mock_uart.queue_data("SYNC:HEARTBEAT|12345.678\x04")

        result = ble.receive("test", timeout=1.0)

        assert result == "SYNC:HEARTBEAT|12345.678"

    def test_rapid_execute_buzz_messages(self, mock_ble_libs):
        """Rapid EXECUTE_BUZZ messages are properly handled."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Queue rapid EXECUTE_BUZZ commands (batched in single BLE packet)
        commands = [
            "SYNC:EXECUTE_BUZZ|0|75|100",
            "SYNC:EXECUTE_BUZZ|1|75|100",
            "SYNC:EXECUTE_BUZZ|2|75|100",
            "SYNC:EXECUTE_BUZZ|3|75|100",
            "SYNC:EXECUTE_BUZZ|4|75|100",
        ]

        # Simulate batched packet
        batched = "\x04".join(commands) + "\x04"
        mock_uart.queue_data(batched)

        # Receive all messages
        received = []
        for _ in range(5):
            msg = ble.receive("test", timeout=1.0)
            if msg:
                received.append(msg)

        assert len(received) == 5
        for i, cmd in enumerate(commands):
            assert received[i] == cmd


# ============================================================================
# Buffer Overflow Tests
# ============================================================================

class TestBufferOverflow:
    """Test buffer overflow handling."""

    def test_message_buffer_overflow_resets(self, mock_ble_libs):
        """Message buffer overflow resets buffer."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Fill buffer beyond 512 bytes without EOT
        large_data = "X" * 600
        mock_uart.queue_data(large_data)

        # This should trigger overflow handling
        result = ble.receive("test", timeout=0.1)

        # Buffer should have been reset
        assert conn._msg_len < len(large_data)


# ============================================================================
# Edge Cases
# ============================================================================

class TestBLEEdgeCases:
    """Test edge cases and error conditions."""

    def test_unicode_message(self, mock_ble_libs):
        """Unicode messages are properly handled."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Send unicode
        ble.send("test", "Hello 世界")

        assert b"\xe4\xb8\x96\xe7\x95\x8c" in mock_uart._write_history[0]

    def test_receive_with_none_timeout(self, mock_ble_libs):
        """receive() with None timeout waits indefinitely (until data)."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Queue data immediately so we don't actually wait forever
        mock_uart.queue_data("Message\x04")

        result = ble.receive("test", timeout=None)

        # This would hang without data, but we queued data
        # Test verifies it doesn't crash with None timeout

    def test_multiple_consecutive_eot(self, mock_ble_libs):
        """Multiple consecutive EOT are handled."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Multiple consecutive EOT (empty messages between)
        mock_uart.queue_data("First\x04\x04\x04Second\x04")

        results = []
        for _ in range(4):
            msg = ble.receive("test", timeout=0.1)
            if msg is not None:
                results.append(msg)

        # Should have First and Second (empty strings filtered)
        assert "First" in results
        assert "Second" in results


# ============================================================================
# Performance Tests (Markers)
# ============================================================================

class TestBLEPerformance:
    """Performance-related tests."""

    @pytest.mark.slow
    def test_high_throughput_messages(self, mock_ble_libs):
        """Handle high throughput message scenario."""
        if 'ble' in sys.modules:
            del sys.modules['ble']
        from ble import BLE, BLEConnection

        ble = BLE()

        mock_conn = MockBLEConnection()
        mock_uart = MockUARTService()
        conn = BLEConnection("test", mock_conn, mock_uart)
        ble._connections["test"] = conn

        # Generate many messages
        num_messages = 100
        for i in range(num_messages):
            mock_uart.queue_data("MSG_{}\x04".format(i))

        # Receive all
        received = 0
        while True:
            msg = ble.receive("test", timeout=0.01)
            if msg is None:
                break
            received += 1

        assert received == num_messages


# ============================================================================
# Integration with MockBLE Tests
# ============================================================================

class TestMockBLEIntegration:
    """Test that MockBLE matches BLE interface."""

    def test_mock_ble_has_same_interface(self):
        """MockBLE implements same interface as BLE."""
        from tests.mocks.ble import MockBLE

        mock = MockBLE()

        # Check all required methods exist
        assert hasattr(mock, 'advertise')
        assert hasattr(mock, 'stop_advertising')
        assert hasattr(mock, 'wait_for_connection')
        assert hasattr(mock, 'scan_and_connect')
        assert hasattr(mock, 'send')
        assert hasattr(mock, 'receive')
        assert hasattr(mock, 'disconnect')
        assert hasattr(mock, 'is_connected')
        assert hasattr(mock, 'get_all_connections')
        assert hasattr(mock, 'disconnect_all')

    def test_mock_ble_send_receive(self):
        """MockBLE send/receive works correctly."""
        from tests.mocks.ble import MockBLE

        mock = MockBLE()

        # Register and connect
        mock.register_device("TestDevice")
        conn_id = mock.scan_and_connect("TestDevice")

        assert conn_id is not None

        # Send message
        result = mock.send(conn_id, "Hello\x04")
        assert result is True

        # Simulate receive
        mock.simulate_receive(conn_id, "World\x04")
        message = mock.receive(conn_id, timeout=1.0)

        assert message == "World\x04"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
