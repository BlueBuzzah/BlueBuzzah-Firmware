---
description: Core project context for BlueBuzzah firmware
---
# BlueBuzzah Firmware Overview

Arduino C++ firmware for vibrotactile haptic feedback gloves with BLE connectivity. Supports dual-glove bilateral synchronization (PRIMARY/SECONDARY roles). One codebase builds for two boards, selected by build macro.

## Hardware Targets

| Specification    | BlueBuzzah                        | PentaBuzzer                        |
| ---------------- | --------------------------------- | ---------------------------------- |
| **Board**        | Adafruit Feather nRF52840 Express | Seeed XIAO ESP32-S3                |
| **Env / macro**  | `adafruit_feather_nrf52840` / `BOARD_BLUEBUZZAH_NRF52` | `pentabuzzer_esp32s3` / `BOARD_PENTABUZZER_ESP32S3` |
| **MCU**          | nRF52840 (ARM Cortex-M4F @ 64MHz) | ESP32-S3 (Xtensa LX7 dual-core @ 240MHz) |
| **Motors**       | 4 (index-pinky)                   | 5 (adds thumb on mux channel 4)    |
| **BLE stack**    | Bluefruit / SoftDevice            | NimBLE-Arduino 2.x                 |
| **Flash**        | 1 MB (~800KB for user code)       | 8 MB (dedicated LittleFS partition) |
| **Battery sense**| VBAT divider on ADC               | DRV2605 VBAT register (0x21) over I2C |
| **Power switch** | none                              | slide switch + deep sleep          |
| **Framework**    | Arduino via PlatformIO            | Arduino via PlatformIO (pioarduino, IDF 5) |
| **C++ Standard** | C++20 (gnu++20)                   | C++20 (gnu++20)                    |

## Project Structure

```
src/                        # Implementation files
├── main.cpp                # setup()/loop() entry point
├── hardware.cpp            # Motors, LED, battery, I2C mux
├── ble_manager_nrf52.cpp   # Bluefruit BLE backend (nRF)
├── ble_manager_esp32.cpp   # NimBLE BLE backend (ESP32-S3)
├── fs_backend_*.cpp        # Filesystem shim (nrf52 / esp32 / native mock)
├── power_controller_*.cpp  # Power switch + deep sleep (Penta) or no-op (nRF)
├── therapy_engine.cpp      # Pattern generation, motor scheduling
├── sync_protocol.cpp       # PRIMARY<->SECONDARY sync messages
├── state_machine.cpp       # 11-state therapy FSM
├── menu_controller.cpp     # Phone command routing
└── profile_manager.cpp     # Therapy profiles (via fs_backend)
include/                    # Header files (board_config.h, platform.h, ...)
test/                       # PlatformIO unit tests (Unity)
docs/                       # Technical documentation
```

## Key Libraries

| Library           | Purpose                 | Board      | Include                 |
| ----------------- | ----------------------- | ---------- | ----------------------- |
| Bluefruit         | BLE stack, UART service | nRF only   | `<bluefruit.h>`         |
| NimBLE-Arduino    | BLE stack, NUS service  | ESP32 only | `<NimBLEDevice.h>`      |
| Adafruit DRV2605  | Haptic motor driver     | both       | `<Adafruit_DRV2605.h>`  |
| Adafruit NeoPixel | RGB status LED          | both       | `<Adafruit_NeoPixel.h>` |
| TCA9548A          | I2C multiplexer         | both       | `<TCA9548A.h>`          |
| Adafruit LittleFS | Settings persistence    | nRF only   | via `fs_backend.h`      |
| arduino-esp32 LittleFS | Settings persistence | ESP32 only | via `fs_backend.h`   |

## Build Commands

```bash
pio run -e adafruit_feather_nrf52840   # Compile (BlueBuzzah)
pio run -e pentabuzzer_esp32s3         # Compile (PentaBuzzer)
pio run -e <env> -t upload             # Flash firmware
pio device monitor                     # Serial monitor (115200 baud)
pio test -e native                     # Run unit tests (4-actuator config)
pio test -e native_penta               # Run unit tests (5-actuator config)
pio test -e native_coverage            # Tests with coverage
```

## Documentation

Key docs in `docs/`:
- `ARCHITECTURE.md` - Architecture source of truth
- `BLE_PROTOCOL.md` - Mobile app command protocol (all BLE commands)
- `SYNCHRONIZATION_PROTOCOL.md` - PRIMARY<->SECONDARY coordination
