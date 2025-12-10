# BlueBuzzah Synchronization Protocol

**Version:** 2.4.0
**Last Updated:** 2025-12-09

---

## Overview

BlueBuzzah uses a bilateral synchronization protocol to coordinate haptic feedback between two gloves (PRIMARY and SECONDARY) over Bluetooth Low Energy. The protocol achieves <2ms synchronization accuracy using IEEE 1588 PTP-inspired clock synchronization with absolute time scheduling.

| Metric | Target | Achieved |
|--------|--------|----------|
| Bilateral sync accuracy | <50ms | <2ms |
| Clock offset precision | <5ms | <1ms |
| BLE latency compensation | Yes | PTP 4-timestamp |

### Core Principles

- **PRIMARY commands, SECONDARY follows** — All therapy decisions originate from PRIMARY
- **Absolute time scheduling** — Commands include future activation timestamps
- **Continuous synchronization** — Clock offset maintained throughout session
- **Fail-safe design** — SECONDARY halts if PRIMARY connection lost

---

## Session Lifecycle

```mermaid
sequenceDiagram
    participant P as PRIMARY
    participant S as SECONDARY

    rect rgb(230, 245, 255)
        Note over P,S: Phase 1: Connection
        P->>P: Advertise "BlueBuzzah"
        S->>S: Scan for "BlueBuzzah"
        S->>P: BLE Connect
        S->>P: READY
    end

    rect rgb(255, 245, 230)
        Note over P,S: Phase 2: Idle Clock Synchronization
        loop Every 1s (keepalive)
            P->>S: PING (T1)
            S->>P: PONG (T2, T3)
            Note over P: Compute clock offset
        end
        Note over P: 5+ valid samples = sync ready (~5s)
    end

    rect rgb(230, 255, 230)
        Note over P,S: Phase 3: Therapy Session
        P->>S: START_SESSION
        loop Every buzz event
            P->>S: BUZZ (activation time)
            Note over P,S: Both activate at scheduled time
        end
        loop Every 1s (unified keepalive + sync)
            P->>S: PING (T1)
            S->>P: PONG (T2, T3)
            Note over P: Continuous clock offset maintenance
        end
    end

    rect rgb(255, 230, 230)
        Note over P,S: Phase 4: Session End
        P->>S: STOP_SESSION
        Note over P,S: Motors off, connection maintained
    end
```

### Timing Requirements

| Phase | Maximum Duration |
|-------|------------------|
| Connection establishment | 15 seconds |
| Initial clock sync | 200ms |
| Session start | Immediate after sync |
| Keepalive timeout | 6 seconds (3 missed) |

---

## Clock Synchronization

### PTP 4-Timestamp Exchange

The protocol uses IEEE 1588-inspired clock synchronization to measure the offset between PRIMARY and SECONDARY clocks, independent of network asymmetry.

```mermaid
sequenceDiagram
    participant P as PRIMARY
    participant S as SECONDARY

    Note over P: Record T1
    P->>S: PING:seq|T1
    Note over S: Record T2

    Note over S: Record T3
    S->>P: PONG:seq|0|T2|T3
    Note over P: Record T4

    Note over P: offset = ((T2-T1) + (T3-T4)) / 2
```

### Offset Calculation

```text
offset = ((T2 - T1) + (T3 - T4)) / 2
```

| Timestamp | Device | Event |
|-----------|--------|-------|
| T1 | PRIMARY | PING sent |
| T2 | SECONDARY | PING received |
| T3 | SECONDARY | PONG sent |
| T4 | PRIMARY | PONG received |

A positive offset means SECONDARY's clock is ahead of PRIMARY.

### Filtering and Maintenance

- **Initial sync:** Idle keepalive (1s) accumulates samples, median offset selected
- **Quality filter:** Exchanges with RTT > 30ms are discarded
- **Minimum valid:** At least 5 good samples required (~5s after connect)
- **Drift compensation:** Ongoing sync every 1s corrects for crystal drift
- **Smoothing:** Exponential moving average prevents sudden jumps

---

## Synchronized Execution

### BUZZ Command Flow

