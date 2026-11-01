# BlueBuzzah Synchronization Protocol

**Version:** 2.6.0
**Last Updated:** 2026-06-10

---

## Overview

BlueBuzzah uses a bilateral synchronization protocol to coordinate haptic feedback between two gloves (PRIMARY and SECONDARY) over Bluetooth Low Energy. The protocol uses IEEE 1588 PTP-inspired clock synchronization with absolute time scheduling and microsecond-precision timestamps.

| Metric | Target | Achieved |
|--------|--------|----------|
| Bilateral sync accuracy | <50ms | pending re-validation (see SYNC_VALIDATION.md) |
| Clock offset precision | <5ms | pending re-validation (see SYNC_VALIDATION.md) |
| BLE latency compensation | Yes | PTP 4-timestamp |

> **Note on previous figures:** The "<1ms" and "<500µs" values cited in earlier revisions were measured against the FreeRTOS tick clock, which has ~976µs quantization because DWT was never enabled. Those numbers reflect tick-quantized timestamps, not true sub-millisecond resolution. The system is being re-baselined on the 1MHz hardware timebase introduced in this revision; see SYNC_VALIDATION.md for acceptance gates.

### Core Principles

- **PRIMARY commands, SECONDARY follows** — All therapy decisions originate from PRIMARY
- **Absolute time scheduling** — Commands include future activation timestamps
- **Continuous synchronization** — Clock offset maintained throughout session
- **Fail-safe design** — SECONDARY halts if PRIMARY connection lost

---

## Session Lifecycle

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

    rect rgba(5, 33, 45, 0.5)
        Note over P,S: Phase 1: Connection
        P->>P: Advertise "BlueBuzzah"
        S->>S: Scan for "BlueBuzzah"
        S->>P: BLE Connect
        S->>P: IDENTIFY:SECONDARY
    end

    rect rgba(5, 33, 45, 0.5)
        Note over P,S: Phase 2: Idle Clock Synchronization
        loop Every 70ms until sync valid, then 1s (keepalive)
            P->>S: PING (T1)
            S->>P: PONG (T2, T3)
            Note over P: Compute clock offset
        end
        Note over P: 5+ valid samples = sync ready (~350ms at 70ms PINGs)
    end

    rect rgba(5, 33, 45, 0.5)
        Note over P,S: Phase 3: Therapy Session
        P->>S: START_SESSION
        loop Every ~3.3s (~4.2s on v3)
            P->>S: MACROCYCLE (batch of 12 events, 15 on v3)
            S->>P: MACROCYCLE_ACK
            Note over P,S: Both gloves execute the batch at the scheduled times
        end
        loop Every 250ms during therapy (4Hz)
            P->>S: PING (T1)
            S->>P: PONG (T2, T3)
            Note over P: Continuous clock offset maintenance
        end
    end

    rect rgba(5, 33, 45, 0.5)
        Note over P,S: Phase 4: Session End
        P->>S: STOP_SESSION
        Note over P,S: Motors off, connection maintained
    end
