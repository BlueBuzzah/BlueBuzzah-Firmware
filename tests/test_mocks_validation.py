"""
Mock and Fixture Validation Tests
==================================

Comprehensive tests to validate that all mock implementations and fixtures
work correctly and provide realistic behavior simulation.

Test Categories:
    - Mock hardware validation
    - Mock BLE validation
    - Configuration fixture validation
    - Profile fixture validation
    - Pytest fixture validation

Version: 2.0.0
"""

import pytest
import time
from typing import List

from tests.mocks.hardware import (
    MockHapticController,
    MockBatteryMonitor,
    MockI2CMultiplexer,
    MockNeoPixelLED,
)
from tests.mocks.ble import (
    MockBLE,
    MockBLEConnection,
)

from core.types import ActuatorType, BatteryStatus, DeviceRole


# ============================================================================
# Mock Hardware Validation Tests
# ============================================================================

class TestMockHapticController:
    """Validate MockHapticController behavior."""

    def test_activation_tracking(self):
        """Test that activations are properly tracked."""
        haptic = MockHapticController()

        # Activate multiple fingers
        haptic.activate(finger=0, amplitude=75)
        haptic.activate(finger=1, amplitude=50)
        haptic.activate(finger=2, amplitude=100)

        # Verify tracking
        assert haptic.get_activation_count() == 3
        assert haptic.get_activation_count(finger=0) == 1
        assert haptic.was_finger_activated(0)
        assert haptic.was_finger_activated(1)
        assert haptic.was_finger_activated(2)

        # Verify last activation
        last = haptic.get_last_activation()
        assert last.finger == 2
        assert last.amplitude == 100

    def test_deactivation_tracking(self):
        """Test that deactivations are properly tracked."""
        haptic = MockHapticController()

        # Activate and deactivate
        haptic.activate(finger=0, amplitude=75)
        assert haptic.is_active(0)

        haptic.deactivate(finger=0)
        assert not haptic.is_active(0)
        assert haptic.was_finger_deactivated(0)

    def test_frequency_setting(self):
        """Test frequency configuration."""
        haptic = MockHapticController()

        # Set frequencies
        haptic.set_frequency(finger=0, frequency=175)
        haptic.set_frequency(finger=1, frequency=200)

        # Verify
        assert len(haptic.frequency_changes) == 2
        assert haptic.frequencies[0] == 175
        assert haptic.frequencies[1] == 200

    def test_validation_errors(self):
        """Test that validation errors are raised."""
        haptic = MockHapticController()

        # Invalid finger
        with pytest.raises(ValueError, match="out of range"):
            haptic.activate(finger=5, amplitude=75)

        # Invalid amplitude
        with pytest.raises(ValueError, match="out of range"):
            haptic.activate(finger=0, amplitude=150)

    def test_failure_simulation(self):
        """Test simulated hardware failures."""
        haptic = MockHapticController()
        haptic.fail_on_activate = 0

        # Should raise RuntimeError
        with pytest.raises(RuntimeError, match="Mock activation failure"):
            haptic.activate(finger=0, amplitude=75)


class TestMockBatteryMonitor:
    """Validate MockBatteryMonitor behavior."""

    def test_voltage_reading(self):
        """Test voltage reading."""
        battery = MockBatteryMonitor(voltage=3.85)

        voltage = battery.read_voltage()
        assert voltage == 3.85
        assert battery.read_count == 1

    def test_percentage_calculation(self):
        """Test battery percentage calculation."""
        battery = MockBatteryMonitor()

        # Full charge
        battery.set_voltage(4.2)
        assert battery.get_percentage() == 100

        # Half charge
        battery.set_voltage(3.6)
        percentage = battery.get_percentage()
        assert 40 <= percentage <= 60

        # Empty
        battery.set_voltage(3.0)
        assert battery.get_percentage() == 0

    def test_status_detection(self):
        """Test battery status detection."""
        battery = MockBatteryMonitor()

        # Normal status
        battery.set_voltage(3.85)
        status = battery.get_status()
        assert status.status == "OK"
        assert not status.is_low
        assert not status.is_critical

        # Low battery
        battery.set_voltage(3.35)
        status = battery.get_status()
        assert status.status == "LOW"
        assert status.is_low
        assert not status.is_critical

        # Critical battery
        battery.set_voltage(3.25)
        status = battery.get_status()
        assert status.status == "CRITICAL"
        assert status.is_low
        assert status.is_critical

    def test_drain_simulation(self):
        """Test battery drain simulation."""
        battery = MockBatteryMonitor(voltage=4.0)

        # Enable drain
        battery.enable_drain(rate_per_sec=0.1)  # 0.1V/s

        # Wait briefly
        time.sleep(0.2)

        # Read voltage (should have dropped)
        voltage = battery.read_voltage()
        assert voltage < 4.0
        assert voltage > 3.9  # Approximately 0.02V drop


