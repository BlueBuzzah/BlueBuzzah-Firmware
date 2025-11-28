"""
Mock Hardware Components
========================

Mock implementations of hardware components for testing without physical
hardware dependencies. All mocks track method calls, support assertions,
and provide realistic behavior simulation.

Classes:
    MockHapticController: Mock haptic controller tracking activations
    MockBatteryMonitor: Mock battery with configurable voltage
    MockI2CMultiplexer: Mock I2C multiplexer with channel tracking
    MockNeoPixelLED: Mock LED recording state changes

Version: 2.0.0
"""

from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass, field
import time

from core.types import ActuatorType, BatteryStatus


@dataclass
class ActivationRecord:
    """Record of a haptic activation."""
    finger: int
    amplitude: int
    timestamp: float
    frequency: Optional[int] = None


@dataclass
class DeactivationRecord:
    """Record of a haptic deactivation."""
    finger: int
    timestamp: float


class MockHapticController:
    """
    Mock haptic controller for testing.

    This mock tracks all activations, deactivations, and frequency changes,
    allowing tests to verify correct haptic behavior without physical hardware.

    Features:
        - Tracks activation/deactivation history
        - Maintains active state per finger
        - Configurable frequency per finger
        - Supports activation failures for error testing
        - Provides assertion helpers

    Usage:
        haptic = MockHapticController()

        # Activate finger
        haptic.activate(finger=0, amplitude=75)
        assert haptic.is_active(0)

        # Verify activation
        assert haptic.activation_count == 1
        assert haptic.get_last_activation().amplitude == 75

        # Verify specific finger activated
        assert haptic.was_finger_activated(0)

        # Deactivate
        haptic.deactivate(finger=0)
        assert not haptic.is_active(0)
    """

    def __init__(self, actuator_type: ActuatorType = ActuatorType.LRA):
        """
        Initialize mock haptic controller.

        Args:
            actuator_type: Type of actuator (LRA or ERM)
        """
        self.actuator_type = actuator_type
        self.active_fingers: Dict[int, bool] = {}
        self.frequencies: Dict[int, int] = {}

        # Tracking
        self.activations: List[ActivationRecord] = []
        self.deactivations: List[DeactivationRecord] = []
        self.frequency_changes: List[Tuple[int, int, float]] = []  # (finger, freq, time)

        # Configuration
        self.fail_on_activate: Optional[int] = None  # Finger index to fail on
        self.fail_on_deactivate: Optional[int] = None

    def activate(self, finger: int, amplitude: int) -> None:
        """
        Activate haptic motor for specified finger.

        Args:
            finger: Finger index (0-4)
            amplitude: Vibration amplitude (0-100%)

        Raises:
            ValueError: If finger index or amplitude is out of range
            RuntimeError: If configured to fail for this finger
        """
        self._validate_finger(finger)
        self._validate_amplitude(amplitude)

        # Simulate failure if configured
        if self.fail_on_activate == finger:
            raise RuntimeError(f"Mock activation failure for finger {finger}")

        # Record activation
        self.activations.append(
            ActivationRecord(
                finger=finger,
                amplitude=amplitude,
                timestamp=time.monotonic(),
                frequency=self.frequencies.get(finger)
            )
        )

        # Update active state
        self.active_fingers[finger] = True

    def deactivate(self, finger: int) -> None:
        """
        Deactivate haptic motor for specified finger.

        Args:
            finger: Finger index (0-4)

        Raises:
            ValueError: If finger index is out of range
            RuntimeError: If configured to fail for this finger
        """
        self._validate_finger(finger)

        # Simulate failure if configured
        if self.fail_on_deactivate == finger:
            raise RuntimeError(f"Mock deactivation failure for finger {finger}")

        # Record deactivation
        self.deactivations.append(
            DeactivationRecord(
                finger=finger,
                timestamp=time.monotonic()
            )
        )

        # Update active state
        self.active_fingers[finger] = False

    def set_frequency(self, finger: int, frequency: int) -> None:
        """
        Set resonant frequency for LRA actuator.

        Args:
            finger: Finger index (0-4)
            frequency: Resonant frequency in Hz (150-250)

        Raises:
            ValueError: If finger index or frequency is out of range
        """
        self._validate_finger(finger)

        if not (150 <= frequency <= 250):
            raise ValueError(f"Frequency {frequency} out of range (150-250)")

        # Record frequency change
        self.frequency_changes.append((finger, frequency, time.monotonic()))

        # Update frequency
        self.frequencies[finger] = frequency

    def is_active(self, finger: int) -> bool:
        """
        Check if haptic motor is currently active.

        Args:
            finger: Finger index (0-4)

        Returns:
            True if motor is vibrating, False otherwise
        """
        self._validate_finger(finger)
        return self.active_fingers.get(finger, False)

    # Assertion helpers

    def was_finger_activated(self, finger: int) -> bool:
        """Check if finger was ever activated."""
        return any(a.finger == finger for a in self.activations)

    def was_finger_deactivated(self, finger: int) -> bool:
        """Check if finger was ever deactivated."""
        return any(d.finger == finger for d in self.deactivations)

    def get_activation_count(self, finger: Optional[int] = None) -> int:
        """
        Get count of activations.

        Args:
            finger: Optional finger to filter by

        Returns:
            Number of activations (for specific finger or total)
        """
        if finger is None:
            return len(self.activations)
        return sum(1 for a in self.activations if a.finger == finger)

    def get_deactivation_count(self, finger: Optional[int] = None) -> int:
        """Get count of deactivations."""
        if finger is None:
            return len(self.deactivations)
        return sum(1 for d in self.deactivations if d.finger == finger)

    def get_last_activation(self) -> Optional[ActivationRecord]:
        """Get most recent activation."""
        return self.activations[-1] if self.activations else None

    def get_last_deactivation(self) -> Optional[DeactivationRecord]:
        """Get most recent deactivation."""
        return self.deactivations[-1] if self.deactivations else None

    def get_activations_for_finger(self, finger: int) -> List[ActivationRecord]:
        """Get all activations for specific finger."""
        return [a for a in self.activations if a.finger == finger]

    def reset(self) -> None:
        """Reset all tracking data."""
        self.active_fingers.clear()
        self.activations.clear()
        self.deactivations.clear()
        self.frequency_changes.clear()

    def _validate_finger(self, finger: int) -> None:
        """Validate finger index."""
        if not (0 <= finger < 5):
            raise ValueError(f"Finger index {finger} out of range (0-4)")

    def _validate_amplitude(self, amplitude: int) -> None:
        """Validate amplitude value."""
        if not (0 <= amplitude <= 100):
            raise ValueError(f"Amplitude {amplitude} out of range (0-100)")