```

### Timing Requirements

| Phase | Maximum Duration |
|-------|------------------|
| Initial clock sync | ~350ms (5 samples @ 70ms acquire interval) |
| Session start | After sync is valid; refused with `Sync not ready` until then, then starts anyway (degraded timing) 10s after the SECONDARY connected (`SYNC_START_FALLBACK_MS`) |
| Keepalive timeout | 6 seconds (6 missed) |

---

## Clock Synchronization

### PTP 4-Timestamp Exchange

The protocol uses IEEE 1588-inspired clock synchronization to measure the offset between PRIMARY and SECONDARY clocks, independent of network asymmetry.

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

    Note over P: Record T1
    P->>S: PING:seq|T1
    Note over S: Record T2

    Note over S: Record T3
    S->>P: PONG:seq|tx_micros|T2|T3
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

### RTT Measurement

Round-Trip Time (RTT) is calculated using the IEEE 1588 PTP formula:

```text
RTT = (T4 - T1) - (T3 - T2)
```

This formula isolates network latency from SECONDARY processing time:
- `(T4 - T1)` = total round trip including processing
- `(T3 - T2)` = SECONDARY processing time
- Result = pure network latency (BLE transmission delays only)

Excluding processing time provides:
- More accurate network latency estimates (5-20ms typical)
- Better quality filtering (rejects poor BLE conditions, not slow processing)
- Improved lead time precision (adapts to actual network conditions)

### Transmit Timestamping

T1 (PING sent) and T3 (PONG sent) are stamped at **BLE-stack handoff** (SoftDevice on v2, NimBLE on v3) rather than at message creation. Both PING and PONG are enqueued as deferred-stamp TX entries; `processTxQueue` serializes them with the handoff time at the moment the BLE stack accepts the packet for transmission and reports it through the `onTxStamped` callback.

**Why this matters:** Between message construction and the BLE radio actually picking up a packet, the main loop may run other work (sensor reads, state-machine ticks, motor-queue processing). Stamping at creation captures that variable queuing delay as apparent propagation time, introducing an asymmetric bias into PTP offset samples. Stamping at handoff removes that latency from the measurement.

`onTxStamped` records `pingT1` and `pingSeq` for PING packets so the offset calculation can correlate the correct T1 with the matching PONG response.

**PONG sequence matching:** The PONG handler validates that the received sequence number matches the in-flight `pingSeq`. A sequence-mismatched (stale) PONG is excluded from the offset calculation without clearing the in-flight T1, so a stale reply can't poison an offset sample. It still refreshes the PRIMARY's keepalive timer: any PONG proves the SECONDARY is alive.

### Filtering and Maintenance

- **Initial sync:** Acquire-rate PINGs (70ms) accumulate samples, median offset selected
- **Minimum valid:** At least 5 good samples required (~350ms after connect: PINGs run every 70ms, `SYNC_ACQUIRE_INTERVAL_MS`, until sync is valid)
- **Drift compensation:** Ongoing sync at 4Hz during therapy / 1Hz idle corrects for crystal drift
- **Smoothing:** Exponential moving average prevents sudden jumps
- **Drift rate capping:** Dual caps for safety (see below)

#### Maintenance Sample Quality Gates

Three gates are applied in order before a maintenance sample updates the EMA:

1. **RTT hard ceiling (60ms):** Any exchange with RTT > 60ms is discarded. This rejects samples polluted by BLE retransmissions where the network path is too lossy to yield a reliable offset.

2. **Lucky-packet gate (decaying minimum RTT):** A tracked per-session minimum RTT (`minRTT`) is maintained and decays upward by `SYNC_MIN_RTT_DECAY_US` (200µs) per sample to allow gradual adaptation. Only exchanges with `RTT <= minRTT + SYNC_LUCKY_RTT_MARGIN_US` (10ms) are accepted. This gate selects low-jitter "lucky" packets whose round-trip approximates the true one-way propagation time, providing better offset estimates.

3. **Innovation gate with hard re-anchor on persistence:** If a candidate offset deviates from the current EMA by more than `SYNC_INNOVATION_GATE_US` (5ms) it is initially rejected. After `SYNC_INNOVATION_REJECT_LIMIT` (5) such rejections with no accepted sample in between, the next deviating sample is treated as a true step change (e.g., from a long disconnect or oscillator retrim) and **hard re-anchors** the offset to the new value rather than blending it in via EMA. This prevents the EMA from chasing transient noise while still tracking genuine clock jumps.

#### Dedicated Drift-Measurement Anchor

Drift rate is measured over wall-time windows of at least `SYNC_MIN_DRIFT_INTERVAL_MS` (500ms) using a dedicated anchor pair (`_driftAnchorOffset` / `_driftAnchorTime`). This is separate from the EMA update path, so the 4Hz sync cadence during therapy does not stall drift estimation by providing too-short intervals.

### Outlier Rejection

The clock sync algorithm uses MAD (Median Absolute Deviation) to filter outliers before computing the final offset:

1. Compute preliminary median from all offset samples
2. Filter samples deviating from the preliminary median by more than max(3 × MAD, 5ms)
3. Compute final median from filtered samples only
4. If fewer than 5 samples survive filtering, use the preliminary median instead (sync still becomes valid)

This improves robustness against BLE retransmissions and RF interference that cause anomalous RTT measurements.

### Drift Rate Caps (Dual)

The protocol uses **two separate drift rate caps** for safety:

| Cap | Value | Application | Purpose |
|-----|-------|-------------|---------|
| Measurement cap | ±150 ppm (0.15 µs/ms) | `updateOffsetEMA()` (before drift EMA smoothing) | Clamp implausible measurements |
| Applied cap | ±100 ppm (0.10 µs/ms) | `getCorrectedOffset()`, `getProjectedOffset()` | Conservative correction limit |

**Why two caps?**

1. **Measurement cap (150 ppm):** Clamps measurements that exceed typical crystal drift (±20-50 ppm) before they enter the drift EMA. A measurement of 150 ppm indicates BLE anomalies, not actual clock drift.

2. **Applied cap (100 ppm):** More conservative limit for actual corrections. Even if measured drift is valid, applying too large a correction can cause over-compensation and oscillation.

```cpp
// From config.h:
#define SYNC_MAX_DRIFT_RATE_US_PER_MS 0.15f       // Measurement cap (150 ppm)
#define SYNC_MAX_APPLIED_DRIFT_RATE_US_PER_MS 0.1f  // Applied cap (100 ppm)
```

Typical nRF52840 crystal drift is ±20-50 ppm. The 100 ppm applied cap provides ~2× headroom while preventing runaway drift compensation.

### Warm-Start Sync (Quick Reconnection)

Brief BLE disconnections (interference, range limits) can recover quickly using cached sync state:

| Scenario | Samples Required | Recovery Time |
|----------|------------------|---------------|
| Cold start (first connect) | 5 | ~350ms |
| Warm start (reconnect <15s after the last cached sample) | 3 confirmatory | ~210ms (macrocycles carry the projected offset once the first confirmatory sample agrees) |
| Long disconnect (cache older than 15s) | 5 | ~350ms |

**Warm-start process:**
1. On valid sync, cache current offset + drift rate with a timestamp (updated by every accepted sample)
2. On disconnect, the cache is preserved
3. On reconnect within 15 seconds of the cache timestamp:
   - Project cached offset forward using drift rate
   - Enter warm-start mode
   - Require 3 confirmatory samples within 5ms of projection
4. If a single sample diverges >5ms, abort to cold start (safety fallback)

This shortens re-sync after brief BLE interference events from ~350ms (cold start) to ~210ms.

---

## Synchronized Execution

### MACROCYCLE Command Flow

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
    subgraph PRIMARY
        A[Generate 3 patterns<br/>12 events on v2, 15 on v3] --> B[Calculate base time]
        B --> C[Send MACROCYCLE]
        C --> D[Enqueue all events]
        D --> E[FreeRTOS motor task<br/>executes chain]
    end

    subgraph BLE
        C -.->|3-15ms latency| F
    end

    subgraph SECONDARY
        F[Receive MACROCYCLE] --> G[Convert to local time]
        G --> H[Enqueue all events]
        H --> I[FreeRTOS motor task<br/>executes chain]
    end

    E --> J((Events fire))
    I --> J
    J --> K[Motors activate at the scheduled time]
```

