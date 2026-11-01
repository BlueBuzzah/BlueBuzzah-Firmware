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

| | BlueBuzzah v2 | BlueBuzzah v3 |
| --- | --- | --- |
| **Board** | Adafruit Feather nRF52840 Express | Seeed XIAO ESP32-S3 |
| **Environment** | `adafruit_feather_nrf52840` | `pentabuzzer_esp32s3` |
| **Motors** | 4 (index–pinky) | 5 (adds thumb) |
| **BLE stack** | Bluefruit / SoftDevice | NimBLE-Arduino 2.x |
| **Battery monitoring** | VBAT divider on ADC | DRV2605 VBAT register (0x21) over I2C |
| **Power switch** | none | slide switch + deep sleep |

## Features

- **Bilateral Synchronization**: PTP-inspired clock sync over BLE on a 1MHz hardware timebase; current acceptance target is <5ms clock-offset precision, pending re-validation (see [SYNC_VALIDATION.md](docs/SYNC_VALIDATION.md))
- **Research-Based Therapy**: Six built-in profiles — Regular vCR, Noisy vCR, Hybrid vCR, Gentle, Quick Test, and a user-editable Custom vCR profile
- **Mobile Control**: Comprehensive BLE protocol for iOS/Android app integration
- **Real-Time Session Management**: Pause, resume, and progress tracking
- **Multi-Connection Support**: Simultaneous phone and glove connections
- **Battery Monitoring**: Voltage and percentage reporting on both boards; an orange LED and refused session starts when either glove is too low to finish a 2-hour session
- **Calibration Mode**: Individual finger testing and adjustment
- **Assembly QA**: `MOTOR_DIAG`, `MOTOR_TEST:<n>`, and `MOTOR_PRESENT` serial commands for motor verification

## Quick Start

### Prerequisites

- BlueBuzzah hardware ([hardware specs](https://github.com/BlueBuzzah/BlueBuzzah-Hardware))
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
pio run -e pentabuzzer_esp32s3              # Build (BlueBuzzah v3)
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

- Both devices show a breathing/blinking blue LED during boot (waiting to pair)
- Both switch to solid green when the glove pair connects (ready)

### Basic Usage

1. Power on both devices
2. Wait for boot sequence (up to 30 seconds)
3. Once the gloves pair (solid green LED), therapy begins automatically if no phone connects within the 30-second boot window
4. Connect phone via BLE to "BlueBuzzah" for mobile app control

## Configuration

### Therapy Profiles

The six built-in profiles are defined in `src/profile_manager.cpp` and selected with `PROFILE_LOAD:<id>` over BLE or serial (`SET_PROFILE:REGULAR|NOISY|HYBRID|GENTLE` is a serial-only shortcut for four of them):

| Profile | Internal name | Notes |
| --- | --- | --- |
| Regular vCR | `regular_vcr` | Default; non-mirrored, no jitter |
| Noisy vCR | `noisy_vcr` | Mirrored, 23.5% jitter |
| Hybrid vCR | `hybrid_vcr` | Non-mirrored, 23.5% jitter |
| Gentle | `gentle` | Lower amplitude (30–70%) |
| Quick Test | `quick_test` | 5-minute session |
| Custom vCR | `custom_vcr` | User-editable; see below |

The Custom profile's eight parameters (burst on/off, jitter, amplitude min/max, session length, mirroring, finger count) are edited via `PROFILE_CUSTOM:KEY:VALUE` and persisted to `custom.bin`. Firmware is the sole validator — bounds are the `PARAM_MIN_*`/`PARAM_MAX_*` constants in `include/config.h`.

### System Constants

Key constants in `include/config.h`:

```cpp
#define STARTUP_WINDOW_MS 30000            // Boot connection window
#define BATTERY_CRITICAL_VOLTAGE 3.3f      // Critical battery shutdown (volts)
#define I2C_FREQUENCY 400000               // I2C bus speed (Hz)
#define CONNECTION_LOST_TIMEOUT_MS 30000   // Purple blink duration after peer loss
```

Board-specific values (pins, actuator count, battery availability, the `BATTERY_LOW_VOLTAGE` low-battery threshold) live in `include/board_config.h`.

## Troubleshooting

### Boot Issues

**Orange slow-blinking LED at boot (role not set):**

- The device is waiting for a role over USB serial: send `SET_ROLE:PRIMARY` or `SET_ROLE:SECONDARY` (or run `python deploy.py`)

**Red slow-blinking LED during boot (hardware or BLE initialization failed):**

- If it stays red after boot, the glove is in `ERROR`: no motor driver started, the motor task failed, or (v3) fewer than 4 motors were found. It refuses sessions until rebooted, and its partner PRIMARY refuses them too
- Power cycle the device
- Check battery voltage (> 3.8V)
- Verify I2C connections and motor JST connections

**Breathing blue indefinitely (gloves never pair):**

- Verify device role: `GET_ROLE` in serial monitor
- Re-set role: `SET_ROLE:PRIMARY` or `SET_ROLE:SECONDARY`
- Redeploy firmware: `pio run -e <env> -t upload`

### Therapy Issues

**Motors not vibrating:**

- Run calibration mode to test individual fingers
- Check I2C pullup resistors (4.7k)
- Verify motor connections to DRV2605

**Poor bilateral synchronization (> 10ms):**

- Reduce BLE_INTERVAL_MAX_MS in config.h
- Check for blocking operations in main loop

### Connection Loss

- **Purple flashing**: Peer glove connection lost - devices attempt to reconnect for 30 seconds, then return to idle (breathing blue)

### Battery Issues

Battery monitoring applies to both boards (v2 senses VBAT via an ADC divider; v3 reads
the DRV2605 VBAT register). Voltage is reported by the `BATTERY` command and logged to
serial every 60 seconds. Each glove's LED shows its own battery:

- **Orange LED (breathing, solid, or slow pulse) / `Battery too low` error when a session starts**: A glove is below 3.8V (`BATTERY_LOW_VOLTAGE`) and can't finish a 2-hour session - charge it to 3.9V or more. A session already running continues
- **Red urgent blinking**: Battery critical (< 3.3V) - the session stops; charge the glove: the red blink stops at 3.8V, and the LED stays orange until 3.9V

### BlueBuzzah v3 Power

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
- **[BLE Protocol](https://github.com/BlueBuzzah/BlueBuzzah-Firmware/wiki/BLE-Protocol)**: Command protocol specification (wiki)
- **[Synchronization Protocol](docs/SYNCHRONIZATION_PROTOCOL.md)**: PRIMARY↔SECONDARY glove coordination
- **[Sync Validation](docs/SYNC_VALIDATION.md)**: GPIO-based bilateral sync measurement and acceptance gates
- **[Therapy Session Flow](docs/THERAPY_SESSION_FLOW.md)**: Session state machine flow
- **[Timing Baseline](docs/TIMING_BASELINE.md)**: Timing analysis and profiling
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
