# BlueBuzzah Firmware

Arduino C++ firmware for vibrotactile haptic feedback gloves. One codebase, two boards:

| Board | Env | MCU | Motors | BLE stack |
| ----- | --- | --- | ------ | --------- |
| BlueBuzzah (Adafruit Feather nRF52840 Express) | `adafruit_feather_nrf52840` | nRF52840 | 4 | Bluefruit/SoftDevice |
| v3 (ESP32-S3-WROOM-1U-N8R8) | `pentabuzzer_esp32s3` | ESP32-S3 | 5 (adds thumb) | NimBLE-Arduino 2.x |

The board is selected by a build macro (`BOARD_BLUEBUZZAH_NRF52` / `BOARD_PENTABUZZER_ESP32S3`); board-specific values live in `board_config.h`, platform primitives in `platform.h`.

## Not a medical device

BlueBuzzah is a volunteer-driven, open-source DIY project, not a medical or therapeutic device. Nothing written here (serial and BLE strings, docs, README, wiki, release notes, comments people read) may say or imply that it is a medical device, or that it is cleared, approved or authorized by the FDA or any other regulator. Regulatory status appears only in the verbatim legal disclaimer. Code identifiers, firmware commands, protocol fields, serial markers and file names are not copy and stay as they are. Full rule: "Not a medical device" in the workspace `CLAUDE.md` (`../../CLAUDE.md`).

## Quick Reference

| Item           | Value                        |
| -------------- | ---------------------------- |
| **Framework**  | Arduino via PlatformIO       |
| **C++**        | C++20 (gnu++20)              |
| **Build (nRF)**| `pio run -e adafruit_feather_nrf52840` |
| **Build (v3)**| `pio run -e pentabuzzer_esp32s3` |
| **Flash**      | `pio run -e <env> -t upload` |
| **Test**       | `pio test -e native` (4-actuator) / `pio test -e native_penta` (5-actuator) |
| **Test (one suite)** | `pio test -e native -f test_sync_protocol` |
| **Coverage**   | `pio test -e native_coverage` (macOS) / `native_coverage_gcc` (Linux) |
| **Monitor**    | `pio device monitor` (115200)|
| **Deploy**     | `python deploy.py` (interactive dual-glove deployment, auto-detects board) |

## Module Map

| Module               | Purpose                               |
| -------------------- | ------------------------------------- |
| `main.cpp`           | setup()/loop() entry point            |
| `hardware.cpp`       | Motors, LED, battery, I2C mux         |
| `ble_manager_nrf52.cpp` | Bluefruit BLE backend (nRF52840)   |
| `ble_manager_esp32.cpp` | NimBLE BLE backend (ESP32-S3); same `BLEManager` API |
| `therapy_engine.cpp` | Pattern generation, motor scheduling  |
| `sync_protocol.cpp`  | PRIMARY<->SECONDARY glove sync        |
| `state_machine.cpp`  | 11-state therapy FSM                  |
| `menu_controller.cpp`| Phone command routing                 |
| `profile_manager.cpp`| Therapy profiles (via `fs_backend`)   |
| `vbat_estimator.cpp` | Battery voltage from DRV2605 VBAT register (0x21) — median-of-burst + EMA; used where there is no battery ADC |
| `fs_backend_*.cpp`   | Filesystem shim: InternalFS (nRF) / LittleFS (ESP32) / in-memory mock (native) |
| `power_controller_*.cpp` | v3 power switch + deep sleep before the hardware cuts 3.3 V; no-op on nRF |
| `latency_metrics.cpp`| Runtime latency measurement, RTT tracking, sync quality reporting |
| `hires_clock.cpp`    | 1MHz timebase for sync timestamps: NRF_TIMER4 + HFXO on nRF (start after SoftDevice enable), `esp_timer` on ESP32 |
| `radio_anchor.cpp`   | Hardware timestamps of BLE radio events via SoftDevice radio notifications (sync anchoring); inert stubs on v3 and native |
| `activation_queue.cpp`| FreeRTOS motor event scheduling with paired activate/deactivate |
| `deferred_queue.cpp` | ISR-safe work queue for blocking operations |
| `motor_event_buffer.cpp`| Lock-free staging buffer (BLE callbacks → main loop) |
| `board_config.h`     | Per-board pins, `MAX_ACTUATORS`, battery availability |
| `platform.h`         | Critical sections, memory barrier, system reset, RTOS headers |
| `config.h`           | Shared constants, BLE parameters, tuning values, `PARAM_MIN_*`/`PARAM_MAX_*` therapy bounds |
| `types.h`            | Enums, packed structs, macrocycle format definitions |
| `internal_messages.h`| `INTERNAL_MESSAGES[]` — the single definition site for device-to-device prefixes the menu must not treat as user commands |
| `motor_check.h`      | Boot motor check tokens (`OK`/`OPEN`/`NODRIVER`/`UNKNOWN`), their wire format, the per-finger warning rule, and the fingers patterns use |
| `motor_check_info.h` | `INFO` `MOTP`/`MOTS` emission, used by `MenuController::handleInfo`, tested directly by `test_motor_check_info` |
| `drive_math.h` | Battery percent curve, amplitude→RTP and LRA period calculations; header-only so host tests cover them |

