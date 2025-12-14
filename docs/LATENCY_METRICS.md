# Latency Metrics System

This document describes the latency measurement system for monitoring and analyzing synchronization timing between PRIMARY and SECONDARY devices.

## Overview

The latency metrics system provides runtime-toggleable instrumentation to measure:

1. **Execution Drift** - How accurately each device hits its scheduled buzz time
2. **Ongoing RTT** - BLE round-trip time during therapy sessions (PRIMARY only)
3. **Sync Quality** - Clock synchronization confidence from initial RTT probing

All metrics are collected via software instrumentation with minimal overhead when disabled.

## Serial Commands

| Command | Description |
|---------|-------------|
| `LATENCY_ON` | Enable metrics collection (aggregated stats, auto-report every 30s) |
| `LATENCY_ON_VERBOSE` | Enable with per-buzz logging to Serial |
| `LATENCY_OFF` | Disable metrics collection (prints final report) |
| `GET_LATENCY` | Print current metrics report |
| `RESET_LATENCY` | Clear all metrics and counters |

### Example Usage

```bash
# Connect to device serial (115200 baud)
screen /dev/cu.usbmodem14101 115200

# Enable metrics on both PRIMARY and SECONDARY
LATENCY_ON

# Start therapy session via phone app or serial command

# Check metrics after some buzzes
GET_LATENCY

# Enable verbose mode to see per-buzz timing
LATENCY_ON_VERBOSE

# Disable and see final report
LATENCY_OFF
```

## Metrics Report Format

```
========== LATENCY METRICS ==========
Status: ENABLED (verbose: OFF)
Buzzes: 150
-------------------------------------
SYNC QUALITY (initial probing):
  Probes:     10
  Min RTT:    14,230 us (one-way: 7,115 us)
  Max RTT:    23,100 us
  Spread:     8,870 us
  Offset:     +45,678 us
  Confidence: HIGH
-------------------------------------
EXECUTION DRIFT:
  Last:    +89 us
  Average: +127 us
  Min:     -45 us
  Max:     +2,341 us
  Jitter:  2,386 us
  Late (>1000 us): 3 (2.0%)
-------------------------------------
ONGOING RTT (PRIMARY only):
  Last:    14,890 us
  Average: 15,450 us
  Min:     14,100 us
  Max:     24,500 us
  Samples: 5
=====================================
```

## Metric Definitions

### Execution Drift

Measures the difference between scheduled execution time and actual execution time in microseconds.

```
drift = actualExecutionTime - scheduledExecutionTime
```

| Value | Meaning |
|-------|---------|
| Positive (+) | Device executed late |
| Negative (-) | Device executed early (rare) |
| Zero | Perfect timing |

**Key fields:**
- **Last**: Most recent buzz drift
- **Average**: Mean drift across all buzzes
- **Min/Max**: Range of observed drift values
- **Jitter**: Max - Min (timing consistency)
- **Late**: Count of buzzes exceeding `LATENCY_LATE_THRESHOLD_US` (default 1000us)

### Sync Quality (RTT Probing)

During initial connection, PRIMARY sends multiple RTT probes to measure BLE latency and calculate clock offset. This section shows the results.

| Field | Description |
|-------|-------------|
| **Probes** | Number of RTT probe exchanges completed |
| **Min RTT** | Fastest round-trip time observed |
| **Max RTT** | Slowest round-trip time observed |
| **Spread** | Max - Min RTT (indicates BLE stability) |
| **Offset** | Calculated clock difference between devices |
| **Confidence** | Sync reliability based on RTT spread |

**Confidence Levels:**

| Level | RTT Spread | Interpretation |
|-------|------------|----------------|
| HIGH | < 10ms | Stable BLE, reliable sync |
| MEDIUM | 10-20ms | Some variability, sync adequate |
| LOW | > 20ms | Unstable BLE, sync may drift |

### Ongoing RTT (PRIMARY Only)

Tracks BLE round-trip time continuously via PING/PONG exchanges (every 1 second).

- Monitors BLE latency changes over time
- High variance may indicate degrading sync quality
- Only available on PRIMARY (initiates PING messages)
- Used for adaptive lead time calculation

## Interpreting Results

### Comparing PRIMARY and SECONDARY

To estimate inter-device synchronization:

1. Enable `LATENCY_ON` on **both** devices
2. Run a therapy session
3. Run `GET_LATENCY` on **both** devices
4. Compare execution drift values

```
PRIMARY average drift:   +127 us
SECONDARY average drift: +342 us
───────────────────────────────────
Estimated sync error:    ~215 us (SECONDARY later)
```

**Important:** This comparison is limited by clock synchronization accuracy:
- With RTT probing (HIGH confidence): ±2-3ms uncertainty
- Without RTT probing: ±10-15ms uncertainty

### What Good Results Look Like

| Metric | Good | Acceptable | Concerning |
|--------|------|------------|------------|
| Average Drift | < 500 us | < 2000 us | > 5000 us |
| Jitter | < 1000 us | < 3000 us | > 5000 us |
| Late % | < 1% | < 5% | > 10% |
| RTT Spread | < 10ms | < 20ms | > 30ms |
| Confidence | HIGH | MEDIUM | LOW |

### Troubleshooting

| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| No sync probing data | Devices connected before firmware update | Power cycle both devices |
| SECONDARY shows 0 buzzes | `isClockSyncValid()` returning false | Wait ~5s for sync samples |
| High jitter | System interrupts, BLE callbacks | Check for blocking operations |
| HIGH drift values | Missed scheduled times | Verify lead time calculation |
| LOW confidence | BLE interference | Move devices closer, reduce interference |

## Technical Details

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        PRIMARY                               │
├─────────────────────────────────────────────────────────────┤
│  TherapyEngine                                              │
│    └─► Enqueue events to ActivationQueue                    │
│                                                             │
│  FreeRTOS Motor Task (Priority 4)                           │
│    └─► Sleep until ~2ms before event                        │
│    └─► Busy-wait for sub-ms precision                       │
│    └─► executeMotorEvent()                                  │
│    └─► latencyMetrics.recordExecution(drift)                │
│                                                             │
│  onBLEMessage(PONG)                                         │
│    └─► latencyMetrics.recordRtt(rtt)                        │
│                                                             │
│  RTT Probing (on connection)                                │
│    └─► latencyMetrics.recordSyncProbe(rtt)                  │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                       SECONDARY                              │
├─────────────────────────────────────────────────────────────┤
│  onBLEMessage(MACROCYCLE)                                   │
│    └─► syncProtocol.toLocalTime(executeAt)                  │
│    └─► Enqueue events to ActivationQueue                    │
│                                                             │
│  FreeRTOS Motor Task (Priority 4)                           │
│    └─► Sleep until ~2ms before event                        │
│    └─► Busy-wait for sub-ms precision                       │
│    └─► executeMotorEvent()                                  │
│    └─► latencyMetrics.recordExecution(drift)                │
└─────────────────────────────────────────────────────────────┘
```

### Scheduled Execution Flow

1. PRIMARY calculates `executeAt = now + adaptiveLeadTime`
2. PRIMARY sends MACROCYCLE with 12 batched events to SECONDARY
3. Both devices enqueue events to ActivationQueue
4. FreeRTOS motor task (Priority 4) processes queue:
   - Sleeps until ~2ms before event (non-blocking)
   - Busy-waits final 2ms for sub-millisecond precision
   - Uses I2C pre-selection for ~100μs activation latency
5. Both devices record drift after execution

### Clock Synchronization

SECONDARY converts PRIMARY's scheduled time to local time:

```cpp
localExecTime = syncProtocol.toLocalTime(executeAt);
// Internally: return executeAt + _currentOffset;
```

The offset is calculated using PTP 4-timestamp protocol via PING/PONG exchanges:

```cpp
// IEEE 1588-inspired calculation
offset = ((T2 - T1) + (T3 - T4)) / 2;
// Where T1=PING sent, T2=PING received, T3=PONG sent, T4=PONG received
```

### Configuration Constants

Defined in `include/config.h`:

```cpp
#define LATENCY_REPORT_INTERVAL_MS 30000  // Auto-report every 30s
#define LATENCY_LATE_THRESHOLD_US 1000    // >1ms considered "late"
#define SYNC_PROBE_COUNT 10               // RTT probes during initial sync
#define SYNC_PROBE_INTERVAL_MS 50         // Interval between probes
#define SYNC_PROBE_TIMEOUT_MS 200         // Timeout for probe ACK
```

### Memory Usage

- `LatencyMetrics` struct: ~100 bytes (static allocation)
- RTT probe samples: 40 bytes (10 x uint32_t)
- Zero heap allocation

### Performance Impact

- **Disabled**: Zero overhead (all recording guarded by `if (enabled)`)
- **Enabled**: ~1-2µs per buzz for `getMicros()` call and arithmetic
- **Verbose**: Additional Serial.printf() per buzz (~50-100µs)

## Limitations

### What This System Measures

| Measurable | Not Measurable |
|------------|----------------|
| Software execution timing | Actual motor activation delay |
| Clock offset estimation | True inter-device hardware sync |
| BLE message latency | DRV2605 response time |
| Timing consistency | Sub-millisecond ground truth |

### Accuracy Bounds

The system achieves sub-millisecond bilateral synchronization through:

- PTP-style clock synchronization with outlier rejection
- FreeRTOS motor task with hybrid sleep/busy-wait
- I2C pre-selection for fast motor activation

| Component | Accuracy |
|-----------|----------|
| Clock sync (5+ samples) | ±500μs |
| Motor task timing | <100μs drift |
| I2C pre-selected activation | ~100μs |
| Total bilateral sync | <1ms |

### Ground Truth Measurement

For precise inter-device latency measurement, external equipment is required:

1. **Oscilloscope** - Tap DRV2605 motor outputs, measure signal delta (~100ns accuracy)
2. **Contact microphone** - Record vibrations, analyze onset timing (~100µs accuracy)
3. **High-speed camera** - 240fps smartphone slow-mo (~4ms resolution)

## Related Documentation

- [SYNCHRONIZATION_PROTOCOL.md](SYNCHRONIZATION_PROTOCOL.md) - Clock sync protocol details
- [BLE_PROTOCOL.md](BLE_PROTOCOL.md) - BLE message format
- [THERAPY_ENGINE.md](THERAPY_ENGINE.md) - Buzz scheduling logic
