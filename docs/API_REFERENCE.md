# BlueBuzzah v2 API Reference

**Comprehensive API documentation for BlueBuzzah v2 bilateral haptic therapy system**

Version: 2.0.0
Last Updated: 2025-01-11

---

## Table of Contents

- [Overview](#overview)
- [Core Types and Constants](#core-types-and-constants)
- [Configuration System](#configuration-system)
- [Event System](#event-system)
- [State Machine](#state-machine)
- [Hardware Abstraction](#hardware-abstraction)
- [BLE Communication](#ble-communication)
- [Therapy Engine](#therapy-engine)
- [Synchronization Protocol](#synchronization-protocol)
- [Application Layer](#application-layer)
- [LED UI](#led-ui)
- [Usage Examples](#usage-examples)

---

## Overview

BlueBuzzah v2 provides a clean, layered API for bilateral haptic therapy control. The architecture follows Clean Architecture principles with clear separation between layers:

- **Core**: Types, constants, and fundamental definitions
- **Configuration**: System and therapy configuration management
- **Events**: Event-driven communication between components
- **State**: Explicit state machine for therapy sessions
- **Hardware**: Hardware abstraction for haptic controllers, battery, etc.
- **Communication**: BLE protocol and message handling
- **Therapy**: Pattern generation and therapy execution
- **Application**: High-level use cases and workflows

---

## Core Types and Constants

### Module: `core.types`

#### DeviceRole

Device role in the bilateral therapy system.

```python
from core.types import DeviceRole

class DeviceRole(Enum):
    PRIMARY = "Primary"     # Initiates therapy, controls timing
    SECONDARY = "Secondary" # Follows PRIMARY commands
```

**Methods:**

```python
def is_primary()
```
Check if this is the PRIMARY role. Returns bool.

```python
def is_secondary()
```
Check if this is the SECONDARY role. Returns bool.

**Usage:**
```python
role = DeviceRole.PRIMARY
if role.is_primary():
    advertise_ble()
else:
    scan_for_primary()
```

---

#### TherapyState

Therapy session state machine states.

```python
class TherapyState(Enum):
    IDLE = "idle"
    CONNECTING = "connecting"
    READY = "ready"
    RUNNING = "running"
    PAUSED = "paused"
    STOPPING = "stopping"
    ERROR = "error"
    LOW_BATTERY = "low_battery"
    CRITICAL_BATTERY = "critical_battery"
    CONNECTION_LOST = "connection_lost"
    PHONE_DISCONNECTED = "phone_disconnected"
```

**State Descriptions:**

| State | Description |
|-------|-------------|
| `IDLE` | No active session, waiting for commands |
| `CONNECTING` | Establishing BLE connections during boot |
| `READY` | Connected and ready to start therapy |
| `RUNNING` | Actively delivering therapy |
| `PAUSED` | Therapy temporarily suspended by user |
| `STOPPING` | Graceful shutdown in progress |
| `ERROR` | Unrecoverable error occurred |
| `LOW_BATTERY` | Battery below warning threshold (<3.4V) |
| `CRITICAL_BATTERY` | Battery critically low (<3.3V), immediate shutdown |
| `CONNECTION_LOST` | PRIMARY-SECONDARY connection lost during therapy |
| `PHONE_DISCONNECTED` | Phone connection lost (informational, therapy continues) |

**State Transitions:**
```
IDLE → CONNECTING → READY → RUNNING ⇄ PAUSED → STOPPING → IDLE
                        ↓                           ↑
                      ERROR/LOW_BATTERY/CRITICAL_BATTERY/CONNECTION_LOST
```

**Note:** `PHONE_DISCONNECTED` is an informational state - therapy continues normally when the phone disconnects. Only `CONNECTION_LOST` (PRIMARY-SECONDARY) stops therapy.

**Methods:**

```python
def is_active()
```
Check if state represents an active session (RUNNING or PAUSED). Returns bool.

```python
def is_error()
```
Check if state represents an error condition. Returns bool.

```python
def is_battery_warning()
```
Check if state represents a battery warning. Returns bool.

```python
def can_start_therapy()
```
Check if therapy can be started from this state. Returns bool.

```python
def can_pause()
```
Check if therapy can be paused from this state. Returns bool.

```python
def can_resume()
```
Check if therapy can be resumed from this state. Returns bool.

**Usage:**
```python
state = TherapyState.RUNNING
if state.is_active():
    execute_therapy_cycle()
elif state.can_start_therapy():
    start_new_session()
```

---

#### BootResult

Boot sequence outcome enumeration.

```python
class BootResult(Enum):
    FAILED = "failed"
    SUCCESS_NO_PHONE = "success_no_phone"
    SUCCESS_WITH_PHONE = "success_with_phone"
    SUCCESS = "success"  # For SECONDARY
```

**Methods:**

```python
def is_success()
```
Check if boot was successful. Returns bool.

```python
def has_phone()
```
Check if phone connection was established. Returns bool.

**Usage:**
```python
result = boot_manager.execute_boot_sequence()
if result.is_success():
    if result.has_phone():
        wait_for_phone_commands()
    else:
        start_default_therapy()
```

---

#### BatteryStatus

Battery status information class.

```python
class BatteryStatus:
    """
    Battery status container.

    Attributes:
        voltage - Current voltage in volts (float)
        percentage - Battery percentage 0-100 (int)
        status - Status string: "OK", "LOW", or "CRITICAL"
        is_low - True if below LOW_VOLTAGE threshold
        is_critical - True if below CRITICAL_VOLTAGE threshold
    """
    def __init__(self, voltage, percentage, status, is_low, is_critical):
        self.voltage = voltage
        self.percentage = percentage
        self.status = status
        self.is_low = is_low
        self.is_critical = is_critical
```

**Methods:**

```python
def is_ok()
```
Check if battery status is OK (not low or critical). Returns bool.

```python
def requires_action()
```
Check if battery status requires user action. Returns bool.

**Usage:**
```python
battery = battery_monitor.get_status()
if battery.is_critical:
    shutdown_immediately()
elif battery.is_low:
    show_warning_led()
print(f"Battery: {battery.voltage:.2f}V ({battery.percentage}%)")
```

---

#### SessionInfo

Therapy session information class.

```python
class SessionInfo:
    """
    Therapy session information container.

    Attributes:
        session_id - Unique session identifier (str)
        start_time - Session start timestamp, monotonic time (float)
        duration_sec - Total session duration in seconds (int)
        elapsed_sec - Elapsed time in seconds, excluding pauses (int)
        profile_name - Therapy profile name (str)
        state - Current therapy state (TherapyState)
    """
    def __init__(self, session_id, start_time, duration_sec, elapsed_sec, profile_name, state):
        self.session_id = session_id
        self.start_time = start_time
        self.duration_sec = duration_sec
        self.elapsed_sec = elapsed_sec
        self.profile_name = profile_name
        self.state = state
```

**Methods:**

```python
def progress_percentage()
```
Calculate session progress as percentage (0-100). Returns int.

```python
def remaining_sec()
```
Calculate remaining session time in seconds. Returns int.

```python
def is_complete()
```
Check if session has reached its duration. Returns bool.

**Usage:**
```python
session = SessionInfo(
    session_id="session_001",
    start_time=time.monotonic(),
    duration_sec=7200,
    elapsed_sec=3600,
    profile_name="noisy_vcr",
    state=TherapyState.RUNNING
)

print(f"Progress: {session.progress_percentage()}%")
print(f"Remaining: {session.remaining_sec()}s")
```

---

### Module: `core.constants`

System-wide constants for timing, hardware, battery thresholds, and more.

#### Firmware Version

```python
FIRMWARE_VERSION = "2.0.0"
```
Current firmware version following semantic versioning.

---

#### Timing Constants

```python
STARTUP_WINDOW = const(30)
```
Boot sequence connection window in seconds.

```python
CONNECTION_TIMEOUT = const(30)
```
BLE connection establishment timeout in seconds.

```python
BLE_INTERVAL = 0.0075
```
BLE connection interval in seconds (7.5ms) for sub-10ms synchronization.

```python
SYNC_INTERVAL = 1.0
```
Periodic synchronization interval in seconds (SYNC_ADJ messages).

```python
COMMAND_TIMEOUT = 5.0
```
General BLE command timeout in seconds.

---

#### Hardware Constants

```python
I2C_MULTIPLEXER_ADDR = const(0x70)
```
TCA9548A I2C multiplexer address.

```python
DRV2605_DEFAULT_ADDR = const(0x5A)
```
DRV2605 haptic driver I2C address.

```python
I2C_FREQUENCY = const(400000)
```
I2C bus frequency in Hz (400 kHz Fast Mode).

```python
MAX_ACTUATORS = const(5)
```
Maximum number of haptic actuators per device. Note: In practice, 4 actuators
are used per glove (thumb through ring finger).

```python
MAX_AMPLITUDE = const(100)
```
Maximum haptic amplitude percentage (0-100).

---

#### Pin Assignments

```python
NEOPIXEL_PIN = "D13"
```
NeoPixel LED data pin for visual feedback.

```python
BATTERY_PIN = "A6"
```
Battery voltage monitoring analog pin.

```python
I2C_SDA_PIN = "SDA"
```
I2C data line pin.

```python
I2C_SCL_PIN = "SCL"
```
I2C clock line pin.

---

#### LED Colors

```python
LED_BLUE = (0, 0, 255)      # BLE operations
LED_GREEN = (0, 255, 0)     # Success/Normal
LED_RED = (255, 0, 0)       # Error/Critical
LED_WHITE = (255, 255, 255) # Special indicators
LED_YELLOW = (255, 255, 0)  # Paused
LED_ORANGE = (255, 128, 0)  # Low battery
LED_OFF = (0, 0, 0)         # LED off
```

---

#### Battery Thresholds

```python
CRITICAL_VOLTAGE = 3.3
```
Critical battery voltage (immediate shutdown).

```python
LOW_VOLTAGE = 3.4
```
Low battery warning voltage (warning, therapy continues).

```python
BATTERY_CHECK_INTERVAL = 60.0
```
Battery voltage check interval in seconds during therapy.

---

#### Memory Management

```python
GC_THRESHOLD = const(4096)
```
Garbage collection threshold in bytes.

```python
GC_ENABLED = True
```
Enable automatic garbage collection.

---

## Configuration System

### Module: `config.base`

#### BaseConfig

Base configuration with system defaults.

```python
class BaseConfig:
    """
    Base configuration container.

    Attributes:
        device_role - DeviceRole (PRIMARY or SECONDARY)
        firmware_version - Firmware version string (default "2.0.0")
        startup_window_sec - Boot sequence timeout (default 30)
        connection_timeout_sec - Connection timeout (default 30)
        ble_interval - BLE connection interval (default 0.0075)
        sync_interval - Sync message interval (default 1.0)
    """
    def __init__(self, device_role, firmware_version="2.0.0",
                 startup_window_sec=30, connection_timeout_sec=30,
                 ble_interval=0.0075, sync_interval=1.0):
        self.device_role = device_role
        self.firmware_version = firmware_version
        self.startup_window_sec = startup_window_sec
        self.connection_timeout_sec = connection_timeout_sec
        self.ble_interval = ble_interval
        self.sync_interval = sync_interval
```

**Usage:**
```python
from config.base import BaseConfig
from core.types import DeviceRole

config = BaseConfig(
    device_role=DeviceRole.PRIMARY,
    startup_window_sec=30
)
```

---

### Module: `config.therapy`

#### TherapyConfig

Therapy-specific configuration.

```python
class TherapyConfig:
    """
    Therapy configuration container.

    Attributes:
        profile_name - Profile identifier (str)
        burst_duration_ms - Burst duration in ms (int)
        inter_burst_interval_ms - Interval between bursts in ms (int)
        bursts_per_cycle - Number of bursts per cycle (int)
        pattern_type - Pattern generation algorithm (str)
        actuator_type - LRA or ERM (ActuatorType)
        frequency_hz - Haptic frequency in Hz (int)
        amplitude_percent - Amplitude 0-100 (int)
        jitter_percent - Timing jitter percentage (default 0.0)

    Note: All patterns use mandatory bilateral mirroring - the same anatomical
    finger is always stimulated simultaneously on both hands.
    """
    def __init__(self, profile_name, burst_duration_ms, inter_burst_interval_ms,
                 bursts_per_cycle, pattern_type, actuator_type, frequency_hz,
                 amplitude_percent, jitter_percent=0.0):
        self.profile_name = profile_name
        self.burst_duration_ms = burst_duration_ms
        self.inter_burst_interval_ms = inter_burst_interval_ms
        self.bursts_per_cycle = bursts_per_cycle
        self.pattern_type = pattern_type
        self.actuator_type = actuator_type
        self.frequency_hz = frequency_hz
        self.amplitude_percent = amplitude_percent
        self.jitter_percent = jitter_percent
```

**Class Methods:**

```python
@classmethod
def default_noisy_vcr(cls)
```
Create default Noisy vCR therapy configuration. Returns TherapyConfig.

```python
@classmethod
def default_regular_vcr(cls)
```
Create default Regular vCR therapy configuration. Returns TherapyConfig.

```python
@classmethod
def default_hybrid_vcr(cls)
```
Create default Hybrid vCR therapy configuration. Returns TherapyConfig.

**Usage:**
```python
from config.therapy import TherapyConfig

# Use default profile
config = TherapyConfig.default_noisy_vcr()

# Custom profile
custom = TherapyConfig(
    profile_name="custom_research",
    burst_duration_ms=100,
    inter_burst_interval_ms=668,
    bursts_per_cycle=3,
    pattern_type="random_permutation",
    actuator_type=ActuatorType.LRA,
    frequency_hz=175,
    amplitude_percent=100,
    jitter_percent=23.5
)
```

---

### Module: `config.loader`

#### ConfigLoader

Loads and validates configuration from JSON files.

```python
class ConfigLoader:
    @staticmethod
    def load_from_json(path)
        # Returns Config dict

    @staticmethod
    def validate(config)
        # Returns list of error strings
```

**Usage:**
```python
from config.loader import ConfigLoader

# Load configuration
config = ConfigLoader.load_from_json("/settings.json")

# Validate configuration
errors = ConfigLoader.validate(config)
if errors:
    print(f"Configuration errors: {errors}")
```

---

## Event System

### Module: `events.bus`

#### EventBus

Publish-subscribe event bus for decoupled component communication.

```python
class EventBus:
    def __init__(self):
        self._handlers = {}

    def subscribe(self, event_type, handler):
        # Register handler for event_type

    def unsubscribe(self, event_type, handler):
        # Remove handler for event_type

    def publish(self, event):
        # Dispatch event to all registered handlers

    def clear(self):
        # Remove all handlers
```

**Usage:**
```python
from events.bus import EventBus
from events.events import SessionStartedEvent, BatteryLowEvent

bus = EventBus()

# Subscribe to events
def on_session_started(event: SessionStartedEvent):
    print(f"Session {event.session_id} started")

def on_battery_low(event: BatteryLowEvent):
    print(f"Battery low: {event.voltage}V")

bus.subscribe(SessionStartedEvent, on_session_started)
bus.subscribe(BatteryLowEvent, on_battery_low)

# Publish events
bus.publish(SessionStartedEvent(session_id="001", profile="noisy_vcr"))
bus.publish(BatteryLowEvent(voltage=3.3, percentage=15))

# Cleanup
bus.unsubscribe(SessionStartedEvent, on_session_started)
```

---

### Module: `events.events`

#### Event Classes

Base event class and specific event types.

```python
class Event:
    """Base event class."""
    def __init__(self):
        self.timestamp = time.monotonic()

class SessionStartedEvent(Event):
    def __init__(self, session_id, profile, duration_sec):
        super().__init__()
        self.session_id = session_id
        self.profile = profile
        self.duration_sec = duration_sec

class SessionPausedEvent(Event):
    def __init__(self, session_id):
        super().__init__()
        self.session_id = session_id

class SessionResumedEvent(Event):
    def __init__(self, session_id):
        super().__init__()
        self.session_id = session_id

class SessionStoppedEvent(Event):
    def __init__(self, session_id, elapsed_sec, cycles_completed):
        super().__init__()
        self.session_id = session_id
        self.elapsed_sec = elapsed_sec
        self.cycles_completed = cycles_completed

class BatteryLowEvent(Event):
    def __init__(self, voltage, percentage):
        super().__init__()
        self.voltage = voltage
        self.percentage = percentage

class BatteryCriticalEvent(Event):
    def __init__(self, voltage):
        super().__init__()
        self.voltage = voltage

class ConnectionLostEvent(Event):
    def __init__(self, device_type):
        super().__init__()
        self.device_type = device_type  # "PRIMARY", "SECONDARY", or "PHONE"

class StateTransitionEvent(Event):
    def __init__(self, from_state, to_state, trigger):
        super().__init__()
        self.from_state = from_state
        self.to_state = to_state
        self.trigger = trigger
```

**Usage:**
```python
from events.events import SessionStartedEvent
import time

event = SessionStartedEvent(
    session_id="session_001",
    profile="noisy_vcr",
    duration_sec=7200,
    timestamp=time.monotonic()
)
```

---

## State Machine

### Module: `state.machine`

#### TherapyStateMachine

Explicit state machine for therapy session management.

```python
class TherapyStateMachine:
    def __init__(self, event_bus=None):
        self._state = TherapyState.IDLE
        self._event_bus = event_bus
        self._observers = []

    def get_current_state(self):
        # Returns current TherapyState

    def transition(self, trigger):
        # Attempt state transition, returns bool success

    def can_transition(self, trigger):
        # Check if transition is valid, returns bool

    def reset(self):
        # Reset to IDLE state

    def add_observer(self, callback):
        # Register callback for state changes
```

**StateTrigger Enum:**
```python
class StateTrigger(Enum):
    POWER_ON = "power_on"
    CONNECT = "connect"
    CONNECTED = "connected"
    DISCONNECTED = "disconnected"
    START_SESSION = "start_session"
    PAUSE_SESSION = "pause_session"
    RESUME_SESSION = "resume_session"
    STOP_SESSION = "stop_session"
    SESSION_COMPLETE = "session_complete"
    ERROR_OCCURRED = "error_occurred"
    CRITICAL_BATTERY = "critical_battery"
    CONNECTION_LOST = "connection_lost"
    RESET = "reset"
```

**Usage:**
```python
from state.machine import TherapyStateMachine, StateTrigger
from events.bus import EventBus

# Create state machine
bus = EventBus()
state_machine = TherapyStateMachine(event_bus=bus)

# Check current state
print(f"Current state: {state_machine.get_current_state()}")

# Attempt transition
if state_machine.can_transition(StateTrigger.START_SESSION):
    success = state_machine.transition(StateTrigger.START_SESSION)
    if success:
        print("Session started successfully")

# Add observer for state changes
def on_state_change(from_state, to_state):
    print(f"State changed: {from_state} → {to_state}")

state_machine.add_observer(on_state_change)

# Reset state machine
state_machine.reset()
```

---

## Hardware Abstraction

### Module: `hardware.haptic`

#### HapticController Interface

Interface for haptic motor control. Note: CircuitPython does not support
ABC or async/await. This documents the expected interface.

```python
class HapticController:
    """Interface for haptic motor control."""

    def activate(self, finger, amplitude):
        """Activate motor for specified finger (0-4)."""
        raise NotImplementedError

    def deactivate(self, finger):
        """Deactivate motor for specified finger."""
        raise NotImplementedError

    def deactivate_all(self):
        """Deactivate all motors."""
        raise NotImplementedError

    def is_active(self, finger):
        """Check if motor is currently active. Returns bool."""
        raise NotImplementedError
```

---

#### DRV2605Controller

Implementation for DRV2605 haptic drivers via I2C multiplexer.

```python
class DRV2605Controller(HapticController):
    def __init__(self, multiplexer, actuator_type=ActuatorType.LRA,
                 frequency_hz=175, i2c_addr=0x5A):
        self.multiplexer = multiplexer
        self.actuator_type = actuator_type
        self.default_frequency = frequency_hz
        self.i2c_addr = i2c_addr
        self.active_fingers = {}
        self.frequencies = {}

    def activate(self, finger, amplitude):
        # Activate motor (finger 0-4, amplitude 0-100%)

    def deactivate(self, finger):
        # Deactivate motor for finger

    def stop_all(self, force_all=False):
        # Stop all motors

    def is_active(self, finger):
        # Returns bool

    def set_frequency(self, finger, frequency):
        # Set resonant frequency for LRA (150-250 Hz)
```

**Usage:**
```python
from hardware import DRV2605Controller, I2CMultiplexer, BoardConfig
from core.types import ActuatorType

# Initialize board and multiplexer
board_config = BoardConfig()
mux = I2CMultiplexer(board_config.i2c, address=0x70)

# Create haptic controller
haptic = DRV2605Controller(
    multiplexer=mux,
    actuator_type=ActuatorType.LRA,
    frequency_hz=175
)

# Initialize finger
haptic.initialize_finger(0)

# Activate motor
haptic.activate(finger=0, amplitude=100)  # Thumb at 100%
time.sleep(0.1)
haptic.deactivate(finger=0)

# Stop all motors
haptic.stop_all()
```

---

### Module: `hardware.battery`

#### BatteryMonitor

Battery voltage monitoring and status reporting.

```python
class BatteryMonitor:
    def __init__(self, adc_pin, divider_ratio=2.0, sample_count=10,
                 warning_voltage=3.4, critical_voltage=3.3):
        self.pin = adc_pin
        self.divider_ratio = divider_ratio
        self.sample_count = sample_count
        self.warning_voltage = warning_voltage
        self.critical_voltage = critical_voltage

    def read_voltage(self):
        # Returns float voltage

    def get_percentage(self, voltage=None):
        # Returns int percentage 0-100

    def get_status(self, voltage=None):
        # Returns BatteryStatus

    def is_low(self, voltage=None):
        # Returns bool

    def is_critical(self, voltage=None):
        # Returns bool
```

**Usage:**
```python
from hardware.battery import BatteryMonitor
import board

# Create battery monitor
battery = BatteryMonitor(
    battery_pin=board.A6,
    voltage_divider_ratio=2.0,
    low_voltage=3.4,
    critical_voltage=3.3
)

# Read voltage
voltage = battery.read_voltage()
print(f"Battery: {voltage:.2f}V")

# Get full status
status = battery.get_status()
print(f"Status: {status.status} ({status.percentage}%)")

# Check critical conditions
if battery.is_critical():
    shutdown_device()
elif battery.is_low():
    show_warning()
```

---

### Module: `hardware.multiplexer`

#### I2CMultiplexer

TCA9548A I2C multiplexer for managing multiple DRV2605 devices.

```python
class I2CMultiplexer:
    def __init__(self, i2c, address=0x70):
        self.i2c = i2c
        self.address = address
        self.active_channel = None

    def select_channel(self, channel):
        # Select channel 0-7

    def deselect_all(self):
        # Disable all channels

    def get_current_channel(self):
        # Returns int or None
```

**Usage:**
```python
from hardware.multiplexer import I2CMultiplexer
import board
import busio

# Initialize I2C
i2c = busio.I2C(board.SCL, board.SDA)

# Create multiplexer
mux = I2CMultiplexer(i2c, address=0x70)

# Select channel for finger 0 (thumb)
mux.select_channel(0)

# Communicate with DRV2605 on channel 0
# ...

# Disable all channels
mux.disable_all_channels()
```

---

### Module: `hardware.led`

#### LEDController (Base Class)

Base NeoPixel LED controller.

```python
class LEDController:
    def __init__(self, pixel_pin, num_pixels=1):
        self.pixel = neopixel.NeoPixel(pixel_pin, num_pixels, brightness=0.1)

    def set_color(self, color):
        # color is (r, g, b) tuple

    def off(self):
        # Turn LED off

    def rapid_flash(self, color, frequency=10.0):
        # Flash at high frequency

    def slow_flash(self, color, frequency=1.0):
        # Flash at low frequency

    def flash_count(self, color, count):
        # Flash specific number of times

    def solid(self, color):
        # Set solid color
```

**Usage:**
```python
from hardware.led import LEDController
from core.constants import LED_GREEN, LED_RED
import board

# Create LED controller
led = LEDController(pixel_pin=board.D13, num_pixels=1)

# Solid green
led.solid(LED_GREEN)

# Flash red 5 times
led.flash_count(LED_RED, count=5)

# Turn off
led.off()
```

---

## BLE Communication

### Module: `communication.ble.service`

#### BLEService

Abstracted BLE communication service.

```python
class BLEService:
    def __init__(self, device_name, event_bus=None):
        self.device_name = device_name
        self.event_bus = event_bus
        self._connections = []

    def advertise(self, name):
        # Start BLE advertising

    def scan_and_connect(self, target_name):
        # Scan for and connect to target device

    def send(self, connection, data):
        # Send bytes over connection

    def receive(self, connection):
        # Receive bytes, returns bytes or None

    def disconnect(self, connection):
        # Disconnect from peer

    def is_connected(self):
        # Returns bool

    def get_connections(self):
        # Returns list of connections
```

**Usage:**
```python
from ble import BLEConnection

# Create BLE connection manager
ble = BLEConnection()

# PRIMARY: Start advertising
ble.start_advertising("BlueBuzzah")

# SECONDARY: Scan and connect
ble.scan_and_connect("BlueBuzzah")

# Send data
ble.send(b"PING\n")

# Receive data
data = ble.receive()

# Check connection
if ble.is_connected():
    print("Connected")
```

---

### Module: `communication.protocol.commands`

#### CommandType (Enum)

BLE protocol command types.

```python
class CommandType(Enum):
    # Device Information
    INFO = "INFO"
    BATTERY = "BATTERY"
    PING = "PING"

    # Profile Management
    PROFILE_LIST = "PROFILE_LIST"
    PROFILE_LOAD = "PROFILE_LOAD"
    PROFILE_GET = "PROFILE_GET"
    PROFILE_CUSTOM = "PROFILE_CUSTOM"

    # Session Control
    SESSION_START = "SESSION_START"
    SESSION_PAUSE = "SESSION_PAUSE"
    SESSION_RESUME = "SESSION_RESUME"
    SESSION_STOP = "SESSION_STOP"
    SESSION_STATUS = "SESSION_STATUS"

    # Parameter Adjustment
    PARAM_SET = "PARAM_SET"

    # Calibration
    CALIBRATE_START = "CALIBRATE_START"
    CALIBRATE_BUZZ = "CALIBRATE_BUZZ"
    CALIBRATE_STOP = "CALIBRATE_STOP"

    # System
    HELP = "HELP"
    RESTART = "RESTART"
```

---

#### Command Classes

```python
class Command:
    def __init__(self, command_type, parameters=None, raw=None):
        self.command_type = command_type
        self.parameters = parameters if parameters else {}
        self.raw = raw

class InfoCommand(Command):
    def __init__(self):
        super().__init__(CommandType.INFO)

class SessionStartCommand(Command):
    def __init__(self, profile="", duration_sec=7200):
        super().__init__(CommandType.SESSION_START)
        self.profile = profile
        self.duration_sec = duration_sec

class CalibrateCommand(Command):
    def __init__(self, finger=0, amplitude=100, duration_ms=100):
        super().__init__(CommandType.CALIBRATE_BUZZ)
        self.finger = finger
        self.amplitude = amplitude
        self.duration_ms = duration_ms
```

**Usage:**
```python
from communication.protocol.commands import SessionStartCommand, InfoCommand

# Create commands
info_cmd = InfoCommand()

start_cmd = SessionStartCommand(
    profile="noisy_vcr",
    duration_sec=7200
)

# Access command data
print(f"Command: {start_cmd.command_type}")
print(f"Profile: {start_cmd.profile}")
```

---

### Module: `communication.protocol.handler`

#### ProtocolHandler

BLE protocol message parsing and routing.

```python
class ProtocolHandler:
    def __init__(self, command_processor):
        self.processor = command_processor

    def parse_command(self, data):
        # Parse bytes into Command, returns Command

    def format_response(self, response):
        # Format Response to bytes, returns bytes

    def handle_command(self, command):
        # Process command, returns Response

    def validate_message(self, message):
        # Validate message format, returns bool
```

**Usage:**
```python
from menu import MenuController

# Create menu controller (handles command parsing/routing)
menu = MenuController(session_manager, haptic_controller, ble)

# Process incoming command string
response = menu.process_command("SESSION_START:noisy_vcr:7200")

# Response is a string to send back
print(response)
```

---

## Therapy Engine

### Module: `therapy.engine`

#### TherapyEngine

Core therapy execution engine.

```python
class TherapyEngine:
    def __init__(self, pattern_generator, haptic_controller,
                 battery_monitor=None, state_machine=None):
        self.pattern_gen = pattern_generator
        self.haptic = haptic_controller
        self.battery = battery_monitor
        self.state_machine = state_machine

    def execute_session(self, config, duration_sec):
        # Execute therapy session, returns ExecutionStats

    def execute_cycle(self, config):
        # Execute single therapy cycle

    def execute_pattern(self, pattern):
        # Execute pattern sequence

    def pause(self):
        # Pause therapy

    def resume(self):
        # Resume therapy

    def stop(self):
        # Stop therapy

    def is_paused(self):
        # Returns bool

    def is_stopped(self):
        # Returns bool

    def get_stats(self):
        # Returns ExecutionStats

    def on_cycle_complete(self, callback):
        # Register callback for cycle completion

    def on_battery_warning(self, callback):
        # Register callback for battery warnings

    def on_error(self, callback):
        # Register callback for errors
```

**ExecutionStats:**
```python
class ExecutionStats:
    def __init__(self):
        self.cycles_completed = 0
        self.total_activations = 0
        self.average_cycle_duration_ms = 0.0
        self.timing_drift_ms = 0.0
        self.pause_count = 0
        self.total_pause_duration_sec = 0.0
```

**Usage:**
```python
from therapy import TherapyEngine
from hardware import DRV2605Controller, BatteryMonitor
from state import StateMachine
from config import load_therapy_profile

# Initialize components
haptic = DRV2605Controller(mux)
battery = BatteryMonitor(board_config.battery_sense_pin)
state_machine = StateMachine()

# Create engine
engine = TherapyEngine(
    haptic_controller=haptic,
    battery_monitor=battery,
    state_machine=state_machine
)

# Register callbacks
engine.on_cycle_complete(lambda count: print("Cycle", count))
engine.on_battery_warning(lambda status: print("Battery:", status))

# Execute therapy session
profile = load_therapy_profile("noisy_vcr")
stats = engine.execute_session(profile, duration_sec=7200)

print("Completed", stats.cycles_completed, "cycles")
print("Average cycle duration:", stats.average_cycle_duration_ms, "ms")
```

---

### Module: `therapy.patterns.generator`

#### PatternGenerator Interface

Pattern generation interface. Note: CircuitPython does not support ABC.

```python
class PatternGenerator:
    """Interface for pattern generation."""

    def generate(self, config):
        """Generate therapy pattern from configuration. Returns Pattern."""
        raise NotImplementedError

    def validate_config(self, config):
        """Validate pattern configuration."""
        pass
```

---

#### Pattern

Therapy pattern class.

```python
class Pattern:
    def __init__(self, left_sequence, right_sequence, timing_ms,
                 burst_duration_ms, inter_burst_interval_ms):
        self.left_sequence = left_sequence      # Left hand finger sequence (list)
        self.right_sequence = right_sequence    # Right hand finger sequence (list)
        self.timing_ms = timing_ms              # Inter-burst intervals in ms (list)
        self.burst_duration_ms = burst_duration_ms
        self.inter_burst_interval_ms = inter_burst_interval_ms

    def get_finger_pair(self, index):
        """Get left and right finger for given index. Returns tuple."""
        return (self.left_sequence[index], self.right_sequence[index])

    def get_total_duration_ms(self):
        """Calculate total pattern duration in milliseconds. Returns float."""
        return sum(self.timing_ms) + len(self.timing_ms) * self.burst_duration_ms
```

---

#### PatternConfig

Pattern generation configuration.

All patterns use mandatory bilateral mirroring - the same anatomical finger is always stimulated simultaneously on both hands.

```python
class PatternConfig:
    def __init__(self, num_fingers=4, time_on_ms=100, time_off_ms=67,
                 bursts_per_cycle=3, jitter_percent=0.0, random_seed=None):
        self.num_fingers = num_fingers
        self.time_on_ms = time_on_ms
        self.time_off_ms = time_off_ms
        self.bursts_per_cycle = bursts_per_cycle
        self.jitter_percent = jitter_percent
        self.random_seed = random_seed

    @classmethod
    def from_therapy_config(cls, therapy_config):
        """Convert TherapyConfig to PatternConfig. Returns PatternConfig."""
        pass

    def get_total_duration_ms(self):
        """Calculate total cycle duration in milliseconds. Returns float."""
        pass
```

**Usage:**
```python
from therapy.patterns.generator import PatternConfig, Pattern

# Create configuration
config = PatternConfig(
    num_fingers=4,
    time_on_ms=100,
    time_off_ms=67,
    jitter_percent=23.5
)

# Pattern is generated by PatternGenerator implementations
# Both hands always receive the same finger sequence (bilateral mirroring)
```

---

### Module: `therapy.patterns.rndp`

#### RandomPermutationGenerator

Random permutation pattern generator for Noisy vCR therapy.

```python
class RandomPermutationGenerator(PatternGenerator):
    def __init__(self, random_seed=None):
        self.random_seed = random_seed

    def generate(self, config):
        # Returns Pattern with random permutation sequence
```

**Usage:**
```python
from therapy.patterns.rndp import RandomPermutationGenerator
from therapy.patterns.generator import PatternConfig

# Create generator
generator = RandomPermutationGenerator()

# Generate pattern
config = PatternConfig(jitter_percent=23.5)
pattern = generator.generate(config)

print(f"Left sequence: {pattern.left_sequence}")
print(f"Right sequence: {pattern.right_sequence}")
# Output: [2, 0, 4, 1, 3] (random permutation)
```

---

### Module: `therapy.patterns.sequential`

#### SequentialGenerator

Sequential pattern generator for Regular vCR therapy.

```python
class SequentialGenerator(PatternGenerator):
    def __init__(self):
        pass

    def generate(self, config):
        # Returns Pattern with sequential order
```

**Usage:**
```python
from therapy.patterns.sequential import SequentialGenerator
from therapy.patterns.generator import PatternConfig

# Create generator
generator = SequentialGenerator()

# Generate pattern
config = PatternConfig()
pattern = generator.generate(config)

print(f"Left sequence: {pattern.left_sequence}")
# Output: [0, 1, 2, 3, 4] (sequential order)
```

---

### Module: `therapy.patterns.mirrored`

#### MirroredPatternGenerator

Mirrored bilateral pattern generator for Hybrid vCR therapy.

```python
class MirroredPatternGenerator(PatternGenerator):
    def __init__(self):
        pass

    def generate(self, config):
        # Returns Pattern with mirrored bilateral sequences
```

**Usage:**
```python
from therapy.patterns.mirrored import MirroredPatternGenerator
from therapy.patterns.generator import PatternConfig

# Create generator
generator = MirroredPatternGenerator()

# Generate pattern
config = PatternConfig()
pattern = generator.generate(config)

print(f"Left: {pattern.left_sequence}")
print(f"Right: {pattern.right_sequence}")
# Sequences are mirrored for bilateral symmetry
```

---

## Synchronization Protocol

### Module: `domain.sync.protocol`

#### SyncProtocol

Time synchronization between PRIMARY and SECONDARY devices.

```python
class SyncProtocol:
    def __init__(self):
        self.offset = 0

    def calculate_offset(self, primary_time, secondary_time):
        # Returns int offset in nanoseconds

    def apply_compensation(self, timestamp, offset):
        # Returns int compensated timestamp

    def format_sync_message(self, command_type, payload):
        # Returns bytes formatted message

    def parse_sync_message(self, data):
        # Returns tuple (command_type, payload_dict)
```

**Usage:**
```python
from domain.sync.protocol import SyncProtocol
import time

# Create sync protocol
sync = SyncProtocol()

# Calculate time offset (PRIMARY)
primary_time = time.monotonic_ns()
secondary_time = time.monotonic_ns()  # From SECONDARY
offset = sync.calculate_offset(primary_time, secondary_time)

# Apply compensation (SECONDARY)
compensated_time = sync.apply_compensation(
    timestamp=primary_time,
    offset=offset
)

# Format sync message
message = sync.format_sync_message(
    command_type=SyncCommandType.SYNC_ADJ,
    payload={"timestamp": primary_time}
)

# Parse sync message
cmd_type, payload = sync.parse_sync_message(message)
```

---

### Module: `domain.sync.coordinator`

#### SyncCoordinator

Coordinates bilateral synchronization between devices.

```python
class SyncCoordinator:
    def __init__(self, role, sync_protocol, ble_service):
        self.role = role
        self.sync_protocol = sync_protocol
        self.ble = ble_service

    def establish_sync(self):
        # Returns bool success

    def send_sync_adjustment(self, timestamp):
        # Send SYNC_ADJ message

    def send_execute_command(self, sequence_index, timestamp):
        # Send EXECUTE_BUZZ command

    def wait_for_acknowledgment(self, timeout=2.0):
        # Returns bool success

    def get_time_offset(self):
        # Returns int offset in nanoseconds
```

**Usage:**
```python
from sync import SyncManager
from ble import BLEConnection
from core.types import DeviceRole
import time

# Initialize sync manager (PRIMARY)
ble = BLEConnection()
sync = SyncManager(role=DeviceRole.PRIMARY, ble=ble)

# Establish initial synchronization
success = sync.establish_sync()

# Send periodic sync adjustment
sync.send_sync_adjustment(timestamp=time.monotonic_ns())

# Send execute command
sync.send_execute_command(
    sequence_index=2,
    timestamp=time.monotonic_ns()
)

# Wait for acknowledgment
ack = sync.wait_for_acknowledgment(timeout=2.0)
```

---

## Application Layer

### Module: `application.session.manager`

#### SessionManager

High-level session lifecycle management.

```python
class SessionManager:
    def __init__(self, therapy_engine, profile_manager, event_bus):
        self.engine = therapy_engine
        self.profiles = profile_manager
        self.event_bus = event_bus
        self.current_session = None

    def start_session(self, profile_name, duration_sec):
        # Returns SessionInfo

    def pause_session(self):
        # Pause current session

    def resume_session(self):
        # Resume paused session

    def stop_session(self):
        # Stop current session

    def get_current_session(self):
        # Returns SessionInfo or None

    def get_status(self):
        # Returns dict with session status
```

**Usage:**
```python
from application.session.manager import SessionManager
from therapy import TherapyEngine
from config import load_therapy_profile

# Initialize manager
engine = TherapyEngine(haptic, battery, state_machine)
session_mgr = SessionManager(engine)

# Start session
session = session_mgr.start_session(
    profile_name="noisy_vcr",
    duration_sec=7200
)

# Pause session
session_mgr.pause_session()

# Resume session
session_mgr.resume_session()

# Get status
status = session_mgr.get_status()
print("Session:", status)

# Stop session
session_mgr.stop_session()
```

---

### Module: `application.profile.manager`

#### ProfileManager

Therapy profile management and loading.

```python
class ProfileManager:
    def __init__(self, profiles_dir="/profiles"):
        self.profiles_dir = profiles_dir

    def list_profiles(self):
        # Returns list of profile names

    def load_profile(self, profile_name):
        # Returns TherapyConfig dict

    def save_profile(self, profile_name, config):
        # Save profile to storage

    def delete_profile(self, profile_name):
        # Delete profile from storage

    def get_default_profile(self):
        # Returns default TherapyConfig
```

**Usage:**
```python
from application.profile.manager import ProfileManager

# Create manager
profile_mgr = ProfileManager(profiles_dir="/profiles")

# List available profiles
profiles = profile_mgr.list_profiles()
print(f"Available profiles: {profiles}")

# Load profile
config = profile_mgr.load_profile("noisy_vcr")

# Save custom profile
custom_config = TherapyConfig(...)
profile_mgr.save_profile("custom_research", custom_config)

# Get default profile
default = profile_mgr.get_default_profile()
```

---

### Module: `application.calibration.controller`

#### CalibrationController

Calibration mode for individual finger testing.

```python
class CalibrationController:
    def __init__(self, haptic_controller, event_bus=None):
        self.haptic = haptic_controller
        self.event_bus = event_bus
        self.active = False

    def start_calibration(self):
        # Enter calibration mode

    def test_finger(self, finger, amplitude=100, duration_ms=100):
        # Test individual finger motor

    def test_all_fingers(self, amplitude=100, duration_ms=100):
        # Test all fingers sequentially

    def stop_calibration(self):
        # Exit calibration mode

    def is_calibrating(self):
        # Returns bool
```

**Usage:**
```python
from hardware import DRV2605Controller

# Create controller
haptic = DRV2605Controller(mux)
calibration = CalibrationController(haptic_controller=haptic)

# Start calibration mode
calibration.start_calibration()

# Test individual finger
calibration.test_finger(
    finger=0,       # Thumb
    amplitude=75,   # 75% intensity
    duration_ms=200 # 200ms buzz
)

# Test all fingers sequentially
calibration.test_all_fingers(amplitude=100, duration_ms=100)

# Stop calibration
calibration.stop_calibration()
```

---

### Module: `application.commands.processor`

#### CommandProcessor

Central command routing and processing.

```python
class CommandProcessor:
    def __init__(self, session_manager, profile_manager,
                 calibration_controller, battery_monitor):
        self.session_mgr = session_manager
        self.profile_mgr = profile_manager
        self.calibration = calibration_controller
        self.battery = battery_monitor
        self._handlers = {}

    def process_command(self, command):
        # Process command and return Response

    def register_handler(self, command_type, handler):
        # Register handler for command type
```

**Usage:**
```python
from menu import MenuController

# Create menu controller (wraps command processing)
menu = MenuController(session_mgr, haptic, ble)

# Process command string
response = menu.process_command("SESSION_START:noisy_vcr:7200")

print("Response:", response)
```

---

## LED UI

### Module: `ui.boot_led`

#### BootSequenceLEDController

Specialized LED controller for boot sequence visual feedback.

```python
class BootSequenceLEDController(LEDController):
    def indicate_ble_init(self) -> None

    def indicate_connection_success(self) -> None

    def indicate_waiting_for_phone(self) -> None

    def indicate_ready(self) -> None

    def indicate_failure(self) -> None
```

**Usage:**
```python
from ui.boot_led import BootSequenceLEDController
import board

# Create boot LED controller
led = BootSequenceLEDController(pixel_pin=board.D13)

# Boot sequence stages
led.indicate_ble_init()              # Rapid blue flash
# ... wait for connections ...
led.indicate_connection_success()    # 5x green flash
led.indicate_waiting_for_phone()     # Slow blue flash
# ... timeout or phone connects ...
led.indicate_ready()                 # Solid green

# Or on failure
led.indicate_failure()               # Slow red flash
```

---

### Module: `ui.therapy_led`

#### TherapyLEDController

LED controller for therapy session visual feedback.

```python
class TherapyLEDController(LEDController):
    def set_therapy_state(self, state):
        # Set LED pattern based on TherapyState

    def update_breathing(self):
        # Update breathing animation (call at ~20Hz)

    def fade_out(self, color, duration_sec):
        # Fade LED from color to off

    def alternate_flash(self, color1, color2, frequency):
        # Alternate between two colors
```

**Usage:**
```python
from led import LEDController
from core.types import TherapyState
import board
import time

# Create LED controller
led = LEDController(board.NEOPIXEL)

# Set state-based LED patterns
led.set_therapy_state(TherapyState.RUNNING)  # Breathing green
led.set_therapy_state(TherapyState.PAUSED)   # Slow yellow pulse
led.set_therapy_state(TherapyState.STOPPING) # Fading green

# Update breathing effect (call periodically at ~20Hz)
while therapy_running:
    led.update_breathing()
    time.sleep(0.05)
```

---

## Usage Examples

### Complete System Initialization

```python
import gc
import time
import board
import busio
from config import load_device_config, load_therapy_profile
from hardware import BoardConfig, I2CMultiplexer, DRV2605Controller, BatteryMonitor
from led import LEDController
from ble import BLEConnection
from state import StateMachine
from therapy import TherapyEngine
from application.session.manager import SessionManager
from boot import BootSequence

def main():
    # Collect garbage after imports
    gc.collect()

    # Load device configuration
    config = load_device_config("settings.json")

    # Initialize hardware
    board_config = BoardConfig()
    mux = I2CMultiplexer(board_config.i2c, address=0x70)
    haptic = DRV2605Controller(mux)
    battery = BatteryMonitor(board_config.battery_sense_pin)
    led = LEDController(board_config.neopixel_pin)

    # Initialize fingers
    for finger in range(4):
        haptic.initialize_finger(finger)

    # Initialize BLE
    ble = BLEConnection()

    # Execute boot sequence
    boot = BootSequence(config, ble, led)
    boot_result = boot.execute()

    if not boot_result.is_success():
        print("Boot failed - exiting")
        return

    # Initialize therapy components
    state_machine = StateMachine()
    engine = TherapyEngine(haptic, battery, state_machine)

    # Initialize session manager
    session_mgr = SessionManager(engine)

    # Load therapy profile
    profile = load_therapy_profile("noisy_vcr")

    # Start therapy session
    session = session_mgr.start_session(
        profile_name="noisy_vcr",
        duration_sec=7200
    )

    print("Session started:", session.session_id)

    # Main loop
    while not session.is_complete():
        # Update LED
        led.set_therapy_state(state_machine.get_current_state())
        led.update_breathing()

        # Check for commands from phone
        ble.poll_commands()

        # Yield to system
        time.sleep(0.05)

    print("Session complete")

# Run application
main()
```

---

### Simple Therapy Execution

```python
from therapy import TherapyEngine
from hardware import DRV2605Controller, I2CMultiplexer, BoardConfig
from config import load_therapy_profile

# Initialize hardware
board_config = BoardConfig()
mux = I2CMultiplexer(board_config.i2c)
haptic = DRV2605Controller(mux)

# Initialize fingers
for finger in range(4):
    haptic.initialize_finger(finger)

# Create engine
engine = TherapyEngine(haptic_controller=haptic)

# Load profile and execute therapy
profile = load_therapy_profile("noisy_vcr")
stats = engine.execute_session(profile, duration_sec=60)

print("Completed", stats.cycles_completed, "cycles")
```

---

### Event-Driven Architecture

```python
from events.bus import EventBus
from events.events import (
    SessionStartedEvent,
    SessionPausedEvent,
    BatteryLowEvent
)

# Create event bus
bus = EventBus()

# Define handlers
def on_session_started(event: SessionStartedEvent):
    print(f"Session {event.session_id} started with {event.profile}")
    led.indicate_therapy_running()

def on_session_paused(event: SessionPausedEvent):
    print(f"Session {event.session_id} paused")
    led.indicate_paused()

def on_battery_low(event: BatteryLowEvent):
    print(f"Low battery: {event.voltage}V ({event.percentage}%)")
    led.indicate_low_battery()

# Subscribe handlers
bus.subscribe(SessionStartedEvent, on_session_started)
bus.subscribe(SessionPausedEvent, on_session_paused)
bus.subscribe(BatteryLowEvent, on_battery_low)

# Publish events (done automatically by components)
bus.publish(SessionStartedEvent(
    session_id="001",
    profile="noisy_vcr",
    duration_sec=7200
))
```

---

### Custom Pattern Generator

```python
from therapy.patterns.generator import PatternGenerator, Pattern, PatternConfig

class CustomPatternGenerator(PatternGenerator):
    """Custom pattern with specific research requirements."""

    def generate(self, config):
        # Validate configuration
        self.validate_config(config)

        # Generate custom sequence (e.g., alternating pairs)
        # Using 4 fingers per hand (0-3)
        left_sequence = [0, 0, 1, 1, 2, 2, 3, 3]
        right_sequence = [0, 0, 1, 1, 2, 2, 3, 3]

        # Calculate timing
        timing_ms = [config.time_on_ms + config.time_off_ms] * len(left_sequence)

        return Pattern(
            left_sequence=left_sequence,
            right_sequence=right_sequence,
            timing_ms=timing_ms,
            burst_duration_ms=config.time_on_ms,
            inter_burst_interval_ms=config.time_off_ms
        )

# Use custom generator
custom_gen = CustomPatternGenerator()
config = PatternConfig(time_on_ms=150, time_off_ms=100)
pattern = custom_gen.generate(config)
```

---

### State Machine Integration

```python
from state.machine import TherapyStateMachine, StateTrigger
from events.bus import EventBus

# Create state machine with event bus
event_bus = EventBus()
state_machine = TherapyStateMachine(event_bus=event_bus)

# Add observer for state changes
def on_state_change(from_state, to_state):
    print(f"State: {from_state} → {to_state}")
    update_led(to_state)

state_machine.add_observer(on_state_change)

# Perform transitions
state_machine.transition(StateTrigger.CONNECTED)
state_machine.transition(StateTrigger.START_SESSION)
state_machine.transition(StateTrigger.PAUSE_SESSION)
state_machine.transition(StateTrigger.RESUME_SESSION)
state_machine.transition(StateTrigger.STOP_SESSION)

# Check state
current = state_machine.get_current_state()
if current.can_start_therapy():
    start_session()
```

---

## Type Aliases and Imports

### Common Import Patterns

```python
# Core types (from core/ module)
from core.types import DeviceRole, TherapyState, ActuatorType, BatteryStatus

# Constants (from core/ module)
from core.constants import (
    FIRMWARE_VERSION,
    STARTUP_WINDOW,
    CRITICAL_VOLTAGE,
    LOW_VOLTAGE,
)

# Configuration (from src/config.py)
from config import load_device_config, load_therapy_profile

# State management (from src/state.py)
from state import StateMachine

# Hardware (from src/hardware.py)
from hardware import (
    BoardConfig,
    I2CMultiplexer,
    DRV2605Controller,
    BatteryMonitor,
    LEDPin
)

# BLE Communication (from src/ble.py)
from ble import BLEConnection

# LED control (from src/led.py)
from led import LEDController

# Therapy (from src/therapy.py)
from therapy import TherapyEngine

# Session management (from src/application/session/manager.py)
from application.session.manager import SessionManager
```

---

## Error Handling

### Common Exceptions

```python
# Configuration errors
try:
    config = load_device_config("settings.json")
except OSError:
    print("Settings file not found")
except ValueError as e:
    print("Invalid configuration:", e)

# Therapy errors
try:
    engine.execute_session(profile, duration_sec=7200)
except RuntimeError as e:
    print("Therapy execution failed:", e)
    state_machine.transition(StateTrigger.ERROR_OCCURRED)

# Battery errors
if battery.is_critical():
    voltage = battery.read_voltage()
    print("Critical battery:", voltage, "V")
    # Handle critical battery

# BLE errors
try:
    ble.scan_and_connect("BlueBuzzah")
except Exception as e:
    print("Connection failed:", e)
    boot_result = BootResult.FAILED
```

---

## Testing

BlueBuzzah v2 uses **hardware integration testing** to validate firmware functionality on actual Feather nRF52840 devices.

**Current Test Coverage:**
- 8/18 BLE protocol commands tested (44%)
- Calibration commands (CALIBRATE_START, CALIBRATE_BUZZ, CALIBRATE_STOP)
- Session commands (SESSION_START, SESSION_PAUSE, SESSION_RESUME, SESSION_STOP, SESSION_STATUS)
- Memory stress testing
- Synchronization latency measurement

**Testing Approach:**
- Manual testing on actual hardware via BLE
- No automated unit tests or mocks currently implemented
- See [Testing Guide](TESTING.md) for detailed procedures

**Future Testing:**
- Automated unit tests with mock implementations (planned)
- CI/CD integration (planned)
- Code coverage reporting (planned)

---

## Version Information

**API Version**: 2.0.0
**Protocol Version**: 2.0.0
**Firmware Version**: 2.0.0

---

## Additional Resources

- [Architecture Guide](ARCHITECTURE.md) - System design and patterns
- [Boot Sequence](BOOT_SEQUENCE.md) - Boot process and LED indicators
- [Therapy Engine](THERAPY_ENGINE.md) - Pattern generation and timing
- [Firmware Architecture](FIRMWARE_ARCHITECTURE.md) - Module structure and design
- [Testing Guide](TESTING.md) - Hardware integration testing procedures
- [Synchronization Protocol](SYNCHRONIZATION_PROTOCOL.md) - Device sync details

---

## Support

For questions, issues, or contributions:

- **GitHub**: [BlueBuzzah Repository](https://github.com/yourusername/bluebuzzah)
- **Issues**: [GitHub Issues](https://github.com/yourusername/bluebuzzah/issues)
- **Discussions**: [GitHub Discussions](https://github.com/yourusername/bluebuzzah/discussions)

---

**Last Updated**: 2025-01-11
**Document Version**: 1.0.0