class MockBatteryMonitor:
    """
    Mock battery monitor for testing.

    This mock simulates battery voltage readings and status checks,
    allowing tests to verify battery monitoring behavior without
    physical hardware.

    Features:
        - Configurable voltage with gradual drain
        - Threshold detection (low/critical)
        - Percentage calculation
        - Monitoring history
        - Configurable failure modes

    Usage:
        battery = MockBatteryMonitor(voltage=3.85)

        # Read voltage
        voltage = battery.read_voltage()
        assert voltage == 3.85

        # Get status
        status = battery.get_status()
        assert status.percentage > 50
        assert not status.is_low

        # Simulate drain
        battery.set_voltage(3.35)
        assert battery.is_low()

        # Simulate critical
        battery.set_voltage(3.25)
        assert battery.is_critical()
    """

    def __init__(
        self,
        voltage: float = 3.85,
        divider_ratio: float = 2.0,
        sample_count: int = 10
    ):
        """
        Initialize mock battery monitor.

        Args:
            voltage: Initial battery voltage (V)
            divider_ratio: Voltage divider ratio
            sample_count: Number of samples to average
        """
        self._voltage = voltage
        self.divider_ratio = divider_ratio
        self.sample_count = sample_count

        # Monitoring state
        self.monitoring = False
        self.check_interval = 60.0
        self.last_check_time: Optional[float] = None
        self.last_status: Optional[BatteryStatus] = None

        # Tracking
        self.read_count = 0
        self.voltage_history: List[Tuple[float, float]] = []  # (voltage, timestamp)

        # Drain simulation
        self.drain_rate_per_sec = 0.0  # V/s
        self._last_drain_time: Optional[float] = None

    def read_voltage(self) -> float:
        """
        Read battery voltage.

        Returns:
            Battery voltage in volts
        """
        # Apply drain if enabled
        if self.drain_rate_per_sec > 0 and self._last_drain_time:
            elapsed = time.monotonic() - self._last_drain_time
            self._voltage -= self.drain_rate_per_sec * elapsed
            self._voltage = max(3.0, self._voltage)  # Minimum safe voltage

        self._last_drain_time = time.monotonic()

        # Track read
        self.read_count += 1
        self.voltage_history.append((self._voltage, time.monotonic()))

        return self._voltage

    def set_voltage(self, voltage: float) -> None:
        """Set battery voltage for testing."""
        self._voltage = voltage
        self._last_drain_time = time.monotonic()

    def enable_drain(self, rate_per_sec: float) -> None:
        """
        Enable gradual battery drain.

        Args:
            rate_per_sec: Drain rate in V/s
        """
        self.drain_rate_per_sec = rate_per_sec
        self._last_drain_time = time.monotonic()

    def disable_drain(self) -> None:
        """Disable battery drain."""
        self.drain_rate_per_sec = 0.0

    def get_percentage(self, voltage: Optional[float] = None) -> int:
        """
        Calculate battery percentage from voltage.

        Args:
            voltage: Battery voltage (if None, reads from ADC)

        Returns:
            Battery percentage (0-100)
        """
        if voltage is None:
            voltage = self.read_voltage()

        # Simple linear approximation
        # 4.2V = 100%, 3.0V = 0%
        if voltage >= 4.2:
            return 100
        if voltage <= 3.0:
            return 0

        percentage = ((voltage - 3.0) / (4.2 - 3.0)) * 100
        return int(percentage)

    def get_status(self, voltage: Optional[float] = None) -> BatteryStatus:
        """
        Get comprehensive battery status.

        Args:
            voltage: Battery voltage (if None, reads from ADC)

        Returns:
            BatteryStatus with voltage, percentage, and status
        """
        if voltage is None:
            voltage = self.read_voltage()

        percentage = self.get_percentage(voltage)

        # Determine status
        if voltage < 3.3:
            status_str = "CRITICAL"
            is_low = True
            is_critical = True
        elif voltage < 3.4:
            status_str = "LOW"
            is_low = True
            is_critical = False
        else:
            status_str = "OK"
            is_low = False
            is_critical = False

        battery_status = BatteryStatus(
            voltage=voltage,
            percentage=percentage,
            status=status_str,
            is_low=is_low,
            is_critical=is_critical,
        )

        self.last_status = battery_status
        return battery_status

    def is_low(self, voltage: Optional[float] = None) -> bool:
        """Check if battery is below LOW_VOLTAGE threshold (3.4V)."""
        if voltage is None:
            voltage = self.read_voltage()
        return voltage < 3.4

    def is_critical(self, voltage: Optional[float] = None) -> bool:
        """Check if battery is below CRITICAL_VOLTAGE threshold (3.3V)."""
        if voltage is None:
            voltage = self.read_voltage()
        return voltage < 3.3

    def start_monitoring(self, interval: Optional[float] = None) -> None:
        """Start background battery monitoring."""
        if interval is not None:
            self.check_interval = interval

        self.monitoring = True
        self.last_check_time = time.monotonic()
        self.last_status = self.get_status()

    def stop_monitoring(self) -> None:
        """Stop background battery monitoring."""
        self.monitoring = False
        self.last_check_time = None

    def check_battery(self) -> Optional[BatteryStatus]:
        """Perform periodic battery check if monitoring is enabled."""
        if not self.monitoring:
            return None

        current_time = time.monotonic()

        if (
            self.last_check_time is None
            or current_time - self.last_check_time >= self.check_interval
        ):
            status = self.get_status()
            self.last_check_time = current_time
            return status

        return None

    def get_last_status(self) -> Optional[BatteryStatus]:
        """Get the most recent battery status."""
        return self.last_status

    def force_check(self) -> BatteryStatus:
        """Force an immediate battery check."""
        status = self.get_status()
        self.last_check_time = time.monotonic()
        return status


