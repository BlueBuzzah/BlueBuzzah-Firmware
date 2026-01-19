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

The therapy session follows an 11-state finite state machine with graceful error handling:

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

    IDLE --> CONNECTING: BLE scan/advertise

    CONNECTING --> READY: SECONDARY connects\n(PRIMARY receives READY)

    READY --> RUNNING: SESSION_START command\nfrom phone

    RUNNING --> PAUSED: SESSION_PAUSE command
    RUNNING --> STOPPING: SESSION_STOP command
    RUNNING --> LOW_BATTERY: Battery warning\n(>50s remaining)
    RUNNING --> CONNECTION_LOST: No PONG for 6s
    RUNNING --> ERROR: Emergency stop\nor timeout

    PAUSED --> RUNNING: SESSION_RESUME command
    PAUSED --> STOPPING: SESSION_STOP command
    PAUSED --> ERROR: Connection lost

    LOW_BATTERY --> RUNNING: Battery recovers
    LOW_BATTERY --> CRITICAL_BATTERY: Battery critical

    CRITICAL_BATTERY --> IDLE: Forced shutdown

    CONNECTION_LOST --> READY: RECONNECTED\n(3 retry attempts,\n2s between)
    CONNECTION_LOST --> IDLE: RECONNECT_FAILED\n(all retries exhausted)

    ERROR --> IDLE: RESET

    STOPPING --> IDLE: Motors off,\ncleanup complete

    READY --> IDLE: Timeout or\nmanual reset

    PHONE_DISCONNECTED --> READY: Phone reconnects\n(within 30s)
    PHONE_DISCONNECTED --> IDLE: Phone timeout\n(>30s)
```

**State Descriptions:**

| State                | Description                                             |
| -------------------- | ------------------------------------------------------- |
| `IDLE`               | System initialized, no connections                      |
| `CONNECTING`         | BLE scanning/advertising for device pairing             |
| `READY`              | Devices connected, synchronized, awaiting session start |
| `RUNNING`            | Active therapy session, motors executing patterns       |
| `PAUSED`             | Session paused, motors idle, timing preserved           |
| `STOPPING`           | Graceful shutdown in progress                           |
| `ERROR`              | Error condition, requires reset                         |
| `LOW_BATTERY`        | Battery low (`3.3-3.6V`), continue with warning         |
| `CRITICAL_BATTERY`   | Battery critical (`<3.3V`), forced shutdown             |
| `CONNECTION_LOST`    | SECONDARY disconnected, attempting reconnection         |
| `PHONE_DISCONNECTED` | Phone disconnected, gloves remain paired                |

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
    S->>P: READY message
    P->>P: STATE: IDLE → READY
    P->>P: Start keepalive (PING/PONG every 1s)

    Note over P,S: PHASE 2: Clock Synchronization (5+ seconds)
    loop Every 1 second (keepalive)
        P->>S: PING:seq|T1 (T1=PRIMARY time)
        Note over S: Record T2 (receive time)
        S->>S: Record T3 (send time)
        S->>P: PONG:seq|0|T2|T3
        Note over P: Record T4 (receive time)<br/>Calculate offset = ((T2-T1)+(T3-T4))/2
        P->>P: Collect offset samples (EMA smoothing)
    end
    Note over P: After 5 valid samples (~5s):<br/>Clock sync ready

    Note over APP,P: PHASE 3: User Commands
    APP->>P: PROFILE_LOAD or PROFILE_CUSTOM
    P->>S: PARAM_UPDATE:... (broadcast params)

    APP->>P: SESSION_START
    Note over P: Check: SECONDARY connected? Battery OK?
    P->>S: START_SESSION:seq|ts
    P->>P: STATE: READY → RUNNING
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

    A --> B["PRIMARY: Start macrocycle generation<br/>(3 patterns = 12 buzz events)"]

    B --> C["Generate all 12 events<br/>with absolute activation times"]

    C --> D["Calculate activate_time<br/>= now + lead_time<br/>(lead_time = RTT + 3σ margin)"]

    D --> E["Serialize MACROCYCLE message<br/>MC:seq|baseTime|12|events..."]

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

    O -->|Yes - Pattern Complete| P["Check inter-pattern timing"]

    P --> Q{Pattern count<br/>== 3?}

    Q -->|No| R["Next pattern in macrocycle<br/>(no relaxation)"]
    Q -->|Yes| S["Inter-macrocycle RELAX<br/>(2× pattern duration = 1336ms)"]

    R --> B
    S --> B

    style A fill:#35B6F2,color:#0a0a0a
    style Q fill:#17a2b8,color:#fafafa
    style R fill:#35B6F2,color:#0a0a0a
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
        S->>P: PONG:42|0|T2|T3
    end

    rect rgba(5, 33, 45, 0.5)
        Note over P: T4 = getMicros()<br/>(receive response)
        Note over P: Calculate:<br/>offset = ((T2-T1) + (T3-T4)) / 2
        Note over P: offset > 0 → SECONDARY ahead
        Note over P: offset < 0 → PRIMARY ahead
        Note over P: Add to circular buffer<br/>Sort & compute median
    end

    Note over P,S: After 5+ valid samples:<br/>Clock sync ready ✓

    Note over P,S: During therapy:<br/>Refresh offset every 1s<br/>(EMA smoothing for drift)
```