### Lead Time Calculation

The activation time is set in the future to ensure SECONDARY receives and processes the command before the scheduled time:

```text
activate_time = current_time + lead_time
lead_time = average_RTT + safety_margin + processing_overhead + generation_overhead
```

`average_RTT` is twice the smoothed one-way latency from PING/PONG (`calculateAdaptiveLeadTime()`). Until 3 RTT samples exist the lead time is a fixed, unclamped 50ms (`SYNC_LEAD_TIME_US` 35ms + 10ms + 5ms).

| Parameter | Value | Purpose |
|-----------|-------|---------|
| Minimum lead time | 70ms | Covers RTT (~40ms) + variance (~5ms) + processing (10ms) + generation (5ms) + margin (10ms) |
| Maximum lead time | 150ms | Conservative upper bound for worst-case BLE conditions |
| Safety margin | 6× smoothed one-way latency deviation | 3σ at RTT scale; handles jitter |
| Processing overhead | 10ms | BLE callback + deserialization + queue forwarding (SECONDARY) |
| Generation overhead | 5ms | Macrocycle generation + serialization (PRIMARY) |

### Time Conversion

SECONDARY converts PRIMARY timestamps to local time:

```text
local_time = primary_time + clock_offset
```

---

## Therapy Event Cycle

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
    A[Therapy Pattern Generator] --> B{Macrocycle ready?}
    B -->|Yes| C[Generate 3 patterns<br/>12 events on v2, 15 on v3]
    B -->|No| D[Wait for completion]
    D --> B

    C --> E[Calculate base time<br/>now + lead_time]
    E --> F[Create MACROCYCLE message]
    F --> G[Send to SECONDARY]
    G --> I[Enqueue all events]

    I --> J{Next event time?}
    J -->|Wait| J
    J -->|Ready| K[FreeRTOS motor task<br/>activates motor]
    K --> L[Motor ON for duration]
    L --> M[Auto-deactivate]
    M --> N{More events?}
    N -->|Yes| J
    N -->|No| O[Relax period]
    O --> B
```

### Macrocycle Structure

One therapy macrocycle consists of 3 patterns (12 buzz events on v2, 15 on v3) followed by a relax period. Values below are for the default timing; v3 values in parentheses:

| Component | Duration |
|-----------|----------|
| Motor ON time | 100ms (configurable) |
| Motor OFF time | 67ms (configurable) |
| Fingers per pattern | 4 (5); profile `FINGERS`, limited to detected motors |
| Patterns per macrocycle | 3 |
| Events per macrocycle | 12 (15): 3 patterns × fingers |
| Pattern duration | ~668ms (~835ms) |
| Inter-macrocycle relax | ~1336ms (~1670ms): 2× pattern duration |
| **Total macrocycle** | **~3.3 seconds (~4.2 seconds)**, plus the lead time |

### Macrocycle Batching Architecture

PRIMARY sends all of a macrocycle's events in a single MACROCYCLE message. This batching approach provides:

- **~4× reduction in BLE traffic** (~200 bytes vs ~720 bytes)
- **No per-buzz BLE traffic** — every motor event is sent before the first buzz (PING/PONG continues at 4Hz)
- **Single clock offset application** — less computation, fewer rounding errors
- **Cleaner architecture** — macrocycle as atomic unit

SECONDARY's `ActivationQueue` schedules all events with their local activation times, then processes them as time elapses. PRIMARY does not wait for `MC_ACK`: it enqueues its own events right after sending, and any `MC_ACK` only refreshes its SECONDARY keepalive timer. If the macrocycle can't be queued for the SECONDARY, PRIMARY skips it locally too.

---

## Error Handling

### Connection Loss Recovery

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
    [*] --> Normal

    Normal --> ConnectionLost: Keepalive timeout or BLE disconnect

    ConnectionLost --> EmergencyStop: Immediate
    EmergencyStop --> Reconnecting: Motors safe (purple LED)

    Reconnecting --> Normal: Peer traffic resumes
    Reconnecting --> Idle: 30s without peer

    Idle --> Normal: Peer reconnects (scanning/advertising continue)
```

