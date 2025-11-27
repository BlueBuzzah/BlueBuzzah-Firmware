"""
Core Type Definitions
=====================
Type definitions for the BlueBuzzah v2 system.

This module defines the core types, enums, and classes used throughout
the BlueBuzzah bilateral haptic therapy system. These are plain Python
classes and enums compatible with CircuitPython.

Types:
    - DeviceRole: PRIMARY or SECONDARY device role (Enum)
    - TherapyState: Session state machine states (Enum)
    - BootResult: Boot sequence outcomes (Enum)
    - ActuatorType: Haptic actuator types (Enum)
    - SyncCommandType: Synchronization command types (Enum)
    - BatteryStatus: Battery status information (Class)
    - DeviceConfig: Device configuration data (Class)
    - SessionInfo: Therapy session information (Class)

Module: core.types
Version: 2.0.0
"""

# Simple Enum implementation for CircuitPython compatibility
class Enum:
    """
    Simple Enum base class for CircuitPython.

    This is a lightweight enum implementation that works without metaclasses.
    Each enum value is an instance of the enum class.

    Usage:
        class Color(Enum):
            RED = "red"
            GREEN = "green"

        # Access values
        print(Color.RED.value)  # "red"

        # Comparison
        if color == Color.RED:
            ...
    """
    def __init__(self, value, name=None):
        self._value = value
        self._name = name

    @property
    def value(self):
        return self._value

    def __eq__(self, other):
        if isinstance(other, self.__class__):
            return self._value == other._value
        return False

    def __ne__(self, other):
        return not self.__eq__(other)

    def __hash__(self):
        return hash(self._value)

    def __repr__(self):
        if self._name:
            return f"{self.__class__.__name__}.{self._name}"
        return f"{self.__class__.__name__}({self._value!r})"

    def __str__(self):
        return str(self._value)


class DeviceRole(Enum):
    """
    Device role in the bilateral therapy system.

    The BlueBuzzah system consists of two devices working in coordination:
    - PRIMARY: Initiates therapy, controls timing, manages phone connection
    - SECONDARY: Follows PRIMARY commands, maintains synchronization

    Usage:
        role = DeviceRole.PRIMARY
        if role == DeviceRole.PRIMARY:
            advertise_ble()
        else:
            scan_for_primary()
    """
    def __init__(self, value, name=None):
        super().__init__(value, name)

    def is_primary(self):
        """Check if this is the PRIMARY role."""
        return self == DeviceRole.PRIMARY

    def is_secondary(self):
        """Check if this is the SECONDARY role."""
        return self == DeviceRole.SECONDARY

# Create enum instances
DeviceRole.PRIMARY = DeviceRole("Primary", "PRIMARY")
DeviceRole.SECONDARY = DeviceRole("Secondary", "SECONDARY")


class TherapyState(Enum):
    """
    Therapy session state machine states.

    State transitions:
        IDLE -> CONNECTING -> READY -> RUNNING <-> PAUSED -> STOPPING -> IDLE
                                   |                             ^
                                   v                             |
                              ERROR/LOW_BATTERY/CRITICAL_BATTERY/CONNECTION_LOST

    States:
        IDLE: No active session, waiting for commands
        CONNECTING: Establishing BLE connections during boot
        READY: Connected and ready to start therapy
        RUNNING: Actively delivering therapy
        PAUSED: Therapy temporarily suspended by user
        STOPPING: Graceful shutdown in progress
        ERROR: Unrecoverable error occurred
        LOW_BATTERY: Battery below warning threshold (< 3.4V)
        CRITICAL_BATTERY: Battery critically low (< 3.3V), immediate shutdown
        CONNECTION_LOST: PRIMARY-SECONDARY connection lost during therapy
        PHONE_DISCONNECTED: Phone connection lost (informational, therapy continues)

    Usage:
        state = TherapyState.IDLE
        if state == TherapyState.RUNNING:
            execute_therapy_cycle()
        elif state == TherapyState.PAUSED:
            display_paused_led()
    """
    def is_active(self):
        """Check if state represents an active session (running or paused)."""
        return self in (TherapyState.RUNNING, TherapyState.PAUSED)

    def is_error(self):
        """Check if state represents an error condition."""
        return self in (
            TherapyState.ERROR,
            TherapyState.CRITICAL_BATTERY,
            TherapyState.CONNECTION_LOST,
        )

    def is_battery_warning(self):
        """Check if state represents a battery warning."""
        return self in (TherapyState.LOW_BATTERY, TherapyState.CRITICAL_BATTERY)

    def can_start_therapy(self):
        """Check if therapy can be started from this state."""
        return self in (TherapyState.IDLE, TherapyState.READY)

    def can_pause(self):
        """Check if therapy can be paused from this state."""
        return self == TherapyState.RUNNING

    def can_resume(self):
        """Check if therapy can be resumed from this state."""
        return self == TherapyState.PAUSED

