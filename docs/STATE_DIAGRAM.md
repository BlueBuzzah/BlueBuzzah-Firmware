# BlueBuzzah Firmware State & Communication Diagrams

This document provides visual representations of the firmware's states, transitions, and communication patterns between PRIMARY (left glove), SECONDARY (right glove), and Phone devices.

## Table of Contents

1. [TherapyState State Machine](#1-therapystate-state-machine)
2. [Boot Sequence](#2-boot-sequence)
3. [Therapy Session Flow](#3-therapy-session-flow)
4. [Connection Loss & Recovery](#4-connection-loss--recovery)
5. [Message Reference](#5-message-reference)

---

## 1. TherapyState State Machine

The `TherapyState` enum defines all possible states of the therapy system. Both PRIMARY and SECONDARY devices maintain their own state, synchronized via SYNC messages.

### 1.1 Complete State Diagram

```mermaid
stateDiagram-v2
    [*] --> IDLE : Power On

    %% Normal Operation States
    IDLE --> CONNECTING : BLE scan/advertise
    CONNECTING --> READY : CONNECTED
    CONNECTING --> IDLE : Connection timeout

    READY --> RUNNING : START_SESSION
    IDLE --> RUNNING : START_SESSION (direct start)

    RUNNING --> PAUSED : PAUSE_SESSION
    PAUSED --> RUNNING : RESUME_SESSION

    RUNNING --> STOPPING : STOP_SESSION
    PAUSED --> STOPPING : STOP_SESSION
    STOPPING --> IDLE : STOPPED

    %% Error States
    RUNNING --> ERROR : ERROR / EMERGENCY_STOP
    PAUSED --> ERROR : ERROR / EMERGENCY_STOP
    READY --> ERROR : ERROR
    ERROR --> IDLE : Error cleared

    %% Battery States
    RUNNING --> LOW_BATTERY : Battery < 20%
    LOW_BATTERY --> RUNNING : Battery recovered
    LOW_BATTERY --> CRITICAL_BATTERY : Battery < 5%
    CRITICAL_BATTERY --> IDLE : Forced shutdown

    %% Connection States
    RUNNING --> CONNECTION_LOST : DISCONNECTED
    PAUSED --> CONNECTION_LOST : DISCONNECTED
    READY --> CONNECTION_LOST : DISCONNECTED

    CONNECTION_LOST --> READY : Reconnection success
    CONNECTION_LOST --> IDLE : Reconnection failed (3 attempts)

    %% Phone Disconnection (PRIMARY only)
    RUNNING --> PHONE_DISCONNECTED : Phone BLE lost
    PAUSED --> PHONE_DISCONNECTED : Phone BLE lost
    PHONE_DISCONNECTED --> RUNNING : Phone reconnected
    PHONE_DISCONNECTED --> IDLE : Phone timeout
```

### 1.2 State Descriptions

| State | Value | Description |
|-------|-------|-------------|
| `IDLE` | 0 | No active session, system ready for connection |
| `CONNECTING` | 1 | Establishing BLE connection between devices |
| `READY` | 2 | Connected and ready to start therapy session |
| `RUNNING` | 3 | Active therapy session in progress |
| `PAUSED` | 4 | Session temporarily paused, can resume |
| `STOPPING` | 5 | Session ending, cleanup in progress |
| `ERROR` | 6 | Error condition, motors stopped |
| `LOW_BATTERY` | 7 | Battery below 20%, session can continue |
| `CRITICAL_BATTERY` | 8 | Battery below 5%, forced shutdown |
| `CONNECTION_LOST` | 9 | Inter-device connection lost, attempting recovery |
| `PHONE_DISCONNECTED` | 10 | Phone BLE connection lost (PRIMARY only) |

### 1.3 State Triggers

| Trigger | Description |
|---------|-------------|
| `CONNECTED` | BLE connection established |
| `DISCONNECTED` | BLE connection lost |
| `START_SESSION` | Begin therapy session |
| `PAUSE_SESSION` | Temporarily pause session |
| `RESUME_SESSION` | Resume paused session |
| `STOP_SESSION` | End therapy session |
| `STOPPED` | Session cleanup complete |
| `ERROR` | Error condition detected |
| `EMERGENCY_STOP` | Immediate motor shutdown required |

---

## 2. Boot Sequence

The boot sequence establishes BLE connections between PRIMARY, SECONDARY, and optionally a Phone.

### 2.1 Full Boot Sequence

```mermaid
sequenceDiagram
    autonumber
    participant Phone as Phone App
    participant PRIMARY as PRIMARY<br/>(Left Glove)
    participant SECONDARY as SECONDARY<br/>(Right Glove)

    Note over PRIMARY,SECONDARY: Power On - Both devices initialize

    rect rgb(240, 248, 255)
        Note over PRIMARY,SECONDARY: Phase 1: Hardware Initialization
        PRIMARY->>PRIMARY: Initialize BLE stack
        PRIMARY->>PRIMARY: Initialize hardware (motors, ADC)
        PRIMARY->>PRIMARY: Load configuration
        SECONDARY->>SECONDARY: Initialize BLE stack
        SECONDARY->>SECONDARY: Initialize hardware (motors, ADC)
        SECONDARY->>SECONDARY: Load configuration
    end

    rect rgb(255, 248, 240)
        Note over PRIMARY,SECONDARY: Phase 2: Inter-Device Connection
        PRIMARY->>PRIMARY: Start advertising "BlueBuzzah"
        SECONDARY->>SECONDARY: Start scanning for "BlueBuzzah"
        SECONDARY-->>PRIMARY: Connection request
        PRIMARY->>SECONDARY: Connection accepted
        PRIMARY->>SECONDARY: SYNC:CONNECTED
        SECONDARY->>SECONDARY: State → READY
        PRIMARY->>PRIMARY: State → READY
    end

    rect rgb(240, 255, 240)
        Note over Phone,PRIMARY: Phase 3: Phone Connection (Optional)
        Phone->>Phone: Scan for "BlueBuzzah"
        Phone-->>PRIMARY: Connection request
        PRIMARY->>Phone: Connection accepted
        PRIMARY->>Phone: Characteristic discovery
        Phone->>PRIMARY: Subscribe to notifications
        PRIMARY->>PRIMARY: Phone connected flag = true
    end

    alt All connections successful
        Note over PRIMARY: BootResult = SUCCESS_WITH_PHONE
    else No phone connected
        Note over PRIMARY: BootResult = SUCCESS_NO_PHONE
    else SECONDARY connection failed
        Note over PRIMARY: BootResult = FAILED
    end
```

### 2.2 Boot Result States

```mermaid
stateDiagram-v2
    [*] --> Initializing

    Initializing --> ScanningForSecondary : PRIMARY role
    Initializing --> ScanningForPrimary : SECONDARY role

    ScanningForSecondary --> SecondaryConnected : Found & connected
    ScanningForSecondary --> FAILED : Timeout (30s)

    ScanningForPrimary --> ConnectedToPrimary : Found & connected
    ScanningForPrimary --> FAILED : Timeout (30s)

    SecondaryConnected --> WaitingForPhone : 10s window
    WaitingForPhone --> SUCCESS_WITH_PHONE : Phone connected
    WaitingForPhone --> SUCCESS_NO_PHONE : Timeout

    ConnectedToPrimary --> SUCCESS : SECONDARY ready
```

---

## 3. Therapy Session Flow

### 3.1 Session Start

```mermaid
sequenceDiagram
    autonumber
    participant Phone as Phone App
    participant PRIMARY as PRIMARY<br/>(Left Glove)
    participant SECONDARY as SECONDARY<br/>(Right Glove)

    Note over Phone,SECONDARY: Starting Therapy Session

    Phone->>PRIMARY: BLE Write: SESSION_START<br/>{mode, intensity, duration}

    PRIMARY->>PRIMARY: Validate parameters
    PRIMARY->>PRIMARY: Initialize therapy engine
    PRIMARY->>PRIMARY: State → RUNNING

    PRIMARY->>SECONDARY: SYNC:START_SESSION:mode|0|intensity|75|duration|300

    SECONDARY->>SECONDARY: Parse SYNC message
    SECONDARY->>SECONDARY: Initialize local therapy
    SECONDARY->>SECONDARY: State → RUNNING

    SECONDARY->>PRIMARY: SYNC:ACK:START_SESSION

    PRIMARY->>Phone: BLE Notify: SESSION_STARTED<br/>{session_id, timestamp}
```

### 3.2 Active Session - Pattern Execution

```mermaid
sequenceDiagram
    autonumber
    participant Phone as Phone App
    participant PRIMARY as PRIMARY<br/>(Left Glove)
    participant SECONDARY as SECONDARY<br/>(Right Glove)

    Note over PRIMARY,SECONDARY: Continuous Pattern Execution Loop

    loop Every pattern interval (configurable)
        PRIMARY->>PRIMARY: Generate pattern<br/>(RANDOM/SEQUENTIAL/MIRRORED)

        PRIMARY->>PRIMARY: Execute local buzz<br/>activate_motor(left_channel, intensity, duration)

        alt Noisy vCR (mirror_pattern=True)
            Note over PRIMARY,SECONDARY: Mirrored: same finger on both hands<br/>(avoids bilateral masking interference)
            PRIMARY->>SECONDARY: SYNC:EXECUTE_BUZZ:ch|2|int|75|dur|100
            SECONDARY->>SECONDARY: activate_motor(2, 75, 100)
        else Regular vCR (mirror_pattern=False)
            Note over PRIMARY,SECONDARY: Non-mirrored: independent finger sequences<br/>(increases spatial randomization)
            PRIMARY->>SECONDARY: SYNC:EXECUTE_BUZZ:ch|3|int|75|dur|100
            SECONDARY->>SECONDARY: activate_motor(3, 75, 100)
        end
    end

    loop Every 2 seconds
        PRIMARY->>SECONDARY: SYNC:HEARTBEAT:ts|1234567890
        SECONDARY->>SECONDARY: Reset heartbeat timer
        SECONDARY->>PRIMARY: SYNC:ACK:HEARTBEAT
    end

    loop Every 5 seconds (if phone connected)
        PRIMARY->>Phone: BLE Notify: STATUS_UPDATE<br/>{elapsed, battery, state}
    end
```

### 3.3 Session Pause/Resume

```mermaid
sequenceDiagram
    autonumber
    participant Phone as Phone App
    participant PRIMARY as PRIMARY<br/>(Left Glove)
    participant SECONDARY as SECONDARY<br/>(Right Glove)

    Note over Phone,SECONDARY: Pause Session

    Phone->>PRIMARY: BLE Write: PAUSE_SESSION

    PRIMARY->>PRIMARY: Stop pattern generation
    PRIMARY->>PRIMARY: Deactivate all motors
    PRIMARY->>PRIMARY: State → PAUSED

    PRIMARY->>SECONDARY: SYNC:PAUSE_SESSION

    SECONDARY->>SECONDARY: Deactivate all motors
    SECONDARY->>SECONDARY: State → PAUSED
    SECONDARY->>PRIMARY: SYNC:ACK:PAUSE_SESSION

    PRIMARY->>Phone: BLE Notify: SESSION_PAUSED

    Note over Phone,SECONDARY: Resume Session

    Phone->>PRIMARY: BLE Write: RESUME_SESSION

    PRIMARY->>PRIMARY: State → RUNNING
    PRIMARY->>PRIMARY: Resume pattern generation

    PRIMARY->>SECONDARY: SYNC:RESUME_SESSION

    SECONDARY->>SECONDARY: State → RUNNING
    SECONDARY->>PRIMARY: SYNC:ACK:RESUME_SESSION

    PRIMARY->>Phone: BLE Notify: SESSION_RESUMED
```

### 3.4 Session Stop

```mermaid
sequenceDiagram
    autonumber
    participant Phone as Phone App
    participant PRIMARY as PRIMARY<br/>(Left Glove)
    participant SECONDARY as SECONDARY<br/>(Right Glove)

    Note over Phone,SECONDARY: Stop Session

    Phone->>PRIMARY: BLE Write: STOP_SESSION

    PRIMARY->>PRIMARY: State → STOPPING
    PRIMARY->>PRIMARY: Stop pattern generation
    PRIMARY->>PRIMARY: Deactivate all motors

    PRIMARY->>SECONDARY: SYNC:STOP_SESSION

    SECONDARY->>SECONDARY: State → STOPPING
    SECONDARY->>SECONDARY: Deactivate all motors
    SECONDARY->>PRIMARY: SYNC:ACK:STOP_SESSION

    PRIMARY->>PRIMARY: Cleanup session data
    PRIMARY->>PRIMARY: State → IDLE

    PRIMARY->>SECONDARY: SYNC:STOPPED
    SECONDARY->>SECONDARY: State → IDLE

    PRIMARY->>Phone: BLE Notify: SESSION_STOPPED<br/>{total_duration, total_patterns}
```

### 3.5 Emergency Stop

```mermaid
sequenceDiagram
    autonumber
    participant Phone as Phone App
    participant PRIMARY as PRIMARY<br/>(Left Glove)
    participant SECONDARY as SECONDARY<br/>(Right Glove)

    Note over Phone,SECONDARY: Emergency Stop (any source)

    alt Phone initiates
        Phone->>PRIMARY: BLE Write: EMERGENCY_STOP
    else PRIMARY detects error
        PRIMARY->>PRIMARY: Error detected
    else SECONDARY detects error
        SECONDARY->>PRIMARY: SYNC:ERROR:type|motor_fault
    end

    PRIMARY->>PRIMARY: IMMEDIATE motor shutdown
    PRIMARY->>PRIMARY: State → ERROR

    PRIMARY->>SECONDARY: SYNC:EMERGENCY_STOP

    SECONDARY->>SECONDARY: IMMEDIATE motor shutdown
    SECONDARY->>SECONDARY: State → ERROR

    PRIMARY->>Phone: BLE Notify: EMERGENCY_STOPPED<br/>{reason}
```

---

## 4. Connection Loss & Recovery

### 4.1 SECONDARY Detects Connection Loss

```mermaid
sequenceDiagram
    autonumber
    participant PRIMARY as PRIMARY<br/>(Left Glove)
    participant SECONDARY as SECONDARY<br/>(Right Glove)

    Note over PRIMARY,SECONDARY: Normal Operation

    PRIMARY->>SECONDARY: SYNC:HEARTBEAT:ts|1000
    SECONDARY->>PRIMARY: SYNC:ACK:HEARTBEAT

    Note over PRIMARY: PRIMARY goes silent<br/>(connection issue)

    SECONDARY->>SECONDARY: Wait 2s... no heartbeat
    SECONDARY->>SECONDARY: Wait 4s... no heartbeat
    SECONDARY->>SECONDARY: Wait 6s... TIMEOUT!

    Note over SECONDARY: Heartbeat timeout (6s = 3 missed)

    rect rgb(255, 230, 230)
        SECONDARY->>SECONDARY: EMERGENCY STOP motors
        SECONDARY->>SECONDARY: State → CONNECTION_LOST
    end

    loop Reconnection attempts (max 3)
        SECONDARY->>SECONDARY: Attempt reconnection
        alt Connection restored
            SECONDARY-->>PRIMARY: BLE reconnected
            PRIMARY->>SECONDARY: SYNC:CONNECTED
            SECONDARY->>SECONDARY: State → READY
            Note over SECONDARY: Recovery successful
        else Connection failed
            SECONDARY->>SECONDARY: Wait 2s before retry
        end
    end

    alt All attempts failed
        SECONDARY->>SECONDARY: State → IDLE
        Note over SECONDARY: Manual intervention required
    end
```

### 4.2 Phone Connection Loss (PRIMARY)

```mermaid
sequenceDiagram
    autonumber
    participant Phone as Phone App
    participant PRIMARY as PRIMARY<br/>(Left Glove)
    participant SECONDARY as SECONDARY<br/>(Right Glove)

    Note over Phone,SECONDARY: Session Running

    PRIMARY->>Phone: BLE Notify: STATUS_UPDATE

    Note over Phone: Phone disconnects<br/>(out of range, app closed)

    Phone--xPRIMARY: Connection lost

    PRIMARY->>PRIMARY: Detect phone disconnect
    PRIMARY->>PRIMARY: State → PHONE_DISCONNECTED

    PRIMARY->>SECONDARY: SYNC:PHONE_DISCONNECTED

    Note over PRIMARY,SECONDARY: Session continues<br/>(autonomous mode)

    loop Phone reconnection window (30s)
        PRIMARY->>PRIMARY: Continue advertising
        alt Phone reconnects
            Phone-->>PRIMARY: BLE reconnected
            PRIMARY->>PRIMARY: State → RUNNING
            PRIMARY->>Phone: BLE Notify: RECONNECTED
            Note over Phone,PRIMARY: Session resumes with phone
        end
    end

    alt Phone timeout
        PRIMARY->>PRIMARY: Stop session gracefully
        PRIMARY->>PRIMARY: State → IDLE
        PRIMARY->>SECONDARY: SYNC:STOP_SESSION
    end
```

### 4.3 Complete Recovery Flow

```mermaid
stateDiagram-v2
    [*] --> NormalOperation

    NormalOperation --> ConnectionLost : Heartbeat timeout (6s)

    ConnectionLost --> EmergencyStop : Immediate
    EmergencyStop --> ReconnectAttempt1 : Motors safe

    ReconnectAttempt1 --> Recovered : Success
    ReconnectAttempt1 --> Wait2s_1 : Failed

    Wait2s_1 --> ReconnectAttempt2
    ReconnectAttempt2 --> Recovered : Success
    ReconnectAttempt2 --> Wait2s_2 : Failed

    Wait2s_2 --> ReconnectAttempt3
    ReconnectAttempt3 --> Recovered : Success
    ReconnectAttempt3 --> GiveUp : Failed

    Recovered --> NormalOperation : State → READY
    GiveUp --> IDLE : State → IDLE

    IDLE --> [*] : Manual restart required
```

---

## 5. Message Reference

### 5.1 SYNC Message Format

All inter-device messages use the SYNC protocol:

```text
SYNC:<command>:<key1>|<value1>|<key2>|<value2>...<EOT>
```

- **Prefix**: `SYNC:` identifies synchronization messages
- **Command**: Action to perform
- **Parameters**: Pipe-delimited key-value pairs
- **Terminator**: EOT character (0x04)

### 5.2 SYNC Commands

| Command | Direction | Parameters | Description |
|---------|-----------|------------|-------------|
| `CONNECTED` | PRIMARY → SECONDARY | (none) | Connection established |
| `START_SESSION` | PRIMARY → SECONDARY | `mode`, `intensity`, `duration` | Begin therapy session |
| `PAUSE_SESSION` | PRIMARY → SECONDARY | (none) | Pause active session |
| `RESUME_SESSION` | PRIMARY → SECONDARY | (none) | Resume paused session |
| `STOP_SESSION` | PRIMARY → SECONDARY | (none) | End therapy session |
| `STOPPED` | PRIMARY → SECONDARY | (none) | Session cleanup complete |
| `EXECUTE_BUZZ` | PRIMARY → SECONDARY | `ch`, `int`, `dur` | Activate specific motor |
| `HEARTBEAT` | PRIMARY → SECONDARY | `ts` | Keep-alive signal |
| `EMERGENCY_STOP` | PRIMARY → SECONDARY | (none) | Immediate motor shutdown |
| `PHONE_DISCONNECTED` | PRIMARY → SECONDARY | (none) | Phone BLE lost |
| `ACK` | SECONDARY → PRIMARY | `<command>` | Acknowledge received command |
| `ERROR` | Either → Either | `type`, `msg` | Error condition |

### 5.3 BLE Commands (Phone → PRIMARY)

| Command | Characteristic | Payload | Description |
|---------|---------------|---------|-------------|
| `SESSION_START` | Control | `{mode, intensity, duration}` | Start therapy session |
| `SESSION_PAUSE` | Control | (none) | Pause active session |
| `SESSION_RESUME` | Control | (none) | Resume paused session |
| `SESSION_STOP` | Control | (none) | End therapy session |
| `EMERGENCY_STOP` | Control | (none) | Immediate shutdown |
| `GET_STATUS` | Status | (none) | Request current state |
| `SET_CONFIG` | Config | `{key, value}` | Update configuration |

### 5.4 BLE Notifications (PRIMARY → Phone)

| Notification | Characteristic | Payload | Description |
|--------------|---------------|---------|-------------|
| `SESSION_STARTED` | Status | `{session_id, ts}` | Session began |
| `SESSION_PAUSED` | Status | `{elapsed}` | Session paused |
| `SESSION_RESUMED` | Status | `{elapsed}` | Session resumed |
| `SESSION_STOPPED` | Status | `{duration, patterns}` | Session ended |
| `STATUS_UPDATE` | Status | `{state, battery, elapsed}` | Periodic update |
| `ERROR` | Status | `{code, message}` | Error occurred |

### 5.5 Timing Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Heartbeat Interval | 2 seconds | Time between HEARTBEAT messages |
| Heartbeat Timeout | 6 seconds | 3 missed heartbeats triggers connection lost |
| Reconnection Attempts | 3 | Maximum reconnection tries |
| Reconnection Delay | 2 seconds | Wait between reconnection attempts |
| Phone Timeout | 30 seconds | Time to wait for phone reconnection |
| Status Update Interval | 5 seconds | Frequency of phone status notifications |
| Boot Scan Timeout | 30 seconds | Maximum time searching for devices |
| Phone Wait Window | 10 seconds | Time to wait for phone at boot |

---

## Related Documentation

- [BLE Protocol](BLE_PROTOCOL.md) - Detailed BLE characteristic definitions
- [Synchronization Protocol](SYNCHRONIZATION_PROTOCOL.md) - SYNC message implementation
- [API Reference](API_REFERENCE.md) - Complete API documentation
- [Technical Reference](TECHNICAL_REFERENCE.md) - System architecture details
