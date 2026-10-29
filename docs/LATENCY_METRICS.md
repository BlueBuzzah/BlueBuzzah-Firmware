# Latency Metrics System

This document describes the latency measurement system for monitoring and analyzing synchronization timing between PRIMARY and SECONDARY devices.

## Overview

The latency metrics system provides runtime-toggleable instrumentation to measure:

1. **Execution Drift** - How accurately each device hits its scheduled buzz time
2. **Ongoing RTT** - BLE round-trip time during therapy sessions (PRIMARY only)
3. **Sync Quality** - Report section for initial RTT probing; not populated by current firmware (see below)

All metrics are collected via software instrumentation with minimal overhead when disabled.

## Serial Commands

> **Note:** These commands are available via **USB Serial only** (115200 baud), not over BLE.
> Use `pio device monitor` or a serial terminal like `screen` to access them.

| Command | Description |
|---------|-------------|
| `LATENCY_ON` | Enable metrics collection (aggregated stats, auto-report every 30s while therapy runs) |
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
  (no sync probing data)
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

Measures the difference between scheduled execution time and actual execution time in microseconds. Each motor event counts as one buzz in the report: activations (on enabled motors) and deactivations are both recorded.

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

**Not populated by current firmware.** `LatencyMetrics::recordSyncProbe()` and `finalizeSyncProbing()` exist, but nothing calls them: clock sync uses the unified PING/PONG exchange (see [Configuration Constants](#configuration-constants)), not a separate probing phase. The report always prints `(no sync probing data)` here. Use the `GET_SYNC_STATS` serial command for clock sync status (offset, drift, RTT, adaptive lead time).

The fields below describe what the section would show if probes were recorded.

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

Tracks BLE round-trip time continuously via PING/PONG exchanges (every 70ms until clock sync is valid, then 250ms during therapy and 1 second idle).

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
Execution-latency difference: ~215 us (SECONDARY later)
```

**Important:** Each device measures drift against its own scheduled time, so clock-offset error between the gloves does not show up in either number. Use GPIO ground truth ([SYNC_VALIDATION.md](SYNC_VALIDATION.md)) to measure true bilateral skew.

### What Good Results Look Like

| Metric | Good | Acceptable | Concerning |
|--------|------|------------|------------|
| Average Drift | < 500 us | < 2000 us | > 5000 us |
| Jitter | < 1000 us | < 3000 us | > 5000 us |
| Late % | < 1% | < 5% | > 10% |

### Troubleshooting

| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| No sync probing data | Expected: the section is never populated | Use `GET_SYNC_STATS` for clock sync status |
| SECONDARY shows 0 buzzes | Metrics not enabled on SECONDARY (they are per-device) | Send `LATENCY_ON` to each glove's serial port |
| High jitter | System interrupts, BLE callbacks | Check for blocking operations |
| HIGH drift values | Missed scheduled times | Verify lead time calculation |

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
│    └─► Sleep until ~1ms before event                        │
│    └─► Busy-wait for sub-ms precision                       │
│    └─► executeMotorEvent()                                  │
│    └─► latencyMetrics.recordExecution(drift)                │
│                                                             │
│  onBLEMessage(PONG)                                         │
│    └─► latencyMetrics.recordRtt(rtt)                        │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                       SECONDARY                              │
├─────────────────────────────────────────────────────────────┤
│  onBLEMessage(MACROCYCLE)                                   │
│    └─► localBaseTime = mc.baseTime + mc.clockOffset         │
│    └─► Enqueue events to ActivationQueue                    │
│                                                             │
│  FreeRTOS Motor Task (Priority 4)                           │
│    └─► Sleep until ~1ms before event                        │
│    └─► Busy-wait for sub-ms precision                       │
│    └─► executeMotorEvent()                                  │
│    └─► latencyMetrics.recordExecution(drift)                │
└─────────────────────────────────────────────────────────────┘
```

### Scheduled Execution Flow

1. PRIMARY calculates `executeAt = now + adaptiveLeadTime`
2. PRIMARY sends MACROCYCLE with all of the macrocycle's events batched (3 patterns × fingers: 12 on v2, 15 on v3) to SECONDARY
3. Both devices enqueue events to ActivationQueue
4. FreeRTOS motor task (Priority 4) processes queue:
   - Sleeps until ~1ms before event when it is more than 2ms away (non-blocking)
   - Busy-waits the final stretch (under 2ms) for sub-millisecond precision
   - Uses I2C pre-selection for ~100μs activation latency
5. Both devices record drift after execution

### Clock Synchronization

PRIMARY stamps its clock offset into every macrocycle, and SECONDARY converts PRIMARY's base time to local time with it:

```cpp
// PRIMARY (onSendMacrocycle)
mcCopy.clockOffset = syncProtocol.getCorrectedOffset();

// SECONDARY (onBLEMessage, MC: handler)
int64_t offset = mc.clockOffset;
uint64_t localBaseTime = static_cast<uint64_t>(static_cast<int64_t>(mc.baseTime) + offset);
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

// Clock sync uses unified PING/PONG keepalive (not separate probing):
#define KEEPALIVE_INTERVAL_MS 1000        // PING/PONG every 1 second
#define SYNC_ACQUIRE_INTERVAL_MS 70       // PING/PONG every 70ms until sync valid
#define SYNC_MIN_VALID_SAMPLES 5          // Minimum samples before sync valid
#define SYNC_RTT_QUALITY_THRESHOLD_US 60000  // Reject samples with RTT > 60ms
```

### Memory Usage

- `LatencyMetrics` struct: ~100 bytes (static allocation)
- Clock sync collects offsets in a 10-sample buffer (median) until valid, then maintains them with an EMA (exponential moving average)
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

The system is designed for sub-millisecond bilateral synchronization through:

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

---

## 2026-06-10 — Hardware timebase note

The accuracy figures in the "Accuracy Bounds" table above (clock sync ±500µs, total bilateral sync <1ms) were originally derived from measurements taken against the FreeRTOS tick clock. Because DWT was never enabled, `micros()` on nRF52840 was tick-quantized at ~976µs resolution; those figures cannot be interpreted as true sub-millisecond measurements.

As of the hardware timebase rework, `getMicros()` reads a 1MHz NRF_TIMER4 counter clocked from the HFXO crystal (1µs resolution). The accuracy figures are pending re-measurement on the new timebase — see `docs/SYNC_VALIDATION.md` for the measurement protocol and acceptance gates, and `docs/TIMING_BASELINE.md` for results as they are recorded.

The execution-drift numbers in `docs/TIMING_BASELINE.md` (December 2025 baseline: +473µs PRIMARY, +500µs SECONDARY average) are similarly affected: a significant portion of those averages was tick-quantization error, not actual motor activation latency.

---

## Related Documentation

- [SYNCHRONIZATION_PROTOCOL.md](SYNCHRONIZATION_PROTOCOL.md) - Clock sync protocol details
- [BLE-Protocol (wiki)](https://github.com/BlueBuzzah/BlueBuzzah-Firmware/wiki/BLE-Protocol) - BLE message format
- [THERAPY_ENGINE.md](THERAPY_ENGINE.md) - Buzz scheduling logic