**Offset Calculation Formula:**

```
offset = ((T2 - T1) + (T3 - T4)) / 2
RTT = (T4 - T1) - (T3 - T2)
```

**Synchronization Parameters:**

- **Target accuracy**: <2ms bilateral sync
- **Confidence threshold**: RTT < 80ms
- **Minimum samples**: 5 for sync ready
- **Smoothing**: Exponential moving average (α = 0.2)
- **Update frequency**: Every 1 second (with keepalive)

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

    P->>P: Connection lost
    S->>S: Connection lost
    Note over P: Cache sync state:<br/>• Last clock offset<br/>• Last drift rate<br/>• Cached timestamp
    Note over S: Wait for reconnection

    rect rgba(5, 33, 45, 0.5)
        Note over P,S: Reconnection within 15 seconds
        S->>P: BLE Reconnect
        S->>P: READY message
        P->>P: Check sync cache age
        Note over P: Cache age < 15s?<br/>→ WARM START
    end

    P->>S: PING:seq|T1 (1st keepalive)
    S->>P: PONG:seq|0|T2|T3
    Note over P: Single sample + cached state<br/>= sync ready (~3s vs 5+s)

    P->>P: STATE: READY → can resume
```

**Warm-Start Conditions:**

| Condition | Warm-Start | Cold-Start |
|-----------|:----------:|:----------:|
| Disconnect duration < 15s | ✓ | ✗ |
| Disconnect duration ≥ 15s | ✗ | ✓ |
| First connection (no cache) | ✗ | ✓ |
| Device reboot | ✗ | ✓ |

**Recovery Time Comparison:**

| Start Type | Required Samples | Time to Sync Ready |
|------------|:----------------:|:------------------:|
| Cold-Start | 5+ samples | ~5+ seconds |
| Warm-Start | 1-2 samples | ~2-3 seconds |

**Sync Cache Contents:**

```cpp
struct SyncCache {
    int32_t lastClockOffset;      // Microseconds offset
    float lastDriftRate;          // us/ms drift compensation
    uint32_t cachedTimestamp;     // When cache was written
    uint8_t confidenceLevel;      // Sample quality indicator
};

// From config.h:
// SYNC_WARM_START_VALIDITY_MS = 15000 (15 seconds)
```

**Purpose:** Warm-start significantly improves therapy continuity after transient BLE issues (interference, temporary out-of-range) by avoiding full re-synchronization.

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
        B["2. baseTime<br/>= 500ms + lead_time<br/>= 550ms"]
        C["3. Send MACROCYCLE<br/>(12 events)"]
        D["4. Schedule all in queue<br/>baseTime=550ms"]
    end

    subgraph BLE["Bluetooth Latency<br/>(~7.5-20ms)"]
        E["Transit (7.5-20ms)"]
    end

    subgraph SECONDARY["SECONDARY (offset=-1ms)"]
        F["5. Receive MACROCYCLE<br/>(550ms PRIMARY time)"]
        G["6. Convert to LOCAL<br/>time = 550ms + (-1ms)<br/>= 549ms"]
        H["7. Schedule all in queue<br/>baseTime=549ms"]
        I["8. Wait for 549ms"]
    end

    subgraph EXECUTION["Motor Activation"]
        J["Both motors activate<br/>within <2ms of each other"]
    end

    A --> B --> C --> E --> F --> G --> H --> I --> J
    C --> D --> J
    D -.->|Waits for<br/>550ms| J
    I -.->|Waits for<br/>549ms| J
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

    B --> B1["START_SESSION:seq|ts"]
    B --> B2["PAUSE_SESSION:seq|ts"]
    B --> B3["RESUME_SESSION:seq|ts"]
    B --> B4["STOP_SESSION:seq|ts"]

    C --> C1["PING:seq|T1<br/>(every 1s, all states)"]
    C --> C2["PONG:seq|0|T2|T3<br/>(response)"]

    D --> D2["MACROCYCLE:seq|baseTime|12|events<br/>(batched, 3 patterns)"]
    D --> D3["MACROCYCLE_ACK:seq<br/>(acknowledgment)"]

    E --> E1["GET_BATTERY"]
    E --> E2["BAT_RESPONSE:voltage"]

    A --> B
    A --> C
    A --> D
    A --> E

    B1 --> F["All motors start<br/>@scheduled time"]
    B2 --> G["All motors stop<br/>immediately"]
    B3 --> H["Resume therapy<br/>from paused point"]
    B4 --> I["Clean shutdown"]

    D1 --> J["Activate single finger<br/>at activate_time"]
    D2 --> K["Activate 12 events<br/>over ~3.3 seconds"]

    E1 --> L["Query SECONDARY<br/>battery voltage"]
```

