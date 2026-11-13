# Bilateral Sync Optimization Report

**Date:** 2026-11-12
**Hardware:** v2 (Feather nRF52840), one pair
**Branch:** `fix/sync-anchor-pairing`
**Scope:** how far apart the two gloves fire the same activation, measured from outside both devices

This report covers one bench session: the first external measurement of bilateral skew, what it found, each change made in response, and the skew before and after. The per-run record is in `docs/TIMING_BASELINE.md` (2026-11-12 section). The mechanism is documented in `docs/SYNCHRONIZATION_PROTOCOL.md` (Connection-Anchor Timestamping).

## Summary

Average skew between the gloves went from about 1.5 ms to about 13 µs, and the 95th percentile from 3.7 ms to under 35 µs.

| Metric | Start (PTP only) | Final (anchors) | Change |
|---|---|---|---|
| Average skew, `mean \|offset\|` | 1.54 ms | 13 µs | about 120x lower |
| P95 skew, `P95 \|offset\|` | 3.70 ms | 27 µs | about 135x lower |
| Worst case in any run | 4.33 ms | 124 µs | about 35x lower |
| Standard deviation | 1.41 ms | 16 µs | about 90x lower |
| Mean signed offset | -1.40 ms | -3 µs | centred |

"Start" is the mean of five runs on `3e3f71b`. "Final" is the mean of three runs on `39a7d41`. Both sets are a `TEST` session with no phone connected. The full comparison, including the phone-connected runs, is in [Starting and final metrics](#starting-and-final-metrics).

The final numbers apply to v2 builds with anchor timestamping on, on both gloves. They were measured with `-DSYNC_ANCHOR_TIMESTAMPING_ENABLED=1`; that is now the v2 default. It stays off on v3, which has no radio anchors.

## Method

Each glove toggles a debug pin (A0 on v2) at the start of every motor ACTIVATE, just before the I2C write. Both pins go to an fx2lafw logic analyzer sampling at 2 MHz. `scripts/sync_skew.py` pairs each PRIMARY edge with the nearest SECONDARY edge and reports the signed offsets, SECONDARY minus PRIMARY. The full protocol is `docs/SYNC_VALIDATION.md`.

The firmware can't report this number itself. Its offset, lateness figures and `GET_SYNC_STATS` all come from the same clock mapping, so a constant error in that mapping is invisible to them.

Conditions for every run:

- Glove link at a 10.00 ms connection interval on 2M PHY.
- `TEST` session: 120 s, 432 activations per glove, 36 macrocycles of 12.
- Build flag `-DSYNC_DEBUG_GPIO_ENABLED=1`.
- No run had an unpaired edge.

## Process

### 1. First capture: the gate fails

The first run on unmodified firmware missed the first acceptance gate (< 300 µs average, < 1 ms P95) by a wide margin: 1.67 ms average and 3.73 ms P95.

Splitting the 792 pairs into their macrocycles showed where the error was:

- Inside one macrocycle the gloves agreed to about 10 µs.
- Between macrocycles the mean skew stepped by up to 1.9 ms, wandering from +1.0 ms to -4.3 ms over the run.

So scheduling and execution were already good. The whole error was the clock offset that each macrocycle is sent with. The 1.3 ms standard deviation the script reported was that slow wander, not jitter.

### 2. Tooling: one command per run

The first capture's serial logs were a 10,000-byte tail that didn't overlap the capture, so the offset each macrocycle used couldn't be matched to its measured skew. `scripts/sync_capture.sh` was written to make every run complete and repeatable. It:

1. Reboots both gloves and logs each one from its first boot line.
2. Waits for the pair to link.
3. Runs the logic analyzer through `sigrok-cli`.
4. Sends `DEBUG:1` and `TEST` to the PRIMARY.
5. Stops when the session ends, converts the capture and runs `sync_skew.py`.

Each run lands in `temp/analyzer-runs/<timestamp>/` with the capture, both logs, the commit and the result. Two later options cover the phone cases: `NO_REBOOT=1` keeps a phone connected, and `NO_TEST=1` waits for a session started from the app.

Things learned while building it:

- `pio device monitor` refuses to run unless its output is a terminal, so it can't be piped to a file. The script reads the serial ports directly.
- A serial port opened without `raw -echo` echoes the glove's own output back to it as commands.
- PulseView holds the analyzer, so it must be closed for `sigrok-cli` to open it.
- An app session start can send `PROFILE_LOAD`, which reboots the PRIMARY and drops its USB port. The log readers reopen the port when it returns.

### 3. Root cause: PTP assumes two equal legs

With full logs, the offset each macrocycle was sent with could be subtracted from its measured skew. That gives the true clock offset, and it came out as a smooth line drifting about 3 ppm, moving at most 22-27 µs between macrocycles. Two conclusions follow: the measurement is trustworthy, and every sync sample can be scored against truth.

Scored that way, the PTP samples were off by an amount that tracked the round-trip time:

| RTT | Median PTP sample error |
|---|---|
| 12 ms | -4.05 ms |
| 15 ms | -2.60 ms |
| 17 ms | -1.50 ms |
| 18 ms | -0.64 ms |
| 20 ms | +0.36 ms |

The slope is half a millisecond of error per millisecond of RTT, which means all of the RTT variation is on one leg:

- The PONG (central to peripheral) waits for the next connection event, so it always takes about one interval, 10 ms.
- The PING takes anywhere from 1 to 10 ms, depending on where in the interval it was handed to the radio.

PTP halves the round trip and assumes both legs are equal, so each sample is wrong by `(RTT - 2 x interval) / 2`. The existing lucky-packet gate made it worse: it prefers the shortest round trips, which are the most lopsided. Skew simply followed the RTT as it drifted between 12 and 20 ms.

### 4. Anchor timestamping had never run

Connection-anchor timestamping was already in the firmware for exactly this problem: each glove hardware-timestamps the same BLE connection event, and the difference is a direct clock comparison that doesn't depend on either leg. Enabling its build flag changed nothing.

The boot logs showed why. The SECONDARY printed `Radio-anchor timestamping active`, and the PRIMARY printed `radio-anchor init failed - PTP-only sync`. The SoftDevice accepts the radio-notification configuration only while the radio is idle, and a PRIMARY starts advertising inside `BLEManager::begin()`, before the anchors were started. The feature had never worked on a PRIMARY.

**Optimization 1** (`6c430ad`): start the radio anchors before the role setup, ahead of advertising.

Result: average skew 412 µs, P95 1.85 ms. Better, but short of the gate.

### 5. Anchors mispair by whole connection intervals

Correctly paired anchor samples landed within 15-47 µs of the true offset. The remaining error came from the ones that weren't paired correctly:

- About 17% of exchanges paired the wrong connection event, mostly exactly ±10 ms off.
- The firmware detected these as implausible and fell back to that exchange's PTP offset, which is the biased measurement from step 3.
- In two consecutive macrocycles every sample was a mispair, and the skew went to -3.5 ms.

The cause is a race in the pairing rule. The anchor fires about 800 µs before its connection event. A PING handed to the radio inside that lead is carried by an event whose anchor has already passed, so "the first anchor after the handoff" is the next event's. Mispairs come in bursts because the PING's position in the interval drifts slowly in and out of that window.

### 6. A dead end: dropping the bad samples

The first attempt was to drop a mispaired sample instead of falling back to PTP (`200f00e`).

It looked good: 46-64 µs average over four runs, with a steady +48 µs bias. Setting `SYNC_ANCHOR_BIAS_US` to 50 brought one run to 26 µs. Extending the drop to samples with no anchor at all then produced a run where the gloves were 9.8 ms apart for the entire session.

What happened in that run:

1. Four of the first five anchors after boot were mispaired the same way.
2. The initial median settled 10 ms off.
3. Every correct anchor that followed was 10 ms from the held offset, so it was judged implausible and dropped: 426 of them.
4. Nothing could pull the offset back. The PTP fallback had been the cause of the millisecond errors, but it was also the only escape route.

The earlier good runs carried the same hazard and happened to start with correctly paired anchors. The lesson is the design rule the final fix is built on: **never judge an anchor against the offset currently held.**

`200f00e` is kept in the branch history as the record of this step. `5a748a6` replaces it.

### 7. The fix: snap to a coarse reference

A mispaired anchor is wrong by a whole number of connection intervals, so it can be corrected if there is a rough, independent reference to correct it towards.

The obvious reference, the PTP offset with the step 3 correction applied per sample, was checked against the logs before any firmware was written and failed: an exchange whose reply was retransmitted can have a normal-looking 21 ms RTT and read 10 ms low. Candidate rules were then replayed offline against all 3,477 logged exchanges from the seven anchor runs, scored against the true offset.

**Optimization 2** (`5a748a6`), the rule that came out of the replay:

- **Coarse reference.** Every exchange with an RTT within two intervals plus 1 ms contributes `ptpOffset + interval - RTT / 2`. The reference is the median of the last 7 of these, and needs at least 3. The median outvotes the retransmitted replies.
- **Snap.** Each anchor offset is shifted by whole connection intervals to the value nearest the reference.
- **Tolerance.** A snapped offset within 750 µs of the reference is used. One further away isn't a whole-interval mispair (it's an advertising or phone-link event) and is rejected.
- **No usable anchor.** The reference itself is used as the sample, so nothing falls back to raw PTP and nothing is dropped.