### Timeout Behavior

| Event | Timeout | Action |
|-------|---------|--------|
| No PING/PONG (keepalive) | 6s (`KEEPALIVE_TIMEOUT_MS`) | Emergency stop; SECONDARY → connection-lost |
| Boot window elapses with SECONDARY connected but no phone (PRIMARY) | 30s (`STARTUP_WINDOW_MS`) | Auto-start therapy |

> **Note:** There is no dedicated "no SECONDARY found", "no READY", or "no MACROCYCLE" timeout in the current firmware — a stalled or lost link is caught by the 6s keepalive, which triggers the emergency stop. The 30s `STARTUP_WINDOW_MS` is the PRIMARY auto-start window (it begins therapy when no phone has connected), not a startup-abort timeout. Detailed connection/boot timing lives in [BOOT_SEQUENCE.md](BOOT_SEQUENCE.md).

---

## Message Reference

### Message Format

All `SyncCommand` messages use the format (the plain-string internal messages noted in the Handshake and Status tables are the exceptions):

```text
COMMAND:field1|field2|field3|...
```

- Field delimiter: `|` (pipe)
- Message terminator: `0x04` (EOT)
- Timestamps: Microseconds since boot

### Handshake Messages

| Message | Direction | Fields | Example |
|---------|-----------|--------|---------|
| `IDENTIFY:SECONDARY` | S → P | (plain string, not pipe-delimited) | `IDENTIFY:SECONDARY` |
| `GLOVE_ERROR` | S → P | (plain string; sent right after `IDENTIFY:SECONDARY` only when the SECONDARY has a boot hardware fault and is in `ERROR`) | `GLOVE_ERROR` |
| `START_SESSION` | P → S | seq, timestamp | `START_SESSION:1\|50000` |
| `PAUSE_SESSION` | P → S | seq, timestamp | `PAUSE_SESSION:0\|61000000` |
| `RESUME_SESSION` | P → S | seq, timestamp | `RESUME_SESSION:0\|65000000` |
| `STOP_SESSION` | P → S | seq, timestamp | `STOP_SESSION:99\|120000000` |

### Synchronization Messages

| Message | Direction | Fields | Example |
|---------|-----------|--------|---------|
| `PING` | P → S | seq, T1 | `PING:42\|1000000` |
| `PONG` (legacy) | S → P | seq, tx_micros, T2, T3 (T2High, T2Low, T3High, T3Low once either exceeds 32 bits) | `PONG:42\|1000550\|1000500\|1000600` |
| `PONG` (anchor, 6 data fields) | S → P | seq, tx_micros, T2High, T2Low, T3High, T3Low, AnchorHigh, AnchorLow — *see anchor section* | `PONG:42\|1000550\|0\|1000500\|0\|1000600\|0\|999800` |