## Cross-Repo Contracts

- **`INTERNAL_MESSAGES[]` is a trapdoor.** Any command *not* matched by a prefix in `include/internal_messages.h` reaches `MenuController::handleCommand()`, which is how the desktop Updater (over USB serial) and the mobile app reach the menu. Adding `INFO`, `PROFILE_LOAD`, `PROFILE_GET`, or `PROFILE_CUSTOM` there silently severs the Updater with no compile error — `test_menu_commands` guards exactly this by running the real `MenuController::isInternalMessage`.
- **Serial response markers are a wire format.** `[MENU-TX]` (`menu_controller.cpp`), plus `[CONFIG]`, `[ERROR]` and the `"Role set to"` / `"Profile set to"` strings from `main.cpp`, are parsed verbatim by BlueBuzzah-Updater. Rewording either side breaks the other. `[READY]` is printed as the last line of `setup()` and after the `DEVICE NOT CONFIGURED` banner; the Updater's boot drain ends on it before sending settings (falling back to a silence timeout for firmware without it), so keep it last. The Updater also scans for `[SETTING]`, which this firmware no longer emits — it treats its absence as success, so don't reintroduce it expecting a behavior change.
- **Motor check tokens and `INFO` `MOTP`/`MOTS`.** The tokens in `motor_check.h` and the keys `motor_check_info.h` emits are parsed by BuzzahBuddy (`Models/MotorCheck.cs`, `Models/GloveDeviceInfo.cs`), which warns by the same per-finger rule as `isMotorCheckProblem()`. Renaming a token or key, or changing the rule, means updating the app.
- **Firmware is the sole validator of therapy parameters.** The `PARAM_*` bounds in `config.h` are mirrored into the Updater (`src/lib/therapy-bounds.ts`) for input constraint only; that copy can never widen what the device accepts. Changing a bound here means updating the mirror.
- **v3 package manifest**: `scripts/package_penta.py` produces the schema `esp/manifest.rs` in the Updater parses — keep the two in sync on any field change.
- **Release asset names are a contract.** `.github/workflows/release.yml` (runs when a GitHub release is published) renames builds to `BlueBuzzah-Firmware-<tag>-<sha7>.zip` (nRF DFU) and `BlueBuzzah-Firmware-v3-<tag>-<sha7>.zip`; the Updater picks the board from that name.
- **BLE protocol spec lives in the wiki** (`../BlueBuzzah-Firmware.wiki/BLE-Protocol.md`). Any change to a command, response, error string, or parameter range updates that page and BuzzahBuddy's `MockBluetoothService` in the same PR.

## Testing

- Native envs compile `src/` via `test_build_src = true` minus a `build_src_filter` exclusion list. Suites for excluded modules pull the source in directly (`#include "../../src/sync_protocol.cpp"`): `test_sync_protocol`, `test_state_machine`, `test_profile_manager`, `test_therapy_engine`, `test_activation_queue`, `test_deferred_queue`, and `test_menu_commands` (via `menu_harness.h`: sync_protocol, therapy_engine, state_machine, profile_manager, menu_controller). Including a non-excluded `.cpp` this way gives duplicate symbols.
- **`test_menu_commands` compiles the real `menu_controller.cpp`** with fakes for `hardware.h` and `ble_manager.h`. It also guards `INTERNAL_MESSAGES[]` routing. `test_motor_check_info` tests the `MOTP`/`MOTS` lines.
- The exclusion list lives once, in `env:native`; `native_penta`, `native_coverage` and `native_coverage_gcc` reuse it. A new platform-specific `.cpp` must be excluded there, plus the other board's device env.
- Mocks (`test/mocks/src/Arduino.h`): `mockAdvanceMicros()`, `mockEnableTimeAutoIncrement()`, `MockSerial` (`captured()`/`clearCaptured()`). `test/mocks/src/freertos_fake.h` stands in for FreeRTOS in `test_activation_queue`. The in-memory filesystem is `src/fs_backend_mock.cpp`, which the native envs build.
- CI (`test.yml`) runs `native_coverage_gcc` (4-actuator) plus all of `native_penta` (5-actuator), and warns (without failing) under 90% line coverage (`scripts/coverage_table.sh`). Run `native_penta` locally for anything touching `MAX_ACTUATORS`.

## Gotchas

