# BlueBuzzah Firmware

Arduino C++ firmware for vibrotactile haptic feedback gloves (Adafruit Feather nRF52840 Express).

## Quick Reference

| Item           | Value                        |
| -------------- | ---------------------------- |
| **Board**      | Adafruit Feather nRF52840    |
| **Framework**  | Arduino via PlatformIO       |
| **C++**        | C++20 (gnu++20)              |
| **Build**      | `pio run`                    |
| **Flash**      | `pio run -t upload`          |
| **Test**       | `pio test -e native`         |
| **Monitor**    | `pio device monitor` (115200)|

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
| `docs/ARDUINO_FIRMWARE_ARCHITECTURE.md` | Architecture (source of truth) |
| `docs/BLE_PROTOCOL.md`             | Mobile app command protocol    |
| `docs/COMMAND_REFERENCE.md`        | All 18 BLE commands            |
| `docs/SYNCHRONIZATION_PROTOCOL.md` | PRIMARY<->SECONDARY protocol   |