PONG has two wire forms; see [Connection-Anchor Timestamping](#connection-anchor-timestamping-experimental) for the 6-data-field format and detection rules. When anchor timestamping is disabled (default), only the legacy 4-field form is used.

> **The PONG `timestamp` field is the message construction time, not `0`.** The `SyncCommand` constructor stamps `getMicros()` and `createPongWithTimestamps` / `createPongWithAnchor` never override it, so the field carries the PONG's build time (between T2 and T3). The PTP math reads T2/T3 from the positional data fields and ignores this field. Earlier doc revisions that showed `PONG:…|0|…` were inaccurate.

**Unified Keepalive + Clock Sync:**

The protocol uses a single unified mechanism for both keepalive and clock synchronization:

| State | Mechanism | Interval | Purpose |
|-------|-----------|----------|---------|
| Acquiring (sync not yet valid, or warm start) | PING/PONG | 70ms | Fast initial clock sync + keepalive |
| Idle / no session | PING/PONG | 1 second | Clock sync + keepalive (unified) |
| Therapy active | PING/PONG | 250ms (4Hz) | Higher-cadence sync during therapy |

- **PING** provides: Clock sync timestamp (T1) + proof PRIMARY is alive
- **PONG** provides: Clock sync response (T2, T3) + proof SECONDARY is alive
- **Continuous sync:** Clock offset is maintained throughout all states
- **No separate keepalive:** PING/PONG serves both purposes efficiently

This unified approach provides continuous clock synchronization throughout all session states. The increased 4Hz cadence during therapy reduces the maximum interval between offset updates from 1s to 250ms, improving drift-compensation responsiveness.

### Therapy Messages

| Message | Direction | Fields | Example |
|---------|-----------|--------|---------|
| `MACROCYCLE` | P → S | seq, baseTime, count, events... | See below |
| `MACROCYCLE_ACK` | S → P | seq, tx_micros | `MC_ACK:42\|5050120` |
| `DEACTIVATE` (reserved) | — | seq, tx_micros | `DEACTIVATE:43\|5050120` |

> **`serialize()` always emits the `seq|timestamp` pair**, and the timestamp is the message construction time (`getMicros()`), **never `0`**. The 2-arg `SyncCommand` constructor calls `setTimestampNow()`, so `createMacrocycleAck` produces `MC_ACK:42|<micros>` (not `MC_ACK:42` and not `MC_ACK:42|0`). PRIMARY ignores the field for `MC_ACK` and does no ACK matching or retransmission: an `MC_ACK` only refreshes its SECONDARY keepalive timer (`seq` is logged in debug mode).
>
> **`DEACTIVATE` is reserved/unused.** The `SyncCommandType::DEACTIVATE` value and `createDeactivate()` factory exist, but no `DEACTIVATE` message is sent over BLE: SECONDARY turns motors off locally via its paired activate/deactivate `ActivationQueue`. The factory does not override the construction timestamp, so if it were ever serialized it would be `DEACTIVATE:43|<micros>`. The same applies to the `BUZZ` type, which was superseded by `MACROCYCLE` batching.

**MACROCYCLE format (V5):**

```text
MC:seq|baseHigh|baseLow|offHigh|offLow|dur|count|d,f,a[,fo]|d,f,a[,fo]|...
```

| Field | Description |
|-------|-------------|
| seq | Sequence number for ACK matching |
| baseHigh | High 32 bits of baseTime (µs) |
| baseLow | Low 32 bits of baseTime (µs) |
| offHigh | High 32 bits of clockOffset (signed) |
| offLow | Low 32 bits of clockOffset |
| dur | Common ON duration for all events (ms) |
| count | Number of events (3 patterns × fingers: 12 on v2, 15 on v3 by default; at most `MACROCYCLE_MAX_EVENTS`) |
| d | Delta time from baseTime (ms) |
| f | SECONDARY finger index (0 to motors − 1: 0-3 on v2, 0-4 on v3) |
| a | Amplitude percentage (0-100) |
| fo | Frequency offset (optional, omitted when 0): `(freq - 200) / 5` for 200-455 Hz range |

**Example MACROCYCLE:**

```text
MC:1|0|5050000|0|12345|100|12|0,0,100,10|167,1,100,10|334,2,100,10|...
```

Each event carries `fo=10`, decoded as `200 + 10×5 = 250 Hz`; current firmware sends 250 Hz on every event. An event without an `fo` field decodes as `fo=0` (200 Hz).

**Note:** The V5 format preserves full microsecond precision for baseTime by splitting the 64-bit value into two 32-bit parts, avoiding the up to 999µs precision loss in the previous millisecond-based format.

SECONDARY applies clock offset once to baseTime, then schedules all events via an activation queue. This reduces BLE traffic from 12 messages to 1 per macrocycle (~200 bytes vs ~720 bytes).

### Parameter Messages

None are sent. `PARAM_UPDATE`, `SEED`, `SEED_ACK` and `ACK_PARAM_UPDATE` survive only as reserved prefixes in `INTERNAL_MESSAGES[]` (`include/internal_messages.h`); nothing sends or handles them. Therapy parameters reach the SECONDARY inside each `MC` (duration, amplitude, frequency per event).

### Status Messages

| Message | Direction | Fields | Example |
|---------|-----------|--------|---------|
| `GET_BATTERY` | P → S | (none) | `GET_BATTERY` |
| `BATRESPONSE` | S → P | voltage (reply to `GET_BATTERY`: the SECONDARY's last motor-idle reading, `0.00` if none yet); voltage and level (pushed after every motor-idle SECONDARY battery check; level 0 normal, 1 low, 2 critical) | `BATRESPONSE:3.68`, `BATRESPONSE:3.68\|0` |
| `LED_OFF_SYNC` | P → S | 0 or 1 (mirrors `THERAPY_LED_OFF`) | `LED_OFF_SYNC:1` |
| `DEBUG_SYNC` | P → S | 0 or 1 (mirrors `DEBUG`) | `DEBUG_SYNC:1` |
| `CALIB_BUZZ` | P → S | finger, intensity, duration (relayed `CALIBRATE_BUZZ`) | `CALIB_BUZZ:1:80:500` |
| `CALIB_STOP` | P → S | (none) | `CALIB_STOP` |
| `DEBUG_FLASH` | P → S | seq, timestamp, flash time (sent at each macrocycle start, debug mode only) | `DEBUG_FLASH:7\|5000000\|5070000` |

These are plain colon-delimited strings, except `DEBUG_FLASH`, which is a `SyncCommand` frame.

---

## Safety Validations

The protocol includes several safety mechanisms to prevent incorrect synchronization:

| Validation | Threshold | Action |
|------------|-----------|--------|
| baseTime freshness | ±5 seconds | SECONDARY plays nothing from a macrocycle whose local base time is further than this from now (still sends `MC_ACK`) |
| Critical battery | SECONDARY in `CRITICAL_BATTERY` | SECONDARY acknowledges each macrocycle but plays nothing until the PRIMARY ends the session |
| Hardware fault | SECONDARY in `ERROR` (sent `GLOVE_ERROR`) | SECONDARY acknowledges each macrocycle but plays nothing. The PRIMARY refuses starts while linked to it (`Secondary glove error`, flag cleared on every SECONDARY connect) and ends a running session (not a bench `TEST`) with reason `ERROR` |
| Drift rate (measured) | ±150 ppm | Cap measured drift for plausibility |
| Drift rate (applied) | ±100 ppm | Cap corrections to prevent oscillation |
| Elapsed time | 10 seconds max | Cap EMA time delta to prevent overflow |
| RTT quality | 60ms | Reject samples affected by retransmissions |

These validations ensure that BLE interference or momentary disconnects don't cause the sync system to make large erroneous corrections.

---

## Tuning Guidance

### RTT Quality Threshold

The `SYNC_RTT_QUALITY_THRESHOLD_US` (60ms) balances sample acceptance vs quality:
- **Too low (20ms):** Rejects most samples, slow sync convergence
- **Too high (200ms):** Accepts retransmission-affected samples, poor accuracy
- **Recommended:** 60ms rejects ~10-20% of samples

### Connection Interval Trade-offs

| Interval | Latency | Battery | Use Case |
|----------|---------|---------|----------|
| 7.5ms | Lowest | Highest drain | Active therapy |
| 15ms | Medium | Medium drain | Idle monitoring |
| 30ms | Higher | Lower drain | Background connection |

Current setting: 7.5-10ms on the glove link for maximum sync accuracy during therapy; the phone link requests 30-50ms.

### Outlier Threshold

The outlier rejection uses MAD (Median Absolute Deviation) with minimum 5ms:
- **Automatic:** Adapts to network conditions (3× MAD)
- **Minimum floor:** Never rejects samples within 5ms of median
- **Aggressive mode:** Reduce floor to 3ms if network is very stable

---

## Connection-Anchor Timestamping (Experimental)

> **Status:** Off by default (`SYNC_ANCHOR_TIMESTAMPING_ENABLED 0`). **v2 (nRF52) only**: radio notifications are a SoftDevice feature, and on v3 `radio_anchor` is an inert stub, so sync stays on PTP. PRIMARY parses every PONG form regardless of the flag, so mismatched builds fall back to PTP rather than breaking.

### Motivation

Standard PTP offset estimation averages out asymmetric BLE path delays. When the SoftDevice schedules the radio connection event at a predictable wall-clock instant on both devices, each glove can record an independent "anchor" timestamp of that event and use the difference as a direct physical clock comparison — bypassing the asymmetry problem entirely.

### Radio Notification Anchors

A radio-notification ISR (SWI1, priority 2) fires approximately 800µs before each connection event. The ISR records the local `getMicros()` value into a 16-slot ring buffer (`SYNC_ANCHOR_RING_SIZE`). Because the SoftDevice schedules connection events at the same wall-clock instant on both ends of a link, the difference between PRIMARY's anchor and SECONDARY's anchor is a direct measure of clock offset without any assumption about path symmetry.

### 6-Data-Field PONG Wire Format

When anchor timestamping is enabled, SECONDARY attaches its most recent rx anchor to the PONG reply using the extended wire format created by `createPongWithAnchor`.

> **Wire framing note:** The standard `PONG:seq|timestamp|...` framing header (sequence number and the construction-time `timestamp` field — non-zero, ignored by the PTP math) precedes these data fields. The table below describes the six data fields that follow that framing header.

| Data field index | Name | Description |
|-----------------|------|-------------|
| 0 | T2High | High 32 bits of T2 (PING rx timestamp) |
| 1 | T2Low | Low 32 bits of T2 |
| 2 | T3High | High 32 bits of T3 (PONG tx timestamp) |
| 3 | T3Low | Low 32 bits of T3 |
| 4 | AnchorHigh | High 32 bits of SECONDARY rx anchor |
| 5 | AnchorLow | Low 32 bits of SECONDARY rx anchor |

PRIMARY detects the anchor format by the presence of data field index 4 (`hasData("4")`), which only the anchor form populates. The legacy 64-bit form stops at data field index 3; the legacy short form stops at data field index 1. The legacy forms are silently handled as plain PTP exchanges.

### Anchor Pairing Rules

**SECONDARY (rx anchor):** The rx anchor attached to each PONG is unambiguous — it is the most recent anchor that arrived within `SYNC_ANCHOR_RX_WINDOW_US` (15ms) before the PONG rx callback. Because connection events are regular, there is normally exactly one candidate.

**PRIMARY (tx anchor):** After T1 is stamped at SoftDevice handoff, PRIMARY searches its anchor ring for the first anchor that arrives within `SYNC_ANCHOR_TX_WINDOW_US` (25ms) of T1. The 25ms window is approximately 2× the maximum BLE connection interval, which is long enough to always capture the next connection event after the PING is handed off.

### Offset Calculation

```text
anchorOffset = secondaryAnchor - primaryAnchor - SYNC_ANCHOR_BIAS_US
```

`SYNC_ANCHOR_BIAS_US` is a calibrated constant (default 0) that corrects for any systematic asymmetry between the peripheral (PRIMARY) and central (SECONDARY) radio-event timing. Positive values correct for peripheral RX-window widening where PRIMARY's anchor fires slightly early relative to SECONDARY's.

The anchor offset replaces the PTP offset for that sample when it is considered plausible (see pre-filter below). PTP is still calculated on every exchange and used as the fallback.

### Pre-filter Against Converged Median

Before an anchor offset is accepted, it is checked against the current converged PTP median. If the deviation exceeds `SYNC_ANCHOR_PREFILTER_US` (1.5ms), the anchor pair is rejected and the PTP offset for that exchange is used instead. This prevents implausible anchor readings (missed connection event, ring-wrap collision) from corrupting the EMA.

### Caveats

- **Both-gloves-same-flag requirement:** A SECONDARY built with the anchor flag emits 6-data-field PONGs; one built without it emits plain PONGs. PRIMARY parses either form, but only computes anchor offsets when it is built with the flag and receives a 6-field PONG. Otherwise the exchange is a plain PTP sample. This is safe (it falls back to PTP), but both gloves need the flag if anchor-based accuracy is the goal.
- **Dual-link suppression:** On nRF52840, when PRIMARY is simultaneously connected to a phone app and to SECONDARY, the SoftDevice schedules two separate connection intervals. The anchor ring may contain events from either link. The `SYNC_ANCHOR_RX_WINDOW_US` and `SYNC_ANCHOR_TX_WINDOW_US` windows are sized conservatively to select the correct event, but this has not been validated under all dual-link timing configurations. Use with caution on dual-connected hardware.

---

## Known Limitations

### SECONDARY Stateless Design

The SECONDARY glove operates statelessly—it receives macrocycles with clock offset but doesn't send timing feedback. This simplifies the protocol but means:
- PRIMARY cannot detect SECONDARY clock drift directly
- Sync quality relies entirely on PING/PONG exchanges
- If PING/PONG fails, sync degrades silently

### Timestamp Precision

The firmware uses a dedicated 1MHz hardware timebase (`hires_clock` module) rather than the FreeRTOS tick clock. On v3 this is `esp_timer` (always available; the HFXO watchdog and tick-clock fallback below are nRF-only). On v2:

- **Hardware timebase:** NRF_TIMER4 is configured as a 1MHz free-running counter clocked from the on-board 32MHz HFXO crystal. This gives 1µs resolution independent of the FreeRTOS tick rate (previously ~976µs quantization because DWT was never enabled).
- **HFXO watchdog:** `loop()` calls `hiresClockEnsureHfclk()` once per second. It checks whether the HFCLK is still running and, if not, **re-asserts** the HFCLK request (`sd_clock_hfclk_request()`) so TIMER4 stays on the crystal. It logs only on failure — `[CLOCK] HFCLK re-request failed` — and is otherwise silent; the counter keeps advancing throughout.
- **Fallback path:** If `hiresClockBegin()` fails at boot (e.g., SoftDevice pre-empts TIMER4 allocation), `setup()` logs the degradation once (`[CLOCK] WARNING: hardware timebase unavailable - falling back to tick clock (~1ms resolution)`). `getMicros()` then switches sources **silently** based on `hiresClockIsRunning()`. All sync time math (`syncNowMs()` = `getMicros() / 1000`) uses a single unified clock domain in either case.
- **Overflow safety:** `getMicros()` returns a `uint64_t`. It extends the 32-bit source (which wraps every ~71.6 minutes) with an overflow counter, updated inside a critical section so concurrent callers can't double-count a wrap. At 1MHz the 64-bit result wraps at ~584,000 years.

### Interrupt Safety

Sync state variables are accessed from both main loop and BLE callbacks. Key considerations:
- Connection state marked `volatile` for cache coherency
- Sequence ID generator is NOT thread-safe (main loop only)
- Large structures (Macrocycle) should only be modified from main loop

### ARM printf Limitations

- 64-bit printf (%llu, %lld) not supported by the nRF52 Arduino toolchain (Cortex-M4); both boards use the same split format
- Macrocycle serialization splits 64-bit values into high/low 32-bit parts
- This adds ~10 bytes overhead per macrocycle but ensures portability

---

## Protocol Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| RTT quality threshold (`SYNC_RTT_QUALITY_THRESHOLD_US`) | 60ms | Hard ceiling — discard samples with higher RTT |
| Minimum valid samples (`SYNC_MIN_VALID_SAMPLES`) | 5 | Required for valid sync (~350ms from connect) |
| Outlier threshold (`SYNC_OUTLIER_THRESHOLD_US`) | 5ms | Floor of the max(3 × MAD, 5ms) offset outlier threshold |
| PING/PONG interval (acquiring, `SYNC_ACQUIRE_INTERVAL_MS`) | 70ms | Until clock sync is valid (cold or warm start) |
| PING/PONG interval (idle) | 1s | Keepalive + clock sync when no therapy session active |
| PING/PONG interval during therapy (`SYNC_ACTIVE_INTERVAL_MS`) | 250ms (4Hz) | Higher cadence while therapy is running |
| Keepalive timeout (SECONDARY, `KEEPALIVE_TIMEOUT_MS`) | 6s | 6 missed PINGs = connection lost |
| Keepalive timeout (PRIMARY, `PRIMARY_KEEPALIVE_TIMEOUT_MS`) | 6s | During therapy: sends `STOP_SESSION` and shuts down its motors |
| Lead time range | 70-150ms | Adaptive scheduling window |
| Initial lead time | 50ms (not clamped) | `SYNC_LEAD_TIME_US` (35ms) + processing + generation overhead, used until 3 RTT samples exist |
| Processing overhead (`SYNC_PROCESSING_OVERHEAD_US`) | 10ms | SECONDARY processing time allowance |
| Generation overhead (`SYNC_GENERATION_OVERHEAD_US`) | 5ms | PRIMARY macrocycle generation + serialization |
| Max drift rate (measurement) (`SYNC_MAX_DRIFT_RATE_US_PER_MS`) | ±150 ppm | Measurement cap for plausibility |
| Max drift rate (applied) (`SYNC_MAX_APPLIED_DRIFT_RATE_US_PER_MS`) | ±100 ppm | Conservative correction limit |
| Warm-start validity (`SYNC_WARM_START_VALIDITY_MS`) | 15s | Cache valid for this long after its last update |
| BLE connection interval (glove link) | 7.5-10ms | Low-latency communication (6-8 BLE units); phone link requests 30-50ms |
| Lucky-packet RTT margin (`SYNC_LUCKY_RTT_MARGIN_US`) | 10ms | Accept only RTT ≤ minRTT + 10ms |
| Minimum RTT decay (`SYNC_MIN_RTT_DECAY_US`) | 200µs/sample | Per-sample upward creep of tracked minRTT |
| Innovation gate (`SYNC_INNOVATION_GATE_US`) | 5ms | Reject offset jumps larger than this threshold |
| Innovation reject limit (`SYNC_INNOVATION_REJECT_LIMIT`) | 5 samples | Persistent deviation triggers hard re-anchor |
| Min drift measurement interval (`SYNC_MIN_DRIFT_INTERVAL_MS`) | 500ms | Minimum window for drift rate estimation |
| Anchor ring size (`SYNC_ANCHOR_RING_SIZE`) [experimental] | 16 slots | Radio-notification timestamps retained |
| Anchor RX window (`SYNC_ANCHOR_RX_WINDOW_US`) [experimental] | 15ms | Max age of rx anchor vs rx callback |
| Anchor TX window (`SYNC_ANCHOR_TX_WINDOW_US`) [experimental] | 25ms | Max lookahead from PING handoff to tx anchor |
| Anchor bias (`SYNC_ANCHOR_BIAS_US`) [experimental] | 0µs | Calibrated central-vs-peripheral constant (set during bench validation) |
| Anchor pre-filter (`SYNC_ANCHOR_PREFILTER_US`) [experimental] | 1.5ms | Reject anchor pairs deviating more than this from converged median |

---

## See Also

- **[BLE-Protocol (wiki)](https://github.com/BlueBuzzah/BlueBuzzah-Firmware/wiki/BLE-Protocol)** — Phone app command protocol
- **[THERAPY_ENGINE.md](THERAPY_ENGINE.md)** — Pattern generation
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — System design overview
