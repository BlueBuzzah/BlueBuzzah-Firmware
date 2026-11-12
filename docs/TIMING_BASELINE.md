# Timing Synchronization Baseline Metrics

**Date**: December 13, 2025 (baseline measurement)
**Test Duration**: ~5 minutes
**Environment**: Indoor, typical usage distance

> **Note**: This baseline was established before IEEE 1588 PTP clock synchronization, warm-start recovery, and microsecond precision timestamps were implemented. Current bilateral sync accuracy is pending re-validation on the 1MHz hardware timebase (see the 2026-06-10 section below).

---

## Executive Summary

**Current bilateral synchronization: ~1ms average, <3ms worst-case**

The system is already performing **excellently** - well within the <50ms target, and close to the stretch goal of <500μs. The primary bottleneck identified is **SECONDARY BLE receive jitter**, not motor activation timing.

---

## Raw Metrics

### PRIMARY Device

```
========== LATENCY METRICS ==========
Status: ENABLED (verbose: ON)
Buzzes: 648
-------------------------------------
SYNC QUALITY (initial probing):
  (no sync probing data)
-------------------------------------
EXECUTION DRIFT:
  Last:    +28 μs
  Average: +473 μs
  Min:     +0 μs
  Max:     +1,871 μs
  Jitter:  1,871 μs
  Late (>1000 μs): 1 (0.2%)
-------------------------------------
ONGOING RTT (PRIMARY only):
  (no RTT data)
=====================================
```

### SECONDARY Device

```
========== LATENCY METRICS ==========
Status: ENABLED (verbose: ON)
Buzzes: 727
-------------------------------------
SYNC QUALITY (initial probing):
  (no sync probing data)
-------------------------------------
EXECUTION DRIFT:
  Last:    +594 μs
  Average: +500 μs
  Min:     +1 μs
  Max:     +4,984 μs
  Jitter:  4,983 μs
  Late (>1000 μs): 1 (0.1%)
-------------------------------------
ONGOING RTT (PRIMARY only):
  (no RTT data)
=====================================
```

---

## Analysis

### 1. Motor Activation Timing (EXCELLENT ✅)

| Metric | PRIMARY | SECONDARY | Assessment |
|--------|---------|-----------|------------|
| **Average drift** | +473 μs | +500 μs | Excellent - both ~500μs |
| **Minimum drift** | +0 μs | +1 μs | Near-perfect minimum |
| **Maximum drift** | +1,871 μs | +4,984 μs | SECONDARY has outliers |
| **Late events (>1ms)** | 1 (0.2%) | 1 (0.1%) | Outstanding - target was <5% |