**Message Routing:**

- **Phone → PRIMARY**: BLE UART service (Nordic UART Service)
- **PRIMARY → SECONDARY**: Custom sync protocol over BLE UART
- **Keepalive**: PING/PONG every 1s in all connected states

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
    I --> J

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
- **SEQUENTIAL**: Fingers activate in order (0→1→2→3 or reverse)
- **MIRRORED**: Both hands use identical finger sequences (noisy vCR mode)

**Timing:**

- Event duration: TIME_ON (100ms) + TIME_OFF (67ms) + jitter
- Pattern duration: 4 events × ~167ms = ~668ms
- Macrocycle: 3 patterns + 1336ms relaxation = ~3.3s

---

## 8. Battery & Error Handling

Battery monitoring and error recovery flows:

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
    [*] --> RUNNING

    RUNNING --> NORMAL: Battery >3.6V
    RUNNING --> LOW: Battery 3.3-3.6V\n(>50s remaining)
    RUNNING --> CRITICAL: Battery <3.3V\n(immediately)

    NORMAL --> LOW: Voltage drops

    LOW --> NORMAL: Voltage recovers
    LOW --> CRITICAL: Voltage drops further

    CRITICAL --> SHUTDOWN: Motors stop\nSession ends

    RUNNING --> CONNECTION_LOST: No PONG\nfor 6 seconds

    CONNECTION_LOST --> RECONNECT: Attempt\nreconnection

    RECONNECT --> RUNNING: Success
    RECONNECT --> RETRY2: Failed,\nwait 2s

    RETRY2 --> RUNNING: Success
    RETRY2 --> RETRY3: Failed,\nwait 2s

    RETRY3 --> RUNNING: Success
    RETRY3 --> IDLE: Failed,\nall retries\nexhausted

    SHUTDOWN --> [*]
    IDLE --> [*]
```

**Battery Thresholds:**

- **Normal**: >3.6V (full operation)
- **Low**: 3.3-3.6V (warning, >50s estimated remaining)
- **Critical**: <3.3V (immediate forced shutdown)

**Connection Loss Handling:**

- **Detection**: 6 seconds without PONG response (3 missed keepalives)
- **Reconnection**: 3 attempts with 2s intervals
- **Timeout**: Return to IDLE after all retries exhausted

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

    C["Apply EMA smoothing<br/>smooth_lat = α×measured + (1-α)×prev"]

    D["Calculate variance<br/>σ² = deviation from smoothed"]

    E["Compute lead time<br/>lead_time = smooth_lat + 3σ"]

    F["Clamp to bounds<br/>70ms ≤ lead_time ≤ 150ms"]

    G["Use for MACROCYCLE events<br/>baseTime = now + lead_time"]

    A --> B --> C --> D --> E --> F --> G

    Note over A: Minimum: 70ms (SYNC_MIN_LEAD_TIME_US)<br/>Maximum: 150ms (SYNC_MAX_LEAD_TIME_US)

    style E fill:#17a2b8,color:#fafafa
    style F fill:#35B6F2,color:#0a0a0a
```

**Lead Time Formula:**

