# BlueBuzzah Firmware

[![BlueBuzzah Firmware](https://img.shields.io/github/v/release/BlueBuzzah/BlueBuzzah-Firmware?label=version)](https://github.com/BlueBuzzah/BlueBuzzah-Firmware/releases/latest)
[![Test Suite Status](https://img.shields.io/github/actions/workflow/status/BlueBuzzah/BlueBuzzah-Firmware/test.yml?branch=main&label=tests)](https://github.com/BlueBuzzah/BlueBuzzah-Firmware/actions/workflows/test.yml)
[![Test Coverage](https://img.shields.io/codecov/c/github/BlueBuzzah/BlueBuzzah-Firmware)](https://app.codecov.io/gh/BlueBuzzah/BlueBuzzah-Firmware/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Arduino-orange.svg)](https://platformio.org/)
[![Snyk Security](https://snyk.io/test/github/buzzahbuddy/bluebuzzah-firmware/badge.svg)](https://app.snyk.io/org/rbonestell/project/29618332-cdb9-45be-ba57-41da82771ad0)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

BlueBuzzah is a medical device research platform implementing vibrotactile Coordinated Reset (vCR) therapy for Parkinson's disease treatment. The system consists of two synchronized haptic gloves that deliver precisely timed vibration patterns to fingers, based on research showing therapeutic benefits from desynchronization of pathological neural oscillations.

## Hardware Targets

One codebase builds for two hardware generations, selected by PlatformIO environment:

| | BlueBuzzah (v2) | PentaBuzzer (v3) |
| --- | --- | --- |
| **Board** | Adafruit Feather nRF52840 Express | Seeed XIAO ESP32-S3 |
| **Environment** | `adafruit_feather_nrf52840` | `pentabuzzer_esp32s3` |
| **Motors** | 4 (index–pinky) | 5 (adds thumb) |
| **BLE stack** | Bluefruit / SoftDevice | NimBLE-Arduino 2.x |
| **Battery monitoring** | VBAT divider on ADC | none (reports healthy) |
| **Power switch** | none | slide switch + deep sleep |

## Features

- **Bilateral Synchronization**: Sub-millisecond (<1ms) synchronization between left and right gloves
- **Research-Based Therapy**: Regular vCR, Noisy vCR, and Hybrid vCR profiles
- **Mobile Control**: Comprehensive BLE protocol for iOS/Android app integration
- **Real-Time Session Management**: Pause, resume, and progress tracking
- **Multi-Connection Support**: Simultaneous phone and glove connections
- **Battery Safety**: Monitoring with automatic shutdown at critical levels (BlueBuzzah v2)
- **Calibration Mode**: Individual finger testing and adjustment
- **Assembly QA**: `MOTOR_DIAG`, `MOTOR_TEST:<n>`, and `MOTOR_PRESENT` serial commands for motor verification

## Quick Start

### Prerequisites

- BlueBuzzah or PentaBuzzer hardware ([hardware specs](https://github.com/BlueBuzzah/BlueBuzzah-Hardware))
- [VS Code](https://code.visualstudio.com/)
- [PlatformIO IDE extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)

> **Note:** The PlatformIO IDE extension is already configured as a recommended extension for this project. VS Code will prompt you to install it when you open the workspace.

### Installation

```bash
# Clone repository
git clone https://github.com/BlueBuzzah/BlueBuzzah-Firmware.git
cd BlueBuzzah-Firmware

# Deploy to connected device(s) - builds, uploads, and configures roles
python deploy.py
```

The deploy script automatically:

- Detects connected devices and their board type (Feather nRF52840 or XIAO ESP32-S3)
- Builds and uploads the matching firmware for each board
- **1 device**: Prompts for role selection (PRIMARY/SECONDARY)
- **2 devices**: Auto-assigns PRIMARY to first, SECONDARY to second
- Verifies role configuration after upload

Role is persisted to flash and survives power cycles.

**Manual commands** (if needed):

```bash
python deploy.py --list                     # List connected devices
pio run -e adafruit_feather_nrf52840        # Build (BlueBuzzah v2)
pio run -e pentabuzzer_esp32s3              # Build (PentaBuzzer v3)
pio run -e <env> -t upload                  # Flash firmware
pio device monitor                          # Open serial monitor (115200 baud)
# In monitor: GET_ROLE     # Query current role
# In monitor: SET_ROLE:PRIMARY (or SET_ROLE:SECONDARY)
```

### Running Tests

```bash
pio test -e native          # Unit tests (4-actuator config)
pio test -e native_penta    # Unit tests (5-actuator config)
```

### Verify Deployment

- Both devices show rapid blue LED flashing during boot
- PRIMARY shows 5x green flash when SECONDARY connects
- Both show solid green when ready

### Basic Usage

1. Power on both devices
2. Wait for boot sequence (up to 30 seconds)
3. After boot success (solid green LED), therapy begins automatically
4. Connect phone via BLE to "BlueBuzzah" for mobile app control

## Configuration

### Therapy Profiles

Therapy profiles are defined in `include/config.h` and managed by the ProfileManager. See the code for available configuration options.

### System Constants

Key constants in `include/config.h`:

```cpp
#define STARTUP_WINDOW_MS 30000            // Boot connection window
#define BATTERY_CRITICAL_VOLTAGE 3.3f      // Critical battery shutdown (volts)
#define BATTERY_LOW_VOLTAGE 3.4f           // Low battery warning (volts)
#define I2C_FREQUENCY 400000               // I2C bus speed (Hz)
#define CONNECTION_LOST_TIMEOUT_MS 30000   // Purple blink duration after peer loss
```

Board-specific values (pins, actuator count, battery availability) live in `include/board_config.h`.

## Troubleshooting

### Boot Issues

**Red flashing LED after 30 seconds:**

- Power cycle both devices simultaneously
- Move devices closer together (< 1m)
- Check battery voltage (> 3.4V)
- Verify I2C connections

**Blue flashing indefinitely:**

- Verify device role: `GET_ROLE` in serial monitor
- Re-set role: `SET_ROLE:PRIMARY` or `SET_ROLE:SECONDARY`
- Redeploy firmware: `pio run -t upload`

### Therapy Issues

**Motors not vibrating:**

- Run calibration mode to test individual fingers
- Check I2C pullup resistors (4.7k)
- Verify motor connections to DRV2605

**Poor bilateral synchronization (> 10ms):**

- Reduce BLE_INTERVAL_MAX_MS in config.h
- Check for blocking operations in main loop

### Connection Loss

- **Purple flashing**: Peer glove connection lost - devices attempt to reconnect for 30 seconds, then return to idle (pulsing blue)

### Battery Issues (BlueBuzzah v2 only)

- **Orange LED**: Battery low (< 3.4V) - charge soon
- **Rapid red flashing**: Battery critical (< 3.3V) - charge immediately

### PentaBuzzer Power

- Motors require a battery connected - they cannot run on USB power alone (DRV2605 drivers are powered from VBat)

## Contributing

### Code Style

- C++ best practices for embedded systems
- Arduino conventions for setup()/loop() structure
- SOLID principles and clean architecture

### Pull Request Process

1. Create feature branch from `main`
2. Test on actual hardware
3. Submit PR with description and test results

## Documentation

- **[Architecture Guide](docs/ARCHITECTURE.md)**: System design and patterns (source of truth)
- **[API Reference](docs/API_REFERENCE.md)**: Module API documentation
- **[BLE Protocol](docs/BLE_PROTOCOL.md)**: Command protocol specification
- **[Synchronization Protocol](docs/SYNCHRONIZATION_PROTOCOL.md)**: PRIMARY↔SECONDARY glove coordination
- **[Testing Guide](docs/TESTING.md)**: Test framework and patterns
- **[Calibration Guide](docs/CALIBRATION_GUIDE.md)**: Motor calibration procedures
- **[Boot Sequence](docs/BOOT_SEQUENCE.md)**: Boot process and LED indicators
- **[Therapy Engine](docs/THERAPY_ENGINE.md)**: Pattern generation internals
- **[Latency Metrics](docs/LATENCY_METRICS.md)**: Performance measurement guide

## License

MIT License - see [LICENSE](LICENSE) for details.

## Contact

- **Issues**: [GitHub Issues](https://github.com/BlueBuzzah/BlueBuzzah-Firmware/issues)
- **Discussions**: [GitHub Discussions](https://github.com/BlueBuzzah/BlueBuzzah-Firmware/discussions)
