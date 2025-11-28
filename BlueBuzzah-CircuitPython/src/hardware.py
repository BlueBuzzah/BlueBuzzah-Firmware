"""
Consolidated Hardware Module
=============================
Simplified hardware abstractions for BlueBuzzah v2.

This module consolidates all hardware interfaces into a single file:
- Board configuration and pin assignments
- I2C multiplexer control
- DRV2605 haptic controller
- Battery voltage monitoring
- NeoPixel LED control

Module: hardware
Version: 2.0.0
"""

import time
from core.types import ActuatorType, BatteryStatus
from core.constants import (
    DRV2605_DEFAULT_ADDR,
    I2C_MULTIPLEXER_ADDR,
    MAX_AMPLITUDE,
    MIN_FREQUENCY_HZ,
    MAX_FREQUENCY_HZ,
    CRITICAL_VOLTAGE,
    LOW_VOLTAGE,
)


# ============================================================================
# Board Configuration
# ============================================================================

class BoardConfig:
    """
    Board-specific configuration for nRF52840.

    Attributes:
        board_id: Board identifier string
        neopixel_pin: Pin for NeoPixel LED
        battery_sense_pin: Pin for battery voltage ADC
        i2c: I2C bus instance
        tca9548a_address: I2C multiplexer address
    """

    def __init__(self):
        """Initialize board configuration with pin assignments."""
        import board
        import busio

        self.board_id = getattr(board, "board_id", "nrf52840")

        # Pin assignments
        self.neopixel_pin = board.NEOPIXEL
        self.battery_sense_pin = board.VOLTAGE_MONITOR if hasattr(board, "VOLTAGE_MONITOR") else board.A6

        # I2C bus configuration
        self.i2c = busio.I2C(board.SCL, board.SDA, frequency=400000)

        # I2C multiplexer address
        self.tca9548a_address = I2C_MULTIPLEXER_ADDR


# ============================================================================
# I2C Multiplexer
# ============================================================================

class I2CMultiplexer:
    """
    Simple TCA9548A I2C multiplexer controller.

    The multiplexer enables communication with multiple I2C devices
    (haptic drivers) sharing the same address on different channels.

    Attributes:
        i2c: I2C bus instance
        address: Multiplexer I2C address
        active_channel: Currently selected channel
    """

    # I2C lock timeout to prevent infinite busy-wait
    I2C_LOCK_TIMEOUT_SEC = 0.1  # 100ms

    def __init__(self, i2c, address= I2C_MULTIPLEXER_ADDR):
        """
        Initialize I2C multiplexer.

        Args:
            i2c: I2C bus instance
            address: Multiplexer address (default: 0x70)
        """
        self.i2c = i2c
        self.address = address
        self.active_channel= None

        # Verify multiplexer is present
        try:
            self.deselect_all()
        except Exception as e:
            raise RuntimeError("Multiplexer not found at 0x{:02X}: {}".format(address, e))

    def _acquire_i2c_lock(self, timeout_sec=None):
        """
        Acquire I2C lock with timeout to prevent infinite busy-wait.

        Args:
            timeout_sec: Timeout in seconds (default: I2C_LOCK_TIMEOUT_SEC)

        Raises:
            RuntimeError: If lock cannot be acquired within timeout
        """
        if timeout_sec is None:
            timeout_sec = self.I2C_LOCK_TIMEOUT_SEC

        start = time.monotonic()
        while not self.i2c.try_lock():
            if time.monotonic() - start > timeout_sec:
                raise RuntimeError("I2C lock timeout after {}s".format(timeout_sec))
            time.sleep(0.001)  # 1ms yield between attempts

    def select_channel(self, channel):
        """
        Select multiplexer channel (0-7).

        Args:
            channel: Channel number

        Raises:
            ValueError: If channel out of range
            RuntimeError: If I2C lock timeout or communication fails
        """
        if not (0 <= channel <= 7):
            raise ValueError("Channel {} out of range (0-7)".format(channel))

        try:
            channel_mask = 1 << channel
            # Acquire I2C lock with timeout (prevents infinite busy-wait)
            self._acquire_i2c_lock()
            try:
                self.i2c.writeto(self.address, bytes([channel_mask]))
                self.active_channel = channel
            finally:
                self.i2c.unlock()
        except Exception as e:
            raise RuntimeError("Failed to select channel {}: {}".format(channel, e))

    def deselect_all(self):
        """
        Disable all multiplexer channels.

        Raises:
            RuntimeError: If I2C lock timeout or communication fails
        """
        try:
            # Acquire I2C lock with timeout (prevents infinite busy-wait)
            self._acquire_i2c_lock()
            try:
                self.i2c.writeto(self.address, bytes([0x00]))
                self.active_channel = None
            finally:
                self.i2c.unlock()
        except Exception as e:
            raise RuntimeError("Failed to deselect channels: {}".format(e))