In the replay this kept 97% of samples and snapped none to a wrong interval. The reference sat 0.07-0.18 ms from the true offset, and because it is rebuilt from the last seven exchanges it can't hold a wrong value.

Result: 18-32 µs average over three runs, P95 38-146 µs.

### 8. The first macrocycle

Those three runs still started 130-580 µs off for their first one or two macrocycles. The first exchanges after a connect arrive before the reference has its three samples, so their raw PTP offsets went into the initial median, which settled up to 1.9 ms off while the EMA caught up.

**Optimization 3** (`39a7d41`): during initial sync, skip exchanges that arrive before the reference exists, up to 10. The limit lets a link too slow to produce a reference still sync on PTP. Two to four exchanges were skipped per run.

Result: 11-16 µs average, P95 24-34 µs, and the first macrocycle within 36 µs.

### 9. With a phone connected

A phone link puts its own radio events, and their anchors, between the glove link's. Two runs covered it:

| Case | Average | P95 | Worst case |
|---|---|---|---|
| `TEST`, phone connected | 19 µs | 37 µs | 130 µs |
| Session started from BuzzahBuddy (647 pairs in 180 s) | 20 µs | 36 µs | 94 µs |

About 13-15% of anchors were rejected with a phone connected, against 2-4% without. Those samples used the coarse reference, and skew was unchanged.