# Create enum instances
TherapyState.IDLE = TherapyState("idle", "IDLE")
TherapyState.CONNECTING = TherapyState("connecting", "CONNECTING")
TherapyState.READY = TherapyState("ready", "READY")
TherapyState.RUNNING = TherapyState("running", "RUNNING")
TherapyState.PAUSED = TherapyState("paused", "PAUSED")
TherapyState.STOPPING = TherapyState("stopping", "STOPPING")
TherapyState.ERROR = TherapyState("error", "ERROR")
TherapyState.LOW_BATTERY = TherapyState("low_battery", "LOW_BATTERY")
TherapyState.CRITICAL_BATTERY = TherapyState("critical_battery", "CRITICAL_BATTERY")
TherapyState.CONNECTION_LOST = TherapyState("connection_lost", "CONNECTION_LOST")
TherapyState.PHONE_DISCONNECTED = TherapyState("phone_disconnected", "PHONE_DISCONNECTED")


class BootResult(Enum):
    """
    Boot sequence outcome.

    Results:
        FAILED: Boot sequence failed (no SECONDARY connection)
        SUCCESS_NO_PHONE: PRIMARY-SECONDARY connected, no phone (default therapy)
        SUCCESS_WITH_PHONE: All connections established (PRIMARY only)
        SUCCESS: Boot successful (SECONDARY only)

    Usage:
        result = await boot_manager.execute_boot_sequence()
        if result == BootResult.FAILED:
            indicate_error()
        elif result == BootResult.SUCCESS_WITH_PHONE:
            wait_for_phone_commands()
        else:
            start_default_therapy()
    """
    def is_success(self):
        """Check if boot was successful."""
        return self != BootResult.FAILED

    def has_phone(self):
        """Check if phone connection was established."""
        return self == BootResult.SUCCESS_WITH_PHONE

# Create enum instances
BootResult.FAILED = BootResult("failed", "FAILED")
BootResult.SUCCESS_NO_PHONE = BootResult("success_no_phone", "SUCCESS_NO_PHONE")
BootResult.SUCCESS_WITH_PHONE = BootResult("success_with_phone", "SUCCESS_WITH_PHONE")
BootResult.SUCCESS = BootResult("success", "SUCCESS")


class ActuatorType(Enum):
    """
    Haptic actuator types supported by the system.

    Types:
        LRA: Linear Resonant Actuator (preferred for vCR therapy)
        ERM: Eccentric Rotating Mass motor

    The DRV2605 driver supports both LRA and ERM actuators with
    different control parameters and waveforms.

    Usage:
        config.actuator_type = ActuatorType.LRA
        drv2605.set_mode(config.actuator_type)
    """
    pass

# Create enum instances
ActuatorType.LRA = ActuatorType("lra", "LRA")
ActuatorType.ERM = ActuatorType("erm", "ERM")