# ============================================================================
# Haptic Controller (DRV2605)
# ============================================================================

class DRV2605Controller:
    """
    DRV2605 haptic driver controller for up to 5 actuators.

    Controls vibrotactile actuators (LRA or ERM) through I2C multiplexer,
    using real-time playback (RTP) mode for precise amplitude control.

    Attributes:
        multiplexer: I2C multiplexer instance
        actuator_type: LRA or ERM
        i2c_addr: DRV2605 I2C address
        active_fingers: Dict tracking active state per finger
        frequencies: Dict storing frequency per finger
    """

    # DRV2605 Registers
    REG_MODE = 0x01
    REG_RTP_INPUT = 0x02
    REG_FEEDBACK_CTRL = 0x1A
    REG_CONTROL1 = 0x1B
    REG_CONTROL3 = 0x1D

    # Mode values
    MODE_REALTIME_PLAYBACK = 0x05

    def __init__(
        self,
        multiplexer,
        actuator_type= ActuatorType.LRA,
        frequency_hz= 175,
        i2c_addr= DRV2605_DEFAULT_ADDR,
    ):
        """
        Initialize DRV2605 controller.

        Args:
            multiplexer: I2C multiplexer instance
            actuator_type: LRA or ERM
            frequency_hz: Default resonant frequency (150-250 Hz)
            i2c_addr: DRV2605 I2C address (default: 0x5A)
        """
        self.multiplexer = multiplexer
        self.actuator_type = actuator_type
        self.i2c_addr = i2c_addr
        self.default_frequency = frequency_hz

        self.active_fingers= {}
        self.frequencies= {}

    def initialize_finger(self, finger):
        """
        Initialize DRV2605 for specific finger.

        Args:
            finger: Finger index (0-4)
        """
        if not (0 <= finger < 5):
            raise ValueError("Finger {} out of range (0-4)".format(finger))

        # CRITICAL FIX: Retry I2C initialization up to 3 times
        # Finger 4 (channel 4) may have longer I2C path or timing issues
        max_retries = 3
        for attempt in range(max_retries):
            self.multiplexer.select_channel(finger)

            try:
                # Exit standby
                self._write_register(self.REG_MODE, 0x00)

                # DRV2605 datasheet section 7.4.2:
                # "Wait 5ms after exiting standby before writing to registers"
                # CRITICAL FIX: Increase delay for channel 4 (longer I2C path)
                if finger == 4:
                    time.sleep(0.010)  # 10ms for finger 4
                else:
                    time.sleep(0.005)  # 5ms for others

                # Configure for actuator type
                if self.actuator_type == ActuatorType.LRA:
                    self._write_register(self.REG_FEEDBACK_CTRL, 0x80)  # LRA mode
                    self._write_register(self.REG_CONTROL3, 0x01)  # LRA mode bit
                else:
                    self._write_register(self.REG_FEEDBACK_CTRL, 0x00)  # ERM mode
                    self._write_register(self.REG_CONTROL3, 0x00)  # ERM mode

                # Set RTP mode
                self._write_register(self.REG_MODE, self.MODE_REALTIME_PLAYBACK)

                # Set frequency if LRA
                if self.actuator_type == ActuatorType.LRA:
                    self.set_frequency(finger, self.default_frequency)

                self.active_fingers[finger] = False

                # Success - break out of retry loop
                break

            except Exception as e:
                if attempt < max_retries - 1:
                    # Retry after short delay
                    print("[WARNING] Finger {} init attempt {} failed: {}, retrying...".format(finger, attempt + 1, e))
                    time.sleep(0.050)  # 50ms delay before retry
                else:
                    # Final attempt failed - mark as disabled
                    print("[ERROR] Finger {} init failed after {} attempts: {}".format(finger, max_retries, e))
                    self.active_fingers[finger] = None  # Mark as disabled
                    raise

            finally:
                self.multiplexer.deselect_all()

    def activate(self, finger, amplitude):
        """
        Activate haptic motor at specified amplitude.

        Args:
            finger: Finger index (0-4)
            amplitude: Vibration amplitude (0-100%)
        """
        if not (0 <= finger < 5):
            raise ValueError("Finger {} out of range (0-4)".format(finger))
        if not (0 <= amplitude <= MAX_AMPLITUDE):
            raise ValueError("Amplitude {} out of range (0-{})".format(amplitude, MAX_AMPLITUDE))

        # CRITICAL FIX: Skip if finger is disabled (None)
        if self.active_fingers.get(finger, False) is None:
            # Finger is disabled due to init failure - skip silently
            return

        # Convert 0-100% to 0-127
        rtp_value = int((amplitude / 100.0) * 127)

        self.multiplexer.select_channel(finger)

        try:
            self._write_register(self.REG_RTP_INPUT, rtp_value)
            self.active_fingers[finger] = True
        except Exception as e:
            # Mark finger as disabled on persistent errors
            print("[ERROR] Finger {} activation failed: {}, disabling".format(finger, e))
            self.active_fingers[finger] = None
            raise
        finally:
            self.multiplexer.deselect_all()

    def deactivate(self, finger):
        """
        Deactivate haptic motor.

        Args:
            finger: Finger index (0-4)
        """
        if not (0 <= finger < 5):
            raise ValueError("Finger {} out of range (0-4)".format(finger))

        # CRITICAL FIX: Skip if finger is disabled (None)
        if self.active_fingers.get(finger, False) is None:
            # Finger is disabled due to init failure - skip silently
            return

        self.multiplexer.select_channel(finger)

        try:
            self._write_register(self.REG_RTP_INPUT, 0x00)
            self.active_fingers[finger] = False
        except Exception as e:
            # Mark finger as disabled on persistent errors
            print("[ERROR] Finger {} deactivation failed: {}, disabling".format(finger, e))
            self.active_fingers[finger] = None
            raise
        finally:
            self.multiplexer.deselect_all()

    def set_frequency(self, finger, frequency):
        """
        Set resonant frequency for LRA actuator.

        Args:
            finger: Finger index (0-4)
            frequency: Resonant frequency (150-250 Hz)
        """
        if not (0 <= finger < 5):
            raise ValueError("Finger {} out of range (0-4)".format(finger))
        if not (MIN_FREQUENCY_HZ <= frequency <= MAX_FREQUENCY_HZ):
            raise ValueError("Frequency {} Hz out of range".format(frequency))

        self.frequencies[finger] = frequency

        # Calculate drive time
        drive_time = int(5000 / frequency) & 0x1F

        self.multiplexer.select_channel(finger)

        try:
            self._write_register(self.REG_CONTROL1, drive_time)
        finally:
            self.multiplexer.deselect_all()

    def is_active(self, finger):
        """
        Check if finger's motor is active.

        Args:
            finger: Finger index (0-4)

        Returns:
            True if active, False otherwise
        """
        return self.active_fingers.get(finger, False)

    def stop_all(self, force_all=False):
        """
        Stop all haptic motors immediately.

        Args:
            force_all: If True, turn off ALL motors regardless of tracked state.
                      Use at boot to ensure clean state. Default: False
        """
        for finger in range(5):
            # Force mode: turn off unconditionally (for boot/shutdown)
            # Normal mode: only turn off if tracked as active
            should_stop = force_all or self.active_fingers.get(finger, False)

            if should_stop:
                try:
                    self.multiplexer.select_channel(finger)
                    self._write_register(self.REG_RTP_INPUT, 0x00)
                    self.active_fingers[finger] = False
                except Exception:
                    pass  # Best effort
                finally:
                    self.multiplexer.deselect_all()

    def emergency_stop(self):
        """
        Emergency stop all motors (synchronous version).
        Forces unconditional shutoff of ALL motors.
        """
        self.stop_all(force_all=True)

    def _write_register(self, register, value):
        """
        Write to DRV2605 register.

        Args:
            register: Register address
            value: Value to write (0-255)
        """
        try:
            i2c = self.multiplexer.i2c
            buffer = bytearray([register, value])
            # Acquire I2C lock with timeout to prevent infinite busy-wait
            timeout_start = time.monotonic()
            while not i2c.try_lock():
                if time.monotonic() - timeout_start > 0.1:  # 100ms timeout
                    raise RuntimeError("I2C lock timeout writing to register 0x{:02X}".format(register))
                time.sleep(0.001)  # 1ms yield between attempts
            try:
                i2c.writeto(self.i2c_addr, buffer)
            finally:
                i2c.unlock()
        except Exception as e:
            raise RuntimeError("Failed to write register 0x{:02X}: {}".format(register, e))