## Optimizations

| # | Commit | Change | Average skew after |
|---|---|---|---|
| - | `3e3f71b` | Starting point: PTP only | 1.22-1.87 ms |
| 1 | `6c430ad` | Start radio anchors before advertising, so they run on a PRIMARY | 412 µs |
| - | `200f00e` | Drop mispaired samples, anchor bias 50 µs (superseded: could lock one interval off) | 26-64 µs, or 9.8 ms |
| 2 | `5a748a6` | Snap anchors in whole intervals to a coarse reference; use the reference when there is no usable anchor | 18-32 µs |
| 3 | `39a7d41` | Skip exchanges that arrive before the reference exists during initial sync | 11-16 µs |

Supporting changes on the branch:

- `SYNC_ANCHOR_BIAS_US` set to 50, the steady offset measured over four runs (1,728 pairs).
- `SYNC_ANCHOR_PREFILTER_US` removed, along with the comparison against the held offset.
- Two host tests in `test_sync_protocol`, built from rows of the bench logs, including the 21 ms and 35 ms exchanges that defeat a per-sample correction.
- `scripts/sync_capture.sh`.

## Starting and final metrics

All values are `scripts/sync_skew.py` output for the firmware trigger offset. Negative means the SECONDARY fired first.

### Start: `3e3f71b`, PTP only, no phone