class MockI2CMultiplexer:
    """
    Mock I2C multiplexer for testing.

    This mock simulates TCA9548A multiplexer behavior, tracking channel
    selection and providing device simulation.

    Features:
        - Channel selection tracking
        - Simulated device presence per channel
        - Channel scanning
        - Configurable failures

    Usage:
        mux = MockI2CMultiplexer()

        # Add simulated device on channel 0
        mux.add_device(channel=0, address=0x5A)

        # Select channel
        mux.select_channel(0)
        assert mux.get_active_channel() == 0

        # Scan for devices
        devices = mux.scan_channel(0)
        assert 0x5A in devices
    """

    def __init__(self, i2c=None, address: int = 0x70):
        """
        Initialize mock multiplexer.

        Args:
            i2c: I2C bus (unused in mock)
            address: Multiplexer I2C address
        """
        self.i2c = i2c
        self.address = address
        self.active_channel: Optional[int] = None

        # Simulated devices: channel -> set of device addresses
        self.devices: Dict[int, set] = {i: set() for i in range(8)}

        # Tracking
        self.channel_selections: List[Tuple[int, float]] = []

    def select_channel(self, channel: int) -> None:
        """Select a specific multiplexer channel."""
        if not (0 <= channel <= 7):
            raise ValueError(f"Channel {channel} out of range (0-7)")

        self.active_channel = channel
        self.channel_selections.append((channel, time.monotonic()))

    def deselect_all(self) -> None:
        """Deselect all channels."""
        self.active_channel = None

    def get_active_channel(self) -> Optional[int]:
        """Get currently active channel."""
        return self.active_channel

    def add_device(self, channel: int, address: int) -> None:
        """Add simulated device on channel."""
        if not (0 <= channel <= 7):
            raise ValueError(f"Channel {channel} out of range (0-7)")
        self.devices[channel].add(address)

    def remove_device(self, channel: int, address: int) -> None:
        """Remove simulated device from channel."""
        if not (0 <= channel <= 7):
            raise ValueError(f"Channel {channel} out of range (0-7)")
        self.devices[channel].discard(address)

    def scan_channel(self, channel: int) -> List[int]:
        """Scan a specific channel for I2C devices."""
        if not (0 <= channel <= 7):
            raise ValueError(f"Channel {channel} out of range (0-7)")
        return sorted(list(self.devices[channel]))

    def scan_channels(self) -> List[int]:
        """Scan all channels for devices."""
        return [ch for ch in range(8) if self.devices[ch]]

    def reset(self) -> None:
        """Reset multiplexer to default state."""
        self.deselect_all()