- **nRF-only code is gated on `NRF52840_XXAA`, not `BOARD_BLUEBUZZAH_NRF52`** (`radio_anchor.cpp`, `hires_clock.cpp`). Native envs define `BOARD_BLUEBUZZAH_NRF52` too; `NATIVE_TEST_BUILD` selects the no-op branches in `platform.h`.
- **BLE callback context differs per board**: Bluefruit's 3KB "Callback" task on nRF (radio notifications in `radio_anchor.cpp` are true ISRs), NimBLE host task (core 0) on ESP32. No I2C in callbacks — hand off through `MotorEventBuffer` (single-producer/single-consumer), `DeferredQueue` (more than one producer: `enqueue()` takes a critical section; `loop()` is the only consumer) or a semaphore (`safetyShutdownSema`). `MenuController::handleCommand()` never runs there: `onBLEMessage` copies menu commands, the `TEST`/`STOP` shortcuts and a SECONDARY's `LED_OFF_SYNC`/`DEBUG_SYNC` (flash saves) with `enqueueMenuCommand()`, and `loop()` runs them via `processPendingMenuCommand()` (see MENU COMMAND DEFERRAL in `main.cpp`). Everything else from the other glove still runs inline.
- **`CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192` (v3) predates that deferral**: it was sized while menu commands ran in the NimBLE host task and hasn't been re-measured since.
- **`loop()` runs under the 5 s task watchdog on v3** (enabled last in `setup()`). Wrap loop-context work that can legitimately block 5 s or more in a `LoopWatchdogPause` (`platform.h`), as `MOTOR_DIAG`, `MOTOR_PRESENT` and the SECONDARY connect do.
- **FreeRTOS stack units differ**: words on nRF, bytes on ESP32 (`MOTOR_TASK_STACK`).
- **`SYNC_DEBUG_GPIO_PIN` is per board** (`SYNC_DEBUG_GPIO_PIN_OVERRIDE` in `board_config.h`): `A0` on nRF, GPIO40 (unrouted WROOM pad 33, hand-wired for bench tests) on v3. Never use `A0` on v3: that's GPIO1, the shared DRV2605 enable line, and toggling it wipes every driver's registers.
- **`BLE_USE_2M_PHY` is tested with `#ifdef`**: `-DBLE_USE_2M_PHY=0` does not disable 2M PHY. `DEBUG_ENABLED`, `SKIP_BOOT_SEQUENCE`, `SYNC_DEBUG_GPIO_ENABLED`, `SYNC_ANCHOR_TIMESTAMPING_ENABLED` are `#ifndef` defaults in `config.h` and are safe to override with `-D`.
- **The nRF env is built with `-Ofast`** (added by the Adafruit core; implies finite-math-only), so `isnan`/`isfinite` and comparison-based NaN checks are optimised away there. Float validity checks must test the bits, as `isFiniteFloat()` in `profile_manager.cpp` does.
- **`FIRMWARE_VERSION` comes from the release tag at build time.** `release.yml` strips the leading `v` and passes it as `-DFIRMWARE_VERSION` (overriding the `#ifndef` default in `config.h`) and as `BLUEBUZZAH_VERSION` for the v3 manifest. Local builds and the tag's source tree report the `config.h` default.
- **`partitions_penta.csv` has a single `ota_0`**: no A/B OTA on v3.
- **Worktrees go only in `.claude/worktrees/<name>`** inside this repo (gitignored), never beside it in the workspace's `projects/` or anywhere else.

## Key Documentation

| Document                           | Purpose                        |
| ---------------------------------- | ------------------------------ |
| `docs/ARCHITECTURE.md`             | System architecture (source of truth) |
| `docs/API_REFERENCE.md`            | Module API documentation       |
| `docs/SYNCHRONIZATION_PROTOCOL.md` | PRIMARY↔SECONDARY protocol (wiki `Synchronization-Protocol.md` is the published copy) |
| `docs/SYNC_VALIDATION.md`          | GPIO-based bilateral sync validation (`SYNC_DEBUG_GPIO_ENABLED`) |
| `docs/BLE_PROTOCOL.md`             | Pointer to the BLE protocol spec in the wiki |
| `docs/BOOT_SEQUENCE.md`            | Startup initialization flow    |
| `docs/CALIBRATION_GUIDE.md`        | Motor/sensor calibration procedures |
| `docs/LATENCY_METRICS.md`          | Performance measurement guide  |
| `docs/TESTING.md`                  | Test framework and patterns    |
| `docs/THERAPY_ENGINE.md`           | Pattern generation internals   |
| `docs/THERAPY_SESSION_FLOW.md`     | Session state machine flow     |
| `docs/TIMING_BASELINE.md`          | Timing analysis and profiling  |
| `docs/ORIGINAL_PARAMETERS.md`      | Legacy parameter reference     |
