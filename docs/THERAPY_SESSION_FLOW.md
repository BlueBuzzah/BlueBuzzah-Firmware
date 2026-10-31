# Therapy Session Flow & Device Communication

This document provides comprehensive visual diagrams of the BlueBuzzah therapy session lifecycle, firmware responsibilities, and PRIMARY ↔ SECONDARY device communication.

## Overview

The BlueBuzzah system uses a dual-glove architecture with synchronized bilateral haptic feedback:

- **PRIMARY glove**: Connects to smartphone, generates therapy patterns, coordinates SECONDARY
- **SECONDARY glove**: Receives commands from PRIMARY, executes synchronized motor activations
- **Synchronization**: IEEE 1588 PTP-style clock offset calculation achieves <2ms bilateral sync
- **Communication**: BLE UART service for phone ↔ PRIMARY, custom sync protocol for PRIMARY ↔ SECONDARY

---

## 1. Session Lifecycle State Machine

The therapy session follows an 11-state finite state machine (`src/state_machine.cpp`). The diagram shows the transitions current firmware actually fires; CONNECTING and ERROR are defined but never entered (see the table below). `BATTERY_CRITICAL` enters CRITICAL_BATTERY from any state except during a bench `TEST`; only the edge from LOW_BATTERY is drawn:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'labelTextColor': '#fafafa',
  'stateBkg': '#0d3a4d',
  'stateBorder': '#35B6F2',
  'stateLabelColor': '#fafafa',
  'compositeBackground': '#05212D',
  'compositeBorder': '#35B6F2',
  'compositeTitleBackground': '#0d3a4d',
  'transitionColor': '#35B6F2',
  'transitionLabelColor': '#a3a3a3',
  'noteBkgColor': '#05212D',
  'noteBorderColor': '#35B6F2',
  'noteTextColor': '#fafafa'
}}}%%
stateDiagram-v2
    [*] --> IDLE: Boot

    IDLE --> READY: CONNECTED\n(glove pair connects,\nor peer keepalive alive after a stop)

    READY --> RUNNING: START_SESSION\n(SESSION_START or auto-start)
    IDLE --> RUNNING: START_SESSION\n(no SECONDARY connected)

    RUNNING --> PAUSED: PAUSE_SESSION
    RUNNING --> STOPPING: STOP_SESSION\n(SESSION_STOP, session end,\nSECONDARY keepalive timeout)
    RUNNING --> CONNECTION_LOST: DISCONNECTED\n(link drop, or SECONDARY's\n6s keepalive timeout)

    PAUSED --> RUNNING: RESUME_SESSION
    PAUSED --> STOPPING: STOP_SESSION
    PAUSED --> CONNECTION_LOST: DISCONNECTED

    READY --> CONNECTION_LOST: DISCONNECTED

    CONNECTION_LOST --> READY: CONNECTED\n(reconnect or peer traffic resumes)
    CONNECTION_LOST --> IDLE: RECONNECT_FAILED\n(30s without peer)

    STOPPING --> IDLE: STOPPED\n(motors off)

    READY --> PHONE_DISCONNECTED: PHONE_LOST
    RUNNING --> PHONE_DISCONNECTED: PHONE_LOST
    PHONE_DISCONNECTED --> READY: PHONE_RECONNECTED\n(back to previous state)
    PHONE_DISCONNECTED --> RUNNING: PHONE_RECONNECTED\n(back to previous state)
    PHONE_DISCONNECTED --> STOPPING: STOP_SESSION\n(session ended while phone away)
    PHONE_DISCONNECTED --> CONNECTION_LOST: DISCONNECTED

    RUNNING --> LOW_BATTERY: BATTERY_WARNING\n(this glove below 3.8V)
    LOW_BATTERY --> RUNNING: BATTERY_OK\n(back to 3.9V or more)
    LOW_BATTERY --> PAUSED: PAUSE_SESSION
    LOW_BATTERY --> STOPPING: STOP_SESSION
    LOW_BATTERY --> CONNECTION_LOST: DISCONNECTED
    LOW_BATTERY --> PHONE_DISCONNECTED: PHONE_LOST
    PHONE_DISCONNECTED --> LOW_BATTERY: PHONE_RECONNECTED\n(back to previous state)
    LOW_BATTERY --> CRITICAL_BATTERY: BATTERY_CRITICAL\n(this glove critical)
    CRITICAL_BATTERY --> IDLE: BATTERY_OK\n(charged to 3.8V or more)
```

**State Descriptions:**

| State                | Description                                             |
| -------------------- | ------------------------------------------------------- |
| `IDLE`               | No session; glove pair not connected (advertising/scanning) |
| `CONNECTING`         | Defined but never entered (pairing goes IDLE → READY)  |
| `READY`              | Glove pair connected, awaiting session start            |
| `RUNNING`            | Active therapy session, motors executing patterns       |
| `PAUSED`             | Session paused, motors idle, timing preserved           |
| `STOPPING`           | Stop in progress; `STOPPED` follows immediately, so it lands in IDLE (then READY if the pair is still connected) |
| `ERROR`              | Defined but never entered (no code fires `ERROR_OCCURRED` or `EMERGENCY_STOP`) |
| `LOW_BATTERY`        | Session running on a low battery (this glove below 3.8V); behaves like RUNNING, orange slow pulse |
| `CRITICAL_BATTERY`   | This glove's battery critical; motors stopped, red urgent blink, until it charges to 3.8V |
| `CONNECTION_LOST`    | Glove link lost; therapy stopped, purple blink, waits up to 30s for the peer |
| `PHONE_DISCONNECTED` | PRIMARY only: phone lost from READY, RUNNING or LOW_BATTERY; informational, keeps the interrupted state's LED |

---

## 2. Session Initialization Sequence

Complete boot-to-ready sequence including connection establishment and clock synchronization:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'actorBkg': '#0d3a4d',
  'actorBorder': '#35B6F2',
  'actorTextColor': '#fafafa',
  'actorLineColor': '#35B6F2',
  'signalColor': '#35B6F2',
  'signalTextColor': '#fafafa',
  'labelBoxBkgColor': '#05212D',
  'labelBoxBorderColor': '#35B6F2',
  'labelTextColor': '#fafafa',
  'loopTextColor': '#fafafa',
  'noteBkgColor': '#05212D',
  'noteBorderColor': '#35B6F2',
  'noteTextColor': '#fafafa',
  'activationBkgColor': '#0d3a4d',
  'activationBorderColor': '#35B6F2',
  'sequenceNumberColor': '#fafafa'
}}}%%
sequenceDiagram
    participant APP as Smartphone App
    participant P as PRIMARY Glove
    participant S as SECONDARY Glove

    Note over P,S: PHASE 1: Connection Establishment
    P->>P: Boot, advertise "BlueBuzzah"
    S->>S: Boot, scan for "BlueBuzzah"
    S->>P: BLE Connect
    S->>P: IDENTIFY:SECONDARY
    P->>P: STATE: IDLE → READY
    S->>S: STATE: IDLE → READY
    P->>P: Start keepalive (PING/PONG every 70ms until sync valid, then 1s)

    Note over P,S: PHASE 2: Clock Synchronization (~350ms)
    loop Every 70ms until sync valid (keepalive)
        P->>S: PING:seq|T1 (T1=PRIMARY time)
        Note over S: Record T2 (receive time)
        S->>S: Record T3 (send time)
        S->>P: PONG:seq|tx_micros|T2|T3
        Note over P: Record T4 (receive time)<br/>Calculate offset = ((T2-T1)+(T3-T4))/2
        P->>P: Collect offset samples (EMA smoothing)
    end
    Note over P: After 5 valid samples (~350ms):<br/>Clock sync ready

    Note over APP,P: PHASE 3: User Commands
    APP->>P: PROFILE_LOAD or PROFILE_CUSTOM
    Note over P: Profile stays on PRIMARY<br/>(PROFILE_LOAD saves and reboots)<br/>SECONDARY gets timing in each MC: message

    APP->>P: SESSION_START
    Note over P: Check: both gloves' batteries OK?<br/>SECONDARY connected → clock sync valid?<br/>(else Sync not ready error until 10s after connect)
    P->>P: STATE: READY → RUNNING
    P->>S: START_SESSION:seq|ts
    S->>S: STATE: READY → RUNNING

    Note over P,S: PHASE 4: Active Therapy (see next diagram)
```

---

## 3. Active Therapy Session Flow

Pattern generation, message batching, and motor scheduling during an active session:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'mainBkg': '#0d3a4d',
  'nodeBorder': '#35B6F2',
  'clusterBkg': '#05212D',
  'clusterBorder': '#35B6F2',
  'titleColor': '#fafafa',
  'edgeLabelBackground': '#0a0a0a'
}}}%%
flowchart TD
    A["SESSION RUNNING<br/>(PRIMARY & SECONDARY both in RUNNING state)"]

    A --> B["PRIMARY: Start macrocycle generation<br/>(3 patterns × fingers = 12 buzz events at 4 fingers)"]

    B --> C["Generate all 12 events<br/>with delta times from baseTime"]

    C --> D["Calculate baseTime<br/>= now + lead_time<br/>(lead_time = RTT + 3σ margin + overheads)"]

    D --> E["Serialize MACROCYCLE message<br/>MC:seq|baseHigh|baseLow|offHigh|offLow|dur|count|events..."]

    E --> F["Send MACROCYCLE to SECONDARY<br/>(~200 bytes, 72% reduction)"]

    F --> G["PRIMARY: Schedule all 12 events<br/>in ActivationQueue"]
    G --> H["SECONDARY: Receive MACROCYCLE<br/>Apply clock offset to baseTime"]
    H --> I["SECONDARY: Schedule all 12 events<br/>in ActivationQueue<br/>(convert PRIMARY time to LOCAL time)"]

    I --> J["FreeRTOS Motor Task processes queue"]

    J --> K["Wait for activation_time"]

    K --> L["Motor activates at scheduled time<br/>(both gloves synchronized)"]

    L --> M["Motor ON for TIME_ON<br/>(100ms typical)"]

    M --> N["Motor OFF for TIME_OFF + jitter<br/>(67ms + variation)"]

    N --> O{All events in<br/>macrocycle<br/>processed?}

    O -->|No| J

    O -->|Yes - Macrocycle Complete| S["Inter-macrocycle RELAX<br/>(2× pattern duration = 1336ms)"]

    S --> B

    style A fill:#35B6F2,color:#0a0a0a
    style S fill:#05212D,color:#fafafa