class SyncCommandType(Enum):
    """
    Synchronization command types for PRIMARY-SECONDARY coordination.

    Commands:
        SYNC_ADJ: Periodic time synchronization adjustment
        SYNC_ADJ_START: Signal to start synchronized execution
        EXECUTE_BUZZ: Command to execute specific buzz sequence
        BUZZ_COMPLETE: Acknowledgment of buzz sequence completion
        FIRST_SYNC: Initial time synchronization during boot
        ACK_SYNC_ADJ: Acknowledgment of synchronization adjustment
        START_SESSION: Command to start therapy session
        PAUSE_SESSION: Command to pause therapy session
        RESUME_SESSION: Command to resume paused session
        STOP_SESSION: Command to stop therapy session
        DEACTIVATE: Command to deactivate device/connection
        HEARTBEAT: Periodic heartbeat to verify connection

    Protocol flow:
        1. FIRST_SYNC - Establish initial time offset
        2. SYNC_ADJ - Periodic adjustments (every macrocycle)
        3. START_SESSION - Begin therapy session
        4. EXECUTE_BUZZ - PRIMARY commands SECONDARY to buzz
        5. BUZZ_COMPLETE - SECONDARY confirms completion
        6. HEARTBEAT - Periodic connection verification

    Usage:
        command = SyncCommandType.EXECUTE_BUZZ
        uart.write(f"{command}:{sequence_index}\\n")
    """
    pass

# Create enum instances
SyncCommandType.SYNC_ADJ = SyncCommandType("SYNC_ADJ", "SYNC_ADJ")
SyncCommandType.SYNC_ADJ_START = SyncCommandType("SYNC_ADJ_START", "SYNC_ADJ_START")
SyncCommandType.EXECUTE_BUZZ = SyncCommandType("EXECUTE_BUZZ", "EXECUTE_BUZZ")
SyncCommandType.BUZZ_COMPLETE = SyncCommandType("BUZZ_COMPLETE", "BUZZ_COMPLETE")
SyncCommandType.FIRST_SYNC = SyncCommandType("FIRST_SYNC", "FIRST_SYNC")
SyncCommandType.ACK_SYNC_ADJ = SyncCommandType("ACK_SYNC_ADJ", "ACK_SYNC_ADJ")
SyncCommandType.START_SESSION = SyncCommandType("START_SESSION", "START_SESSION")
SyncCommandType.PAUSE_SESSION = SyncCommandType("PAUSE_SESSION", "PAUSE_SESSION")
SyncCommandType.RESUME_SESSION = SyncCommandType("RESUME_SESSION", "RESUME_SESSION")
SyncCommandType.STOP_SESSION = SyncCommandType("STOP_SESSION", "STOP_SESSION")
SyncCommandType.DEACTIVATE = SyncCommandType("DEACTIVATE", "DEACTIVATE")
SyncCommandType.HEARTBEAT = SyncCommandType("HEARTBEAT", "HEARTBEAT")


class BatteryStatus:
    """
    Battery status information.

    Attributes:
        voltage: Current battery voltage in volts
        percentage: Estimated battery percentage (0-100)
        status: Status string ("OK", "LOW", "CRITICAL")
        is_low: True if voltage below LOW_VOLTAGE threshold
        is_critical: True if voltage below CRITICAL_VOLTAGE threshold

    Usage:
        battery = check_battery_voltage()
        if battery.is_critical:
            shutdown_immediately()
        elif battery.is_low:
            show_warning()
    """
    def __init__(self, voltage, percentage, status, is_low=False, is_critical=False):
        self.voltage = voltage
        self.percentage = percentage
        self.status = status
        self.is_low = is_low
        self.is_critical = is_critical

    def __str__(self):
        """String representation for logging."""
        return f"{self.voltage:.2f}V ({self.percentage}%, {self.status})"

    def is_ok(self):
        """Check if battery status is OK (not low or critical)."""
        return not (self.is_low or self.is_critical)

    def requires_action(self):
        """Check if battery status requires user action."""
        return self.is_low or self.is_critical


