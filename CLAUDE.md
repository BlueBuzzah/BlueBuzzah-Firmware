# BlueBuzzah Firmware

Arduino C++ firmware for vibrotactile haptic feedback gloves. One codebase, two boards:

| Board | Env | MCU | Motors | BLE stack |
| ----- | --- | --- | ------ | --------- |
| BlueBuzzah (Adafruit Feather nRF52840 Express) | `adafruit_feather_nrf52840` | nRF52840 | 4 | Bluefruit/SoftDevice |
| PentaBuzzer (Seeed XIAO ESP32-S3) | `pentabuzzer_esp32s3` | ESP32-S3 | 5 (adds thumb) | NimBLE-Arduino 2.x |

The board is selected by a build macro (`BOARD_BLUEBUZZAH_NRF52` / `BOARD_PENTABUZZER_ESP32S3`); board-specific values live in `board_config.h`, platform primitives in `platform.h`.

## Quick Reference

| Item           | Value                        |
| -------------- | ---------------------------- |
| **Framework**  | Arduino via PlatformIO       |
| **C++**        | C++20 (gnu++20)              |
| **Build (nRF)**| `pio run -e adafruit_feather_nrf52840` |
| **Build (Penta)**| `pio run -e pentabuzzer_esp32s3` |
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
| `fs_backend_*.cpp`   | Filesystem shim: InternalFS (nRF) / LittleFS (ESP32) / in-memory mock (native) |
| `power_controller_*.cpp` | PentaBuzzer power switch + deep sleep; no-op on nRF |
| `latency_metrics.cpp`| Runtime latency measurement, RTT tracking, sync quality reporting |
| `hires_clock.cpp`    | 1MHz hardware timebase (NRF_TIMER4 + HFXO) for sync timestamps; start after SoftDevice enable |
| `radio_anchor.cpp`   | Hardware timestamps of BLE radio events via SoftDevice radio notifications (sync anchoring); inert stubs on native |
| `activation_queue.cpp`| FreeRTOS motor event scheduling with paired activate/deactivate |
| `deferred_queue.cpp` | ISR-safe work queue for blocking operations |
| `motor_event_buffer.cpp`| Lock-free staging buffer (BLE callbacks → main loop) |
| `board_config.h`     | Per-board pins, `MAX_ACTUATORS`, battery availability |
| `platform.h`         | Critical sections, memory barrier, system reset, RTOS headers |
| `config.h`           | Shared constants, BLE parameters, tuning values |
| `types.h`            | Enums, packed structs, macrocycle format definitions |

## Configuration

Detailed guidance is organized in modular rule files:

| Rule File                          | Scope                          |
| ---------------------------------- | ------------------------------ |
| `.claude/rules/project-overview.md`| Project context, libs, docs    |
| `.claude/rules/cpp-patterns.md`    | C++20 patterns (*.cpp, *.h)    |
| `.claude/rules/hardware.md`        | I2C, mux, DRV2605 patterns     |
| `.claude/rules/code-standards.md`  | Prohibited patterns, standards |

## Embedded Skill

The `adafruit-arduino-cpp` skill provides deep embedded development patterns:
- Memory management (pre-allocation, fixed arrays)
- Non-blocking timing (`millis()`-based state machines)
- BLE callbacks and message framing
- I2C multiplexer management
- Anti-patterns to avoid

Activated automatically for `.cpp`/`.h` files or when discussing BLE, timing, or embedded topics.

## Key Documentation

| Document                           | Purpose                        |
| ---------------------------------- | ------------------------------ |
| `docs/ARCHITECTURE.md`             | System architecture (source of truth) |
| `docs/API_REFERENCE.md`            | Module API documentation       |
| `docs/BLE_PROTOCOL.md`             | Mobile app command protocol    |
| `docs/SYNCHRONIZATION_PROTOCOL.md` | PRIMARY↔SECONDARY protocol     |
| `docs/SYNC_VALIDATION.md`          | GPIO-based bilateral sync validation (`SYNC_DEBUG_GPIO_ENABLED`) |
| `docs/BOOT_SEQUENCE.md`            | Startup initialization flow    |
| `docs/CALIBRATION_GUIDE.md`        | Motor/sensor calibration procedures |
| `docs/LATENCY_METRICS.md`          | Performance measurement guide  |
| `docs/TESTING.md`                  | Test framework and patterns    |
| `docs/THERAPY_ENGINE.md`           | Pattern generation internals   |
| `docs/THERAPY_SESSION_FLOW.md`     | Session state machine flow     |
| `docs/TIMING_BASELINE.md`          | Timing analysis and profiling  |
| `docs/ORIGINAL_PARAMETERS.md`      | Legacy parameter reference     |