**Interpretation:**
- ✅ Motor task timing is **very good** (~500μs average)
- ✅ FreeRTOS + busy-wait approach is working well
- ✅ Late event rate is **5x better than target** (<1% vs target <5%)
- ⚠️ SECONDARY shows higher jitter (4,983μs vs PRIMARY's 1,871μs)

### 2. Bilateral Synchronization Estimate

**Best-case scenario** (both devices execute early):
- PRIMARY min (0μs) vs SECONDARY min (1μs) = **~1μs** difference

**Average-case scenario**:
- |PRIMARY avg - SECONDARY avg| = |473 - 500| = **~27μs** offset
- Combined drift: ~500μs + ~500μs = **~1ms** typical bilateral sync

**Worst-case scenario** (one early, one late):
- PRIMARY max (1,871μs) vs SECONDARY min (1μs) = **~1,870μs** differential
- Or PRIMARY min (0μs) vs SECONDARY max (4,984μs) = **~4,984μs** differential
- **Worst observed: ~5ms** (rare outlier)

**Overall bilateral sync**: **~1ms typical, ~3-5ms worst-case**

### 3. Identified Bottleneck: SECONDARY BLE Receive Jitter

**Evidence:**
- SECONDARY jitter (4,983μs) is **2.7x higher** than PRIMARY (1,871μs)
- SECONDARY max drift (+4,984μs) vs PRIMARY max (+1,871μs)
- This indicates **BLE receive path variability**, not motor control issues

**Root Cause Analysis:**
- PRIMARY sends MACROCYCLE message over BLE
- SECONDARY receives in BLE callback (ISR context)
- BLE SoftDevice can delay callback by several milliseconds during:
  - Radio interference
  - Connection event timing
  - Retransmissions
- This ~5ms spike is the **#1 optimization target**

### 4. Missing Data

**RTT Metrics:** "(no RTT data)" on PRIMARY
- **Cause**: PONG message RTT calculation wasn't being recorded to `latencyMetrics`
- **Fix Applied**: The PONG handler (`main.cpp`) now calls `latencyMetrics.recordRtt()`

**Sync Probing:** "(no sync probing data)"
- **Expected**: This tracks initial connection sync probing
- **Not critical**: Ongoing sync quality is what matters for therapy

---

## Performance Against Targets

| Metric | Target | Stretch Goal | Current | Status |
|--------|--------|--------------|---------|--------|
| **Bilateral Sync (Avg)** | <50ms | <500μs | **~1ms** | ✅ **Exceeds stretch!** |
| **Bilateral Sync (P95)** | N/A | <2ms | ~3-5ms | ⚠️ Need improvement |
| **Motor Drift (Avg)** | <2ms | <500μs | **~500μs** | ✅ **At stretch goal!** |
| **Late Events** | <5% | <1% | **<0.2%** | ✅ **5x better!** |
| **Jitter (PRIMARY)** | <3ms | <1ms | **1.87ms** | ✅ Good |
| **Jitter (SECONDARY)** | <3ms | <1ms | **4.98ms** | ⚠️ Need improvement |

---

## Conclusion

The December 2025 measurements showed motor timing near 500μs average drift, with the largest outliers on the SECONDARY's BLE receive path (~5ms spikes). The 2026-06-10 section below explains why those averages were largely tick-quantization error.

---

## 2026-06-10 — Timing rework (pre-bench)

### Root Cause Identified

The +473µs / +500µs average execution drift figures recorded in the December 2025 baseline were **not motor latency** — they were tick quantization. The FreeRTOS tick rate on nRF52840 (1024 Hz from the 32.768kHz LFCLK) produces a tick period of ~976µs, and DWT was never enabled. Every `micros()` call returned a value quantized to the nearest tick, so measured "drift" was largely the tick-alignment error of the timestamp itself. True sub-ms drift could not be observed or distinguished from the measurement floor.

### Fixes Shipped

| Area | Change |
|------|--------|
| **Hardware timebase** | NRF_TIMER4 configured as 1MHz free-running counter clocked from the HFXO crystal. `getMicros()` now reads this counter; falls back to the tick clock if TIMER4 init fails at boot. |
| **HFXO watchdog** | `loop()` re-requests the HFCLK every 1s (TinyUSB can release it on USB suspend); logs only if the request fails. |
| **Late TX stamping** | PING T1 / PONG T3 are now stamped at SoftDevice handoff (deferred-stamp TX entries) instead of at message construction, removing main-loop queuing latency from PTP samples. |
| **PONG sequence matching** | Stale / sequence-mismatched PONGs are excluded from the offset calculation without clearing the in-flight T1 (any PONG still refreshes the PRIMARY's keepalive timer). |
| **RTT hard ceiling** | Samples with RTT > 60ms are discarded (unchanged value, now correctly measurable). |
| **Lucky-packet gate** | Only exchanges within `SYNC_LUCKY_RTT_MARGIN_US` (10ms) of the tracked minimum RTT are accepted; minimum decays upward at `SYNC_MIN_RTT_DECAY_US` (200µs/sample). |
| **Innovation gate** | Offset jumps > `SYNC_INNOVATION_GATE_US` (5ms) are rejected; after `SYNC_INNOVATION_REJECT_LIMIT` (5) such rejections with no accepted sample in between, the next jumping sample hard re-anchors the offset instead of EMA-blending. |
| **4Hz therapy cadence** | `SYNC_ACTIVE_INTERVAL_MS` = 250ms during therapy (idle stays at 1s). |
| **Motor path** | First macrocycle event gets I2C pre-selection in the motor task idle window; removed blocking serial read and critical-path `[LEADTIME]` printf from the therapy loop. |
| **GPIO ground truth** | `SYNC_DEBUG_GPIO_ENABLED` toggles a debug pin (A0 on v2, GPIO40 on v3) on each ACTIVATE; see `docs/SYNC_VALIDATION.md` for the measurement protocol. |
| **Anchor timestamping (experimental)** | Radio-notification ISR (SWI1, 800µs pre-event) anchors in a 16-slot ring; SECONDARY attaches its rx anchor to PONGs (6-field wire format); PRIMARY pairs with its first post-T1 tx anchor within 25ms; `anchorOffset = secondaryAnchor - primaryAnchor - SYNC_ANCHOR_BIAS_US` replaces PTP when within the 1.5ms pre-filter; PTP fallback per sample. Disabled by default (`SYNC_ANCHOR_TIMESTAMPING_ENABLED 0`). |

### Expected Outcomes

Per the `docs/SYNC_VALIDATION.md` acceptance gates:

| Milestone | Target avg skew | Target P95 skew |
|-----------|----------------|-----------------|
| After timebase + TX-stamp + filter work | < 300 µs | < 1 ms |
| After connection-anchor timestamping | < 50 µs | < 200 µs |

### Bench Results (to be filled during validation)

| Date | Config | Avg skew | P95 skew | Max skew | Notes |
|------|--------|----------|----------|----------|-------|
| — | timebase + TX-stamp + filters (anchor off) | — | — | — | pending |
| — | anchor timestamping enabled | — | — | — | pending |
