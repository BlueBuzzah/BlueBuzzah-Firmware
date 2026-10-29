# Bilateral Sync Validation Protocol

## Setup
1. Build both gloves with `-DSYNC_DEBUG_GPIO_ENABLED=1` (add to build_flags in
   platformio.ini, or pass via PLATFORMIO_BUILD_FLAGS). The flag only builds on
   the nRF52840 (v2) board. Don't port it to PentaBuzzer (v3): its `A0` is GPIO1,
   the shared DRV2605 enable line, and toggling it resets every motor driver.
2. Connect logic analyzer: CH0 -> PRIMARY A0, CH1 -> SECONDARY A0, shared GND.
   Sample rate >= 1MHz.
3. Start a therapy session (auto-start or the app; the `TEST` serial command runs
   a fixed 2-minute session, `TEST_DURATION_SEC`, too short for the capture below).

## Measurement
- Every motor ACTIVATE toggles the pin. With mirrored patterns, edges should
  appear pairwise. Skew = |t(CH0 edge) - t(CH1 edge)| per pair.
- Capture >= 5 minutes (~1000 events). Report avg / P95 / max skew.
- Also capture the soft metrics: enable latencyMetrics and record the 30s
  reports (execution drift per device) plus `[SYNC] RTT=...` lines (PRIMARY
  only, printed with debug mode on: send `DEBUG:1`).

## Acceptance gates
| Milestone | Avg skew | P95 skew |
|---|---|---|
| After the timebase + TX-stamp + filter work | < 300 µs | < 1 ms |
| After connection-anchor timestamping        | < 50 µs  | < 200 µs |

## Soak test
8-hour session at ~2m separation; verify no sync resets
(`[SYNC] Clock sync reset` absent), no keepalive timeouts, no late-event
spikes in latencyMetrics, skew stable start-to-end (drift compensation
working).

## Record results
Append dated results tables to docs/TIMING_BASELINE.md.