```

**Key Metrics:**

- Pattern duration: ~668ms (4 events × 167ms average)
- Macrocycle duration: ~3.3s (3 patterns + relaxation)
- Batching efficiency: 72% bandwidth reduction (200 bytes vs 720 bytes)

---

## 4. Clock Synchronization (PTP 4-Timestamp)

IEEE 1588 PTP-style clock offset calculation for sub-2ms bilateral synchronization:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'actorBkg': '#0d3a4d',
  'actorBorder': '#35B6F2',
  'actorTextColor': '#fafafa',
  'actorLineColor': '#35B6F2',
  'signalColor': '#35B6F2',
  'signalTextColor': '#fafafa',
  'labelBoxBkgColor': '#05212D',
  'labelBoxBorderColor': '#35B6F2',
  'labelTextColor': '#fafafa',
  'loopTextColor': '#fafafa',
  'noteBkgColor': '#05212D',
  'noteBorderColor': '#35B6F2',
  'noteTextColor': '#fafafa',
  'activationBkgColor': '#0d3a4d',
  'activationBorderColor': '#35B6F2',
  'sequenceNumberColor': '#fafafa'
}}}%%
sequenceDiagram
    participant P as PRIMARY<br/>(Clock A)
    participant S as SECONDARY<br/>(Clock B)

    Note over P,S: IEEE 1588 PTP-Style Offset Calculation

    rect rgba(5, 33, 45, 0.5)
        Note over P: T1 = getMicros()
        P->>S: PING:42|T1
        Note over P: Wait for response
    end

    rect rgba(5, 33, 45, 0.5)
        Note over S: T2 = getMicros()<br/>(receive time on SECONDARY)
        Note over S: Process & prepare response
        Note over S: T3 = getMicros()<br/>(send time on SECONDARY)
        S->>P: PONG:42|tx_micros|T2|T3
    end

    rect rgba(5, 33, 45, 0.5)
        Note over P: T4 = getMicros()<br/>(receive response)
        Note over P: Calculate:<br/>offset = ((T2-T1) + (T3-T4)) / 2
        Note over P: offset > 0 → SECONDARY ahead
        Note over P: offset < 0 → PRIMARY ahead
        Note over P: Add to circular buffer<br/>Sort & compute median
    end

    Note over P,S: After 5+ valid samples:<br/>Clock sync ready ✓

    Note over P,S: During therapy:<br/>Refresh offset every 250ms<br/>(EMA smoothing for drift)
```

