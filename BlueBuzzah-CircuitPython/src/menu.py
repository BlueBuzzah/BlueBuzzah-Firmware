"""
Menu Command Layer
==================
Consolidated command parsing, handling, and response formatting for BlueBuzzah v2.

This module implements all 18 commands from BLE_PROTOCOL.md v2.0.0 in a single
MenuController class. Replaces the complex communication/protocol and
application/commands layers with a simple, direct implementation.

Commands (18 total):
    Device Info, BATTERY, PING
    Profiles, PROFILE_LOAD, PROFILE_GET, PROFILE_CUSTOM
    Session, SESSION_PAUSE, SESSION_RESUME, SESSION_STOP, SESSION_STATUS
    Parameters: PARAM_SET
    Calibration, CALIBRATE_BUZZ, CALIBRATE_STOP
    System, RESTART

Module: menu
Version: 2.0.0
"""

import time


# Message terminator (EOT character)
EOT = "\x04"

# Internal messages that should be passed through (not filtered here)
INTERNAL_MESSAGES = [
    "EXECUTE_BUZZ",
    "BUZZ_COMPLETE",
    "PARAM_UPDATE",
    "SEED",
    "SEED_ACK",
    "GET_BATTERY",
    "BATRESPONSE",
    "ACK_PARAM_UPDATE",
]


