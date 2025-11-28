"""
Mock BLE Components
===================

Mock implementations of BLE communication for testing without physical hardware.
Updated to match consolidated BLE class from src/ble.py.

Classes:
    MockBLEConnection: Mock BLE connection with data transfer simulation
    MockBLE: Mock BLE service matching src/ble.py interface

Version: 2.0.0 (Consolidated)
"""

from typing import Dict, List, Optional, Any
from dataclasses import dataclass, field
import time
from collections import deque


@dataclass
class MockAdvertisement:
    """Mock BLE advertisement."""
    complete_name: str
    rssi: int = -50
    service_uuids: List[str] = field(default_factory=list)


class MockBLEConnection:
    """
    Mock BLE connection for testing.

    Simulates a BLE connection ID returned by the consolidated BLE class.
    The connection ID is a string that maps to actual connection state.

    Features:
        - Connection state simulation
        - Data send/receive queues
        - RSSI simulation
        - Disconnection simulation

    Usage:
        conn = MockBLEConnection("BlueBuzzah")
        assert conn.is_connected()

        # Queue data for receiving
        conn.queue_receive("PONG\n\x04")
        data = conn.read_message()
        assert data == "PONG\n\x04"
    """

    def __init__(self, name: str, rssi: int = -50):
        """
        Initialize mock connection.

        Args:
            name: Connection identifier
            rssi: Signal strength (dBm)
        """
        self.name = name
        self.rssi = rssi
        self.connected = True
        self.connected_at = time.monotonic()
        self.last_seen = time.monotonic()

        # Data queues (string-based to match consolidated BLE)
        self.receive_queue: deque = deque()
        self.send_history: List[str] = []

        # Configuration
        self.disconnect_on_send = False
        self.fail_on_send = False
        self.send_delay_sec = 0.0

    def write(self, message: str) -> None:
        """
        Write message to connection.

        Args:
            message: String message to send

        Raises:
            RuntimeError: If connection lost or send fails
        """
        if not self.connected:
            raise RuntimeError("Connection lost")

        if self.fail_on_send:
            raise RuntimeError("Send failure")

        # Simulate delay
        if self.send_delay_sec > 0:
            time.sleep(self.send_delay_sec)

        # Track send
        self.send_history.append(message)
        self.last_seen = time.monotonic()

        # Simulate disconnection if configured
        if self.disconnect_on_send:
            self.connected = False

    def read_message(self, timeout: Optional[float] = None) -> Optional[str]:
        """
        Read message from connection.

        Args:
            timeout: Read timeout in seconds

        Returns:
            Received message string or None if timeout/no data
        """
        if not self.connected:
            return None

        if timeout is None or timeout == 0:
            # Non-blocking read
            if self.receive_queue:
                message = self.receive_queue.popleft()
                self.last_seen = time.monotonic()
                return message
            return None
        else:
            # Timeout-based read
            start_time = time.monotonic()
            while time.monotonic() - start_time < timeout:
                if self.receive_queue:
                    message = self.receive_queue.popleft()
                    self.last_seen = time.monotonic()
                    return message
                time.sleep(0.01)
            return None

    def queue_receive(self, message: str) -> None:
        """Queue message for next read operation."""
        self.receive_queue.append(message)

    def is_connected(self) -> bool:
        """Check if connection is active."""
        return self.connected

    def update_last_seen(self) -> None:
        """Update last communication timestamp."""
        self.last_seen = time.monotonic()

    def time_since_last_seen(self) -> float:
        """Seconds since last communication."""
        return time.monotonic() - self.last_seen

    def disconnect(self) -> None:
        """Disconnect from device."""
        self.connected = False

    def get_sent_messages(self) -> List[str]:
        """Get all sent messages."""
        return self.send_history.copy()

    def get_last_sent(self) -> Optional[str]:
        """Get most recently sent message."""
        return self.send_history[-1] if self.send_history else None

    def clear_history(self) -> None:
        """Clear all transmission history."""
        self.send_history.clear()