**Offset Calculation Formula:**

```
offset = ((T2 - T1) + (T3 - T4)) / 2
RTT = (T4 - T1) - (T3 - T2)
```

**Synchronization Parameters:**

- **Target accuracy**: <2ms bilateral sync
- **Confidence threshold**: RTT < 60ms
- **Minimum samples**: 5 for sync ready
- **Smoothing**: Exponential moving average (α = 0.1)
- **Update frequency**: Every PING/PONG (70ms until sync valid, 250ms during therapy, 1s idle)

### Warm-Start Sync Recovery

After brief BLE disconnects, the firmware can skip full clock synchronization using cached sync state:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'actorBkg': '#0d3a4d',
  'actorBorder': '#35B6F2',
  'actorTextColor': '#fafafa',
  'actorLineColor': '#35B6F2',
  'signalColor': '#35B6F2',
  'signalTextColor': '#fafafa',
  'labelBoxBkgColor': '#05212D',
  'labelBoxBorderColor': '#35B6F2',
  'labelTextColor': '#fafafa',
  'loopTextColor': '#fafafa',
  'noteBkgColor': '#05212D',
  'noteBorderColor': '#35B6F2',
  'noteTextColor': '#fafafa',
  'activationBkgColor': '#0d3a4d',
  'activationBorderColor': '#35B6F2',
  'sequenceNumberColor': '#fafafa'
}}}%%
sequenceDiagram
    participant P as PRIMARY
    participant S as SECONDARY

    Note over P,S: SCENARIO: Brief BLE disconnect (<15s)

    Note over P: Sync cache (refreshed on every accepted sample):<br/>• Last clock offset<br/>• Last drift rate<br/>• Cached timestamp
    P->>P: Connection lost
    S->>S: Connection lost
    Note over S: Wait for reconnection

    rect rgba(5, 33, 45, 0.5)
        Note over P,S: Reconnection within 15 seconds
        S->>P: BLE Reconnect
        S->>P: IDENTIFY:SECONDARY
        P->>P: STATE: CONNECTION_LOST → READY
        P->>P: Check sync cache age
        Note over P: Cache age < 15s?<br/>→ WARM START
    end

    P->>S: PING:seq|T1 (1st keepalive)
    S->>P: PONG:seq|tx_micros|T2|T3
    Note over P: 3 confirmatory samples (each within 5ms<br/>of the projected offset) = sync ready<br/>(~210ms vs ~350ms)
