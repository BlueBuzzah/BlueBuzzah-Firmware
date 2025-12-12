---
description: Core project context for BlueBuzzah firmware
---
# BlueBuzzah Firmware Overview

Arduino C++ firmware for vibrotactile haptic feedback gloves with BLE connectivity. Supports dual-glove bilateral synchronization (PRIMARY/SECONDARY roles).

## Hardware Target

| Specification    | Value                             |
| ---------------- | --------------------------------- |
| **Board**        | Adafruit Feather nRF52840 Express |
| **MCU**          | nRF52840 (ARM Cortex-M4F @ 64MHz) |
| **RAM**          | 256 KB (~200KB after BLE stack)   |
| **Flash**        | 1 MB (~800KB for user code)       |
| **Framework**    | Arduino via PlatformIO            |
| **C++ Standard** | C++20 (gnu++20)                   |

## Project Structure

```
src/                        # Implementation files
├── main.cpp                # setup()/loop() entry point
├── hardware.cpp            # Motors, LED, battery, I2C mux
├── ble_manager.cpp         # BLE stack, connections, UART service
├── therapy_engine.cpp      # Pattern generation, motor scheduling
├── sync_protocol.cpp       # PRIMARY<->SECONDARY sync messages
├── state_machine.cpp       # 11-state therapy FSM
├── menu_controller.cpp     # Phone command routing
└── profile_manager.cpp     # Therapy profiles (LittleFS)
include/                    # Header files
test/                       # PlatformIO unit tests (Unity)
docs/                       # Technical documentation
```

## Key Libraries

| Library           | Purpose                 | Include                 |
| ----------------- | ----------------------- | ----------------------- |
| Bluefruit         | BLE stack, UART service | `<bluefruit.h>`         |
| Adafruit DRV2605  | Haptic motor driver     | `<Adafruit_DRV2605.h>`  |
| Adafruit NeoPixel | RGB status LED          | `<Adafruit_NeoPixel.h>` |
| TCA9548A          | I2C multiplexer         | `<TCA9548A.h>`          |
| Adafruit LittleFS | Settings persistence    | `<Adafruit_LittleFS.h>` |

## Build Commands

```bash
pio run                      # Compile
pio run -t upload            # Flash firmware
pio device monitor           # Serial monitor (115200 baud)
pio test -e native           # Run unit tests
pio test -e native_coverage  # Tests with coverage
```

## Documentation

Key docs in `docs/`:
- `ARDUINO_FIRMWARE_ARCHITECTURE.md` - Architecture source of truth
- `BLE_PROTOCOL.md` - Mobile app command protocol
- `COMMAND_REFERENCE.md` - All 18 BLE commands
- `SYNCHRONIZATION_PROTOCOL.md` - PRIMARY<->SECONDARY coordination
