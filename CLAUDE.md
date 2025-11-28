# BlueBuzzah Firmware - Arduino/PlatformIO Development Guide

## Quick Reference

| Specification  | Value                             | Notes                          |
| -------------- | --------------------------------- | ------------------------------ |
| **Board**      | Adafruit Feather nRF52840 Express | Nordic SoC with BLE 5.0        |
| **MCU**        | nRF52840                          | ARM Cortex-M4F @ 64MHz         |
| **RAM**        | 256 KB                            | ~240KB available after BLE     |
| **Flash**      | 1 MB                              | ~800KB for user code           |
| **Framework**  | Arduino                           | Via PlatformIO                 |
| **Platform**   | nordicnrf52                       | Adafruit nRF52 BSP             |
| **Filesystem** | LittleFS                          | Internal settings persistence  |

---

## Project Context

Arduino C++ firmware for vibrotactile haptic feedback gloves with BLE connectivity. Supports dual-glove bilateral synchronization (PRIMARY/SECONDARY roles).

---

## Project Structure

```
/
├── platformio.ini              # Build configuration
├── src/                        # C++ source files
│   ├── main.cpp                # setup() / loop() entry point
│   ├── hardware.cpp            # DRV2605, NeoPixel, battery, I2C mux
│   ├── ble_manager.cpp         # BLE stack, connections, UART service
│   ├── therapy_engine.cpp      # Pattern generation, motor scheduling
│   ├── sync_protocol.cpp       # PRIMARY<->SECONDARY sync messages
│   ├── state_machine.cpp       # 11-state therapy FSM
│   ├── menu_controller.cpp     # Phone command routing
│   └── profile_manager.cpp     # Therapy profile loading/saving
├── include/                    # Header files
│   ├── config.h                # Pin definitions, constants
│   ├── types.h                 # Enums, structs, device roles
│   └── [module].h              # Declarations for each .cpp
└── BlueBuzzah-CircuitPython/   # Legacy reference (not active)
```

---

## Build Commands

```bash
pio run                    # Compile
pio run -t upload          # Flash firmware to device
pio device monitor         # Open serial monitor (115200 baud)
pio run -t clean           # Clean build artifacts
```

For PlatformIO functionality, refer to the official documentation:
https://docs.platformio.org/en/latest/home/index.html

---

## Key Libraries

| Library                   | Purpose                        | Include              |
| ------------------------- | ------------------------------ | -------------------- |
| Bluefruit (built-in)      | BLE stack, UART service        | `<bluefruit.h>`      |
| Adafruit DRV2605          | Haptic motor driver            | `<Adafruit_DRV2605.h>` |
| Adafruit NeoPixel         | RGB status LED                 | `<Adafruit_NeoPixel.h>` |
| TCA9548A                  | I2C multiplexer                | `<TCA9548A.h>`       |
| Adafruit LittleFS         | Internal settings storage      | `<Adafruit_LittleFS.h>` |

---

## Hardware Architecture

```
nRF52840 MCU
├── NeoPixel LED (status indicator)
├── Battery ADC (voltage monitoring)
└── I2C Bus @ 400kHz
    └── TCA9548A Multiplexer (0x70)
        ├── Ch0: DRV2605 - Thumb
        ├── Ch1: DRV2605 - Index
        ├── Ch2: DRV2605 - Middle
        ├── Ch3: DRV2605 - Ring
        └── Ch4: DRV2605 - Pinky
```

---

## Module Responsibilities

| Module             | Role                                                    |
| ------------------ | ------------------------------------------------------- |
| `main.cpp`         | Arduino entry point, orchestrates initialization        |
| `hardware.cpp`     | Low-level hardware: motors, LED, battery, I2C mux       |
| `ble_manager.cpp`  | BLE advertising, connections, UART RX/TX                |
| `therapy_engine.cpp` | Pattern algorithms (random, sequential, mirrored)     |
| `sync_protocol.cpp` | Serialize/deserialize sync commands between gloves    |
| `state_machine.cpp` | FSM: IDLE→READY→RUNNING→PAUSED→STOPPING, etc.         |
| `menu_controller.cpp` | Parse phone commands, route to appropriate handlers |
| `profile_manager.cpp` | Load/save therapy profiles from LittleFS            |

---

## Code Patterns

### Timing
- Use `millis()` for millisecond timing
- Use `micros()` for microsecond precision
- Avoid `delay()` in main loop (blocks BLE callbacks)

### BLE
- Event-driven via callbacks (connect, disconnect, RX)
- Messages delimited by EOT character (`0x04`)
- PRIMARY: 2 peripheral connections (phone + SECONDARY)
- SECONDARY: 1 central connection (to PRIMARY)

### Memory
- Pre-allocate buffers at startup (avoid heap fragmentation)
- Use `static` or global for persistent data
- 256KB RAM available - much more headroom than CircuitPython

### I2C Multiplexer
- Select channel before each DRV2605 operation
- Motors share address 0x5A, differentiated by mux channel

---

## Development Notes

- Serial output at 115200 baud for debugging
- BLE service uses Nordic UART Service (NUS)
- Device role configured via serial: `SET_ROLE:PRIMARY` or `SET_ROLE:SECONDARY`
- Build flags: `-DCFG_DEBUG=0 -DNRF52840_XXAA -w`

---

## Code Standards

### Prohibited in Source Code

**NEVER include planning artifacts in source code:**

- Work package identifiers (WP1, WP2, etc.)
- Phase numbers (Phase 1, Phase 2, etc.)
- ADR references
- Sprint/iteration identifiers
- Migration plan references
- "TODO: implement in WP3" style comments

**Rationale:** Source code should be self-documenting and timeless. Planning artifacts belong in documentation (docs/), not in code comments, log messages, or file headers.

### Acceptable Practices

- Version numbers in headers (e.g., `@version 2.0.0`)
- Feature descriptions without planning context
- Technical TODO comments (e.g., `// TODO: optimize buffer allocation`)
- References to GitHub issues (e.g., `// Fixes #123`)