```

**Warm-Start Conditions:**

| Condition | Warm-Start | Cold-Start |
|-----------|:----------:|:----------:|
| Disconnect duration < 15s | ✓ | ✗ |
| Disconnect duration ≥ 15s | ✗ | ✓ |
| First connection (no cache) | ✗ | ✓ |
| Device reboot (or a sample > 5ms off the projection) | ✗ | ✓ |

**Recovery Time Comparison:**

| Start Type | Required Samples | Time to Sync Ready |
|------------|:----------------:|:------------------:|
| Cold-Start | 5+ samples | ~350ms |
| Warm-Start | 3 samples | ~210ms |

**Sync Cache Contents:**

```cpp
// include/sync_protocol.h
struct WarmStartCache {
    int64_t cachedOffset;        // Last known median offset
    float cachedDriftRate;       // Last known drift rate (us/ms)
    uint32_t cacheTimestamp;     // syncNowMs() epoch (getMicros()/1000) when cached
    bool isValid;                // Cache contains usable data
};

// From config.h:
// SYNC_WARM_START_VALIDITY_MS = 15000 (15 seconds)
// SYNC_WARM_START_MIN_SAMPLES = 3
// SYNC_WARM_START_TOLERANCE_US = 5000 (5ms)
```

**Purpose:** Warm-start gets the gloves back to a valid clock sync sooner after transient BLE issues (interference, temporary out-of-range) by avoiding full re-synchronization. The session itself is stopped on connection loss and must be started again.

---

## 5. Synchronized Motor Activation

How both gloves achieve sub-2ms synchronization despite BLE latency:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'mainBkg': '#0d3a4d',
  'nodeBorder': '#35B6F2',
  'clusterBkg': '#05212D',
  'clusterBorder': '#35B6F2',
  'titleColor': '#fafafa',
  'edgeLabelBackground': '#0a0a0a'
}}}%%
flowchart LR
    subgraph PRIMARY["PRIMARY (0ms reference)"]
        A["1. Generate macrocycle<br/>t=500ms"]
        B["2. baseTime<br/>= 500ms + lead_time<br/>= 570ms"]
        C["3. Send MACROCYCLE<br/>(12 events)"]
        D["4. Schedule all in queue<br/>baseTime=570ms"]
    end

    subgraph BLE["Bluetooth Latency<br/>(~7.5-20ms)"]
        E["Transit (7.5-20ms)"]
    end

    subgraph SECONDARY["SECONDARY (offset=-1ms)"]
        F["5. Receive MACROCYCLE<br/>(570ms PRIMARY time,<br/>offset carried in MC:)"]
        G["6. Convert to LOCAL<br/>time = 570ms + (-1ms)<br/>= 569ms"]
        H["7. Schedule all in queue<br/>baseTime=569ms"]
        I["8. Wait for 569ms"]
    end

    subgraph EXECUTION["Motor Activation"]
        J["Both motors activate<br/>within <2ms of each other"]
    end

    A --> B --> C --> E --> F --> G --> H --> I --> J
    C --> D --> J
    D -.->|Waits for<br/>570ms| J
    I -.->|Waits for<br/>569ms| J
```

