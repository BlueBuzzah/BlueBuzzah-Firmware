"""
BLE - Minimal Working Implementation
"""

import time

# Import debug flag for conditional logging (CRITICAL-003 fix)
from core.constants import DEBUG_ENABLED

try:
    from adafruit_ble import BLERadio
    from adafruit_ble.advertising.standard import ProvideServicesAdvertisement
    from adafruit_ble.advertising import Advertisement, LazyObjectField
    from adafruit_ble.services.nordic import UARTService
    import _bleio
except ImportError:
    BLERadio = None
    ProvideServicesAdvertisement = None
    Advertisement = None
    LazyObjectField = None
    UARTService = None
    _bleio = None


class BLEConnection:
    def __init__(self, name, ble_connection, uart_service):
        self.name = name
        self.ble_connection = ble_connection
        self.uart = uart_service
        self.connected_at = time.monotonic()
        self.last_seen = time.monotonic()

        # MEM-002 FIX: Pre-allocated buffers to eliminate allocations in hot path
        self._rx_buffer = bytearray(256)    # Read buffer for uart.readinto()
        self._msg_buffer = bytearray(512)   # Message accumulation buffer
        self._msg_len = 0                    # Current message length

        # EOT-FIX: Queue for complete messages when multiple arrive in one BLE packet
        # BLE 5.0 high throughput can batch multiple messages together
        self._pending_messages = []

    def update_last_seen(self):
        self.last_seen = time.monotonic()

    def is_connected(self):
        try:
            return self.ble_connection.connected
        except:
            return False


