# Bilateral Sync Validation Protocol

Measures how far apart the two gloves trigger, from outside both devices. The
firmware can't report this itself: its offset, lateness figures and
`GET_SYNC_STATS` all come from the same clock mapping, so a constant error in
that mapping is invisible to them.

The method is the same on both hardware versions. Only the build, the board
preparation and the probe points differ:

1. [Equipment and PulseView setup](#equipment-and-pulseview-setup) (both versions)
2. Build and wire: [v2](#v2-feather-nrf52840) or [v3](#v3-esp32-s3-wroom-1u-board)
3. [Capture](#capture) and [run the sessions](#run-the-sessions) (both versions)
4. [Analyse](#analyse) and [record the results](#record-results) (both versions)

A capture gives up to three measurements:

| Measurement | Channels | What it covers |
|---|---|---|
| Firmware trigger offset | The debug pin on each glove | Clock sync and scheduling: when each glove's firmware fires the activation |
| Motor drive offset | One LRA terminal per glove | The full path to the motor: firmware, I2C, multiplexer and DRV2605 start-up |
| Command-to-drive delay | Debug pin and LRA terminal on one glove | One glove's hardware path alone. A steady difference between the gloves is skew that firmware tuning can't remove |

The debug pin toggles at the start of every motor ACTIVATE, just before the
I2C write. Each edge, rising or falling, is one activation. The motor channels
time when the driver starts driving, not when the motor physically vibrates.
Both gloves use the same LRA, so its spin-up time cancels out of the offset.

## Equipment and PulseView setup

- LONELY BINARY 8-channel 24 MHz USB logic analyzer. Any analyzer that runs on
  sigrok's `fx2lafw` driver works the same way.
- Jumper wires or test hooks: 4 signal leads and 1 ground lead to each glove.
- PulseView, sigrok's free capture software.
- Two gloves of the same hardware version, charged.
- A USB cable to the PRIMARY for serial commands and logs.
- v3 only: a fine-tip soldering iron, flux and thin wire (30 AWG wire-wrap
  wire or similar). See [Prepare the boards](#prepare-the-boards).

To set up PulseView:

1. Download PulseView for your OS from sigrok.org (Downloads page) and install
   it. If macOS refuses to open it, Control-click the app and choose Open, or
   allow it under System Settings → Privacy & Security.
2. Plug the analyzer into the computer.
3. In PulseView's device selector, pick the driver
   **fx2lafw (generic driver for FX2 based LAs)**, click **Scan for devices**,
   and select the analyzer. Use PulseView, not Saleae's software: these clones
   aren't licensed for it.

## v2 (Feather nRF52840)

### Build

Build and flash both gloves with the debug pin enabled:

```bash
PLATFORMIO_BUILD_FLAGS=-DSYNC_DEBUG_GPIO_ENABLED=1 pio run -e adafruit_feather_nrf52840 -t upload
```

Record the commit you flashed (`git rev-parse --short HEAD`). Every result
needs it. The debug pin is the Feather's **A0** header pin.

### Wire

| Analyzer | Connect to | Required |
|---|---|---|
| D0 | PRIMARY Feather **A0** | Yes |
| D1 | SECONDARY Feather **A0** | Yes |
| D2 | PRIMARY index-finger LRA, one terminal | For motor timing |
| D3 | SECONDARY index-finger LRA, the **same** terminal | For motor timing |
| GND | **GND on both gloves** (Feather GND pin) | Yes |

Then follow the [wiring rules](#wiring-rules-both-versions).

## v3 (ESP32-S3-WROOM-1U board)

This is the BlueBuzzah v3 board in `BlueBuzzah-Hardware/PCB/v3`, built around
the ESP32-S3-WROOM-1U-N8R8 module.

### Build

Build and flash both gloves with the debug pin enabled:

```bash
PLATFORMIO_BUILD_FLAGS=-DSYNC_DEBUG_GPIO_ENABLED=1 pio run -e pentabuzzer_esp32s3 -t upload
```

Record the commit you flashed (`git rev-parse --short HEAD`). Every result
needs it.

The debug pin is **GPIO40**. It isn't routed anywhere on the board, so it has
to be wired by hand (next section). Never move the debug pin to `A0` on this
board: that's GPIO1, the shared DRV2605 enable line, and toggling it resets
every motor driver.

### Prepare the boards

GPIO40 is **module pad 33**. Its only exposed copper is the end of its
footprint pad, about 0.5 mm, sticking out past the module's metal can. On each
glove:

1. Open the case.
2. Hold the board component side up, with the motor sockets (silkscreen 1–5)
   along the top. The module's top edge has a row of 14 pads.
3. Count from the **right-hand** end, the end nearer the BOOT button (SW2).
   Pad 33 is the **7th** pad.
4. Check it against the KiCad PCB (`PCB/v3/kicad/BlueBuzzah_v3.kicad_pcb`)
   before you solder.
5. Tack a short length of thin wire onto the exposed end of pad 33, using
   flux and a fine tip. Its neighbours, pads 32 (GPIO39) and 34 (GPIO41), are
   also unconnected, so an accidental bridge to either one is harmless.
   Bridges anywhere else are not.
6. Route the wire out of the case and secure it so it can't pull the pad off.

Leave the wires in place for later runs. They do nothing in a normal build.

### Wire

| Analyzer | Connect to | Required |
|---|---|---|
| D0 | PRIMARY **GPIO40** wire | Yes |
| D1 | SECONDARY **GPIO40** wire | Yes |
| D2 | PRIMARY motor socket **1** (index finger), one pin | For motor timing |
| D3 | SECONDARY motor socket **1**, the **same** pin | For motor timing |
| GND | **GND on both gloves:** the `−` pin of the battery socket (silkscreen `-Bat+`) | Yes |

- The motor and battery sockets are through-hole, so you can clip onto their
  solder joints on the back of the board while the motor and battery stay
  plugged in.
- Socket N is finger N−1: socket 1 is the index finger and socket 5 is the
  thumb.
- Each glove needs its battery fitted and charged. The motors run from the
  battery, not USB, so a glove on USB alone triggers on time but never
  vibrates.

Then follow the wiring rules below.

## Wiring rules (both versions)

- **Both gloves' grounds must go to the analyzer's GND.** Without a shared
  ground the readings are meaningless. The PRIMARY's USB cable to the computer
  is fine: it shares the same ground.
- **Probe one LRA terminal only, measured against GND.** Never connect a
  channel across both terminals of a motor. Each terminal swings between 0 V
  and the motor driver's supply, at most battery voltage (4.2 V). Check that's
  within the analyzer's input limit before connecting.
- Use the same finger and the same terminal on both gloves. Finger N is the
  same finger on both gloves.
- Keep the leads short and away from the BLE antenna.
- Leave D2 and D3 off for a debug-pin-only capture. The analysis skips the
  motor measurements when they're missing.

## Capture

- **Channels:** enable D0–D3 (or D0–D1 for the debug pin only), and disable
  D4–D7.
- **Sample rate: 2 MHz.** That's 0.5 µs resolution. The driver's switching
  pulses can be short, and a slower rate may miss the first one. If PulseView
  reports dropped samples, use 1 MHz instead.
- **Sample count: 500 M,** about 4 minutes at 2 MHz. Stop the capture early
  once the session ends.

**Check the first capture before trusting the motor channels.** Zoom in on D2
and D3 between two buzzes. They must be flat. Each buzz should be a dense
block of edges with clean, flat gaps between blocks. If an idle terminal keeps
toggling, the burst starts mean nothing: use the debug pin only. The analysis
script also warns when it sees a burst longer than a second.

## Run the sessions

**Profile.** The firmware trigger offset works with any profile, so a
debug-pin-only baseline stays on the default Regular vCR. Only if you're
capturing D2/D3: the motor channels need a **mirrored** profile, so that the
probed finger buzzes on both gloves at the same moment. Regular vCR isn't
mirrored, so most of its buzzes would go unpaired. In that case:

1. Stop any session, then send `PROFILE_LOAD:2` (Noisy vCR, mirrored) to the
   PRIMARY over serial or from the app. The PRIMARY builds the pattern for
   both gloves, so the SECONDARY needs no change. The PRIMARY reboots and the
   pair reconnects. `TEST` uses the loaded profile.
2. After the runs, restore the default with `PROFILE_LOAD:1` (Regular vCR).

Open the PRIMARY's serial monitor (`pio device monitor`) for both runs. Note
the negotiated connection interval each time a link connects:

- v2 prints `[BLE] Link params: ... interval=<n>ms`.
- v3 prints `[BLE] <link> connection interval: <n>ms`.

**Run 1: phone disconnected.**

1. Make sure no phone is connected to the PRIMARY.
2. Click **Run** in PulseView.
3. Send `TEST` to the PRIMARY. It runs a fixed 2-minute session
   (`TEST_DURATION_SEC`). A linked SECONDARY refuses `TEST`, so send it to the
   PRIMARY only.
4. Stop the capture after the session ends.

**Run 2: phone connected.**

1. Connect BuzzahBuddy, start a session, and leave the app on the session screen.
2. Click **Run** in PulseView and let it capture for at least 3 minutes.
3. Stop the capture, then stop the session.

**Save each run twice:** File → Save (`.sr`, keeps the raw capture), and
File → Export → Value Change Dump (`.vcd`, used by the analysis).

## Analyse

```bash
python3 scripts/sync_skew.py run1.vcd
```

The script pairs each PRIMARY edge with the nearest SECONDARY edge within
20 ms. For each measurement it prints the number of pairs, the mean, the
standard deviation, the mean and P95 of the gap's size, and the worst case.

- **Bilateral offsets are SECONDARY minus PRIMARY.** A negative mean means the
  SECONDARY fires first.
- Each run needs **at least 300 firmware trigger pairs**. If `TEST` gives fewer,
  capture a second run and report both.
- Only the probed finger's buzzes count toward the motor figures, so they have
  about a quarter as many pairs as the debug pin on v2, and a fifth on v3.
- Unpaired edges are activations one glove dropped, for example late events.
- Options: `--primary`, `--secondary`, `--primary-motor` and
  `--secondary-motor` rename the channels (defaults D0–D3). `--quiet-ms`
  (default 4) is the gap that ends a motor burst. It must be longer than any
  pause inside one buzz and shorter than the 5 ms minimum gap between
  consecutive buzzes of any finger. The default suits the 250 Hz preset
  profiles. If one buzz comes out as several pairs, raise it.

## Acceptance gates

Measured on the firmware trigger offset, on either hardware version. "Avg
skew" is the script's `mean |offset|`, and "P95 skew" is its `P95 |offset|`.

| Milestone | Avg skew | P95 skew |
|---|---|---|
| After the timebase + TX-stamp + filter work | < 300 µs | < 1 ms |
| After connection-anchor timestamping        | < 50 µs  | < 200 µs |

Connection-anchor timestamping (`radio_anchor.cpp`) exists only on v2. On v3
it's an inert stub, so only the first gate applies there.

## Soft metrics

Optionally capture the firmware's own view alongside a run. Enable
latencyMetrics and record its 30 s reports (execution drift per device), plus
the `[SYNC] RTT=...` lines, printed by the PRIMARY only, with debug mode on
(send `DEBUG:1`).

## Soak test

Run an 8-hour session at about 2 m separation. Check:
- No sync resets: `[SYNC] Clock sync reset` never appears.
- No keepalive timeouts.
- No late-event spikes in latencyMetrics.
- Skew stays stable from start to end, showing the offset EMA tracks drift.

## Record results

Add a dated section to `docs/TIMING_BASELINE.md` for each set of runs, with:
- the hardware version (v2 or v3), the firmware commit and the profile measured
- whether a phone was connected, and the connection intervals
- the script's output for each run