**Key Insight:** By scheduling events with absolute activation times (in PRIMARY's timebase) and applying clock offset conversion on SECONDARY, the system decouples synchronization accuracy from BLE transmission latency.

---

## 6. Message Types & Flow

All messages exchanged between devices during a therapy session:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'mainBkg': '#0d3a4d',
  'nodeBorder': '#35B6F2',
  'clusterBkg': '#05212D',
  'clusterBorder': '#35B6F2',
  'titleColor': '#fafafa',
  'edgeLabelBackground': '#0a0a0a'
}}}%%
graph TD
    A["PRIMARY ← Phone Commands"]

    B["Handshake Messages"]
    C["Synchronization Messages"]
    D["Therapy Messages"]
    E["Status Messages"]

    B --> B0["IDENTIFY:SECONDARY<br/>(SECONDARY → PRIMARY on connect)"]
    B --> B1["START_SESSION:seq|ts"]
    B --> B2["PAUSE_SESSION:seq|ts"]
    B --> B3["RESUME_SESSION:seq|ts"]
    B --> B4["STOP_SESSION:seq|ts"]

    C --> C1["PING:seq|T1<br/>(70ms acquiring, 250ms therapy, 1s idle)"]
    C --> C2["PONG:seq|tx_micros|T2|T3<br/>(response)"]

    D --> D2["MC:seq|baseHigh|baseLow|offHigh|offLow|dur|count|events<br/>(batched, 3 patterns)"]
    D --> D3["MC_ACK:seq|ts<br/>(acknowledgment)"]

    E --> E1["GET_BATTERY"]
    E --> E2["BATRESPONSE:voltage (reply)<br/>BATRESPONSE:voltage|level<br/>(after every SECONDARY battery check)"]

    A --> B
    A --> C
    A --> D
    A --> E

    B1 --> F["SECONDARY state → RUNNING<br/>(motors follow MC: events)"]
    B2 --> G["SECONDARY state → PAUSED"]
    B3 --> H["SECONDARY state → RUNNING"]
    B4 --> I["Clean shutdown<br/>(queues cleared, motors off)"]

    D2 --> K["Activate 12 events<br/>over ~3.3 seconds"]

    E1 --> L["Query SECONDARY<br/>battery voltage"]
```

**Message Routing:**

- **Phone → PRIMARY**: BLE UART service (Nordic UART Service)
- **PRIMARY → SECONDARY**: Custom sync protocol over BLE UART
- **Keepalive**: PING/PONG in all connected states: every 70ms until clock sync is valid, 250ms during therapy, 1s idle

---

## 7. Pattern Generation (RNDP/SEQUENTIAL/MIRRORED)

How the firmware generates vibrotactile patterns (3 patterns = 1 macrocycle):

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'mainBkg': '#0d3a4d',
  'nodeBorder': '#35B6F2',
  'clusterBkg': '#05212D',
  'clusterBorder': '#35B6F2',
  'titleColor': '#fafafa',
  'edgeLabelBackground': '#0a0a0a'
}}}%%
flowchart TD
    A["Start Macrocycle<br/>(12 events total)"]

    B["Pattern 1<br/>(4 events, ~668ms)"]
    C["Pattern 2<br/>(4 events, ~668ms)"]
    D["Pattern 3<br/>(4 events, ~668ms)"]
    E["Inter-macrocycle RELAX<br/>(~1336ms = 2× pattern)"]

    A --> F{Pattern Type?}

    F -->|RNDP<br/>Random| G["Generate random<br/>permutation (0,1,2,3)"]
    F -->|SEQ<br/>Sequential| H["Generate sequence<br/>(0→1→2→3)"]
    F -->|MIRRORED| I["Both hands same<br/>fingers"]

    G --> J{Mirror Pattern?}
    H --> J
    I --> K

    J -->|Yes| K["PRIMARY finger =<br/>SECONDARY finger<br/>(noisy vCR)"]
    J -->|No| L["PRIMARY & SECONDARY<br/>independent sequences<br/>(standard vCR)"]

    K --> M["Apply jitter<br/>(TIME_OFF ± variation)"]
    L --> M

    M --> N["Event 1: F0, 100ms ON, 67ms OFF"]
    M --> O["Event 2: F1, 100ms ON, 67ms OFF"]
    M --> P["Event 3: F2, 100ms ON, 67ms OFF"]
    M --> Q["Event 4: F3, 100ms ON, 67ms OFF"]

    N --> B
    O --> B
    P --> B
    Q --> B

    B --> C
    C --> D
    D --> E
    E --> A

    style B fill:#0d3a4d,color:#fafafa
    style C fill:#0d3a4d,color:#fafafa
    style D fill:#0d3a4d,color:#fafafa
    style E fill:#6c757d,color:#fafafa
```

**Pattern Types:**

- **RNDP (Random Permutation)**: Finger order randomized each pattern
- **SEQUENTIAL**: Fingers activate in order (0→1→2→3)
- **MIRRORED**: Both hands use the same random finger order, regardless of the mirror setting

**Timing:**

- Event duration: TIME_ON (100ms) + TIME_OFF (67ms) + jitter
- Pattern duration: 4 events × ~167ms = ~668ms
- Macrocycle: 3 patterns + 1336ms relaxation = ~3.3s