class MockBLE:
    """
    Mock BLE class matching consolidated src/ble.py interface.

    This mock simulates the BLE communication layer for testing without
    physical BLE hardware.

    Features:
        - Simulated advertising
        - Device discovery simulation
        - Connection establishment (returns connection IDs)
        - Multiple connection support
        - String-based message send/receive
        - Device registry for testing

    Usage:
        ble = MockBLE()

        # Advertise
        ble.advertise("BlueBuzzah")
        assert ble._advertising

        # Scan and connect
        ble.register_device("BlueBuzzah", rssi=-45)
        conn_id = ble.scan_and_connect("BlueBuzzah", timeout=5.0)
        assert conn_id is not None
        assert ble.is_connected(conn_id)

        # Send/receive messages
        ble.send(conn_id, "PING\n\x04")
        ble.simulate_receive(conn_id, "PONG\n\x04")
        message = ble.receive(conn_id, timeout=1.0)
        assert message == "PONG\n\x04"
    """

    def __init__(self):
        """Initialize mock BLE."""
        # Device registry for testing
        self.registered_devices: Dict[str, MockAdvertisement] = {}

        # Connection tracking (conn_id -> MockBLEConnection)
        self._connections: Dict[str, MockBLEConnection] = {}

        # Advertising state
        self._advertising = False
        self.advertised_name: Optional[str] = None

        # Scanning state
        self._scanning = False

        # Configuration
        self.fail_on_connect: Optional[str] = None  # Device name to fail on
        self.scan_timeout_sec = 0.0  # Simulated scan delay

    def advertise(self, name: str) -> None:
        """
        Start BLE advertising with specified device name.

        Args:
            name: Device name to advertise
        """
        self._advertising = True
        self.advertised_name = name

    def stop_advertising(self) -> None:
        """Stop BLE advertising."""
        self._advertising = False
        self.advertised_name = None

    def wait_for_connection(self, conn_id: str, timeout: Optional[float] = None) -> Optional[str]:
        """
        Wait for incoming connection (for PRIMARY device).

        Args:
            conn_id: Connection identifier
            timeout: Timeout in seconds

        Returns:
            Connection ID if successful, None if timeout
        """
        # Simulate waiting (in mock, we create immediately if registered)
        if self.scan_timeout_sec > 0 and timeout:
            time.sleep(min(self.scan_timeout_sec, timeout))

        # Check if device is registered (simulating incoming connection)
        # In mock, we use registered_devices as "devices trying to connect"
        for device_name, advertisement in self.registered_devices.items():
            if device_name not in self._connections:
                # Create new connection
                connection = MockBLEConnection(conn_id, rssi=advertisement.rssi)
                self._connections[conn_id] = connection
                return conn_id

        return None

    def scan_and_connect(
        self, target: str, timeout: float = 30.0, conn_id: str = "primary"
    ) -> Optional[str]:
        """
        Scan for target device and establish connection.

        Args:
            target: Target device name to connect to
            timeout: Scan timeout in seconds
            conn_id: Connection identifier (default: "primary")

        Returns:
            Connection ID if successful, None if timeout or failure
        """
        # Simulate scan delay
        if self.scan_timeout_sec > 0:
            time.sleep(min(self.scan_timeout_sec, timeout))

        # Check if device is registered
        if target not in self.registered_devices:
            return None

        # Check for simulated failure
        if self.fail_on_connect == target:
            return None

        # Create connection
        advertisement = self.registered_devices[target]
        connection = MockBLEConnection(conn_id, rssi=advertisement.rssi)

        # Track connection
        self._connections[conn_id] = connection

        return conn_id

    def send(self, conn_id: str, message: str) -> bool:
        """
        Send message over connection.

        Args:
            conn_id: Connection identifier
            message: Message string to send

        Returns:
            True if send successful, False otherwise
        """
        if conn_id not in self._connections:
            return False

        conn = self._connections[conn_id]

        if not conn.is_connected():
            return False

        try:
            conn.write(message)
            return True
        except Exception:
            return False

    def receive(self, conn_id: str, timeout: Optional[float] = None) -> Optional[str]:
        """
        Receive message from connection.

        Args:
            conn_id: Connection identifier
            timeout: Receive timeout in seconds

        Returns:
            Message string if received, None if timeout
        """
        if conn_id not in self._connections:
            return None

        conn = self._connections[conn_id]

        if not conn.is_connected():
            return None

        return conn.read_message(timeout=timeout)

    def disconnect(self, conn_id: str) -> None:
        """
        Disconnect from device.

        Args:
            conn_id: Connection identifier
        """
        if conn_id in self._connections:
            conn = self._connections[conn_id]
            conn.disconnect()
            del self._connections[conn_id]

    def is_connected(self, conn_id: str) -> bool:
        """
        Check if connection is still active.

        Args:
            conn_id: Connection identifier

        Returns:
            True if connected, False otherwise
        """
        if conn_id not in self._connections:
            return False

        return self._connections[conn_id].is_connected()

    def get_rssi(self, conn_id: str) -> Optional[int]:
        """
        Get RSSI (signal strength) for connection.

        Args:
            conn_id: Connection identifier

        Returns:
            RSSI in dBm, or None if unavailable
        """
        if conn_id in self._connections:
            return self._connections[conn_id].rssi
        return None

    def get_all_connections(self) -> List[str]:
        """
        Get list of all connection IDs.

        Returns:
            List of connection identifiers
        """
        return list(self._connections.keys())

    def disconnect_all(self) -> None:
        """Disconnect all connections."""
        for conn_id in list(self._connections.keys()):
            self.disconnect(conn_id)

    # Test helper methods

    def register_device(
        self, name: str, rssi: int = -50, service_uuids: Optional[List[str]] = None
    ) -> None:
        """
        Register a device for discovery during scanning.

        Args:
            name: Device name
            rssi: Signal strength (dBm)
            service_uuids: List of service UUIDs
        """
        self.registered_devices[name] = MockAdvertisement(
            complete_name=name,
            rssi=rssi,
            service_uuids=service_uuids or []
        )

    def unregister_device(self, name: str) -> None:
        """Remove device from registry."""
        self.registered_devices.pop(name, None)

    def simulate_receive(self, conn_id: str, message: str) -> None:
        """
        Simulate receiving message on connection.

        Args:
            conn_id: Connection identifier
            message: Message to queue for receiving
        """
        if conn_id in self._connections:
            self._connections[conn_id].queue_receive(message)

    def get_connection(self, conn_id: str) -> Optional[MockBLEConnection]:
        """Get connection by ID."""
        return self._connections.get(conn_id)
