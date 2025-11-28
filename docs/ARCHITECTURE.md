# BlueBuzzah v2 Architecture

This document provides a detailed explanation of the BlueBuzzah v2 system architecture, design patterns, and component interactions.

## Table of Contents

- [Overview](#overview)
- [Design Principles](#design-principles)
- [Layer Architecture](#layer-architecture)
- [Component Details](#component-details)
- [Design Patterns](#design-patterns)
- [Data Flow](#data-flow)
- [Integration Points](#integration-points)
- [Scalability and Extensibility](#scalability-and-extensibility)

## Overview

BlueBuzzah v2 is built using Clean Architecture principles, separating concerns into distinct layers with clear boundaries and dependencies flowing inward.

### Architecture Goals

1. **Testability**: Easy to test business logic without hardware
2. **Maintainability**: Clear module boundaries and responsibilities
3. **Extensibility**: Simple to add new features or change implementations
4. **Portability**: Hardware abstraction enables platform changes
5. **Reliability**: Explicit state management and error handling

### High-Level Structure

```
┌─────────────────────────────────────────────────────────────┐
│                    Presentation Layer                       │
│  • BLE Command Handler  • Response Formatter  • LED UI      │
├─────────────────────────────────────────────────────────────┤
│                    Application Layer                        │
│  • Session Manager  • Profile Manager  • Command Processor  │
├─────────────────────────────────────────────────────────────┤
│                      Domain Layer                           │
│  • Therapy Engine  • Pattern Generator  • Sync Protocol     │
├─────────────────────────────────────────────────────────────┤
│                   Infrastructure Layer                      │
│  • BLE Service  • Haptic Driver  • Storage  • Battery Mon   │
├─────────────────────────────────────────────────────────────┤
│                      Hardware Layer                         │
│  • nRF52840  • DRV2605  • TCA9548A  • LRA Motors           │
└─────────────────────────────────────────────────────────────┘
```

## Design Principles

### 1. Clean Architecture

**Dependency Rule**: Dependencies point inward

- Presentation → Application → Domain → Infrastructure → Hardware
- Inner layers know nothing about outer layers
- Interfaces define boundaries

**Benefits**:

- Business logic independent of frameworks
- Testable without external dependencies
- UI and database can change independently

### 2. SOLID Principles

> **CircuitPython Note**: CircuitPython doesn't support type hints, ABC, or decorators like `@abstractmethod`. We use duck typing and document expected interfaces through docstrings instead.

**Single Responsibility**: Each class has one reason to change

```python
class TherapyEngine:
    """Responsible ONLY for executing therapy patterns"""

class PatternGenerator:
    """Responsible ONLY for generating patterns"""

class HapticController:
    """Responsible ONLY for controlling motors"""
```

**Open/Closed**: Open for extension, closed for modification

```python
class PatternGenerator:
    """Base pattern generator - extend by creating new generators.

    Expected interface:
        generate(config) - Returns Pattern object
    """
    def generate(self, config):
        raise NotImplementedError("Subclasses must implement generate()")

class RandomPermutationGenerator(PatternGenerator):
    """Concrete implementation - extends without modifying base"""
    def generate(self, config):
        # Implementation here
        pass
```

**Liskov Substitution**: Subtypes must be substitutable

```python
# Any HapticController can be used interchangeably
haptic = DRV2605Controller(...)
haptic = MockHapticController()  # For testing
```

**Interface Segregation**: Clients shouldn't depend on unused interfaces

```python
# Small, focused interfaces (duck typing)
class HapticController:
    """Expected interface for haptic control.

    Methods:
        activate(finger, amplitude) - Activate motor
        deactivate(finger) - Deactivate motor
    """
    def activate(self, finger, amplitude):
        raise NotImplementedError()

    def deactivate(self, finger):
        raise NotImplementedError()

# Not one massive "DeviceController" interface
```

**Dependency Inversion**: Depend on abstractions, not concretions

```python
class TherapyEngine:
    def __init__(self, pattern_generator, haptic_controller):
        """
        Args:
            pattern_generator: Object with generate() method
            haptic_controller: Object with activate()/deactivate() methods
        """
        # Depends on interfaces (duck typing), not concrete implementations
        self.pattern_generator = pattern_generator
        self.haptic_controller = haptic_controller
```

### 3. Domain-Driven Design

**Ubiquitous Language**: Common terminology

- **Session**: A therapy session from start to completion
- **Cycle**: One complete pattern repetition
- **Burst**: Three rapid buzzes on a finger
- **Macrocycle**: Collection of bursts with pauses
- **Profile**: Configuration for a therapy protocol

**Bounded Contexts**:

- **Therapy Context**: Pattern generation, cycle execution
- **Communication Context**: BLE protocol, messages
- **Hardware Context**: Device control, I2C communication
- **State Context**: Session states, transitions

## Layer Architecture

### Presentation Layer

**Purpose**: Handle external interactions (BLE commands, LED feedback)

**Components**:

- `communication/protocol/handler.py`: Parse and validate BLE commands
- `ui/led_controller.py`: Visual feedback to user
- `ui/boot_led.py`: Boot sequence indicators
- `ui/therapy_led.py`: Therapy session indicators

**Characteristics**:

- No business logic
- Transforms external data to/from domain models
- Handles user interaction protocols

**Example**:

```python
class ProtocolHandler:
    """Parse BLE commands and route to application layer"""

    def handle_command(self, raw_command):
        # Parse command
        command = self.parse(raw_command)

        # Route to application layer
        if command.type == "START_SESSION":
            result = self.session_manager.start_session(command.profile)
            return Response.success(result)

        elif command.type == "GET_STATUS":
            status = self.session_manager.get_status()
            return Response.success(status)
```

### Application Layer

**Purpose**: Orchestrate use cases and coordinate domain objects

**Components**:

> **CircuitPython File Structure Note**: This project uses a flat file structure rather than deep directory nesting. This is intentional for CircuitPython memory optimization:
> - **Import costs**: Each module import allocates RAM. Fewer modules = less memory overhead.
> - **Path resolution**: Deep paths increase import time and RAM usage.
> - **Heap fragmentation**: Many small modules fragment memory more than fewer larger modules.
> - **Conceptual layers**: The architecture maintains logical separation through code organization within files, even though physical directories are consolidated.

**Conceptual Components** (may be consolidated in actual files):

- `session_manager` / `manager.py`: Session lifecycle management
- `profile_manager`: Profile loading and validation
- `calibration_controller`: Calibration workflows
- `command_processor`: Command routing

**Characteristics**:

- Use case implementations
- Transaction boundaries
- Error handling and recovery
- Event publishing

**Example**:

```python
class SessionManager:
    """Manage therapy session lifecycle"""

    def start_session(self, profile_name, duration_sec):
        """
        Start a therapy session.

        Args:
            profile_name: Name of therapy profile to load
            duration_sec: Session duration in seconds

        Returns:
            SessionInfo object with session details
        """
        # Load profile (application service)
        profile = self.profile_manager.load_profile(profile_name)

        # Validate preconditions
        if not self.can_start_session():
            raise RuntimeError("Cannot start session in current state")

        # Execute therapy (domain service)
        self.current_session = self.therapy_engine.execute_session(
            config=profile.therapy_config,
            duration_sec=duration_sec
        )

        # Publish event
        self.event_bus.publish(SessionStartedEvent(self.current_session))

        return self.current_session
```

### Domain Layer

**Purpose**: Core business logic and rules

**Components**:

- `therapy/engine.py`: Therapy execution logic
- `therapy/patterns/generator.py`: Pattern generation algorithms
- `domain/sync/protocol.py`: Bilateral synchronization
- `state/machine.py`: Therapy state machine

**Characteristics**:

- Pure business logic
- No external dependencies
- Framework-agnostic
- Highly testable

**Example**:

```python
class TherapyEngine:
    """Core therapy execution - pure domain logic"""

    def execute_cycle(self, config):
        """Execute one therapy cycle.

        Args:
            config: PatternConfig with timing and pattern settings
        """
        # Generate pattern (domain logic)
        pattern = self.pattern_generator.generate(config)

        # Execute with precise timing (domain logic)
        for i in range(len(pattern)):
            left_finger, right_finger = pattern.get_finger_pair(i)

            # Bilateral activation (infrastructure call through interface)
            self._activate_bilateral(left_finger, right_finger, amplitude=100)

            # Timing control (domain logic) - synchronous sleep
            time.sleep(pattern.burst_duration_ms / 1000.0)

            self._deactivate_bilateral(left_finger, right_finger)

            # Drift compensation (domain logic)
            adjusted_interval = self._calculate_timing_adjustment(pattern, i)
            time.sleep(adjusted_interval / 1000.0)
```

### Infrastructure Layer

**Purpose**: Interface with external systems and hardware

**Components**:

- `communication/ble/service.py`: BLE communication
- `hardware/haptic.py`: Motor control (DRV2605)
- `hardware/battery.py`: Battery monitoring
- `hardware/multiplexer.py`: I2C multiplexing

**Characteristics**:

- Implements interfaces defined by domain
- Handles external resource management
- Deals with framework-specific code
- May have side effects

**Example**:

```python
class DRV2605Controller(HapticController):
    """Concrete implementation of HapticController interface"""

    def activate(self, finger, amplitude):
        """Activate motor for specified finger.

        Args:
            finger: Finger index (0-3)
            amplitude: Amplitude 0-100
        """
        # Infrastructure-level details
        self.multiplexer.select_channel(finger)

        # Hardware-specific protocol
        register_value = self._amplitude_to_register(amplitude)
        self.i2c.writeto(DRV2605_ADDR, bytes([RTP_REGISTER, register_value]))

        self._active_fingers[finger] = amplitude
```

## Component Details

### State Management

The system has 11 distinct states to handle all operational scenarios:

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> CONNECTING: POWER_ON/CONNECT
    CONNECTING --> READY: CONNECTED
    CONNECTING --> ERROR: ERROR_OCCURRED
    CONNECTING --> IDLE: DISCONNECTED

    READY --> RUNNING: START_SESSION
    READY --> ERROR: ERROR_OCCURRED
    READY --> CONNECTION_LOST: DISCONNECTED

    RUNNING --> PAUSED: PAUSE_SESSION
    RUNNING --> STOPPING: STOP_SESSION
    RUNNING --> STOPPING: SESSION_COMPLETE
    RUNNING --> LOW_BATTERY: BATTERY_WARNING
    RUNNING --> CONNECTION_LOST: DISCONNECTED
    RUNNING --> PHONE_DISCONNECTED: PHONE_LOST
    RUNNING --> ERROR: ERROR/EMERGENCY_STOP

    PAUSED --> RUNNING: RESUME_SESSION
    PAUSED --> STOPPING: STOP_SESSION
    PAUSED --> CONNECTION_LOST: DISCONNECTED

    LOW_BATTERY --> RUNNING: BATTERY_OK
    LOW_BATTERY --> CRITICAL_BATTERY: BATTERY_CRITICAL
    LOW_BATTERY --> STOPPING: STOP_SESSION

    CRITICAL_BATTERY --> IDLE: FORCED_SHUTDOWN

    CONNECTION_LOST --> READY: RECONNECTED
    CONNECTION_LOST --> IDLE: RECONNECT_FAILED

    PHONE_DISCONNECTED --> RUNNING: PHONE_RECONNECTED
    PHONE_DISCONNECTED --> IDLE: PHONE_TIMEOUT

    STOPPING --> READY: STOPPED
    STOPPING --> IDLE: DISCONNECTED

    ERROR --> IDLE: RESET
    ERROR --> IDLE: DISCONNECTED
```

**State Descriptions**:

| State | Value | Description |
|-------|-------|-------------|
| `IDLE` | 0 | No active session, system ready |
| `CONNECTING` | 1 | Establishing BLE connection |
| `READY` | 2 | Connected, ready for therapy |
| `RUNNING` | 3 | Active therapy session |
| `PAUSED` | 4 | Session paused, can resume |
| `STOPPING` | 5 | Session ending, cleanup |
| `ERROR` | 6 | Error condition, motors stopped |
| `LOW_BATTERY` | 7 | Battery < 20%, session can continue |
| `CRITICAL_BATTERY` | 8 | Battery < 5%, forced shutdown |
| `CONNECTION_LOST` | 9 | Inter-device BLE lost, attempting recovery |
| `PHONE_DISCONNECTED` | 10 | Phone BLE lost (PRIMARY only) |

**Implementation**:

```python
class TherapyStateMachine:
    """Explicit state machine with validated transitions"""

    def __init__(self):
        self._current_state = TherapyState.IDLE
        self._transition_table = self._build_transition_table()

    def transition(self, trigger):
        """Attempt state transition.

        Args:
            trigger: StateTrigger value

        Returns:
            True if transition succeeded, False if invalid
        """
        key = (self._current_state, trigger)

        if key not in self._transition_table:
            return False  # Invalid transition

        next_state = self._transition_table[key]
        self._emit_state_change(self._current_state, next_state, trigger)
        self._current_state = next_state
        return True
```

### Bilateral Mirroring

The `mirror_pattern` parameter controls whether both hands receive the same finger sequence or independent sequences. This is based on vCR research findings:

| vCR Type | `mirror_pattern` | Behavior | Rationale |
|----------|------------------|----------|-----------|
| **Noisy vCR** | `True` | Same finger on both hands | Avoids bilateral masking interference |
| **Regular vCR** | `False` | Independent sequences per hand | Increases spatial randomization for synaptic decoupling |

**Implementation**:

```python
def generate_random_permutation(num_fingers=4, mirror_pattern=True):
    """Generate random permutation pattern.

    Args:
        num_fingers: Number of fingers (4 = no thumb)
        mirror_pattern: True for noisy vCR, False for regular vCR
    """
    # Generate left hand sequence
    left_sequence = list(range(num_fingers))
    shuffle_in_place(left_sequence)

    # Generate right hand sequence based on mirror setting
    if mirror_pattern:
        # Mirrored: same finger on both hands (noisy vCR)
        right_sequence = left_sequence.copy()
    else:
        # Non-mirrored: independent random sequence (regular vCR)
        right_sequence = list(range(num_fingers))
        shuffle_in_place(right_sequence)

    return Pattern(left_sequence, right_sequence, ...)
```

**Hardware Note**: Both gloves use identical channel-to-finger mapping (channel 0 = pinky on both gloves). The `mirror_pattern` setting controls whether the same channel number is sent to both devices (mirrored) or different channels (non-mirrored).

### Event System

**Purpose**: Decouple components through event-driven communication

**Implementation**:

```python
class EventBus:
    """Publish-subscribe event bus"""

    def __init__(self):
        self._subscribers = {}  # {event_type: [handlers]}

    def subscribe(self, event_type, handler):
        """Register event handler.

        Args:
            event_type: Event class to subscribe to
            handler: Callback function
        """
        if event_type not in self._subscribers:
            self._subscribers[event_type] = []
        self._subscribers[event_type].append(handler)

    def publish(self, event):
        """Publish event to all subscribers.

        Args:
            event: Event instance to publish
        """
        event_type = type(event)
        if event_type in self._subscribers:
            for handler in self._subscribers[event_type]:
                handler(event)
```

**Usage**:

```python
# In application layer
event_bus.subscribe(SessionStartedEvent, self._on_session_started)
event_bus.subscribe(BatteryLowEvent, self._on_battery_low)

# In domain layer
event_bus.publish(SessionStartedEvent(session_info))

# Handler
def _on_session_started(self, event):
    print("Session {} started".format(event.session_id))
    self.led_controller.indicate_therapy_running()
```

### Configuration System

**Layered Configuration**:

1. **Device Configuration** (`settings.json`): Device role
2. **Base Configuration** (`config/base.py`): System defaults
3. **Therapy Configuration** (`config/therapy.py`): Profile settings
4. **Runtime Configuration**: Dynamic adjustments

**Loading Hierarchy**:

```python
def load_configuration() -> Config:
    # 1. Load device role from settings.json
    device_config = DeviceConfig.from_settings_file("/settings.json")

    # 2. Load base configuration with role-specific defaults
    base_config = BaseConfig(device_role=device_config.role)

    # 3. Load therapy configuration (profile-specific)
    therapy_config = TherapyConfig.load_profile("noisy_vcr")

    # 4. Combine into complete configuration
    return Config(
        device=device_config,
        base=base_config,
        therapy=therapy_config
    )
```

### Synchronization Protocol

**PRIMARY-SECONDARY Communication**:

```mermaid
sequenceDiagram
    participant P as PRIMARY
    participant S as SECONDARY

    Note over P,S: Boot Sequence
    P->>S: FIRST_SYNC (establish time offset)
    S->>P: ACK_FIRST_SYNC (with timestamp)

    Note over P,S: Therapy Execution
    loop Every Macrocycle
        P->>S: SYNC_ADJ (periodic resync)
        S->>P: ACK_SYNC_ADJ

        loop Each Burst
            P->>S: EXECUTE_BUZZ(sequence_index)
            par
              P->>P: Execute burst (left hand)
            and
              S->>S: Execute buzz (right hand)
            end
            S->>P: BUZZ_COMPLETE
        end
    end
```

**Time Synchronization**:

```python
class SyncProtocol:
    """Bilateral time synchronization"""

    def calculate_offset(self, primary_time, secondary_time):
        """Calculate time offset between devices.

        Offset = (T_primary - T_secondary) / 2

        This compensates for message transmission time and ensures
        sub-10ms synchronization accuracy.

        Args:
            primary_time: Timestamp from PRIMARY device
            secondary_time: Timestamp from SECONDARY device

        Returns:
            Calculated offset in milliseconds
        """
        return (primary_time - secondary_time) // 2

    def apply_compensation(self, timestamp, offset):
        """Apply offset to timestamp for synchronized execution.

        Args:
            timestamp: Original timestamp
            offset: Calculated offset

        Returns:
            Compensated timestamp
        """
        return timestamp + offset
```

### Heartbeat Protocol

The heartbeat protocol ensures continuous connection monitoring between PRIMARY and SECONDARY devices:

| Parameter | Value | Description |
|-----------|-------|-------------|
| Interval | 2 seconds | Time between heartbeat messages |
| Timeout | 6 seconds | 3 missed heartbeats = connection lost |
| Recovery attempts | 3 | Number of reconnection attempts |
| Recovery delay | 2 seconds | Delay between reconnection attempts |

**Connection Recovery Flow**:

```mermaid
stateDiagram-v2
    [*] --> Connected
    Connected --> HeartbeatMissed: No response
    HeartbeatMissed --> Connected: Response received
    HeartbeatMissed --> ConnectionLost: 3 missed (6s timeout)
    ConnectionLost --> Reconnecting: Start recovery
    Reconnecting --> Connected: Success
    Reconnecting --> Failed: 3 attempts failed
    Failed --> [*]: Emergency stop
```

**Implementation**:

```python
HEARTBEAT_INTERVAL_SEC = 2
HEARTBEAT_TIMEOUT_SEC = 6
RECONNECT_ATTEMPTS = 3
RECONNECT_DELAY_SEC = 2

def check_heartbeat(self):
    """Check heartbeat status and handle timeout."""
    elapsed = time.monotonic() - self._last_heartbeat
    if elapsed > HEARTBEAT_TIMEOUT_SEC:
        self._handle_connection_lost()
    elif elapsed > HEARTBEAT_INTERVAL_SEC:
        self._send_heartbeat()
```

### SYNC Message Format

Inter-device communication uses a structured message format:

**Format**:
```
SYNC:<command>:<key1>|<value1>|<key2>|<value2>...<EOT>
```

**Components**:
- `SYNC:` - Message prefix identifier
- `<command>` - Command type (see table below)
- `:` - Field separator
- `<key>|<value>` - Key-value pairs separated by pipe
- `<EOT>` - End of transmission marker (ASCII 0x04)

**Command Reference**:

| Command | Direction | Description |
|---------|-----------|-------------|
| `CONNECTED` | P→S | Connection established |
| `START_SESSION` | P→S | Begin therapy session |
| `PAUSE_SESSION` | P→S | Pause current session |
| `RESUME_SESSION` | P→S | Resume paused session |
| `STOP_SESSION` | P→S | Stop and end session |
| `STOPPED` | S→P | Session stopped confirmation |
| `EXECUTE_BUZZ` | P→S | Trigger buzz on SECONDARY |
| `HEARTBEAT` | P↔S | Connection keepalive |
| `EMERGENCY_STOP` | P↔S | Immediate motor shutoff |
| `PHONE_DISCONNECTED` | P→S | Phone app disconnected |
| `ACK` | S→P | Acknowledgment |
| `ERROR` | P↔S | Error notification |

**Example Messages**:
```
SYNC:START_SESSION:profile|noisy_vcr|duration|7200<EOT>
SYNC:EXECUTE_BUZZ:finger|2|amplitude|100<EOT>
SYNC:HEARTBEAT:timestamp|1234567890<EOT>
SYNC:ACK:command|START_SESSION<EOT>
```

## Design Patterns

### 1. Strategy Pattern (Pattern Generators)

**Purpose**: Pluggable algorithms for pattern generation

```python
class PatternGenerator:
    """Strategy interface - extend via subclassing.

    Expected interface:
        generate(config) - Returns Pattern object
    """
    def generate(self, config):
        raise NotImplementedError("Subclasses must implement generate()")

class RandomPermutationGenerator(PatternGenerator):
    """Concrete strategy: Random permutation"""
    def generate(self, config):
        sequence = self._generate_random_permutation([0, 1, 2, 3])  # 4 fingers
        return Pattern(left_sequence=sequence, right_sequence=sequence, ...)

class SequentialGenerator(PatternGenerator):
    """Concrete strategy: Sequential pattern"""
    def generate(self, config):
        sequence = [0, 1, 2, 3]  # Fixed order, 4 fingers (no thumb)
        return Pattern(left_sequence=sequence, right_sequence=sequence, ...)

# Usage - strategy is interchangeable
generator = RandomPermutationGenerator()
pattern = generator.generate(config)
```

### 2. Repository Pattern (Hardware Abstraction)

**Purpose**: Abstract data/hardware access

```python
class HapticController:
    """Repository interface for haptic control.

    Expected interface:
        activate(finger, amplitude) - Activate motor
        deactivate(finger) - Deactivate motor
    """
    def activate(self, finger, amplitude):
        raise NotImplementedError()

    def deactivate(self, finger):
        raise NotImplementedError()

class DRV2605Controller(HapticController):
    """Concrete repository: Real hardware"""

class MockHapticController(HapticController):
    """Concrete repository: Mock for testing"""

# Application code doesn't know which implementation
def execute_therapy(haptic):
    haptic.activate(finger=0, amplitude=75)
```

### 3. Dependency Injection

**Purpose**: Invert dependencies for testability

```python
class TherapyEngine:
    """Dependencies injected through constructor"""

    def __init__(self, pattern_generator, haptic_controller, battery_monitor, state_machine):
        """
        Args:
            pattern_generator: Object with generate() method
            haptic_controller: Object with activate()/deactivate() methods
            battery_monitor: Object with get_level() method
            state_machine: TherapyStateMachine instance
        """
        # All dependencies are interfaces (duck typing), not concrete classes
        self.pattern_generator = pattern_generator
        self.haptic_controller = haptic_controller
        self.battery_monitor = battery_monitor
        self.state_machine = state_machine

# Production setup
engine = TherapyEngine(
    pattern_generator=RandomPermutationGenerator(),
    haptic_controller=DRV2605Controller(...),
    battery_monitor=BatteryMonitor(...),
    state_machine=TherapyStateMachine()
)

# Test setup
engine = TherapyEngine(
    pattern_generator=MockPatternGenerator(),
    haptic_controller=MockHapticController(),
    battery_monitor=MockBatteryMonitor(),
    state_machine=TherapyStateMachine()
)
```

### 4. Observer Pattern (Event System)

**Purpose**: Notify interested parties of state changes

```python
class TherapyStateMachine:
    """Observable - notifies observers of state changes"""

    def __init__(self):
        self._observers = []  # List of callback functions

    def add_observer(self, callback):
        """Register observer callback.

        Args:
            callback: Function that accepts StateTransition
        """
        self._observers.append(callback)

    def transition(self, trigger):
        """Transition and notify observers.

        Args:
            trigger: StateTrigger value

        Returns:
            True if transition succeeded, False if invalid
        """
        # ... perform transition ...
        self._notify_observers(transition)

    def _notify_observers(self, transition):
        """Notify all observers of state change."""
        for observer in self._observers:
            observer(transition)

# Usage
def on_state_change(transition):
    print("State: {} -> {}".format(transition.from_state, transition.to_state))

state_machine.add_observer(on_state_change)
```

### 5. Factory Pattern (Object Creation)

**Purpose**: Encapsulate object creation logic

```python
class ProfileFactory:
    """Create therapy profiles from configuration"""

    @staticmethod
    def create_profile(profile_type):
        """Factory method for profile creation.

        Args:
            profile_type: One of "regular_vcr", "noisy_vcr", "hybrid_vcr"

        Returns:
            TherapyConfig instance
        """
        if profile_type == "regular_vcr":
            return TherapyConfig(
                profile_name="Regular vCR",
                burst_duration_ms=100,
                inter_burst_interval_ms=668,
                pattern_type="sequential",
                mirror_pattern=False
            )

        elif profile_type == "noisy_vcr":
            return TherapyConfig(
                profile_name="Noisy vCR",
                burst_duration_ms=100,
                inter_burst_interval_ms=668,
                pattern_type="random",
                mirror_pattern=True
            )

        elif profile_type == "hybrid_vcr":
            return TherapyConfig(
                profile_name="Hybrid vCR",
                burst_duration_ms=100,
                inter_burst_interval_ms=668,
                pattern_type="mirrored",
                mirror_pattern=False
            )

        else:
            raise ValueError("Unknown profile type: {}".format(profile_type))

# Usage
profile = ProfileFactory.create_profile("noisy_vcr")
```

## Data Flow

### Session Start Flow

```mermaid
sequenceDiagram
    participant U as User/App
    participant P as Protocol Handler
    participant SM as Session Manager
    participant TE as Therapy Engine
    participant H as Haptic Controller

    U->>P: START_SESSION(profile, duration)
    P->>SM: start_session(profile, duration)
    SM->>SM: Load profile configuration
    SM->>TE: execute_session(config, duration)

    loop Until Session Complete
        TE->>TE: Generate pattern
        loop For each finger pair
            TE->>H: activate(finger, amplitude)
            TE->>TE: Wait burst_duration
            TE->>H: deactivate(finger)
            TE->>TE: Wait inter_burst_interval
        end
        TE->>SM: Cycle complete event
    end

    TE->>SM: Session complete
    SM->>P: SessionInfo
    P->>U: SUCCESS response
```

### Bilateral Synchronization Flow

```mermaid
sequenceDiagram
    participant PE as PRIMARY Engine
    participant PS as PRIMARY Sync
    participant SS as SECONDARY Sync
    participant SE as SECONDARY Engine

    PE->>PS: Start macrocycle
    PS->>SS: SYNC_ADJ (timestamp)
    SS->>PS: ACK_SYNC_ADJ
    SS->>SS: Calculate time offset

    loop Each burst in sequence
        PE->>PE: Execute buzz (left)
        PS->>SS: EXECUTE_BUZZ(index, timestamp)
        SS->>SE: Trigger buzz (right)
        SE->>SE: Execute buzz (right, compensated time)
        SE->>SS: Complete
        SS->>PS: BUZZ_COMPLETE
    end
```

## Integration Points

### BLE Protocol Integration

**Command Structure**:

```json
{
  "command": "START_SESSION",
  "payload": {
    "profile": "noisy_vcr",
    "duration_sec": 7200
  },
  "timestamp": 1234567890,
  "device_id": "primary"
}
```

**Response Structure**:

```json
{
  "status": "success",
  "data": {
    "session_id": "session_001",
    "profile": "noisy_vcr",
    "duration_sec": 7200,
    "start_time": 1234567890
  },
  "timestamp": 1234567891
}
```

### Hardware Integration

**I2C Communication**:

```python
class I2CMultiplexer:
    """TCA9548A integration"""

    def select_channel(self, channel):
        """Select I2C channel for finger.

        Args:
            channel: Channel number (0-7), corresponds to finger (0-3)
        """
        if not 0 <= channel < 8:
            raise ValueError("Invalid channel: {}".format(channel))

        # TCA9548A: Write channel bitmask to control register
        mask = 1 << channel
        self.i2c.writeto(self.address, bytes([mask]))

class DRV2605Controller:
    """DRV2605 haptic driver integration"""

    def activate(self, finger, amplitude):
        """Activate motor via I2C.

        Args:
            finger: Finger index (0-3)
            amplitude: Amplitude 0-100
        """
        # Select multiplexer channel for this finger
        self.multiplexer.select_channel(finger)

        # Write to DRV2605 RTP register
        register_value = int((amplitude / 100.0) * 127)
        self.i2c.writeto(
            DRV2605_ADDR,
            bytes([RTP_INPUT_REGISTER, register_value])
        )
```

## Scalability and Extensibility

### Adding New Pattern Generators

1. Create class that implements `generate(config)` method
2. Register in factory

```python
class CustomPatternGenerator:
    """New pattern algorithm"""

    def generate(self, config):
        """Generate custom pattern.

        Args:
            config: PatternConfig with timing settings

        Returns:
            Pattern object
        """
        # Implement custom logic
        sequence = self._custom_algorithm(config)
        return Pattern(left_sequence=sequence, right_sequence=sequence, ...)

# Register
PatternGeneratorFactory.register("custom", CustomPatternGenerator)
```

### Adding New Therapy Profiles

1. Create configuration in `therapy/profiles/`
2. Define profile parameters
3. Add to profile factory

```python
def create_custom_profile():
    """Create custom research profile.

    Returns:
        TherapyConfig instance
    """
    return TherapyConfig(
        profile_name="Custom Research Profile",
        burst_duration_ms=150,
        inter_burst_interval_ms=500,
        bursts_per_cycle=4,
        pattern_type="random",  # or "sequential", "mirrored"
        mirror_pattern=True,
        num_fingers=4,
        actuator_type=ActuatorType.LRA,
        frequency_hz=175,
        amplitude_percent=80
    )
```

### Adding New Hardware Platforms

1. Create class with `activate(finger, amplitude)` and `deactivate(finger)` methods
2. Create battery monitor with `get_level()` method
3. Update board configuration

```python
class NewHapticDriver:
    """New hardware driver - implements HapticController interface"""

    def activate(self, finger, amplitude):
        """Activate motor.

        Args:
            finger: Finger index (0-3)
            amplitude: Amplitude 0-100
        """
        # Platform-specific implementation
        pass

    def deactivate(self, finger):
        """Deactivate motor.

        Args:
            finger: Finger index (0-3)
        """
        # Platform-specific implementation
        pass
```

### Adding New BLE Commands

1. Define command in `communication/protocol/commands.py`
2. Add handler in `ProtocolHandler`
3. Update application layer logic

```python
class NewCommand:
    """New command definition"""
    def __init__(self, parameter=""):
        self.command_type = "NEW_FEATURE"
        self.parameter = parameter

class ProtocolHandler:
    def handle_command(self, command):
        """Handle incoming command.

        Args:
            command: Command instance

        Returns:
            Response object
        """
        if isinstance(command, NewCommand):
            result = self._handle_new_feature(command)
            return Response.success(result)
```

## Summary

BlueBuzzah v2 architecture provides:

1. **Clean separation of concerns** through layered architecture
2. **High testability** via dependency injection and abstractions
3. **Maintainability** through SOLID principles and clear boundaries
4. **Extensibility** via strategy pattern and factory methods
5. **Reliability** through explicit state management and error handling
6. **Portability** through hardware abstraction layer

The architecture enables confident refactoring, easy testing, and straightforward feature additions while maintaining code quality and system reliability.