---

## 8. Battery & Error Handling

Battery checks and connection-loss recovery flows:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'labelTextColor': '#fafafa',
  'stateBkg': '#0d3a4d',
  'stateBorder': '#35B6F2',
  'stateLabelColor': '#fafafa',
  'compositeBackground': '#05212D',
  'compositeBorder': '#35B6F2',
  'compositeTitleBackground': '#0d3a4d',
  'transitionColor': '#35B6F2',
  'transitionLabelColor': '#a3a3a3',
  'noteBkgColor': '#05212D',
  'noteBorderColor': '#35B6F2',
  'noteTextColor': '#fafafa'
}}}%%
stateDiagram-v2
    [*] --> READY

    READY --> REFUSED: SESSION_START with either\nglove's battery below 3.8V
    REFUSED --> READY: Error "Battery too low"
    READY --> RUNNING: SESSION_START\n(both batteries OK)

    RUNNING --> LOW_BATTERY: This glove below 3.8V\n(session continues, orange LED)
    LOW_BATTERY --> RUNNING: Back to 3.9V or more

    RUNNING --> CONNECTION_LOST: Link drop, or SECONDARY\nhears no PING/MC for 6s
    RUNNING --> IDLE: PRIMARY hears no PONG/MC_ACK\nfor 6s (stop + STOP_SESSION)

    CONNECTION_LOST --> READY: Reconnect or\npeer traffic resumes
    CONNECTION_LOST --> IDLE: 30s without\npeer traffic

    IDLE --> [*]
```

**Battery Thresholds:**

- **Low**: two readings in a row below 3.8V (`BATTERY_LOW_VOLTAGE`, per board in `board_config.h`; provisional until bench-calibrated, see [CALIBRATION_GUIDE.md](./CALIBRATION_GUIDE.md#low-battery-threshold)): the glove can't finish a 2-hour session. Its LED turns orange and a running session continues as LOW_BATTERY. While either glove is low, `SESSION_START` is refused with `Battery too low` and the PRIMARY doesn't auto-start. Clears at ≥3.9V (`BATTERY_LOW_HYSTERESIS_V`)
- **Critical**: two readings in a row below 3.3V (`BATTERY_CRITICAL_VOLTAGE`, `BATTERY_CRITICAL_CONFIRM_READINGS`): the glove enters CRITICAL_BATTERY (motors off, red urgent blink; a critical SECONDARY acknowledges macrocycles but plays none), and the PRIMARY ends the session (`LAST_REASON:BATTERY`) when either glove is critical; a bench `TEST` is never stopped this way. Returns to IDLE once the glove charges to ≥3.8V
- Each glove checks its battery every 60s between pulses, on its first idle loop after boot, and (SECONDARY) right after connecting to the PRIMARY; after each motor-idle reading the SECONDARY pushes its voltage and level to the PRIMARY (`BATRESPONSE:<volts>|<level>`), and the PRIMARY gates and stops on that level. A glove with no reading yet doesn't block a start

**Connection Loss Handling:**

- **Detection**: BLE disconnect on either glove; on the SECONDARY, 6 seconds without a PING or macrocycle (`KEEPALIVE_TIMEOUT_MS`); on the PRIMARY during therapy, 6 seconds without SECONDARY traffic stops the session instead
- **Reconnection**: Scanning/advertising continue; the peer can reconnect at any time
- **Timeout**: Return to IDLE (LED back to breathing blue) after 30s without peer traffic

---

## 9. Lead Time Calculation (Adaptive Scheduling)

Dynamic lead time adjustment based on measured BLE latency:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'mainBkg': '#0d3a4d',
  'nodeBorder': '#35B6F2',
  'clusterBkg': '#05212D',
  'clusterBorder': '#35B6F2',
  'titleColor': '#fafafa',
  'edgeLabelBackground': '#0a0a0a'
}}}%%
flowchart TD
    A["Receive RTT measurement<br/>from PING/PONG cycle"]

    B["Calculate one-way latency<br/>latency = RTT / 2"]

    C["Apply EMA smoothing (α = 0.3)<br/>smooth_lat = α×measured + (1-α)×prev<br/>(samples > 3× smooth_lat rejected)"]

    D["Track variance<br/>EMA of |measured - smooth_lat|"]

    E["Compute lead time<br/>lead_time = 2×smooth_lat + 6×variance<br/>+ 10ms processing + 5ms generation"]

    F["Clamp to bounds<br/>70ms (SYNC_MIN_LEAD_TIME_US) ≤ lead_time<br/>≤ 150ms (SYNC_MAX_LEAD_TIME_US)"]

    G["Use for MACROCYCLE events<br/>baseTime = now + lead_time"]

    A --> B --> C --> D --> E --> F --> G

    style E fill:#17a2b8,color:#fafafa
    style F fill:#35B6F2,color:#0a0a0a
```