# ============================================================================
# Battery Monitor
# ============================================================================

class BatteryMonitor:
    """
    Battery voltage monitoring for LiPo batteries.

    Monitors battery voltage through ADC pin with voltage divider,
    calculates percentage, and checks threshold levels.

    Attributes:
        pin: AnalogIn pin for voltage reading
        divider_ratio: Voltage divider ratio
        sample_count: Number of samples to average
        warning_voltage: Low battery warning threshold
        critical_voltage: Critical battery shutdown threshold
    """

    ADC_REF_VOLTAGE = 3.3
    ADC_MAX_VALUE = 65535

    # LiPo voltage curve (voltage -> percentage)
    VOLTAGE_CURVE = [
        (4.20, 100), (4.15, 95), (4.11, 90), (4.08, 85), (4.02, 80),
        (3.98, 75), (3.95, 70), (3.91, 65), (3.87, 60), (3.85, 55),
        (3.84, 50), (3.82, 45), (3.80, 40), (3.79, 35), (3.77, 30),
        (3.75, 25), (3.73, 20), (3.71, 15), (3.69, 10), (3.61, 5), (3.27, 0),
    ]

    def __init__(
        self,
        adc_pin,
        divider_ratio= 2.0,
        sample_count= 10,
        warning_voltage= LOW_VOLTAGE,
        critical_voltage= CRITICAL_VOLTAGE,
    ):
        """
        Initialize battery monitor.

        Args:
            adc_pin: Pin object for ADC voltage reading
            divider_ratio: Voltage divider ratio
            sample_count: Number of ADC samples to average
            warning_voltage: Low battery warning threshold
            critical_voltage: Critical battery threshold
        """
        # Create AnalogIn from pin for ADC reading
        try:
            import analogio
            self.pin = analogio.AnalogIn(adc_pin)
        except ImportError:
            # Mock for testing on non-CircuitPython platforms
            self.pin = adc_pin

        self.divider_ratio = divider_ratio
        self.sample_count = sample_count
        self.warning_voltage = warning_voltage
        self.critical_voltage = critical_voltage
        self.last_status= None

    def read_voltage(self):
        """
        Read battery voltage with averaging.

        Returns:
            Battery voltage in volts
        """
        total = 0
        for _ in range(self.sample_count):
            total += self.pin.value
            time.sleep(0.001)

        average = total / self.sample_count
        adc_voltage = (average / self.ADC_MAX_VALUE) * self.ADC_REF_VOLTAGE
        battery_voltage = adc_voltage * self.divider_ratio

        return battery_voltage

    def get_percentage(self, voltage= None):
        """
        Calculate battery percentage from voltage.

        Args:
            voltage: Battery voltage (if None, reads from ADC)

        Returns:
            Battery percentage (0-100)
        """
        if voltage is None:
            voltage = self.read_voltage()

        if voltage >= self.VOLTAGE_CURVE[0][0]:
            return 100
        if voltage <= self.VOLTAGE_CURVE[-1][0]:
            return 0

        # Linear interpolation
        for i in range(len(self.VOLTAGE_CURVE) - 1):
            v_high, pct_high = self.VOLTAGE_CURVE[i]
            v_low, pct_low = self.VOLTAGE_CURVE[i + 1]

            if v_low <= voltage <= v_high:
                ratio = (voltage - v_low) / (v_high - v_low)
                percentage = pct_low + ratio * (pct_high - pct_low)
                return int(percentage)

        return 0

    def get_status(self, voltage= None):
        """
        Get comprehensive battery status.

        Args:
            voltage: Battery voltage (if None, reads from ADC)

        Returns:
            BatteryStatus dataclass
        """
        if voltage is None:
            voltage = self.read_voltage()

        percentage = self.get_percentage(voltage)

        # Determine status
        if voltage < self.critical_voltage:
            status_str = "CRITICAL"
            is_low = True
            is_critical = True
        elif voltage < self.warning_voltage:
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

    def is_low(self, voltage= None):
        """Check if battery is below warning threshold."""
        if voltage is None:
            voltage = self.read_voltage()
        return voltage < self.warning_voltage

    def is_critical(self, voltage= None):
        """Check if battery is below critical threshold."""
        if voltage is None:
            voltage = self.read_voltage()
        return voltage < self.critical_voltage


# ============================================================================
# NeoPixel LED
# ============================================================================

class LEDPin:
    """
    Simple NeoPixel LED wrapper.

    Attributes:
        pin: Board pin for NeoPixel
        pixel: NeoPixel instance
    """

    def __init__(self, pin):
        """
        Initialize LED pin.

        Args:
            pin: Board pin object
        """
        import neopixel

        self.pin = pin
        self.pixel = neopixel.NeoPixel(pin, 1, brightness=0.1, auto_write=True)

    def set_color(self, r, g, b):
        """
        Set LED color.

        Args:
            r: Red value (0-255)
            g: Green value (0-255)
            b: Blue value (0-255)
        """
        self.pixel[0] = (r, g, b)

    def off(self):
        """Turn LED off."""
        self.pixel[0] = (0, 0, 0)