class TestMockI2CMultiplexer:
    """Validate MockI2CMultiplexer behavior."""

    def test_channel_selection(self):
        """Test channel selection."""
        mux = MockI2CMultiplexer()

        # Select channel
        mux.select_channel(0)
        assert mux.get_active_channel() == 0

        # Select different channel
        mux.select_channel(3)
        assert mux.get_active_channel() == 3

        # Deselect all
        mux.deselect_all()
        assert mux.get_active_channel() is None

    def test_device_simulation(self):
        """Test simulated device presence."""
        mux = MockI2CMultiplexer()

        # Add devices
        mux.add_device(channel=0, address=0x5A)
        mux.add_device(channel=0, address=0x60)
        mux.add_device(channel=1, address=0x5A)

        # Scan channels
        devices_ch0 = mux.scan_channel(0)
        assert 0x5A in devices_ch0
        assert 0x60 in devices_ch0

        devices_ch1 = mux.scan_channel(1)
        assert 0x5A in devices_ch1

    def test_validation(self):
        """Test channel validation."""
        mux = MockI2CMultiplexer()

        # Invalid channel
        with pytest.raises(ValueError, match="out of range"):
            mux.select_channel(8)


class TestMockNeoPixelLED:
    """Validate MockNeoPixelLED behavior."""

    def test_color_setting(self):
        """Test color setting and tracking."""
        led = MockNeoPixelLED()

        # Set color
        led.set_color(0, 255, 0)
        assert led.current_color == (0, 255, 0)
        assert led.is_on()

        # Turn off
        led.off()
        assert led.current_color == (0, 0, 0)
        assert not led.is_on()

    def test_brightness_control(self):
        """Test brightness adjustment."""
        led = MockNeoPixelLED(brightness=1.0)

        # Change brightness
        led.set_brightness(0.5)
        assert led.get_brightness() == 0.5

        # Track history
        assert len(led.brightness_history) == 1

    def test_color_history(self):
        """Test color change tracking."""
        led = MockNeoPixelLED()

        # Multiple color changes
        led.set_color(255, 0, 0)  # Red
        led.set_color(0, 255, 0)  # Green
        led.set_color(0, 0, 255)  # Blue

        # Verify history
        assert len(led.color_history) == 3
        assert led.color_history[0][0] == (255, 0, 0)
        assert led.color_history[1][0] == (0, 255, 0)
        assert led.color_history[2][0] == (0, 0, 255)


# ============================================================================
# Mock BLE Validation Tests
# ============================================================================

class TestMockBLEConnection:
    """Validate MockBLEConnection behavior."""

    def test_data_transmission(self):
        """Test data send/receive."""
        conn = MockBLEConnection("TestDevice")

        # Send data
        conn.write("HELLO\n")
        assert len(conn.send_history) == 1
        assert conn.get_last_sent() == "HELLO\n"

        # Receive data
        conn.queue_receive("WORLD\n")
        data = conn.read_message()
        assert data == "WORLD\n"

    def test_connection_state(self):
        """Test connection state management."""
        conn = MockBLEConnection("TestDevice")

        # Initially connected
        assert conn.connected

        # Disconnect
        conn.disconnect()
        assert not conn.connected