**Lead Time Formula:**

```
lead_time = RTT + 3σ margin + processing_overhead + generation_overhead
          = 2×smoothed_one_way + 6×variance + 10ms + 5ms
lead_time = clamp(lead_time, 70ms, 150ms)

// Before 3 latency samples: 35ms + 10ms + 5ms = 50ms (not clamped)

// From config.h:
// SYNC_MIN_LEAD_TIME_US = 70000  (70ms minimum)
// SYNC_MAX_LEAD_TIME_US = 150000 (150ms maximum)
```

**Purpose:** Ensures SECONDARY has sufficient time to receive, deserialize, and schedule events before activation. The 70-150ms range accounts for:
- BLE round-trip time (~16-24ms for 2M PHY)
- Processing overhead (SYNC_PROCESSING_OVERHEAD_US = 10ms)
- Generation overhead (SYNC_GENERATION_OVERHEAD_US = 5ms)
- Safety margin for BLE retransmissions

---

## 10. Complete BLE Command Sequence

Full message flow from phone command to synchronized motor execution:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'primaryColor': '#0d3a4d',
  'primaryTextColor': '#fafafa',
  'primaryBorderColor': '#35B6F2',
  'lineColor': '#35B6F2',
  'secondaryColor': '#05212D',
  'tertiaryColor': '#0a0a0a',
  'background': '#0a0a0a',
  'actorBkg': '#0d3a4d',
  'actorBorder': '#35B6F2',
  'actorTextColor': '#fafafa',
  'actorLineColor': '#35B6F2',
  'signalColor': '#35B6F2',
  'signalTextColor': '#fafafa',
  'labelBoxBkgColor': '#05212D',
  'labelBoxBorderColor': '#35B6F2',
  'labelTextColor': '#fafafa',
  'loopTextColor': '#fafafa',
  'noteBkgColor': '#05212D',
  'noteBorderColor': '#35B6F2',
  'noteTextColor': '#fafafa',
  'activationBkgColor': '#0d3a4d',
  'activationBorderColor': '#35B6F2',
  'sequenceNumberColor': '#fafafa'
}}}%%
sequenceDiagram
    participant Ph as Phone
    participant P as PRIMARY
    participant S as SECONDARY

    Ph->>P: SESSION_START
    P->>P: Validate (battery, clock sync if SECONDARY connected)

    P->>P: Load profile settings
    P->>P: State: READY → RUNNING
    P->>S: START_SESSION:seq|ts
    S->>S: State: READY → RUNNING

    P->>Ph: SESSION_STATUS:RUNNING

    loop Every 250ms (keepalive + clock sync)
        P->>S: PING:seq|T1
        S->>P: PONG:seq|tx_micros|T2|T3
        P->>P: Compute clock offset, EMA update
    end

    loop Every ~3.3s (pattern execution)
        alt Batching Mode
            P->>S: MC:seq|baseHigh|baseLow|offHigh|offLow|dur|count|...
            S->>P: MC_ACK:seq|ts
        end

        P->>P: Schedule all 12 events in ActivationQueue
        S->>S: Schedule all 12 events in ActivationQueue

        par Motor Task (PRIMARY)
            P->>P: Wait for activate_time
            P->>P: Execute motor (I2C to DRV2605)
        and Motor Task (SECONDARY)
            S->>S: Wait for activate_time (local)
            S->>S: Execute motor (I2C to DRV2605)
        end
    end

    Ph->>P: SESSION_PAUSE
    P->>P: State: RUNNING → PAUSED
    P->>S: PAUSE_SESSION:seq|ts
    S->>S: State: RUNNING → PAUSED
    P->>Ph: SESSION_STATUS:PAUSED

    Ph->>P: SESSION_RESUME
    P->>P: State: PAUSED → RUNNING
    P->>S: RESUME_SESSION:seq|ts
    S->>S: State: PAUSED → RUNNING
    P->>Ph: SESSION_STATUS:RUNNING

    Note over P,S: ... therapy continues ...

    Ph->>P: SESSION_STOP
    P->>P: Stop therapy, motors off
    P->>P: State: RUNNING/PAUSED → STOPPING → IDLE
    P->>S: STOP_SESSION:seq|ts
    S->>S: Cancel pending events, motors off
    S->>S: State: RUNNING/PAUSED → STOPPING → IDLE
    P->>Ph: SESSION_STATUS:IDLE
    Note over P,S: main loop promotes IDLE → READY while the pair is connected