| Run | Pairs | Mean | Std dev | Average | P95 | Worst case |
|---|---|---|---|---|---|---|
| run 1 | 792 | -1.590 ms | 1.269 ms | 1.666 ms | 3.725 ms | -4.330 ms |
| 19:42:51 | 432 | -1.021 ms | 1.488 ms | 1.219 ms | 3.713 ms | -3.813 ms |
| 19:48:26 | 432 | -1.466 ms | 1.357 ms | 1.533 ms | 3.647 ms | -3.811 ms |
| 19:53:14 | 432 | -1.654 ms | 1.487 ms | 1.867 ms | 3.735 ms | -3.788 ms |
| 19:58:51 | 432 | -1.245 ms | 1.446 ms | 1.429 ms | 3.686 ms | -4.069 ms |
| **Mean of runs** | | **-1.395 ms** | **1.409 ms** | **1.543 ms** | **3.701 ms** | **-4.330 ms** (largest) |

Run 1 was captured by hand in PulseView during a longer session. The last two were built with the anchor flag, but anchors failed to start on the PRIMARY (step 4), so they measured PTP.

### Final: `39a7d41`, anchors, no phone

| Run | Pairs | Mean | Std dev | Average | P95 | Worst case |
|---|---|---|---|---|---|---|
| 20:51:04 | 432 | -0.008 ms | 0.019 ms | 0.016 ms | 0.034 ms | -0.124 ms |
| 20:53:21 | 432 | +0.001 ms | 0.015 ms | 0.012 ms | 0.024 ms | -0.056 ms |
| 20:55:37 | 432 | -0.003 ms | 0.013 ms | 0.011 ms | 0.024 ms | -0.089 ms |
| **Mean of runs** | | **-0.003 ms** | **0.016 ms** | **0.013 ms** | **0.027 ms** | **-0.124 ms** (largest) |

### Final: `39a7d41` firmware, phone connected

| Run | Pairs | Mean | Std dev | Average | P95 | Worst case |
|---|---|---|---|---|---|---|
| 21:02:33, `TEST` | 432 | +0.003 ms | 0.022 ms | 0.019 ms | 0.037 ms | -0.130 ms |
| 21:13:15, started from the app | 647 | -0.004 ms | 0.024 ms | 0.020 ms | 0.036 ms | -0.094 ms |

There is no starting measurement with a phone connected, so these have no "before" to compare against.

### Against the acceptance gates

| Gate (`docs/SYNC_VALIDATION.md`) | Average | P95 | Start | Final |
|---|---|---|---|---|
| After the timebase, TX-stamp and filter work | < 300 µs | < 1 ms | Fails: 1.54 ms, 3.70 ms | Passes |
| After connection-anchor timestamping | < 50 µs | < 200 µs | Fails | Passes: 13 µs, 27 µs (20 µs, 37 µs at worst with a phone) |

### What did not change

Timing inside a macrocycle was about 10 µs before any change and is about 10 µs now. The work removed the error in the clock offset between the gloves. It did not make either glove's own scheduling faster or tighter, and it didn't need to.

## Limits of these results

- **One pair, one evening.** Every run used the same two v2 gloves, a few minutes apart, at one connection interval (10 ms).
- **Short sessions.** The longest capture was 180 s. The 8-hour soak test in `docs/SYNC_VALIDATION.md` has not been run.
- **Trigger timing only.** The debug pin marks when the firmware fires the activation. The motor channels (D2, D3) were not connected, so motor drive offset and command-to-drive delay were not measured.
- **Phone cases not covered:** start-up with a phone already connected, and the PRIMARY reboot that a `PROFILE_LOAD` from the app causes.
- **The reply-leg assumption.** The coarse reference relies on the PONG taking one connection interval. That held in every run, but it is a property of this v2 link, not of BLE.
- **v3 is unmeasured.** It has no radio anchors, so it syncs on PTP alone. The bias found in step 3 may apply there, and its size on NimBLE is unknown.
- **v2 only.** Anchor timestamping has since been made the v2 default. v3 builds are unchanged by this work.
