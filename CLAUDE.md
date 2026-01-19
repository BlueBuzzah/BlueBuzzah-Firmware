# BlueBuzzah Firmware

Arduino C++ firmware for vibrotactile haptic feedback gloves (Adafruit Feather nRF52840 Express).

## Quick Reference

| Item           | Value                        |
| -------------- | ---------------------------- |
| **Board**      | Adafruit Feather nRF52840    |
| **Framework**  | Arduino via PlatformIO       |
| **C++**        | C++20 (gnu++20)              |
| **Build**      | `pio run -e adafruit_feather_nrf52840` |
| **Flash**      | `pio run -e adafruit_feather_nrf52840 -t upload` |
| **Test**       | `pio test -e native`         |
| **Coverage**   | `pio test -e native_coverage` (macOS) / `native_coverage_gcc` (Linux) |
| **Monitor**    | `pio device monitor` (115200)|
| **Deploy**     | `python deploy.py` (interactive dual-glove deployment) |

## Module Map

| Module               | Purpose                               |
| -------------------- | ------------------------------------- |
| `main.cpp`           | setup()/loop() entry point            |
| `hardware.cpp`       | Motors, LED, battery, I2C mux         |
| `ble_manager.cpp`    | BLE stack, UART service               |
| `therapy_engine.cpp` | Pattern generation, motor scheduling  |
| `sync_protocol.cpp`  | PRIMARY<->SECONDARY glove sync        |
| `state_machine.cpp`  | 11-state therapy FSM                  |
| `menu_controller.cpp`| Phone command routing                 |
| `profile_manager.cpp`| Therapy profiles (LittleFS)           |
| `latency_metrics.cpp`| Runtime latency measurement, RTT tracking, sync quality reporting |
| `activation_queue.cpp`| FreeRTOS motor event scheduling with paired activate/deactivate |
| `deferred_queue.cpp` | ISR-safe work queue for blocking operations |
| `motor_event_buffer.cpp`| Lock-free staging buffer (BLE callbacks → main loop) |
| `config.h`           | Hardware constants, BLE parameters, tuning values |
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
| `docs/BOOT_SEQUENCE.md`            | Startup initialization flow    |
| `docs/CALIBRATION_GUIDE.md`        | Motor/sensor calibration procedures |
| `docs/LATENCY_METRICS.md`          | Performance measurement guide  |
| `docs/TESTING.md`                  | Test framework and patterns    |
| `docs/THERAPY_ENGINE.md`           | Pattern generation internals   |
| `docs/THERAPY_SESSION_FLOW.md`     | Session state machine flow     |
| `docs/TIMING_BASELINE.md`          | Timing analysis and profiling  |
| `docs/ORIGINAL_PARAMETERS.md`      | Legacy parameter reference     |