class TestMockBLE:
    """Validate MockBLE behavior."""

    def test_advertising(self):
        """Test BLE advertising."""
        ble = MockBLE()

        # Start advertising
        ble.advertise("BlueBuzzah")
        assert ble._advertising
        assert ble.advertised_name == "BlueBuzzah"

        # Stop advertising
        ble.stop_advertising()
        assert not ble._advertising

    def test_device_registration(self):
        """Test device registration for scanning."""
        ble = MockBLE()

        # Register device
        ble.register_device("TestDevice", rssi=-50)
        assert "TestDevice" in ble.registered_devices

        # Unregister
        ble.unregister_device("TestDevice")
        assert "TestDevice" not in ble.registered_devices

    def test_connection_establishment(self):
        """Test connection establishment."""
        ble = MockBLE()

        # Register and connect
        ble.register_device("BlueBuzzah", rssi=-45)
        conn_id = ble.scan_and_connect("BlueBuzzah", timeout=1.0)

        assert conn_id is not None
        assert ble.is_connected(conn_id)

    def test_data_transfer(self):
        """Test data transfer over connection."""
        ble = MockBLE()
        ble.register_device("BlueBuzzah")
        conn_id = ble.scan_and_connect("BlueBuzzah")

        # Send data
        success = ble.send(conn_id, "TEST\n")
        assert success

        # Simulate receive
        ble.simulate_receive(conn_id, "RESPONSE\n")
        data = ble.receive(conn_id, timeout=0.1)
        assert data == "RESPONSE\n"


# ============================================================================
# Pytest Fixture Validation Tests
# ============================================================================

class TestPytestFixtures:
    """Validate pytest fixtures from conftest.py."""

    def test_mock_haptic_fixture(self, mock_haptic_controller):
        """Test mock haptic controller fixture."""
        assert isinstance(mock_haptic_controller, MockHapticController)
        assert mock_haptic_controller.actuator_type == ActuatorType.LRA

    def test_mock_battery_fixture(self, mock_battery_monitor):
        """Test mock battery monitor fixture."""
        assert isinstance(mock_battery_monitor, MockBatteryMonitor)
        voltage = mock_battery_monitor.read_voltage()
        assert voltage > 3.0

    def test_mock_ble_fixture(self, mock_ble):
        """Test mock BLE fixture."""
        assert isinstance(mock_ble, MockBLE)

    def test_device_config_fixture(self, test_device_config):
        """Test device configuration fixture."""
        assert test_device_config['role'] == DeviceRole.PRIMARY

    def test_therapy_config_fixture(self, test_therapy_config):
        """Test therapy configuration fixture."""
        assert test_therapy_config['actuator_type'] == ActuatorType.LRA
        assert test_therapy_config['jitter_percent'] > 0


# ============================================================================
# Integration Tests
# ============================================================================

class TestMockIntegration:
    """Test integration of multiple mocks."""

    def test_therapy_simulation(
        self,
        mock_haptic_controller,
        mock_battery_monitor,
        test_therapy_config
    ):
        """Test simulated therapy session."""
        # Simulate a few activations
        for finger in range(5):
            mock_haptic_controller.activate(finger=finger, amplitude=75)
            time.sleep(0.01)
            mock_haptic_controller.deactivate(finger=finger)

        # Verify activations
        assert mock_haptic_controller.get_activation_count() == 5

        # Check battery
        status = mock_battery_monitor.get_status()
        assert status.status == "OK"

    def test_ble_communication_simulation(self, mock_ble):
        """Test simulated BLE communication."""
        # Setup devices
        mock_ble.register_device("PRIMARY", rssi=-40)
        mock_ble.register_device("SECONDARY", rssi=-45)

        # Connect
        conn1 = mock_ble.scan_and_connect("PRIMARY")
        conn2 = mock_ble.scan_and_connect("SECONDARY", conn_id="secondary")

        assert conn1 is not None
        assert conn2 is not None

        # Exchange data
        mock_ble.send(conn1, "SYNC\n")
        mock_ble.simulate_receive(conn2, "ACK\n")

        data = mock_ble.receive(conn2, timeout=0.1)
        assert data == "ACK\n"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