class BLE:
    def __init__(self):
        if BLERadio is None:
            raise RuntimeError("BLE libraries not available")

        print("[BLE] Initializing BLE radio...")

        # Set default adapter name before creating BLERadio
        if _bleio:
            try:
                _bleio.adapter.name = "BlueBuzzah"
                print("[BLE] Set default adapter name to: BlueBuzzah")
            except Exception as e:
                print("[BLE] WARNING: Could not set default adapter name: {}".format(e))

        self.ble = BLERadio()
        self.uart_service = UARTService()
        self.advertisement = ProvideServicesAdvertisement(self.uart_service)
        self._connections = {}
        print("[BLE] BLE radio initialized")

        # Format MAC address
        mac_parts = []
        for b in self.ble.address_bytes:
            mac_parts.append("{:02x}".format(b))
        mac_addr = ":".join(mac_parts)
        print("[BLE] MAC address: {}".format(mac_addr))

    def advertise(self, name):
        print("[BLE] Setting name to '{}'".format(name))

        # Stop any existing advertising
        try:
            self.ble.stop_advertising()
        except:
            pass

        # CRITICAL: Set the adapter name BEFORE creating anything else
        # This affects the default advertising name
        if _bleio:
            try:
                _bleio.adapter.name = name
                print("[BLE] Set adapter name to: {}".format(_bleio.adapter.name))
            except Exception as e:
                print("[BLE] WARNING: Could not set adapter name: {}".format(e))

        # BLE-001 FIX: Reuse existing BLERadio instance instead of recreating
        # Creating a new BLERadio() wastes ~20-40KB RAM and may cause issues
        self.ble.name = name

        # Create UART service (must be recreated for new advertisement)
        self.uart_service = UARTService()

        # Create advertisement with the service
        self.advertisement = ProvideServicesAdvertisement(self.uart_service)

        # CRITICAL: Manually inject the complete_name into the advertisement data buffer
        # The Advertisement class stores data in an internal buffer that gets transmitted
        try:
            # Build the complete_name field: Type (0x09) + Length + Name bytes
            name_bytes = name.encode('utf-8')
            name_len = len(name_bytes) + 1  # +1 for the type byte
            complete_name_field = bytes([name_len, 0x09]) + name_bytes

            # Append to the advertisement's internal data buffer
            if hasattr(self.advertisement, '_data'):
                # Get current data and append complete_name field
                current_data = bytes(self.advertisement._data)
                self.advertisement._data = bytearray(current_data + complete_name_field)
                print("[BLE] Injected complete_name '{}' into advertisement data".format(name))
            else:
                print("[BLE] WARNING: Cannot access advertisement _data buffer")
        except Exception as e:
            print("[BLE] WARNING: Could not inject advertisement complete_name: {}".format(e))

        print("[BLE] Starting advertising...")
        self.ble.start_advertising(self.advertisement)
        time.sleep(0.5)

        print("[BLE] *** ADVERTISING STARTED ***")
        print("[BLE] Radio name: {}".format(self.ble.name))
        try:
            print("[BLE] Advertisement name: {}".format(self.advertisement.complete_name))
        except:
            print("[BLE] Advertisement name: (unable to read)")
        print("[BLE] Radio advertising: {}".format(self.ble.advertising))
        print("[BLE] Look for '{}' in your BLE scanner NOW".format(name))

    def stop_advertising(self):
        if self.ble.advertising:
            self.ble.stop_advertising()
            print("[BLE] Stopped advertising")

    def set_identity_name(self, name):
        """
        Set BLE adapter identity name without advertising.

        This is used for SECONDARY devices that need to identify themselves
        but don't advertise (only scan and connect).

        Args:
            name: BLE identity name (e.g., "BlueBuzzah-Secondary")
        """
        print("[BLE] Setting identity name to '{}'".format(name))

        if _bleio:
            try:
                _bleio.adapter.name = name
                print("[BLE] Set adapter identity name to: {}".format(_bleio.adapter.name))
            except Exception as e:
                print("[BLE] WARNING: Could not set adapter identity name: {}".format(e))

        try:
            self.ble.name = name
            print("[BLE] Set radio identity name to: {}".format(self.ble.name))
        except Exception as e:
            print("[BLE] WARNING: Could not set radio identity name: {}".format(e))

    def wait_for_connection(self, conn_id, timeout=None):
        # If already connected, return immediately
        if conn_id in self._connections:
            return conn_id

        # Print waiting message only once
        if not hasattr(self, '_waiting_for'):
            self._waiting_for = set()

        if conn_id not in self._waiting_for:
            print(f"[BLE] Waiting for '{conn_id}'...")
            self._waiting_for.add(conn_id)

        start_time = time.monotonic()
        existing = set(c.ble_connection for c in self._connections.values())

        while True:
            if timeout and (time.monotonic() - start_time > timeout):
                return None

            # Check for NEW connections
            if self.ble.connected:
                for conn in self.ble.connections:
                    if conn not in existing:
                        # Wait for connection to fully establish before accessing services
                        # This delay allows BLE stack to exchange service information
                        print(f"[BLE] New connection detected, waiting 200ms for service discovery...")
                        time.sleep(0.2)

                        # CRITICAL FIX: Access UARTService through the connection object
                        # When acting as a peripheral (server), we must get the UART service
                        # from the connection to access the correct TX/RX characteristics
                        # that the central (client) is using
                        try:
                            uart = conn[UARTService]
                            print(f"[BLE] *** CRITICAL: Using connection's UART service for '{conn_id}' ***")
                            print(f"[BLE] UART object: {uart}")
                            print(f"[BLE] UART has read(): {hasattr(uart, 'read')}")
                            print(f"[BLE] UART has write(): {hasattr(uart, 'write')}")
                            print(f"[BLE] UART has in_waiting: {hasattr(uart, 'in_waiting')}")
                        except KeyError:
                            print(f"[BLE] ERROR: Could not get UARTService from connection")
                            return None

                        ble_conn = BLEConnection(conn_id, conn, uart)
                        self._connections[conn_id] = ble_conn
                        self._waiting_for.discard(conn_id)
                        print(f"[BLE] *** {conn_id} CONNECTED *** (total: {len(self.ble.connections)})")
                        return conn_id

            time.sleep(0.01)

    def scan_and_connect(self, target, timeout=30.0, conn_id="primary"):
        start_time = time.monotonic()
        print(f"[BLE] Scanning for '{target}' (unfiltered)...")

        try:
            while time.monotonic() - start_time < timeout:
                # UNFILTERED scan - finds all BLE devices
                # This is necessary because injecting the name into PRIMARY's advertisement
                # breaks the ProvideServicesAdvertisement filter
                for advertisement in self.ble.start_scan(timeout=5.0):
                    # Check if name matches
                    if advertisement.complete_name == target:
                        print(f"[BLE] Found '{target}', connecting...")
                        self.ble.stop_scan()

                        try:
                            # Attempt connection
                            connection = self.ble.connect(advertisement)

                            # Wait for connection to be fully established
                            connect_timeout = time.monotonic() + 5.0  # 5 second timeout
                            while not connection.connected and time.monotonic() < connect_timeout:
                                time.sleep(0.1)

                            if not connection.connected:
                                print(f"[BLE] WARNING: Connection to '{target}' failed to establish")
                                continue

                            # Additional delay for service discovery
                            # This allows BLE stack to fully exchange service information
                            print(f"[BLE] Connected to '{target}', waiting for service discovery...")
                            time.sleep(0.2)

                            uart = connection[UARTService]
                            ble_conn = BLEConnection(conn_id, connection, uart)
                            self._connections[conn_id] = ble_conn
                            print(f"[BLE] Connected to '{target}'")
                            return conn_id
                        except KeyError:
                            print(f"[BLE] WARNING: '{target}' found but no UARTService available")
                            return None
                        except Exception as e:
                            print(f"[BLE] Connection failed: {e}")
                            return None

                time.sleep(0.1)

            print(f"[BLE] Scan timeout")
            return None
        finally:
            try:
                self.ble.stop_scan()
            except:
                pass

    def send(self, conn_id, message):
        """
        Send message with EOT terminator.

        Appends 0x04 (EOT) to mark end of message for reliable framing.
        """
        if conn_id not in self._connections:
            print(f"[BLE] SEND FAILED: Connection '{conn_id}' not found in _connections")
            print(f"[BLE] Available connections: {list(self._connections.keys())}")
            return False

        conn = self._connections[conn_id]
        if not conn.is_connected():
            print(f"[BLE] SEND FAILED: Connection '{conn_id}' not connected")
            return False

        try:
            # Append EOT (0x04) terminator
            message_with_eot = message + '\x04'
            data_bytes = message_with_eot.encode('utf-8')

            # CRITICAL-003 FIX: Wrap verbose debug logging with DEBUG_ENABLED
            if DEBUG_ENABLED:
                print(f"[BLE] *** SEND DEBUG for '{conn_id}' ***")
                print(f"[BLE] UART object: {conn.uart}")
                print(f"[BLE] UART object type: {type(conn.uart)}")
                print(f"[BLE] Message (first 100 chars): {message[:100]}...")
                print(f"[BLE] Data bytes length: {len(data_bytes)}")
                print(f"[BLE] Data bytes (first 50): {data_bytes[:50]}")

            # Call write() and capture return value if any
            if DEBUG_ENABLED:
                print(f"[BLE] Calling conn.uart.write()...")
            write_result = conn.uart.write(data_bytes)
            if DEBUG_ENABLED:
                print(f"[BLE] write() returned: {write_result}")

            # Check if there's a flush() method
            if hasattr(conn.uart, 'flush'):
                if DEBUG_ENABLED:
                    print(f"[BLE] Calling conn.uart.flush()...")
                conn.uart.flush()
                if DEBUG_ENABLED:
                    print(f"[BLE] flush() completed")
            elif DEBUG_ENABLED:
                print(f"[BLE] No flush() method available")

            # Check if there's a reset_input_buffer() method
            if DEBUG_ENABLED and hasattr(conn.uart, 'reset_input_buffer'):
                print(f"[BLE] reset_input_buffer() is available")

            conn.update_last_seen()
            if DEBUG_ENABLED:
                print(f"[BLE] *** SEND SUCCESS to '{conn_id}' ***")
            return True
        except Exception as e:
            print(f"[BLE] SEND EXCEPTION on '{conn_id}': {e}")
            import traceback
            traceback.print_exception(e, e, e.__traceback__)
            return False

    def receive(self, conn_id, timeout=None):
        """
        Receive message with EOT framing.

        Buffers incoming bytes until EOT (0x04) is received, then returns
        complete message without the EOT terminator.

        EOT-FIX: Properly handles multiple messages in single BLE packet.
        When multiple EOT-terminated messages arrive together (e.g., during
        high-frequency EXECUTE_BUZZ commands), all messages are queued and
        returned one at a time on subsequent calls.

        Returns:
            Complete message (str) without EOT, or None if timeout/error
        """
        if conn_id not in self._connections:
            print(f"[BLE] receive(): conn_id '{conn_id}' NOT in _connections")
            return None

        conn = self._connections[conn_id]
        if not conn.is_connected():
            print(f"[BLE] receive(): conn_id '{conn_id}' is NOT connected")
            return None

        try:
            # EOT-FIX: Return queued message first (from previous multi-message read)
            if conn._pending_messages:
                message = conn._pending_messages.pop(0)
                conn.update_last_seen()
                return message

            # CRITICAL-003 FIX: Removed high-frequency debug logging from hot path
            # MEM-002 FIX: Use pre-allocated buffers with readinto() to eliminate allocations

            # Get connection's pre-allocated buffers
            rx_buf = conn._rx_buffer
            msg_buf = conn._msg_buffer
            msg_len = conn._msg_len

            start_time = time.monotonic()
            poll_count = 0

            while True:
                if timeout and (time.monotonic() - start_time > timeout):
                    # Timeout - no complete message received
                    return None

                poll_count += 1
                in_waiting = conn.uart.in_waiting

                if in_waiting > 0:
                    # MEM-002 FIX: Use readinto() instead of read() to reuse buffer (NO ALLOCATION)
                    try:
                        bytes_read = conn.uart.readinto(rx_buf)
                    except AttributeError:
                        # Fallback: If readinto() not available, use read()
                        data = conn.uart.read()
                        if data:
                            bytes_read = len(data)
                            rx_buf[:bytes_read] = data
                        else:
                            bytes_read = 0

                    if bytes_read and bytes_read > 0:
                        # EOT-FIX: Process ALL bytes, handling multiple EOT terminators
                        for i in range(bytes_read):
                            byte = rx_buf[i]
                            if byte == 0x04:  # EOT terminator
                                # Message complete - queue it and continue processing
                                if msg_len > 0:
                                    message = msg_buf[:msg_len].decode('utf-8')
                                    conn._pending_messages.append(message)
                                msg_len = 0  # Reset for next message in same packet
                            else:
                                # Accumulate byte
                                if msg_len < len(msg_buf):
                                    msg_buf[msg_len] = byte
                                    msg_len += 1
                                else:
                                    # Buffer overflow - reset
                                    print(f"[BLE] WARNING: Message buffer overflow for '{conn_id}'")
                                    msg_len = 0

                        # Update stored message length
                        conn._msg_len = msg_len

                        # EOT-FIX: Return first queued message (if any complete messages found)
                        if conn._pending_messages:
                            message = conn._pending_messages.pop(0)
                            conn.update_last_seen()
                            return message

                time.sleep(0.01)
        except Exception as e:
            # Log decode errors if DEBUG enabled
            print(f"[BLE] Receive error on '{conn_id}': {e}")
            import traceback
            traceback.print_exception(e, e, e.__traceback__)
            return None

    def disconnect(self, conn_id):
        if conn_id in self._connections:
            try:
                self._connections[conn_id].ble_connection.disconnect()
            except:
                pass
            del self._connections[conn_id]

    def is_connected(self, conn_id):
        if conn_id not in self._connections:
            return False
        return self._connections[conn_id].is_connected()

    def get_all_connections(self):
        return list(self._connections.keys())

    def disconnect_all(self):
        for conn_id in list(self._connections.keys()):
            self.disconnect(conn_id)