```

---

## Firmware Responsibilities

| Responsibility                        | PRIMARY | SECONDARY |
| ------------------------------------- | :-----: | :-------: |
| **Pattern generation**                |    ✓    |     ✗     |
| **Phone communication**               |    ✓    |     ✗     |
| **Clock synchronization (initiator)** |    ✓    |     ✗     |
| **Clock synchronization (responder)** |    ✗    |     ✓     |
| **Lead time calculation**             |    ✓    |     ✗     |
| **Therapy command distribution**      |    ✓    |     ✗     |
| **Therapy command execution**         |    ✓    |     ✓     |
| **Battery monitoring (both gloves)**  |    ✓    |     ✗     |
| **Battery voltage reporting**         |    ✓    |     ✓     |
| **Motor activation**                  |    ✓    |     ✓     |
| **Session state authority**           | Master  | Follower  |

---

## Technical Parameters

### Timing Values

| Parameter             | Value   | Description                               |
| --------------------- | ------- | ----------------------------------------- |
| TIME_ON               | 100ms   | Motor activation duration (per event)     |
| TIME_OFF              | 67ms    | Inter-burst interval (base value)         |
| Lead time             | 70-150ms | Adaptive scheduling margin (config.h bounds) |
| Pattern duration      | ~668ms  | 4 events × (TIME_ON + TIME_OFF + jitter)  |
| Macrocycle duration   | ~3.3s   | 3 patterns + inter-macrocycle relaxation  |
| Relaxation period     | ~1336ms | 2× pattern duration (between macrocycles) |
| PING/PONG frequency   | 70ms / 250ms / 1s | Keepalive and clock sync refresh rate (acquiring / therapy / idle) |
| Connection timeout    | 6s      | SECONDARY: no PING/MC for 6s → connection lost; PRIMARY: no SECONDARY traffic for 6s during therapy → session stopped |
| Connection-lost window | 30s    | Purple LED before returning to IDLE (reconnect still possible) |

### Synchronization Accuracy

| Metric                 | Value                     | Notes                                   |
| ---------------------- | ------------------------- | --------------------------------------- |
| Target accuracy        | <2ms                      | Bilateral synchronization (both gloves) |
| Synchronization method | IEEE 1588 PTP 4-timestamp | Clock offset calculation                |
| Confidence threshold   | RTT < 60ms                | For accepting offset samples            |
| Minimum samples        | 5                         | Before declaring sync ready             |
| Smoothing algorithm    | EMA (α = 0.1)             | Exponential moving average              |
| Update frequency       | ~14 Hz / 4 Hz / 1 Hz      | Every PING/PONG cycle (acquiring / therapy / idle) |

### Message Efficiency

| Mode                | Message Size | Messages/Macrocycle | Total Bytes | Efficiency        |
| ------------------- | ------------ | ------------------- | ----------- | ----------------- |
| Individual messages (removed) | 60 bytes | 12            | 720 bytes   | Removed (legacy)  |
| MACROCYCLE batching | 200 bytes    | 1                   | 200 bytes   | **72% reduction** |

### Battery Thresholds

| Level    | Voltage Range | Action                                            |
| -------- | ------------- | ------------------------------------------------- |
| Normal   | ≥3.8V         | Normal operation                                  |
| Low      | 3.3-3.8V      | After two readings in a row: orange LED; a running session continues as LOW_BATTERY; `SESSION_START` refused (`Battery too low`) and no auto-start if either glove is low; clears at ≥3.9V |
| Critical | <3.3V         | After two readings in a row: CRITICAL_BATTERY (red urgent blink) until charged to ≥3.8V; the PRIMARY stops a running session if either glove is critical |

---

## Related Documentation

This document complements the following technical documentation:

- **[ARCHITECTURE.md](./ARCHITECTURE.md)** - Complete firmware architecture overview
- **[SYNCHRONIZATION_PROTOCOL.md](./SYNCHRONIZATION_PROTOCOL.md)** - Detailed PRIMARY ↔ SECONDARY sync protocol specification
- **[BLE_PROTOCOL.md](./BLE_PROTOCOL.md)** - Phone ↔ PRIMARY BLE command protocol

---

## Summary

The BlueBuzzah firmware implements a sophisticated bilateral synchronization system:

1. **Dual-glove architecture** with PRIMARY/SECONDARY roles for coordinated therapy
2. **IEEE 1588 PTP-style clock synchronization** achieving <2ms bilateral sync accuracy
3. **Adaptive lead time scheduling** that decouples sync accuracy from BLE latency variability
4. **Message batching (MACROCYCLE)** reducing BLE bandwidth by 72%
5. **Graceful error handling** with automatic reconnection and a battery check at session start
6. **FreeRTOS task isolation** for deterministic motor timing (<1ms activation accuracy)

This architecture enables precise bilateral haptic feedback while operating within the constraints of BLE latency (7.5-20ms typical) and embedded hardware limitations (nRF52840: 256KB RAM, 1MB flash).
