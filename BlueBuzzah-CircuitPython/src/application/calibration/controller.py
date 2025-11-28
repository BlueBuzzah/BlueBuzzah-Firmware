"""
Calibration Controller
======================

Manages calibration mode for testing individual haptic actuators and adjusting
parameters. Allows finger-by-finger testing with configurable amplitude and
duration.

The CalibrationController provides a safe environment for testing haptic
output without running full therapy sessions.
"""

import time
from micropython import const

# TODO: These imports reference modules that don't exist yet
# from state.machine import StateTrigger, TherapyStateMachine

# Temporary stubs for missing classes
class StateTrigger:
    """Placeholder for state machine triggers."""
    ENTER_CALIBRATION = "ENTER_CALIBRATION"
    EXIT_CALIBRATION = "EXIT_CALIBRATION"

class TherapyStateMachine:
    """Placeholder state machine."""
    def __init__(self):
        pass

    def transition(self, trigger, metadata=None):
        """Stub transition method."""
        return True


class CalibrationData:
    """
    Data collected during calibration.

    Attributes:
        finger_index: Index of calibrated finger (0-7)
        amplitude: Tested amplitude (0-100%)
        frequency: Tested frequency (Hz)
        duration_ms: Test duration in milliseconds
        timestamp: When calibration was performed
        glove_side: Which glove (LEFT/RIGHT)
        success: Whether test completed successfully
    """

    def __init__(self, finger_index, amplitude, frequency, duration_ms, timestamp, glove_side, success):
        self.finger_index = finger_index
        self.amplitude = amplitude
        self.frequency = frequency
        self.duration_ms = duration_ms
        self.timestamp = timestamp
        self.glove_side = glove_side
        self.success = success


