"""
Consolidated LED Controller
============================
Complete LED control system for BlueBuzzah v2.

This module consolidates all LED functionality into a single file:
- Base LED operations (flash, solid, off)
- Boot sequence patterns (BLE init, connection, ready)
- Therapy state patterns (running/breathing, paused, stopping)
- Battery warnings and critical alerts

Module: led
Version: 2.0.0
"""

import math
import time

try:
    import board
    import neopixel
    from microcontroller import Pin
except ImportError:
    # Mock imports for testing on non-CircuitPython platforms
    board = None
    neopixel = None
    Pin = None

from core.constants import (
    LED_BLUE,
    LED_GREEN,
    LED_OFF,
    LED_ORANGE,
    LED_RED,
    LED_WHITE,
    LED_YELLOW,
    NEOPIXEL_PIN,
)
from core.types import TherapyState

# ============================================================================
# LED Controller
# ============================================================================


class LEDController:
    """
    Complete LED controller for visual feedback.

    This class provides all LED operations for boot sequence, therapy states,
    and system alerts using a single NeoPixel RGB LED.

    LED Patterns:
        Boot Sequence:
            - BLE Init: Rapid blue flash (10Hz)
            - Connection Success: 5x green flash
            - Waiting for Phone: Blue breathing (0.5s cycle, 2Hz pulse rate)
            - Ready: Solid blue
            - Failure: Slow red flash (1Hz)
            - Therapy Start: 3x white flash

        Therapy States:
            - RUNNING: Solid green
            - PAUSED: Slow yellow pulse (1Hz)
            - STOPPING: Fade green to off (2s)
            - LOW_BATTERY: Solid orange
            - CRITICAL_BATTERY: Rapid red flash (5Hz)
            - CONNECTION_LOST: Alternating red/white (2Hz)
            - PHONE_DISCONNECTED: Brief blue flash (100ms)

    Attributes:
        pixels: NeoPixel object controlling the LED
        current_state: Current TherapyState
        breathing_phase: Current phase for boot breathing animation (radians)

    Usage:
        # Boot sequence with breathing animation
        led = LEDController(board.NEOPIXEL)
        led.indicate_ble_init()
        # ... wait for connections ...
        led.stop_flashing()
        led.indicate_connection_success()
        led.indicate_waiting_for_phone()
        while waiting_for_phone:
            led.update_animation()  # Animates breathing during boot
            time.sleep(0.02)
        led.indicate_ready()

        # Therapy session (solid indicators)
        led.set_therapy_state(TherapyState.RUNNING)  # Solid green
    """

    # Flash frequency constants (Hz)
    RAPID_FLASH_HZ = 10  # 10Hz for active operations
    MEDIUM_FLASH_HZ = 2  # 2Hz for intermediate waiting states
    SLOW_FLASH_HZ = 1  # 1Hz for waiting states
    CRITICAL_FLASH_HZ = 5  # 5Hz for critical battery
    CONNECTION_LOST_FLASH_HZ = 2  # 2Hz for connection lost

    # Breathing effect parameters (boot sequence only)
    BREATHING_BOOT_CYCLE_SEC = 0.5  # Fast 0.5-second cycle for boot waiting state (2 pulses/sec)
    BREATHING_MIN_INTENSITY = 0  # 0% minimum brightness
    BREATHING_MAX_INTENSITY = 0.8  # 80% maximum brightness

    def __init__(self, pixel_pin=None):
        """
        Initialize the LED controller.

        Args:
            pixel_pin: Pin object for NeoPixel LED. If None, uses board.NEOPIXEL_PIN.
                      For testing, can pass None if neopixel module unavailable.

        Raises:
            RuntimeError: If NeoPixel initialization fails
        """
        self.pixels = None
        self._flashing = False
        self._flash_stop_time = None
        self._flash_color = LED_BLUE
        self._flash_period = 0.1
        self._flash_last_toggle = 0.0
        self._flash_state = False
        self.current_state = None
        self.breathing_phase = 0.0

        if neopixel is not None:
            try:
                if pixel_pin is None:
                    pixel_pin = getattr(board, NEOPIXEL_PIN)
                self.pixels = neopixel.NeoPixel(
                    pixel_pin, 1, brightness=0.5, auto_write=True
                )
                self.off()
            except Exception as e:
                raise RuntimeError("Failed to initialize NeoPixel: {}".format(e))

    # ========================================================================
    # Base LED Operations
    # ========================================================================

    def start_rapid_flash(self, color):
        """
        Start rapid LED flashing (non-blocking).

        Sets up rapid flash state and returns immediately. Call update_flash()
        periodically to animate the flash.

        Args:
            color: RGB tuple (0-255 for each channel)
        """
        self._flashing = True
        self._flash_color = color
        self._flash_period = 1.0 / self.RAPID_FLASH_HZ
        self._flash_last_toggle = time.monotonic()
        self._flash_state = False  # False = off, True = on

    def start_medium_flash(self, color):
        """
        Start medium LED flashing (non-blocking) at 2Hz.

        Sets up medium flash state and returns immediately. Call update_flash()
        periodically to animate the flash.

        Args:
            color: RGB tuple (0-255 for each channel)
        """
        self._flashing = True
        self._flash_color = color
        self._flash_period = 1.0 / self.MEDIUM_FLASH_HZ
        self._flash_last_toggle = time.monotonic()
        self._flash_state = False  # False = off, True = on

    def start_breathing(self, color, cycle_sec=None):
        """
        Start breathing LED effect (non-blocking).

        Sets up breathing state and returns immediately. Call update_breathing()
        periodically to animate the breathing effect.

        Args:
            color: RGB tuple (0-255 for each channel) - base color for breathing
            cycle_sec: Breathing cycle duration in seconds (default: BREATHING_BOOT_CYCLE_SEC)
        """
        if cycle_sec is None:
            cycle_sec = self.BREATHING_BOOT_CYCLE_SEC

        self._breathing = True
        self._breathing_color = color
        self._breathing_cycle_sec = cycle_sec
        self._breathing_start_time = time.monotonic()
        self.breathing_phase = 0.0

    def update_flash(self):
        """
        Update LED flash animation (call periodically for non-blocking flash).

        Returns:
            True if still flashing, False if stopped
        """
        if not self._flashing:
            return False

        # Check if it's time to toggle
        now = time.monotonic()
        if now - self._flash_last_toggle >= (self._flash_period / 2.0):
            self._flash_state = not self._flash_state
            if self._flash_state:
                self._set_color(self._flash_color)
            else:
                self._set_color(LED_OFF)
            self._flash_last_toggle = now

        return True

    def update_animation(self):
        """
        Update all active LED animations (flash and breathing).

        This is a unified update method that handles both flashing and breathing
        patterns. Call this periodically (recommended ~20-100Hz) during boot
        or main loops to animate the LED.

        Returns:
            True if any animation is active, False if none
        """
        flash_active = self.update_flash()
        self.update_breathing()  # Returns None but updates if breathing active

        # Return True if either animation is active
        breathing_active = hasattr(self, "_breathing") and self._breathing
        return flash_active or breathing_active

    def rapid_flash(self, color, duration_sec=None):
        """
        Flash LED rapidly at 10Hz (blocking version).

        Used for operations that need blocking flash (like success confirmation).
        For non-blocking flash during boot, use start_rapid_flash() and update_flash().

        Args:
            color: RGB tuple (0-255 for each channel)
            duration_sec: Optional duration in seconds. If None, flashes indefinitely.
        """
        self._flashing = True
        self._flash_stop_time = (
            time.monotonic() + duration_sec if duration_sec is not None else None
        )

        period = 1.0 / self.RAPID_FLASH_HZ
        half_period = period / 2.0

        while self._flashing:
            # Check if duration expired
            if (
                self._flash_stop_time is not None
                and time.monotonic() >= self._flash_stop_time
            ):
                self._flashing = False
                self.off()
                return

            # Flash on
            self._set_color(color)
            time.sleep(half_period)

            # Flash off
            self._set_color(LED_OFF)
            time.sleep(half_period)

    def slow_flash(self, color):
        """
        Flash LED slowly at 1Hz.

        Used for waiting states like connection failure or waiting for phone.
        This method runs indefinitely until stopped externally.

        Args:
            color: RGB tuple (0-255 for each channel)
        """
        self._flashing = True
        self._flash_stop_time = None

        period = 1.0 / self.SLOW_FLASH_HZ
        half_period = period / 2.0

        while self._flashing:
            # Flash on
            self._set_color(color)
            time.sleep(half_period)

            # Flash off
            self._set_color(LED_OFF)
            time.sleep(half_period)

    def flash_count(self, color, count):
        """
        Flash LED a specific number of times.

        Used for acknowledgment and success indicators (e.g., 5x green flash
        for connection success, 3x white flash for therapy start).

        Each flash is 100ms on, 100ms off for clear visibility.

        Args:
            color: RGB tuple (0-255 for each channel)
            count: Number of times to flash
        """
        flash_on_duration = 0.1  # 100ms on
        flash_off_duration = 0.1  # 100ms off

        for _ in range(count):
            self._set_color(color)
            time.sleep(flash_on_duration)
            self._set_color(LED_OFF)
            time.sleep(flash_off_duration)

        # Ensure LED is off after flashing
        self.off()

    def solid(self, color):
        """
        Set LED to solid color.

        Used for steady states like ready (solid green) or low battery warning
        (solid orange). Stops any active flashing pattern.

        Args:
            color: RGB tuple (0-255 for each channel)
        """
        self._flashing = False
        self._flash_stop_time = None
        self._set_color(color)

    def off(self):
        """
        Turn LED off.

        Stops any active flashing pattern and sets LED to off (all channels 0).
        """
        self._flashing = False
        self._flash_stop_time = None
        self._set_color(LED_OFF)

    def stop_flashing(self):
        """
        Stop any active flashing pattern.

        This method allows external code to interrupt continuous flashing
        (rapid_flash or slow_flash) without knowing which pattern is active.
        The LED will be turned off.
        """
        self._flashing = False
        self._flash_stop_time = None
        self.off()

    def _set_color(self, color):
        """
        Set the LED to a specific RGB color.

        Internal helper method for setting LED color. Handles both real
        NeoPixel hardware and mock implementations for testing.

        Args:
            color: RGB tuple (0-255 for each channel)
        """
        if self.pixels is not None:
            self.pixels[0] = color
            # Note: auto_write=True in __init__ means color is written immediately

    # ========================================================================
    # Boot Sequence Methods
    # ========================================================================

    def indicate_ble_init(self):
        """
        Indicate BLE initialization in progress (non-blocking).

        Starts rapid blue flashing (10Hz) to show that the device is
        actively initializing BLE and establishing connections.

        This method returns immediately. The boot sequence should call
        update_flash() periodically to animate the LED.

        Boot Sequence Context:
            - PRIMARY: Starts when device begins advertising as 'BlueBuzzah'
            - SECONDARY: Starts when device begins scanning for PRIMARY
        """
        self.start_rapid_flash(LED_BLUE)

    def indicate_connection_success(self):
        """
        Indicate successful PRIMARY-SECONDARY connection.

        Displays 5x rapid green flash to celebrate successful connection
        between PRIMARY and SECONDARY devices.

        Boot Sequence Context:
            - PRIMARY: Triggered when SECONDARY successfully connects
            - SECONDARY: Triggered when connection to PRIMARY established
        """
        self.stop_flashing()  # Ensure any previous flashing is stopped
        self.flash_count(LED_GREEN, 5)

    def indicate_waiting_for_phone(self):
        """
        Indicate waiting for phone connection.

        Displays smooth blue breathing (0.5-second cycle, 2 pulses/sec) to show that
        PRIMARY-SECONDARY connection is established but waiting for optional phone connection.

        Boot Sequence Context:
            - PRIMARY only: Used after SECONDARY connects successfully
            - Shows during the remaining boot window (up to 30 seconds)
        """
        self.start_breathing(LED_BLUE, self.BREATHING_BOOT_CYCLE_SEC)

    def indicate_ready(self):
        """
        Indicate device ready state.

        Displays solid blue LED to show device is ready to begin therapy.

        Boot Sequence Context:
            - SECONDARY: Set after successful connection to PRIMARY
            - PRIMARY: Set after boot window completes (with or without phone)
        """
        self.solid(LED_BLUE)

    def indicate_failure(self):
        """
        Indicate connection failure.

        Displays slow red flashing (1Hz) to show that boot sequence failed.
        This typically means the required PRIMARY-SECONDARY connection could
        not be established within the 30-second boot window.

        Boot Sequence Context:
            - PRIMARY: SECONDARY failed to connect within timeout
            - SECONDARY: PRIMARY not found or connection failed within timeout
        """
        self.slow_flash(LED_RED)

    def indicate_therapy_start(self):
        """
        Indicate therapy session starting.

        Displays 3x white flash to acknowledge that therapy session is
        beginning. This provides visual confirmation to the user that the
        start command was received and therapy will commence.

        The flash sequence takes approximately 600ms to complete.
        """
        self.flash_count(LED_WHITE, 3)

    # ========================================================================
    # Therapy State Methods
    # ========================================================================

    def set_therapy_state(self, state):
        """
        Update LED based on therapy state.

        This method transitions the LED to the appropriate pattern for the
        given therapy state. For RUNNING state, the breathing effect must be
        updated periodically via update_breathing().

        Args:
            state: Current TherapyState to display

        State Priority:
            If multiple conditions occur simultaneously, the highest priority
            state determines the LED pattern (e.g., CONNECTION_LOST overrides
            LOW_BATTERY).
        """
        self.current_state = state

        if state == TherapyState.RUNNING:
            # Solid green - therapy in progress
            self.solid(LED_GREEN)

        elif state == TherapyState.PAUSED:
            # Slow yellow pulse (1Hz)
            self.slow_flash(LED_YELLOW)

        elif state == TherapyState.STOPPING:
            # Fade green to off over 2 seconds
            self.stop_flashing()
            self.fade_out(LED_GREEN, duration_sec=2.0)

        elif state == TherapyState.LOW_BATTERY:
            # Solid orange warning
            self.solid(LED_ORANGE)

        elif state == TherapyState.CRITICAL_BATTERY:
            # Rapid red flash (5Hz)
            self._critical_flash(LED_RED)

        elif state == TherapyState.CONNECTION_LOST:
            # Alternating red/white flash (2Hz)
            self.alternate_flash(
                LED_RED, LED_WHITE, frequency=self.CONNECTION_LOST_FLASH_HZ
            )

        elif state == TherapyState.PHONE_DISCONNECTED:
            # Brief blue flash then return to breathing
            self.stop_flashing()
            self.flash_once(LED_BLUE, duration_ms=100)
            time.sleep(0.1)
            self.set_therapy_state(TherapyState.RUNNING)

        elif state == TherapyState.READY:
            # Solid blue (ready to start)
            self.solid(LED_BLUE)

        elif state == TherapyState.IDLE:
            # LED off
            self.off()

        else:
            # Unknown state - turn off LED
            self.off()

    def update_breathing(self):
        """
        Update breathing effect for boot sequence.

        This method should be called periodically (10-50Hz recommended) during
        boot to animate the breathing LED effect when waiting for phone connection.
        The effect uses a sinusoidal wave to smoothly vary LED intensity.
        The breathing rate is time-based, so call frequency only affects smoothness, not speed.

        Note: Therapy states (RUNNING) use solid colors, not breathing.
              Breathing is only used during boot sequence.

        Usage:
            led.indicate_waiting_for_phone()
            while waiting_for_phone:
                led.update_breathing()  # Call periodically for smooth animation
                time.sleep(0.02)  # 50Hz for smoothest animation
        """
        # Check if boot breathing is active
        is_boot_breathing = hasattr(self, "_breathing") and self._breathing

        if not is_boot_breathing:
            return

        # Initialize timing if needed
        if not hasattr(self, "_breathing_start_time"):
            self._breathing_start_time = time.monotonic()

        # Use configured color and cycle
        color = self._breathing_color
        cycle_sec = self._breathing_cycle_sec

        # Calculate phase based on elapsed time (time-based, not call-count-based)
        elapsed = time.monotonic() - self._breathing_start_time
        self.breathing_phase = (2 * math.pi * elapsed) / cycle_sec

        # Calculate intensity using sinusoidal breathing pattern
        intensity_range = self.BREATHING_MAX_INTENSITY - self.BREATHING_MIN_INTENSITY
        intensity = self.BREATHING_MIN_INTENSITY + intensity_range * (
            0.5 + 0.5 * math.sin(self.breathing_phase)
        )

        # Scale color channels by intensity
        scaled_color = tuple(int(c * intensity) for c in color)
        self._set_color(scaled_color)

    def fade_out(self, color, duration_sec):
        """
        Fade from color to off over specified duration.

        Smoothly fades the LED from the specified color to off using 50 steps.
        Used for graceful therapy stopping indication.

        Args:
            color: RGB tuple (0-255) to fade from
            duration_sec: Duration of fade in seconds
        """
        steps = 50
        delay = duration_sec / steps

        for i in range(steps, 0, -1):
            # Scale color by remaining intensity
            scaled = tuple(int(c * i / steps) for c in color)
            self._set_color(scaled)
            time.sleep(delay)

        self.off()

    def alternate_flash(self, color1, color2, frequency):
        """
        Alternate between two colors at specified frequency.

        Used for critical connection lost state - alternates between red and
        white to create a distinctive urgent pattern.

        This method runs indefinitely until stopped externally via
        stop_flashing() or set_therapy_state().

        Args:
            color1: First RGB tuple (0-255)
            color2: Second RGB tuple (0-255)
            frequency: Frequency in Hz (complete cycles per second)
        """
        self._flashing = True
        period = 1.0 / frequency
        half_period = period / 2.0

        while self._flashing:
            self._set_color(color1)
            time.sleep(half_period)
            self._set_color(color2)
            time.sleep(half_period)

    def flash_once(self, color, duration_ms):
        """
        Flash LED once for specified duration.

        Used for brief acknowledgments like phone disconnection notification.

        Args:
            color: RGB tuple (0-255)
            duration_ms: Flash duration in milliseconds
        """
        self._set_color(color)
        time.sleep(duration_ms / 1000.0)
        self.off()

    def _critical_flash(self, color):
        """
        Flash LED at critical frequency (5Hz).

        Internal method for critical battery flashing pattern.

        Args:
            color: RGB tuple (0-255)
        """
        self._flashing = True
        period = 1.0 / self.CRITICAL_FLASH_HZ
        half_period = period / 2.0

        while self._flashing:
            self._set_color(color)
            time.sleep(half_period)
            self._set_color(LED_OFF)
            time.sleep(half_period)

    def indicate_battery_status(
        self, voltage, low_threshold=3.4, critical_threshold=3.3
    ):
        """
        Update LED based on battery voltage.

        Convenience method to set LED state based on battery voltage thresholds.

        Args:
            voltage: Current battery voltage in volts
            low_threshold: Low battery threshold (default: 3.4V)
            critical_threshold: Critical battery threshold (default: 3.3V)

        Behavior:
            - voltage < critical_threshold: Rapid red flash (CRITICAL_BATTERY)
            - voltage < low_threshold: Solid orange (LOW_BATTERY)
            - voltage >= low_threshold: No change (battery OK)
        """
        if voltage < critical_threshold:
            self.set_therapy_state(TherapyState.CRITICAL_BATTERY)
        elif voltage < low_threshold:
            self.set_therapy_state(TherapyState.LOW_BATTERY)
        # If voltage is OK, don't change current state

    def reset_breathing_phase(self):
        """
        Reset breathing animation phase to start.

        Useful when restarting therapy or synchronizing breathing across
        multiple devices.
        """
        self._breathing_start_time = time.monotonic()
        self.breathing_phase = 0.0

    # ========================================================================
    # Utility Methods
    # ========================================================================

    def is_flashing(self):
        """
        Check if LED is currently in a flashing pattern.

        Returns:
            True if a flash pattern is active, False otherwise
        """
        return self._flashing

    def get_current_state(self):
        """
        Get a string description of the current LED state.

        Returns:
            String describing current state ("flashing", "solid", "off")
        """
        if self._flashing:
            return "flashing"
        elif self.pixels is not None and self.pixels[0] != LED_OFF:
            return "solid"
        else:
            return "off"

    def get_state_description(self):
        """
        Get human-readable description of current therapy state.

        Returns:
            String description of current state and LED pattern
        """
        if self.current_state == TherapyState.RUNNING:
            return "Running: solid green"
        elif self.current_state == TherapyState.PAUSED:
            return "Paused: slow yellow pulse (1Hz)"
        elif self.current_state == TherapyState.STOPPING:
            return "Stopping: fading green to off"
        elif self.current_state == TherapyState.LOW_BATTERY:
            return "Low Battery: solid orange"
        elif self.current_state == TherapyState.CRITICAL_BATTERY:
            return "Critical Battery: rapid red flash (5Hz)"
        elif self.current_state == TherapyState.CONNECTION_LOST:
            return "Connection Lost: alternating red/white (2Hz)"
        elif self.current_state == TherapyState.PHONE_DISCONNECTED:
            return "Phone Disconnected: brief blue flash"
        elif self.current_state == TherapyState.READY:
            return "Ready: solid green"
        elif self.current_state == TherapyState.IDLE:
            return "Idle: LED off"
        else:
            return "Unknown state"