```
lead_time = RTT/2 + processing_overhead + margin
lead_time = clamp(lead_time, 70ms, 150ms)

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
    P->>P: Validate (battery, SECONDARY connected)

    P->>P: Load profile settings
    P->>S: PARAM_UPDATE:ON:0.100:OFF:0.067:...
    S->>S: Update local params

    P->>S: START_SESSION:seq|ts
    P->>P: State: READY → RUNNING
    S->>S: State: READY → RUNNING

    P->>Ph: SESSION_STATUS:RUNNING

    loop Every 1 second (keepalive + clock sync)
        P->>S: PING:seq|T1
        S->>P: PONG:seq|0|T2|T3
        P->>P: Compute clock offset, EMA update
    end

    loop Every ~200ms (pattern execution)
        alt Batching Mode
            P->>S: MACROCYCLE:seq|baseTime|12|...
            S->>P: MACROCYCLE_ACK:seq
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
    P->>S: PAUSE_SESSION:seq|ts
    P->>P: State: RUNNING → PAUSED
    S->>S: State: RUNNING → PAUSED
    P->>Ph: SESSION_STATUS:PAUSED

    Ph->>P: SESSION_RESUME
    P->>S: RESUME_SESSION:seq|ts
    P->>P: State: PAUSED → RUNNING
    S->>S: State: PAUSED → RUNNING
    P->>Ph: SESSION_STATUS:RUNNING

    Note over P,S: ... therapy continues ...

    Ph->>P: SESSION_STOP
    P->>S: STOP_SESSION:seq|ts
    P->>P: State: RUNNING/PAUSED → STOPPING
    S->>S: State: RUNNING/PAUSED → STOPPING
    P->>P: Cancel pending events
    S->>S: Cancel pending events
    P->>P: Motors off
    S->>S: Motors off
    P->>P: State: STOPPING → IDLE
    S->>S: State: STOPPING → IDLE
    P->>Ph: SESSION_STATUS:IDLE
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
| PING/PONG frequency   | 1s      | Keepalive and clock sync refresh rate     |
| Connection timeout    | 6s      | 3 missed PING/PONGs triggers reconnection |
| Reconnection interval | 2s      | Delay between reconnection attempts       |
| Reconnection attempts | 3       | Maximum retries before returning to IDLE  |

### Synchronization Accuracy

| Metric                 | Value                     | Notes                                   |
| ---------------------- | ------------------------- | --------------------------------------- |
| Target accuracy        | <2ms                      | Bilateral synchronization (both gloves) |
| Synchronization method | IEEE 1588 PTP 4-timestamp | Clock offset calculation                |
| Confidence threshold   | RTT < 80ms                | For accepting offset samples            |
| Minimum samples        | 5                         | Before declaring sync ready             |
| Smoothing algorithm    | EMA (α = 0.2)             | Exponential moving average              |
| Update frequency       | 1 Hz                      | Every PING/PONG cycle                   |

### Message Efficiency

| Mode                | Message Size | Messages/Macrocycle | Total Bytes | Efficiency        |
| ------------------- | ------------ | ------------------- | ----------- | ----------------- |
| Individual messages (removed) | 60 bytes | 12            | 720 bytes   | Removed (legacy)  |
| MACROCYCLE batching | 200 bytes    | 1                   | 200 bytes   | **72% reduction** |

### Battery Thresholds

| Level    | Voltage Range | Remaining Time | Action                        |
| -------- | ------------- | -------------- | ----------------------------- |
| Normal   | >3.6V         | Full           | Normal operation              |
| Low      | 3.3-3.6V      | >50s           | Warning, continue             |
| Critical | <3.3V         | <50s           | **Immediate forced shutdown** |

---

## Related Documentation

This document complements the following technical documentation:

- **[ARDUINO_FIRMWARE_ARCHITECTURE.md](./ARDUINO_FIRMWARE_ARCHITECTURE.md)** - Complete firmware architecture overview
- **[SYNCHRONIZATION_PROTOCOL.md](./SYNCHRONIZATION_PROTOCOL.md)** - Detailed PRIMARY ↔ SECONDARY sync protocol specification
- **[BLE_PROTOCOL.md](./BLE_PROTOCOL.md)** - Phone ↔ PRIMARY BLE command protocol
- **[COMMAND_REFERENCE.md](./COMMAND_REFERENCE.md)** - All 18 BLE commands with syntax and examples

---

## Summary

The BlueBuzzah firmware implements a sophisticated bilateral synchronization system:

1. **Dual-glove architecture** with PRIMARY/SECONDARY roles for coordinated therapy
2. **IEEE 1588 PTP-style clock synchronization** achieving <2ms bilateral sync accuracy
3. **Adaptive lead time scheduling** that decouples sync accuracy from BLE latency variability
4. **Message batching (MACROCYCLE)** reducing BLE bandwidth by 72%
5. **Graceful error handling** with automatic reconnection and battery failsafes
6. **FreeRTOS task isolation** for deterministic motor timing (<1ms activation accuracy)

This architecture enables precise bilateral haptic feedback while operating within the constraints of BLE latency (7.5-20ms typical) and embedded hardware limitations (256KB RAM, 1MB flash).
