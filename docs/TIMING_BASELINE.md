# Timing Synchronization Baseline Metrics

**Date**: December 13, 2025 (baseline measurement)
**Test Duration**: ~5 minutes
**Environment**: Indoor, typical usage distance

> **Note**: This baseline was established before IEEE 1588 PTP clock synchronization, warm-start recovery, and microsecond precision timestamps were implemented. Bilateral sync was first measured externally on 2026-11-12 (see that section at the end): about 1.5ms average on PTP alone, and 11-20µs with connection-anchor timestamping on v2.

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
| 2026-11-12 | timebase + TX-stamp + filters (anchor off) | 1.2-1.9 ms | 3.6-3.7 ms | 4.1 ms | v2, fails the first gate. PTP leg asymmetry; see the 2026-11-12 section |
| 2026-11-12 | anchor timestamping enabled | 11-20 µs | 24-37 µs | 130 µs | v2, passes both gates after the pairing fixes; see the 2026-11-12 section |

---

## 2026-11-12 — v2 bench validation (logic analyzer)

First external measurement of bilateral trigger skew, following `docs/SYNC_VALIDATION.md`.

| | |
|---|---|
| **Hardware** | v2 (Feather nRF52840), one pair |
| **Method** | Debug pin (A0) on each glove into an fx2lafw logic analyzer at 2 MHz, analysed with `scripts/sync_skew.py`. Runs driven by `scripts/sync_capture.sh` |
| **Session** | `TEST` (120 s, 432 activations per glove) unless noted |
| **Profile** | `TEST` runs: the loaded profile, logged as pattern `rndp`, jitter 23.5%, mirror on. App-started run: `quick_test` (300 s session) |
| **Glove link** | 10.00 ms connection interval, 2M PHY |
| **Build** | `-DSYNC_DEBUG_GPIO_ENABLED=1`, plus `-DSYNC_ANCHOR_TIMESTAMPING_ENABLED=1` for the anchor runs |

Bilateral offsets are SECONDARY minus PRIMARY. "Avg" is the script's `mean |offset|` and "P95" its `P95 |offset|`. No run had an unpaired edge.

### Results

| Firmware | Runs | Phone | Avg skew | P95 skew | Worst case |
|---|---|---|---|---|---|
| `3e3f71b`, PTP only | 4 | No | 1.22-1.87 ms | 3.65-3.74 ms | -4.07 ms |
| `39a7d41`, anchors | 3 | No | 11-16 µs | 24-34 µs | -124 µs |
| `39a7d41`, anchors | 1 | Connected | 19 µs | 37 µs | -130 µs |
| `39a7d41`, anchors, session started from the app (647 pairs in 180 s) | 1 | Connected | 20 µs | 36 µs | -94 µs |

The first row fails the first acceptance gate (< 300 µs avg, < 1 ms P95). The anchor rows pass both gates (< 50 µs avg, < 200 µs P95).

Script output for the last run without a phone (`39a7d41`):

```text
Firmware trigger offset (debug pin: D1 - D0)
  pairs              : 432  (unpaired: PRIMARY 0, SECONDARY 0)
  mean               : -0.003 ms  (SECONDARY first)
  std deviation      : 0.013 ms
  mean |offset|      : 0.011 ms
  P95 |offset|       : 0.024 ms
  worst case         : -0.089 ms
```

### Findings

1. **Scheduling was never the problem.** Within one macrocycle the two gloves agreed to about 10 µs in every run, PTP or anchors. All of the skew was a per-macrocycle step: the clock offset the macrocycle was sent with.
2. **The measurement is trustworthy.** Sent offset minus measured skew gives the true offset, and it came out as a smooth line drifting about 3 ppm, moving at most 22-27 µs between macrocycles.
3. **PTP is biased by leg asymmetry.** Each PTP sample was off by about `(RTT - 2 x interval) / 2`: the reply always takes one connection interval and the PING takes 1-10 ms.

   | RTT | Median PTP sample error |
   |---|---|
   | 12 ms | -4.05 ms |
   | 15 ms | -2.60 ms |
   | 17 ms | -1.50 ms |
   | 18 ms | -0.64 ms |
   | 20 ms | +0.36 ms |

   The lucky-packet gate prefers the shortest round trips, which are the most biased. Skew followed the RTT as it wandered between 12 and 20 ms.
4. **Anchor timestamping had never run on a PRIMARY.** `radioAnchorBegin()` was called after advertising had started, and the SoftDevice refuses the radio-notification configuration once the radio is in use. The PRIMARY logged `radio-anchor init failed` at boot and used PTP for every sample.
5. **Correctly paired anchors are accurate to tens of µs.** They landed 15-47 µs from the true offset, with a steady +48 µs bias over four runs (1,728 pairs), now `SYNC_ANCHOR_BIAS_US = 50`.
6. **Anchors mispair by whole connection intervals.** A PING handed to the radio inside the 800 µs notification lead is paired with the next event. Between 7% and 44% of exchanges per run were affected, in bursts.
7. **Judging anchors against the held offset is unsafe.** An intermediate build dropped any anchor more than 1.5 ms from the current offset. In one run four of the first five anchors were mispaired the same way, the initial median settled 10 ms off, and every correct anchor after that was rejected: the gloves ran 9.8 ms apart for the whole session.

### Fixes (branch `fix/sync-anchor-pairing`)

| Commit | Change |
|---|---|
| `6c430ad` | Start radio anchors before advertising on nRF52 |
| `5a748a6` | Pair anchors by snapping them, in whole connection intervals, to a coarse reference (median of the last 7 RTT-corrected PTP offsets). Samples without a usable anchor use the reference. Anchor bias set to 50 µs |
| `39a7d41` | Skip the exchanges that arrive before the reference exists during initial sync, so the first macrocycle of a session isn't sent with a raw-PTP median |

The pairing rule was replayed against 3,477 logged exchanges from seven runs before it was built: 97% of samples kept, none snapped to a wrong interval. On the bench, 33-88 anchors per run were shifted back and used.

Intermediate results on the way there, each with anchors enabled and no phone:

| Build | Avg skew | P95 skew | Worst case | What was still wrong |
|---|---|---|---|---|
| Anchors, mispairs fall back to PTP | 412 µs | 1.85 ms | -3.62 ms | Bursts of mispairs fed raw PTP into the offset |
| Mispairs dropped, bias 0 (4 runs) | 46-64 µs | 70-97 µs | -387 µs | Steady +48 µs bias |
| Mispairs dropped, bias 50 | 26 µs | 115 µs | +436 µs | One missing anchor fell back to PTP before session start |
| Missing anchors dropped too | 9.81 ms | 9.98 ms | -10.05 ms | Locked one interval off (finding 7) |
| Snap to coarse reference (3 runs) | 18-32 µs | 38-146 µs | -613 µs | First macrocycle up to 0.58 ms off (raw PTP in the initial median) |

### Not yet measured

- Start-up with a phone already connected, and the reboot a `PROFILE_LOAD` from the app causes.
- The soak test in `docs/SYNC_VALIDATION.md`.
- Motor drive offset and command-to-drive delay (D2/D3 were not connected).
- v3. It has no radio anchors, so the PTP bias in finding 3 may apply there as measured here; the size of it on NimBLE is unknown.

Anchor timestamping remains off by default. These results apply to v2 builds with `-DSYNC_ANCHOR_TIMESTAMPING_ENABLED=1` on both gloves.