class MenuController:
    """
    Menu command controller for BlueBuzzah.

    Handles:
    - Command parsing from BLE strings
    - All 18 protocol command handlers
    - Response formatting (KEY:VALUE with EOT)
    - Error handling
    - Internal message pass-through
    - State validation (no profile changes during sessions)

    Usage:
        menu = MenuController(
            session_manager=session_mgr,
            profile_manager=profile_mgr,
            calibration_controller=calib_ctrl,
            battery_monitor=battery_mon,
            device_role="PRIMARY",
            firmware_version="2.0.0"
        )

        # Parse and handle command
        response = menu.handle_command("BATTERY\n")
        # Returns: "BATP:3.72\nBATS:3.68\n\x04"
    """

    def __init__(
        self,
        session_manager,
        profile_manager,
        calibration_controller,
        battery_monitor,
        device_role= "PRIMARY",
        firmware_version= "2.0.0",
        device_name= "BlueBuzzah"
    ):
        """
        Initialize menu controller.

        Args:
            session_manager: SessionManager instance
            profile_manager: ProfileManager instance
            calibration_controller: CalibrationController instance
            battery_monitor: BatteryMonitor instance
            device_role: Device role (PRIMARY or SECONDARY)
            firmware_version: Firmware version string
            device_name: Device name for INFO command
        """
        self.session_mgr = session_manager
        self.profile_mgr = profile_manager
        self.calib_ctrl = calibration_controller
        self.battery_mon = battery_monitor

        self.device_role = device_role
        self.firmware_version = firmware_version
        self.device_name = device_name

        # Command dispatch table
        self._commands = {
            "INFO": self.handle_info,
            "BATTERY": self.handle_battery,
            "PING": self.handle_ping,
            "PROFILE_LIST": self.handle_profile_list,
            "PROFILE_LOAD": self.handle_profile_load,
            "PROFILE_GET": self.handle_profile_get,
            "PROFILE_CUSTOM": self.handle_profile_custom,
            "SESSION_START": self.handle_session_start,
            "SESSION_PAUSE": self.handle_session_pause,
            "SESSION_RESUME": self.handle_session_resume,
            "SESSION_STOP": self.handle_session_stop,
            "SESSION_STATUS": self.handle_session_status,
            "PARAM_SET": self.handle_param_set,
            "CALIBRATE_START": self.handle_calibrate_start,
            "CALIBRATE_BUZZ": self.handle_calibrate_buzz,
            "CALIBRATE_STOP": self.handle_calibrate_stop,
            "HELP": self.handle_help,
            "RESTART": self.handle_restart,
        }

    def parse_command(self, message):
        """
        Parse command from BLE message string.

        Args:
            message: Raw message string (e.g., "BATTERY\n" or "PROFILE_LOAD:2\n")

        Returns:
            Tuple of (command_name, parameters) or None if invalid
        """
        if not message:
            return None

        # Strip newlines and EOT
        message = message.strip().replace('\n', '').replace(EOT, '')

        if not message:
            return None

        # Check if internal message (pass through without parsing)
        if any(message.startswith(prefix) for prefix in INTERNAL_MESSAGES):
            return None  # Internal messages handled separately

        # Split command and parameters
        parts = message.split(':')
        command = parts[0].upper()
        params = parts[1:] if len(parts) > 1 else []

        return (command, params)

    def handle_command(self, message):
        """
        Handle incoming command and generate response.

        Args:
            message: Raw command message

        Returns:
            Formatted response string with EOT terminator
        """
        parsed = self.parse_command(message)

        if parsed is None:
            # Internal message - pass through
            return message if message.endswith(EOT) else message + EOT

        command, params = parsed

        # Dispatch to handler
        if command in self._commands:
            try:
                return self._commands[command](params)
            except Exception as e:
                return self.format_error("Command execution error: {}".format(e))
        else:
            return self.format_error("Unknown command: {}".format(command))

    # ========================================================================
    # Response formatting
    # ========================================================================

    def format_response(self, **kwargs):
        """
        Format KEY:VALUE response with EOT terminator.

        Args:
            **kwargs: Key-value pairs for response

        Returns:
            Formatted response string: "KEY1:VALUE1\nKEY2:VALUE2\n\x04"
        """
        lines = []
        for key, value in kwargs.items():
            lines.append("{}:{}".format(key, value))

        lines.append(EOT)
        return '\n'.join(lines)

    def format_error(self, message):
        """
        Format error response.

        Args:
            message: Error message

        Returns:
            Formatted error: "ERROR:message\n\x04"
        """
        return "ERROR:{}\n{}".format(message, EOT)

    # ========================================================================
    # Device Information Commands
    # ========================================================================

    def handle_info(self, params):
        """
        Handle INFO command.

        Returns device information including role, firmware, battery, and status.
        """
        try:
            # Get battery status
            battery_primary = self.battery_mon.read_voltage()
            battery_secondary = 0.0  # Placeholder - query from SECONDARY

            # Get session status
            session_active = self.session_mgr.is_session_active()
            status = "ACTIVE" if session_active else "IDLE"

            return self.format_response(
                ROLE=self.device_role,
                NAME=self.device_name,
                FW=self.firmware_version,
                BATP="{:.2f}".format(battery_primary),
                BATS="{:.2f}".format(battery_secondary),
                STATUS=status
            )

        except Exception as e:
            return self.format_error("Info error: {}".format(e))

    def handle_battery(self, params):
        """
        Handle BATTERY command.

        Returns battery voltage for both PRIMARY and SECONDARY gloves.
        Response time: up to 1 second (queries both devices via BLE).
        """
        try:
            # Get PRIMARY battery
            battery_primary = self.battery_mon.read_voltage()

            # Get SECONDARY battery (placeholder - actual implementation queries via BLE)
            battery_secondary = 0.0  # TODO: Query from SECONDARY device

            return self.format_response(
                BATP="{:.2f}".format(battery_primary),
                BATS="{:.2f}".format(battery_secondary)
            )

        except Exception as e:
            return self.format_error("Battery read error: {}".format(e))

    def handle_ping(self, params):
        """
        Handle PING command.

        Simple connection test - returns PONG immediately.
        """
        return self.format_response(PONG="")

    # ========================================================================
    # Profile Management Commands
    # ========================================================================

    def handle_profile_list(self, params):
        """
        Handle PROFILE_LIST command.

        Returns list of available therapy profiles.
        """
        try:
            profiles = self.profile_mgr.list_profiles()

            # Format as multiple PROFILE lines
            lines = []
            for profile_id, profile_name in profiles:
                lines.append("PROFILE:{}:{}".format(profile_id, profile_name))
            lines.append(EOT)

            return '\n'.join(lines)

        except Exception as e:
            return self.format_error("Profile list error: {}".format(e))

    def handle_profile_load(self, params):
        """
        Handle PROFILE_LOAD command.

        Load therapy profile by ID.
        Cannot be called during active session.

        Args:
            params[0]: Profile ID (1, 2, or 3)
        """
        # Validate session state
        if self.session_mgr.is_session_active():
            return self.format_error("Cannot modify parameters during active session")

        if not params:
            return self.format_error("Profile ID required")

        try:
            profile_id = int(params[0])

            # Load profile
            profile = self.profile_mgr.load_profile(profile_id)

            if profile is None:
                return self.format_error("Invalid profile ID")

            return self.format_response(
                STATUS="LOADED",
                PROFILE=profile.name
            )

        except ValueError:
            return self.format_error("Invalid profile ID format")
        except Exception as e:
            return self.format_error("Profile load error: {}".format(e))

    def handle_profile_get(self, params):
        """
        Handle PROFILE_GET command.

        Returns current profile settings.
        """
        try:
            profile = self.profile_mgr.get_current_profile()

            if profile is None:
                return self.format_error("No profile loaded")

            return self.format_response(
                TYPE=profile.actuator_type,
                FREQ=str(profile.frequency_hz),
                VOLT="{:.2f}".format(profile.voltage),
                ON="{:.3f}".format(profile.time_on_sec),
                OFF="{:.3f}".format(profile.time_off_sec),
                SESSION=str(profile.session_duration_min),
                AMPMIN=str(profile.amplitude_min_percent),
                AMPMAX=str(profile.amplitude_max_percent),
                PATTERN=profile.pattern_type,
                MIRROR=str(profile.mirror_patterns),
                JITTER="{:.1f}".format(profile.jitter_percent)
            )

        except Exception as e:
            return self.format_error("Profile get error: {}".format(e))

    def handle_profile_custom(self, params):
        """
        Handle PROFILE_CUSTOM command.

        Set custom therapy parameters.
        Cannot be called during active session.

        Format: PROFILE_CUSTOM:KEY1:VALUE1:KEY2:VALUE2:...
        """
        # Validate session state
        if self.session_mgr.is_session_active():
            return self.format_error("Cannot modify parameters during active session")

        if len(params) < 2 or len(params) % 2 != 0:
            return self.format_error("Invalid parameter format")

        try:
            # Parse key-value pairs
            custom_params = {}
            for i in range(0, len(params), 2):
                key = params[i].upper()
                value = params[i + 1]
                custom_params[key] = value

            # Apply custom parameters
            result = self.profile_mgr.set_custom_profile(custom_params)

            if result:
                # Echo back applied parameters
                return self.format_response(
                    STATUS="CUSTOM_LOADED",
                    **custom_params
                )
            else:
                return self.format_error("Invalid parameter name or value out of range")

        except Exception as e:
            return self.format_error("Custom profile error: {}".format(e))

    # ========================================================================
    # Session Control Commands
    # ========================================================================

    def handle_session_start(self, params):
        """
        Handle SESSION_START command.

        Start therapy session with current profile.
        Response time: up to 500ms (establishes PRIMARY-SECONDARY sync).
        """
        try:
            # Check prerequisites
            if self.session_mgr.is_session_active():
                return self.format_error("Session already active")

            # Check battery levels
            battery_voltage = self.battery_mon.read_voltage()
            if battery_voltage < 3.3:
                return self.format_error("Battery too low")

            # Check SECONDARY connection (PRIMARY only)
            if self.device_role == "PRIMARY":
                # TODO: Check if SECONDARY is connected
                pass

            # Get current profile to pass to start_session
            profile = self.profile_mgr.get_current_profile()
            if profile is None:
                return self.format_error("No profile loaded")

            # Start session with profile
            success = self.session_mgr.start_session(profile)

            if success:
                return self.format_response(SESSION_STATUS="RUNNING")
            else:
                return self.format_error("Failed to start session")

        except Exception as e:
            return self.format_error("Session start error: {}".format(e))

    def handle_session_pause(self, params):
        """
        Handle SESSION_PAUSE command.

        Pause active therapy session.
        """
        try:
            if not self.session_mgr.is_session_active():
                return self.format_error("No active session")

            success = self.session_mgr.pause_session()

            if success:
                return self.format_response(SESSION_STATUS="PAUSED")
            else:
                return self.format_error("Failed to pause session")

        except Exception as e:
            return self.format_error("Session pause error: {}".format(e))

    def handle_session_resume(self, params):
        """
        Handle SESSION_RESUME command.

        Resume paused therapy session.
        """
        try:
            # Check if session is active and paused
            session_info = self.session_mgr.get_session_info()
            if session_info is None:
                return self.format_error("No active session")

            # Check if session is actually paused
            if not hasattr(session_info.state, 'can_resume') or not session_info.state.can_resume():
                return self.format_error("Session is not paused")

            success = self.session_mgr.resume_session()

            if success:
                return self.format_response(SESSION_STATUS="RUNNING")
            else:
                return self.format_error("Failed to resume session")

        except Exception as e:
            return self.format_error("Session resume error: {}".format(e))

    def handle_session_stop(self, params):
        """
        Handle SESSION_STOP command.

        Stop active therapy session.
        """
        try:
            success = self.session_mgr.stop_session()

            if success:
                return self.format_response(SESSION_STATUS="IDLE")
            else:
                # Allow stop even if no session active
                return self.format_response(SESSION_STATUS="IDLE")

        except Exception as e:
            return self.format_error("Session stop error: {}".format(e))

    def handle_session_status(self, params):
        """
        Handle SESSION_STATUS command.

        Get current session status with elapsed time and progress.
        """
        try:
            session_info = self.session_mgr.get_session_info()

            if session_info is None:
                # No active session, return IDLE status
                return self.format_response(
                    SESSION_STATUS="IDLE",
                    ELAPSED="0",
                    TOTAL="0",
                    PROGRESS="0"
                )

            # Get state name (convert TherapyState enum to string)
            state_name = str(session_info.state).split('.')[-1].upper()

            return self.format_response(
                SESSION_STATUS=state_name,
                ELAPSED=str(session_info.elapsed_sec),
                TOTAL=str(session_info.duration_sec),
                PROGRESS=str(int(session_info.progress_percentage()))
            )

        except Exception as e:
            return self.format_error("Session status error: {}".format(e))

    # ========================================================================
    # Parameter Adjustment Command
    # ========================================================================

    def handle_param_set(self, params):
        """
        Handle PARAM_SET command.

        Set individual therapy parameter.
        Cannot be called during active session.

        Format: PARAM_SET:PARAM_NAME:VALUE
        """
        # Validate session state
        if self.session_mgr.is_session_active():
            return self.format_error("Cannot modify parameters during active session")

        if len(params) < 2:
            return self.format_error("Parameter name and value required")

        try:
            param_name = params[0].upper()
            param_value = params[1]

            # Set parameter
            success = self.profile_mgr.set_parameter(param_name, param_value)

            if success:
                return self.format_response(
                    PARAM=param_name,
                    VALUE=param_value
                )
            else:
                return self.format_error("Invalid parameter name or value out of range")

        except Exception as e:
            return self.format_error("Parameter set error: {}".format(e))

    # ========================================================================
    # Calibration Commands
    # ========================================================================

    def handle_calibrate_start(self, params):
        """
        Handle CALIBRATE_START command.

        Enter calibration mode for testing individual tactors.
        """
        try:
            success = self.calib_ctrl.start_calibration()

            if success:
                return self.format_response(MODE="CALIBRATION")
            else:
                return self.format_error("Failed to enter calibration mode")

        except Exception as e:
            return self.format_error("Calibration start error: {}".format(e))

    def handle_calibrate_buzz(self, params):
        """
        Handle CALIBRATE_BUZZ command.

        Test individual tactor.

        Format: CALIBRATE_BUZZ:FINGER:INTENSITY:DURATION
        - FINGER: 0-7 (0-3 = PRIMARY, 4-7 = SECONDARY)
        - INTENSITY: 0-100 (%)
        - DURATION: 50-2000 (milliseconds)
        """
        if not self.calib_ctrl.is_calibrating():
            return self.format_error("Not in calibration mode")

        if len(params) < 3:
            return self.format_error("Finger, intensity, and duration required")

        try:
            finger = int(params[0])
            intensity = int(params[1])
            duration = int(params[2])

            # Validate ranges
            if not (0 <= finger <= 7):
                return self.format_error("Invalid finger index")
            if not (0 <= intensity <= 100):
                return self.format_error("Intensity out of range")
            if not (50 <= duration <= 2000):
                return self.format_error("Duration out of range")

            # Execute buzz
            success = self.calib_ctrl.test_finger(finger, intensity, duration)

            if success:
                return self.format_response(
                    FINGER=str(finger),
                    INTENSITY=str(intensity),
                    DURATION=str(duration)
                )
            else:
                return self.format_error("Calibration buzz failed")

        except ValueError:
            return self.format_error("Invalid parameter format")
        except Exception as e:
            return self.format_error("Calibration buzz error: {}".format(e))

    def handle_calibrate_stop(self, params):
        """
        Handle CALIBRATE_STOP command.

        Exit calibration mode and return to normal operation.
        """
        try:
            success = self.calib_ctrl.stop_calibration()

            if success:
                return self.format_response(MODE="NORMAL")
            else:
                # Allow stop even if not calibrating
                return self.format_response(MODE="NORMAL")

        except Exception as e:
            return self.format_error("Calibration stop error: {}".format(e))

    # ========================================================================
    # System Commands
    # ========================================================================

    def handle_help(self, params):
        """
        Handle HELP command.

        List all available commands.
        """
        # Format as multiple COMMAND lines
        lines = []
        for cmd_name in sorted(self._commands.keys()):
            lines.append("COMMAND:{}".format(cmd_name))
        lines.append(EOT)

        return '\n'.join(lines)

    def handle_restart(self, params):
        """
        Handle RESTART command.

        Reboot device to menu mode.
        Connection will drop after this command.
        """
        try:
            # Send response before restarting
            response = self.format_response(STATUS="REBOOTING")

            # TODO: Schedule restart after response is sent
            # import microcontroller
            # microcontroller.reset()

            return response

        except Exception as e:
            return self.format_error("Restart error: {}".format(e))

    # ========================================================================
    # Utility methods
    # ========================================================================

    def is_internal_message(self, message):
        """
        Check if message is internal synchronization message.

        Args:
            message: Message string

        Returns:
            True if internal message
        """
        message = message.strip().replace('\n', '').replace(EOT, '')

        return any(message.startswith(prefix) for prefix in INTERNAL_MESSAGES)
