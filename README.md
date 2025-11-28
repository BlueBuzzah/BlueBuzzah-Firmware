# BlueBuzzah Firmware

**Bilateral Haptic Therapy System for Parkinson's Disease Research**

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Arduino-orange.svg)](https://platformio.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

BlueBuzzah is a medical device research platform implementing vibrotactile Coordinated Reset (vCR) therapy for Parkinson's disease treatment. The system consists of two synchronized haptic gloves that deliver precisely timed vibration patterns to fingers, based on research showing therapeutic benefits from desynchronization of pathological neural oscillations.

## Features

- **Bilateral Synchronization**: Sub-10ms synchronization between left and right gloves
- **Research-Based Therapy**: Regular vCR, Noisy vCR, and Hybrid vCR profiles
- **Mobile Control**: Comprehensive BLE protocol for iOS/Android app integration
- **Real-Time Session Management**: Pause, resume, and progress tracking
- **Multi-Connection Support**: Simultaneous phone and glove connections
- **Battery Safety**: Monitoring with automatic shutdown at critical levels
- **Calibration Mode**: Individual finger testing and adjustment

## Quick Start

### Prerequisites

- BlueBuzzah hardware ([hardware specs](https://github.com/BlueBuzzah/BlueBuzzah-Hardware))
- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)

### Installation

```bash
# Clone and build
git clone https://github.com/BlueBuzzah/BlueBuzzah-Firmware.git
cd BlueBuzzah-Firmware
pio run

# Upload to device
pio run -t upload

# Configure device role (one-time, in serial monitor)
pio device monitor
# Then type: SET_ROLE:PRIMARY (or SET_ROLE:SECONDARY for second glove)
# Verify with: GET_ROLE
```

Role is persisted to flash and survives power cycles.

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
#define STARTUP_WINDOW_MS 30000      // Boot connection window
#define CRITICAL_VOLTAGE 3.3f        // Critical battery (volts)
#define LOW_VOLTAGE 3.4f             // Low battery warning (volts)
#define I2C_FREQUENCY 400000         // I2C bus speed (Hz)
```

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

- Reduce BLE_INTERVAL_MS in config.h
- Check for blocking operations in main loop

### Battery Issues

- **Orange LED**: Battery low (< 3.4V) - charge soon
- **Rapid red flashing**: Battery critical (< 3.3V) - charge immediately

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

- **[BLE Protocol](docs/BLE_PROTOCOL.md)**: Command protocol specification
- **[Architecture Guide](docs/ARCHITECTURE.md)**: System design and patterns
- **[Testing Guide](docs/TESTING.md)**: Hardware integration testing
- **[Calibration Guide](docs/CALIBRATION_GUIDE.md)**: Motor calibration procedures
- **[Boot Sequence](docs/BOOT_SEQUENCE.md)**: Boot process and LED indicators

## License

MIT License - see [LICENSE](LICENSE) for details.

## Contact

- **Issues**: [GitHub Issues](https://github.com/BlueBuzzah/BlueBuzzah-Firmware/issues)
- **Discussions**: [GitHub Discussions](https://github.com/BlueBuzzah/BlueBuzzah-Firmware/discussions)