# Type alias for BLE connection objects
# In CircuitPython, this would be an adafruit_ble.connection.Connection


class DeviceConfig:
    """
    Device-specific configuration loaded from settings.json.

    Attributes:
        role: Device role (PRIMARY or SECONDARY)
        ble_name: BLE advertising/scanning name
        device_tag: Logging prefix tag

    This configuration is loaded from settings.json on device startup
    and determines the device's behavior in the bilateral system.

    Usage:
        config = DeviceConfig.from_settings_file("/settings.json")
        if config.role == DeviceRole.PRIMARY:
            advertise(config.ble_name)
    """
    def __init__(self, role, ble_name, device_tag):
        self.role = role
        self.ble_name = ble_name
        self.device_tag = device_tag

    @classmethod
    def from_dict(cls, data):
        """
        Create DeviceConfig from dictionary.

        Args:
            data: Dictionary with "deviceRole" key

        Returns:
            DeviceConfig instance

        Raises:
            ValueError: If role is invalid
        """
        role_str = data.get("deviceRole", "Primary")

        # Parse role
        if role_str == "Primary":
            role = DeviceRole.PRIMARY
        elif role_str == "Secondary":
            role = DeviceRole.SECONDARY
        else:
            raise ValueError(f"Invalid device role: {role_str}")

        # Generate BLE name and tag based on role
        ble_name = "BlueBuzzah"  # Both devices advertise/scan for "BlueBuzzah"
        device_tag = f"[{role_str}]"

        return cls(
            role=role,
            ble_name=ble_name,
            device_tag=device_tag,
        )

    @classmethod
    def from_settings_file(cls, path="/settings.json"):
        """
        Load device configuration from settings.json file.

        Args:
            path: Path to settings.json file (default: /settings.json)

        Returns:
            DeviceConfig instance

        Raises:
            OSError: If settings.json doesn't exist
            ValueError: If configuration is invalid

        Example settings.json:
            {
                "deviceRole": "Primary"
            }
        """
        import json

        try:
            with open(path, "r") as f:
                data = json.load(f)
                return cls.from_dict(data)
        except (OSError, IOError) as e:
            raise OSError(f"Could not load settings from {path}: {e}")


class SessionInfo:
    """
    Therapy session information.

    Attributes:
        session_id: Unique session identifier
        start_time: Session start timestamp (monotonic time)
        duration_sec: Total session duration in seconds
        elapsed_sec: Elapsed time in seconds (excluding pauses)
        profile_name: Therapy profile name
        state: Current therapy state

    Usage:
        session = SessionInfo(
            session_id="session_001",
            start_time=time.monotonic(),
            duration_sec=7200,
            elapsed_sec=0,
            profile_name="noisy_vcr",
            state=TherapyState.RUNNING
        )
    """
    def __init__(self, session_id, start_time, duration_sec, elapsed_sec, profile_name, state):
        self.session_id = session_id
        self.start_time = start_time
        self.duration_sec = duration_sec
        self.elapsed_sec = elapsed_sec
        self.profile_name = profile_name
        self.state = state

    def __str__(self):
        """String representation for logging."""
        return f"Session {self.session_id}: {self.profile_name}, {self.elapsed_sec}s/{self.duration_sec}s, {self.state}"

    def progress_percentage(self):
        """Calculate session progress as percentage (0-100)."""
        if self.duration_sec <= 0:
            return 0
        progress = int((self.elapsed_sec / self.duration_sec) * 100)
        return min(100, max(0, progress))

    def remaining_sec(self):
        """Calculate remaining session time in seconds."""
        remaining = self.duration_sec - self.elapsed_sec
        return max(0, remaining)

    def is_complete(self):
        """Check if session has reached its duration."""
        return self.elapsed_sec >= self.duration_sec
