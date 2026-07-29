# Design: Isolate phone traffic from PRIMARY↔SECONDARY comms

**Date:** 2026-07-15
**Status:** Implemented (firmware b40503a on fix/phone-reconnected-transition; app 0244c53 on ia-restructure) — pending on-device verification
**Scope:** Firmware (both boards) + BuzzahBuddy app
**Motivation:** Protect the <5 ms bilateral motor synchronization, which the research
identifies as paramount.

## Problem

On the PRIMARY, the BLE TX path is a single shared FIFO queue (`_txQueue`, 12 slots,
in `include/ble_manager.h`), used for **all** outbound messages to **both** the phone
and the SECONDARY. `processTxQueue()` drains up to 4 entries per `ble.update()` in
strict FIFO order.

The PRIMARY schedules its own motor activations locally from each macrocycle and fires
at the macrocycle's `baseTime` regardless of BLE outcome. The SECONDARY fires at the
same `baseTime` **only if** it received the macrocycle before its lead deadline
(adaptive, 50–150 ms). There is **no macrocycle retransmission** (ACK is keepalive-only;
retransmit is a `TODO`).

Therefore, phone traffic sharing the queue can:
- **delay** a macrocycle past its lead window → SECONDARY fires late (>5 ms skew), or
- **overflow** the 12-slot queue during a phone burst → macrocycle **dropped** → SECONDARY
  silent while PRIMARY buzzes.

Either outcome breaks bilateral simultaneity for that cycle. The contention scales with
app polling added recently (on-connect `SESSION_STATUS`, battery refresh, `INFO` backoff,
during-session status poll).

PINGs (PTP clock-sync + keepalive) are also PRIMARY→SECONDARY traffic on the same queue;
a phone-delayed ping stales the clock-sync sample and can contribute to spurious
keepalive timeouts.

## Goal

Keep phone-app communication from interfering with PRIMARY↔SECONDARY communication, to
the maximum extent achievable in software on a shared-radio two-connection topology.

## Design

Two independent workstreams.

### Workstream 1 — Firmware: destination-based priority TX queue

Split the single TX queue into two lanes, keyed by **destination connection**:

- **Hi lane** — everything bound for the **peer glove** (the PRIMARY↔SECONDARY link). On
  the PRIMARY: macrocycles, pings, session commands (START/STOP/PAUSE/RESUME). On the
  SECONDARY: pongs, macrocycle ACKs. This is the hard-real-time sync channel.
- **Normal lane** — everything bound for the **phone** (INFO / BATTERY / SESSION_STATUS /
  other command responses). Only the PRIMARY has a phone connection, so only the PRIMARY's
  normal lane carries traffic.

`processTxQueue()` drains **all pending hi-lane entries first**, then applies the existing
4-per-update budget to the normal lane. Because peer-glove traffic is modest (one
macrocycle per 50–150 ms lead window + 4 Hz pings + rare session commands), the normal lane
still drains freely between them — phone responses simply yield priority, which is correct
since they are not time-critical.

**Routing rule:** at enqueue time, if the target connection handle is the **peer-glove
handle** (SECONDARY handle on the PRIMARY; PRIMARY handle on the SECONDARY) → hi lane;
otherwise (phone) → normal lane. One handle comparison; no message-content inspection. The
rule is role-symmetric and reuses the existing `getSecondaryHandle()` / `getPrimaryHandle()`
accessors.

**Structure:**
- Factor the queue into a reusable `TxRing` struct (entries + head/tail/count) and hold
  two instances, `_txHi` and `_txNormal`. As implemented both lanes use `TX_QUEUE_SIZE`
  (12) — simpler than a separate hi size, ~6.4 KB extra RAM total, negligible on both
  MCUs (14–16% RAM used post-change).
- Drain-side hygiene: `drainRing` drops any entry whose connection is no longer alive
  (dead handles write 0 bytes forever and would otherwise head-of-line block the lane
  permanently after a disconnect; a recycled handle could also receive a stale or
  partial frame on reconnect).
- The hi ring reuses the **identical reserve-fill-publish + critical-section protocol** as
  the existing ring — cross-core-safe on ESP32-S3, correct on single-core nRF52.