class MockNeoPixelLED:
    """
    Mock NeoPixel LED for testing.

    This mock simulates NeoPixel LED behavior, recording all state changes
    and color settings for test verification.

    Features:
        - Color state tracking
        - Brightness control
        - State change history
        - Animation recording

    Usage:
        led = MockNeoPixelLED()

        # Set color
        led.set_color(0, 255, 0)  # Green
        assert led.current_color == (0, 255, 0)
        assert led.is_on()

        # Change brightness
        led.set_brightness(0.5)
        assert led.get_brightness() == 0.5

        # Turn off
        led.off()
        assert not led.is_on()

        # Check history
        assert len(led.color_history) == 2  # Set color + off
    """

    def __init__(self, pixels=None, brightness: float = 1.0):
        """
        Initialize mock LED.

        Args:
            pixels: NeoPixel instance (unused in mock)
            brightness: Initial brightness (0.0-1.0)
        """
        self.pixels = pixels
        self._brightness = brightness
        self.current_color: Tuple[int, int, int] = (0, 0, 0)

        # Tracking
        self.color_history: List[Tuple[Tuple[int, int, int], float]] = []
        self.brightness_history: List[Tuple[float, float]] = []

    def set_color(self, r: int, g: int, b: int) -> None:
        """Set LED color using RGB values."""
        if not (0 <= r <= 255):
            raise ValueError(f"Red component {r} out of range (0-255)")
        if not (0 <= g <= 255):
            raise ValueError(f"Green component {g} out of range (0-255)")
        if not (0 <= b <= 255):
            raise ValueError(f"Blue component {b} out of range (0-255)")

        self.current_color = (r, g, b)
        self.color_history.append((self.current_color, time.monotonic()))

    def set_color_tuple(self, color: Tuple[int, int, int]) -> None:
        """Set LED color using RGB tuple."""
        r, g, b = color
        self.set_color(r, g, b)

    def set_brightness(self, brightness: float) -> None:
        """Set LED brightness."""
        if not (0.0 <= brightness <= 1.0):
            raise ValueError(f"Brightness {brightness} out of range (0.0-1.0)")

        self._brightness = brightness
        self.brightness_history.append((brightness, time.monotonic()))

    def get_brightness(self) -> float:
        """Get current brightness level."""
        return self._brightness

    def off(self) -> None:
        """Turn LED off."""
        self.current_color = (0, 0, 0)
        self.color_history.append((self.current_color, time.monotonic()))

    def is_on(self) -> bool:
        """Check if LED is currently on (non-zero color)."""
        return self.current_color != (0, 0, 0)

    def get_color(self) -> Tuple[int, int, int]:
        """Get current LED color."""
        return self.current_color

    def fill(self, color: Tuple[int, int, int]) -> None:
        """Fill all pixels with specified color."""
        self.set_color_tuple(color)

    def reset(self) -> None:
        """Reset LED state and history."""
        self.current_color = (0, 0, 0)
        self.color_history.clear()
        self.brightness_history.clear()