class CalibrationController:
    """
    Controls calibration mode for individual finger testing.

    The CalibrationController provides:
        - Safe entry/exit from calibration mode
        - Individual finger testing (buzz commands)
        - Amplitude and duration adjustment
        - Calibration history and results
        - Integration with state machine and event bus

    Calibration mode allows testing each finger independently to verify
    haptic hardware is functioning correctly and to find comfortable
    amplitude settings.

    Example:
        >>> state_machine = TherapyStateMachine()
        >>>
        >>> # Define callbacks
        >>> def on_started(finger, side):
        ...     print(f"Testing {side} finger {finger}")
        >>>
        >>> controller = CalibrationController(
        ...     state_machine,
        ...     on_calibration_started=on_started
        ... )
        >>>
        >>> # Enter calibration mode
        >>> if controller.start_calibration():
        ...     print("Calibration mode active")
        >>>
        >>> # Test a finger
        >>> controller.buzz_finger(finger=0, amplitude=80, duration_ms=500)
        >>>
        >>> # Exit calibration mode
        >>> controller.stop_calibration()
        >>>
        >>> # Get results
        >>> results = controller.get_calibration_results()
    """

    # Finger index mapping (0-7 = thumb to pinky for left/right)
    FINGER_NAMES = {
        0: "Left Thumb",
        1: "Left Index",
        2: "Left Middle",
        3: "Left Ring",
        4: "Right Thumb",
        5: "Right Index",
        6: "Right Middle",
        7: "Right Ring",
    }

    # Safety limits - use const() to store in flash instead of RAM
    MIN_AMPLITUDE = const(0)
    MAX_AMPLITUDE = const(100)
    MIN_DURATION_MS = const(50)
    MAX_DURATION_MS = const(2000)
    MIN_FREQUENCY = const(150)
    MAX_FREQUENCY = const(300)

    def __init__(
        self,
        state_machine,
        on_calibration_started=None,
        on_calibration_complete=None,
        haptic_controller=None,
    ):
        """
        Initialize the calibration controller.

        Args:
            state_machine: State machine for managing system state
            on_calibration_started: Callback when calibration starts (finger_index, glove_side)
            on_calibration_complete: Callback when calibration completes (finger_index, glove_side, passed)
            haptic_controller: Optional haptic controller for actuating motors
        """
        self._state_machine = state_machine
        self._on_calibration_started = on_calibration_started
        self._on_calibration_complete = on_calibration_complete
        self._haptic_controller = haptic_controller
        self._is_calibrating = False
        self._calibration_history = []
        self._max_history = 50

    def start_calibration(self, finger= None):
        """
        Enter calibration mode.

        Args:
            finger: Optional specific finger to calibrate (0-7)

        Returns:
            True if calibration mode started successfully, False otherwise

        Raises:
            RuntimeError: If system is not in a state that allows calibration

        Example:
            >>> if controller.start_calibration():
            ...     print("Ready to test fingers")
        """
        # Check current state - can only calibrate when IDLE or READY
        current_state = self._state_machine.get_current_state()
        if current_state.is_active():
            raise RuntimeError(
                "Cannot enter calibration mode during active therapy session. "
                "Stop session first."
            )

        if self._is_calibrating:
            return False

        self._is_calibrating = True

        # Call calibration started callback
        if finger is not None and self._on_calibration_started:
            self._validate_finger_index(finger)
            glove_side = "LEFT" if finger < 4 else "RIGHT"
            self._on_calibration_started(finger, glove_side)

        print("[CalibrationController] Calibration mode started")
        return True

    def stop_calibration(self):
        """
        Exit calibration mode.

        Returns:
            True if calibration mode stopped successfully, False otherwise

        Example:
            >>> controller.stop_calibration()
        """
        if not self._is_calibrating:
            return False

        self._is_calibrating = False
        print("[CalibrationController] Calibration mode stopped")
        return True

    def buzz_finger(
        self, finger, amplitude, duration_ms, frequency= 175
    ):
        """
        Activate a specific finger for testing.

        Args:
            finger: Finger index (0-7)
                0-3: Left glove (thumb to ring)
                4-7: Right glove (thumb to ring)
            amplitude: Vibration amplitude (0-100%)
            duration_ms: Duration in milliseconds (50-2000)
            frequency: Vibration frequency in Hz (150-300, default 175)

        Returns:
            True if buzz command executed successfully, False otherwise

        Raises:
            ValueError: If parameters are out of range
            RuntimeError: If not in calibration mode

        Example:
            >>> # Test left index finger at 80% for 500ms
            >>> controller.buzz_finger(finger=1, amplitude=80, duration_ms=500)
        """
        if not self._is_calibrating:
            raise RuntimeError(
                "Not in calibration mode. Call start_calibration() first."
            )

        # Validate parameters
        self._validate_finger_index(finger)
        self._validate_amplitude(amplitude)
        self._validate_duration(duration_ms)
        self._validate_frequency(frequency)

        # Determine glove side
        glove_side = "LEFT" if finger < 4 else "RIGHT"
        finger_name = self.FINGER_NAMES.get(finger, "Finger {}".format(finger))

        print(
            "[CalibrationController] Buzzing {} (amplitude={}%, duration={}ms, freq={}Hz)".format(finger_name, amplitude, duration_ms, frequency)
        )

        # Execute buzz via haptic controller
        success = False
        if self._haptic_controller:
            try:
                # Assuming haptic controller has method:
                # buzz_actuator(finger_index, amplitude, duration_ms, frequency)
                # success = self._haptic_controller.buzz_actuator(
                #     finger, amplitude, duration_ms, frequency
                # )
                success = True  # Placeholder
            except Exception as e:
                print("[CalibrationController] Error buzzing finger: {}".format(e))
                success = False
        else:
            # Simulate buzz for testing without hardware
            success = True

        # Record calibration data
        calibration_data = CalibrationData(
            finger_index=finger,
            amplitude=amplitude,
            frequency=frequency,
            duration_ms=duration_ms,
            timestamp=time.monotonic(),
            glove_side=glove_side,
            success=success,
        )
        self._add_to_history(calibration_data)

        # Call calibration complete callback
        if self._on_calibration_complete:
            self._on_calibration_complete(finger, glove_side, success)

        return success

    def get_calibration_results(self):
        """
        Get all calibration test results.

        Returns:
            List of CalibrationData objects (most recent first)

        Example:
            >>> results = controller.get_calibration_results()
            >>> for data in results:
            ...     print(f"Finger {data.finger_index}: {data.amplitude}%")
        """
        return list(reversed(self._calibration_history))

    def get_finger_name(self, finger_index):
        """
        Get human-readable name for a finger index.

        Args:
            finger_index: Finger index (0-7)

        Returns:
            Finger name string

        Example:
            >>> name = controller.get_finger_name(1)
            >>> print(name)  # "Left Index"
        """
        return self.FINGER_NAMES.get(finger_index, "Finger {}".format(finger_index))

    def is_calibrating(self):
        """
        Check if currently in calibration mode.

        Returns:
            True if in calibration mode, False otherwise

        Example:
            >>> if controller.is_calibrating():
            ...     print("Calibration mode active")
        """
        return self._is_calibrating

    def clear_history(self):
        """
        Clear calibration history.

        Example:
            >>> controller.clear_history()
        """
        self._calibration_history.clear()

    def get_last_calibration(
        self, finger= None
    ):
        """
        Get the most recent calibration data.

        Args:
            finger: Optional finger index to filter by

        Returns:
            Most recent CalibrationData, or None if no history

        Example:
            >>> last = controller.get_last_calibration(finger=0)
            >>> if last:
            ...     print(f"Last tested at {last.amplitude}%")
        """
        if not self._calibration_history:
            return None

        if finger is None:
            return self._calibration_history[-1]

        # Find most recent for specific finger
        for data in reversed(self._calibration_history):
            if data.finger_index == finger:
                return data

        return None

    def test_all_fingers(
        self, amplitude= 80, duration_ms= 500, delay_ms= 200
    ):
        """
        Test all fingers in sequence.

        Args:
            amplitude: Amplitude for all tests (0-100%)
            duration_ms: Duration for each test (50-2000ms)
            delay_ms: Delay between finger tests (ms)

        Returns:
            Dictionary mapping finger index to success status

        Example:
            >>> results = controller.test_all_fingers(amplitude=80)
            >>> for finger, success in results.items():
            ...     if not success:
            ...         print(f"Finger {finger} failed")
        """
        if not self._is_calibrating:
            raise RuntimeError("Not in calibration mode")

        results = {}

        for finger in range(8):
            success = self.buzz_finger(finger, amplitude, duration_ms)
            results[finger] = success

            # Delay between fingers
            if finger < 7 and delay_ms > 0:
                time.sleep(delay_ms / 1000.0)

        return results

    def _validate_finger_index(self, finger):
        """Validate finger index is in valid range."""
        if not isinstance(finger, int) or not (0 <= finger <= 7):
            raise ValueError(
                "Finger index must be 0-7 (0-3=Left, 4-7=Right), got {}".format(finger)
            )

    def _validate_amplitude(self, amplitude):
        """Validate amplitude is in safe range."""
        if not isinstance(amplitude, int) or not (
            self.MIN_AMPLITUDE <= amplitude <= self.MAX_AMPLITUDE
        ):
            raise ValueError(
                "Amplitude must be {}-{}%, got {}".format(self.MIN_AMPLITUDE, self.MAX_AMPLITUDE, amplitude)
            )

    def _validate_duration(self, duration_ms):
        """Validate duration is in safe range."""
        if not isinstance(duration_ms, int) or not (
            self.MIN_DURATION_MS <= duration_ms <= self.MAX_DURATION_MS
        ):
            raise ValueError(
                "Duration must be {}-{}ms, got {}".format(self.MIN_DURATION_MS, self.MAX_DURATION_MS, duration_ms)
            )

    def _validate_frequency(self, frequency):
        """Validate frequency is in safe range."""
        if not isinstance(frequency, int) or not (
            self.MIN_FREQUENCY <= frequency <= self.MAX_FREQUENCY
        ):
            raise ValueError(
                "Frequency must be {}-{}Hz, got {}".format(self.MIN_FREQUENCY, self.MAX_FREQUENCY, frequency)
            )

    def _add_to_history(self, data):
        """Add calibration data to history, maintaining max size."""
        self._calibration_history.append(data)

        # Trim history if too long
        if len(self._calibration_history) > self._max_history:
            self._calibration_history = self._calibration_history[-self._max_history :]