- Enqueue selects the ring by destination. `processTxQueue()` gains a hi-lane drain pass
  ahead of the existing normal pass. Both passes share the same per-entry logic
  (late-stamping, partial `bytesSent`, EOT). To avoid divergence and duplication, factor
  the per-entry send into a helper that operates on a ring reference, called for each lane.

**Multi-notify macrocycles (v3):** a 5-motor macrocycle (~230 B) exceeds the 197 B
single-notify limit and fragments across ~2 notifications. The partial-`bytesSent`
machinery is preserved: a fragmenting macrocycle stays at the hi-ring head until fully
sent, so its fragments transmit first on each successive update; phone traffic (a separate
connection — no RX-reassembly interleave risk) runs only after. Completion ≈ 2 connection
events (~15–20 ms), well inside the 50–150 ms lead.

**Both backends:** the same structural change is mirrored in `ble_manager_nrf52.cpp`
(Bluefruit) and `ble_manager_esp32.cpp` (NimBLE); they differ only in the `notify()`
primitive inside `tryWriteImmediate()`. The shared header owns the ring fields.

**No-op on SECONDARY:** the SECONDARY has no phone connection, so its hi lane is unused and
behavior is unchanged.

### Workstream 2 — App: throttle phone polling during an active session

In BuzzahBuddy, reduce the outbound command rate to the gloves while a therapy session is
running (the load that competes with SECONDARY traffic at the radio level, which software
prioritization cannot fully isolate):

- Suspend or slow the battery refresh and any non-essential polling while
  `IsSessionActive`.
- Keep only what the UI genuinely needs during a session (session progress), at the
  lowest acceptable cadence.
- Resume normal cadence when the session ends.

This attacks the competing load at its source and is the necessary complement to the
firmware change on a shared-radio topology.

## Non-goals / acknowledged limits

- **Radio-level airtime sharing** between the phone and SECONDARY connections on the
  PRIMARY (one radio, two links) is scheduled by the SoftDevice / NimBLE stack and cannot
  be overridden in application code. Destination prioritization removes the dominant,
  measurable interference (the software queue); Workstream 2 minimizes the residual radio
  load. Full elimination is not physically possible on this topology.
- **Macrocycle retransmission** remains out of scope: a retransmit arrives too late for a
  50–150 ms-lead cycle. Prevention (priority) is the correct lever, not recovery.

## Regression analysis

- **No message reordering hazard:** SECONDARY-bound messages keep FIFO order among
  themselves (single hi ring); they only overtake phone-bound messages. Macrocycle and
  phone are separate BLE connections, so RX reassembly cannot be corrupted by interleaving.
- **No phone starvation:** hi-lane volume is bounded by therapy generation; the normal lane
  always runs afterward each update. Verify phone command/response still flows during a
  session.
- **No cross-core race:** hi ring uses the same guarded head/tail/count + publish barrier;
  audit both enqueue and drain paths on both backends.
- **Queue-full behavior improves:** phone bursts can no longer evict SECONDARY traffic; a
  hi-ring overflow (should not occur given sizing) degrades to today's drop behavior, not
  worse.
- **PTP/keepalive:** pings now also gain priority over phone, strengthening clock-sync and
  reducing spurious keepalive timeouts. Late-stamping is unchanged.

## Testing / verification

- Build both firmware targets: `pio run -e adafruit_feather_nrf52840` and
  `pio run -e pentabuzzer_esp32s3`; zero new warnings.
- Native unit tests both actuator configs: `pio test -e native` and `pio test -e native_penta`.
- On-device: run a therapy session with the phone connected and actively polling; confirm
  the SECONDARY LED tracks RUNNING and buzzing stays bilaterally in sync; watch for
  `TX queue full` drops and keepalive `[RECOVERY]` lines.
- App: build BuzzahBuddy; confirm session UI still updates and no command/response
  regressions from the reduced polling cadence.

## Rollout

Firmware change lands on the current `fix/phone-reconnected-transition` branch alongside
the related FSM/LED fixes. App change is a separate BuzzahBuddy commit. Both require the
on-device pass before their respective PRs.