```mermaid
flowchart LR
    subgraph PRIMARY
        A[Generate event] --> B[Calculate activate_time]
        B --> C[Send BUZZ]
        C --> D[Schedule local timer]
    end

    subgraph BLE
        C -.->|3-15ms latency| E
    end

    subgraph SECONDARY
        E[Receive BUZZ] --> F[Convert to local time]
        F --> G[Schedule local timer]
    end

    D --> H((Timer fires))
    G --> H
    H --> I[Motors activate simultaneously]
```

### Lead Time Calculation

The activation time is set in the future to ensure SECONDARY receives and processes the command before the scheduled time:

```text
activate_time = current_time + lead_time
lead_time = average_RTT + safety_margin
```

| Parameter | Value | Purpose |
|-----------|-------|---------|
| Minimum lead time | 15ms | Accounts for BLE latency |
| Maximum lead time | 50ms | Must be less than motor ON time |
| Safety margin | 3× latency variance | Handles jitter |

### Time Conversion

SECONDARY converts PRIMARY timestamps to local time:

```text
local_time = primary_time + clock_offset
```

---

## Therapy Event Cycle

```mermaid
flowchart TD
    A[Therapy Pattern Generator] --> B{Next event?}
    B -->|Buzz| C[Select finger + amplitude]
    B -->|Relax| D[Wait period]

    C --> E[Create BUZZ command]
    E --> F[Set activate_time = now + lead_time]
    F --> G[Send to SECONDARY]
    G --> H[Schedule PRIMARY activation]

    H --> I{Time reached?}
    I -->|No| I
    I -->|Yes| J[Activate motor]
    J --> K[Motor ON for duration]
    K --> L[Deactivate motor]
    L --> B

    D --> B
```

### Macrocycle Structure

One therapy macrocycle consists of 3 patterns (12 buzz events total) followed by a relax period:

| Component | Duration |
|-----------|----------|
| Motor ON time | 100ms (configurable) |
| Motor OFF time | 67ms (configurable) |
| Fingers per pattern | 4 |
| Patterns per macrocycle | 3 |
| Events per macrocycle | 12 (3 patterns × 4 fingers) |
| Pattern duration | ~668ms |
| Inter-macrocycle relax | ~1336ms (2× pattern duration) |
| **Total macrocycle** | **~3.3 seconds** |

### Macrocycle Batching Mode

When enabled, PRIMARY sends all 12 events in a single MACROCYCLE message instead of 12 individual BUZZ messages. Benefits:

- **~4× reduction in BLE traffic** (~200 bytes vs ~720 bytes)
- **Zero BLE during motor activity** — all commands sent before first buzz
- **Single clock offset application** — less computation, fewer rounding errors
- **Cleaner architecture** — macrocycle as atomic unit

SECONDARY's `ActivationQueue` schedules all 12 events with their local activation times, then processes them as time elapses.

---

## Error Handling

### Connection Loss Recovery

```mermaid
stateDiagram-v2
    [*] --> Normal

    Normal --> ConnectionLost: Keepalive timeout

    ConnectionLost --> EmergencyStop: Immediate
    EmergencyStop --> Reconnect1: Motors safe

    Reconnect1 --> Normal: Success
    Reconnect1 --> Reconnect2: Failed (wait 2s)

    Reconnect2 --> Normal: Success
    Reconnect2 --> Reconnect3: Failed (wait 2s)

    Reconnect3 --> Normal: Success
    Reconnect3 --> Idle: Failed

    Idle --> [*]: Manual restart required
```

### Timeout Behavior

| Event | Timeout | Action |
|-------|---------|--------|
| No SECONDARY found | 15s | Abort startup |
| No READY received | 8s | Abort startup |
| No BUZZ received | 10s | Emergency stop |
| No PING/PONG | 6s | Emergency stop + reconnect |

---

## Message Reference

### Message Format

All messages use the format:

```text
COMMAND:field1|field2|field3|...
```

- Field delimiter: `|` (pipe)
- Message terminator: `0x04` (EOT)
- Timestamps: Microseconds since boot

### Handshake Messages

| Message | Direction | Fields | Example |
|---------|-----------|--------|---------|
| `READY` | S → P | (none) | `READY` |
| `START_SESSION` | P → S | seq, timestamp | `START_SESSION:1\|50000` |
| `STOP_SESSION` | P → S | seq, timestamp | `STOP_SESSION:99\|120000000` |

### Synchronization Messages

| Message | Direction | Fields | Example |
|---------|-----------|--------|---------|
| `PING` | P → S | seq, T1 | `PING:42\|1000000` |
| `PONG` | S → P | seq, 0, T2, T3 | `PONG:42\|0\|1000500\|1000600` |

**Unified Keepalive + Clock Sync:**

The protocol uses a single unified mechanism for both keepalive and clock synchronization:

| State | Mechanism | Interval | Purpose |
|-------|-----------|----------|---------|
| All states | PING/PONG | 1 second | Clock sync + keepalive (unified) |

- **PING** provides: Clock sync timestamp (T1) + proof PRIMARY is alive
- **PONG** provides: Clock sync response (T2, T3) + proof SECONDARY is alive
- **Continuous sync:** Clock offset is maintained every second, even during therapy
- **No separate keepalive:** PING/PONG serves both purposes efficiently

This unified approach provides continuous clock synchronization (max 1s between samples) throughout all session states, eliminating clock drift during therapy.

### Therapy Messages

| Message | Direction | Fields | Example |
|---------|-----------|--------|---------|
| `BUZZ` | P → S | seq, ts, finger, amp, dur, freq, activate_time | `BUZZ:42\|5000000\|0\|100\|100\|235\|5050000` |
| `MACROCYCLE` | P → S | seq, baseTime, count, events... | See below |
| `MACROCYCLE_ACK` | S → P | seq | `MC_ACK:42` |
| `DEACTIVATE` | P → S | seq, timestamp | `DEACTIVATE:43\|5100000` |

**BUZZ fields:**

| Field | Description |
|-------|-------------|
| finger | Motor index (0-3: index, middle, ring, pinky) |
| amp | Intensity percentage (0-100) |
| dur | ON duration in milliseconds |
| freq | Motor frequency in Hz |
| activate_time | Scheduled activation (PRIMARY clock, µs) |

**MACROCYCLE format:**

```text
MC:seq|baseTime|count|d,f,a,dur,fo|d,f,a,dur,fo|...
```

| Field | Description |
|-------|-------------|
| seq | Sequence number for ACK matching |
| baseTime | Absolute activation time of event 0 (PRIMARY clock, µs) |
| count | Number of events (typically 12: 3 patterns × 4 fingers) |
| d | Delta time from baseTime (ms) |
| f | Finger index (0-3) |
| a | Amplitude percentage (0-100) |
| dur | ON duration (ms) |
| fo | Frequency offset: `(freq - 200) / 5` for 200-455 Hz range |

**Example MACROCYCLE:**

```text
MC:1|5050000|12|0,0,100,100,10|167,1,100,100,10|334,2,100,100,10|...
```

SECONDARY applies clock offset once to baseTime, then schedules all 12 events via an activation queue. This reduces BLE traffic from 12 messages to 1 per macrocycle (~200 bytes vs ~720 bytes).

### Parameter Messages

| Message | Direction | Fields | Example |
|---------|-----------|--------|---------|
| `PARAM_UPDATE` | P → S | key:value pairs | `PARAM_UPDATE:TIME_ON:150:JITTER:10` |
| `SEED` | P → S | random seed | `SEED:123456` |
| `SEED_ACK` | S → P | (none) | `SEED_ACK` |

### Status Messages

| Message | Direction | Fields | Example |
|---------|-----------|--------|---------|
| `GET_BATTERY` | P → S | (none) | `GET_BATTERY` |
| `BAT_RESPONSE` | S → P | voltage | `BAT_RESPONSE:3.68` |

---

## Protocol Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| RTT quality threshold | 80ms | Discard samples with higher RTT |
| Minimum valid samples | 5 | Required for valid sync (~5s from connect) |
| PING/PONG interval | 1s | Unified keepalive + clock sync (all states) |
| Keepalive timeout (SECONDARY) | 6s | 6 missed PINGs = connection lost |
| Keepalive timeout (PRIMARY) | 4s | During therapy (emergency shutdown) |
| BUZZ timeout | 10s | SECONDARY safety halt |
| Lead time range | 15-100ms | Adaptive scheduling window |
| BLE connection interval | 7.5ms | Low-latency communication |

---

## See Also

- **[BLE_PROTOCOL.md](BLE_PROTOCOL.md)** — Phone app command protocol
- **[THERAPY_ENGINE.md](THERAPY_ENGINE.md)** — Pattern generation
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — System design overview
