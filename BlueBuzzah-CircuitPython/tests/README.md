# BlueBuzzah v2 Testing

Hardware integration testing for BlueBuzzah v2 firmware running on Adafruit Feather nRF52840 Express.

## Overview

This directory contains **manual integration tests** that require actual BlueBuzzah hardware connected via Bluetooth Low Energy (BLE). All tests are designed to validate the firmware's BLE protocol implementation and hardware functionality on real devices.

**IMPORTANT**: These are NOT automated unit tests. You must have physical BlueBuzzah hardware and establish a BLE connection before running any tests.

## Test Files

### 1. `calibrate_commands_test.py`
Tests the 3 CALIBRATE commands for haptic motor calibration.

**Tests:**
- `CALIBRATE_START` - Enter calibration mode
- `CALIBRATE_BUZZ` - Test individual motors (0-7) at various intensities
- `CALIBRATE_STOP` - Exit calibration mode

**What it validates:**
- All 8 motors respond correctly
- Intensity scaling works (25%, 50%, 75%, 100%)
- Calibration workflow is smooth for clinical use

**Execution time:** ~5-10 minutes

### 2. `session_commands_test.py`
Tests the 5 SESSION commands for therapy session management.

**Tests:**
- `SESSION_START` - Start therapy session
- `SESSION_PAUSE` - Pause ongoing session
- `SESSION_RESUME` - Resume paused session
- `SESSION_STOP` - Stop session and return statistics
- `SESSION_STATUS` - Get current session information

**What it validates:**
- Session state transitions work correctly
- Commands return proper responses with EOT terminators
- SessionManager (if enabled) handles all commands

**Execution time:** ~3-5 minutes

### 3. `memory_stress_test.py`
Long-running memory profiling for CircuitPython memory management.

**Tests:**
- Memory usage during extended therapy sessions
- Garbage collection effectiveness
- Memory leak detection over 2+ hours

**What it validates:**
- Free memory stays above safe threshold (>10KB)
- No memory leaks during normal operation
- System stability over extended periods

**Execution time:** 2+ hours (configurable)

### 4. `sync_accuracy_test.py`
Measures synchronization latency for multi-device coordination.

**Tests:**
- `EXECUTE_BUZZ` command round-trip latency
- `BUZZ_COMPLETE` acknowledgment timing
- Network jitter measurement

**What it validates:**
- Sync latency stays within acceptable range (7.5-20ms target)
- Consistent timing across multiple trials
- Bluetooth communication reliability

**Execution time:** ~2-5 minutes per test run

## Protocol Coverage

**BLE Commands Tested:** 8 out of 18 total commands (44%)

### Tested Commands ✓
- CALIBRATE_START, CALIBRATE_BUZZ, CALIBRATE_STOP
- SESSION_START, SESSION_PAUSE, SESSION_RESUME, SESSION_STOP, SESSION_STATUS

### Untested Commands ✗
- INFO, BATTERY, PING
- PROFILE_LIST, PROFILE_LOAD, PROFILE_GET, PROFILE_CUSTOM
- PARAM_SET
- HELP, RESTART

## Hardware Requirements

### Required Equipment
1. **BlueBuzzah device** - Feather nRF52840 with BlueBuzzah firmware v2.0+
2. **BLE-capable host** - Computer with Bluetooth 4.0+ or BLE USB adapter
3. **BLE UART connection library** - `adafruit_ble` or equivalent for Python

### Device Setup
1. Flash BlueBuzzah v2 firmware to Feather nRF52840 Express
2. Power on device (USB or battery)
3. Verify device is advertising (LED should indicate BLE status)
4. Note device name (e.g., "BlueBuzzah-PRIMARY" or "BlueBuzzah-SECONDARY")

## Running Tests

### Prerequisites

**Install BLE library** (if not already installed):
```bash
pip install adafruit-blinka-bleio
# or
pip install bleak  # Alternative BLE library
```

### Basic Test Execution

**Step 1: Establish BLE Connection**

```python
# Connect to device using your preferred BLE library
# Example with adafruit_ble:
from adafruit_ble import BLERadio
from adafruit_ble.advertising.standard import ProvideServicesAdvertisement
from adafruit_ble.services.nordic import UARTService

ble = BLERadio()
uart_connection = None

# Scan for device
for advertisement in ble.start_scan(ProvideServicesAdvertisement):
    if "BlueBuzzah" in advertisement.complete_name:
        uart_connection = ble.connect(advertisement)
        break

ble.stop_scan()

if uart_connection and uart_connection.connected:
    uart_service = uart_connection[UARTService]
    print("Connected to BlueBuzzah")
else:
    print("Failed to connect")
```

**Step 2: Run Test**

```python
# Example: Run calibration tests
import tests.calibrate_commands_test as calibrate_test

tester = calibrate_test.CalibrateCommandsTester(uart_service)
calibrate_test.run_all_tests(uart_service)
```

**Step 3: Review Results**

Tests print results to console in real-time:
```
Test 1: CALIBRATE_START
----------------------------------------
    Response: CALIBRATION:STARTED
    ✓ PASS: Entered calibration mode

Test 2: CALIBRATE_BUZZ (Motor 0)
----------------------------------------
    Response: BUZZ:COMPLETE
    ✓ PASS: Motor 0 responded

...
```

### Running Individual Tests

**Calibration Tests:**
```python
import tests.calibrate_commands_test

# After establishing BLE connection:
tests.calibrate_commands_test.run_all_tests(ble_connection)
```

**Session Tests:**
```python
import tests.session_commands_test

# Requires PRIMARY device:
tests.session_commands_test.run_all_tests(ble_connection)
```

**Memory Stress Test:**
```python
import tests.memory_stress_test

# Runs for 2 hours by default:
tests.memory_stress_test.run_stress_test(ble_connection, duration_hours=2)
```

**Sync Latency Test:**
```python
import tests.sync_accuracy_test

# Measures round-trip latency:
tests.sync_accuracy_test.measure_sync_latency(ble_connection, trials=100)
```

## Test Protocol

### Command Format

All tests use the BLE protocol v2.0 format:

**Request:**
```
COMMAND_NAME:ARG1:ARG2\x04
```

**Response:**
```
KEY:VALUE\x04
```

The `\x04` (EOT) character terminates every message.

### Response Validation

Tests check for:
1. **EOT terminator present** - Response must end with `\x04`
2. **Response format** - Expects `KEY:VALUE` pairs
3. **Success indicators** - Keywords like "OK", "STARTED", "COMPLETE"
4. **Error detection** - Keywords like "ERROR", "FAIL"

### Timing Constraints

- **Command timeout:** 2-5 seconds per command
- **Inter-command delay:** 100ms minimum (per protocol spec)
- **Session commands:** Up to 5 seconds (may involve state transitions)

## Test Results

### Current Test Status

| Test File | Commands | Status | Coverage |
|-----------|----------|--------|----------|
| calibrate_commands_test.py | 3 | ✓ Active | CALIBRATE_* |
| session_commands_test.py | 5 | ✓ Active | SESSION_* |
| memory_stress_test.py | - | ✓ Active | Memory monitoring |
| sync_accuracy_test.py | - | ✓ Active | Latency measurement |

**Total BLE Protocol Coverage:** 8/18 commands (44%)

### Typical Results

**Calibration Tests:**
- All 8 motors should respond consistently
- Intensity scaling should be perceptible
- PASS rate: 95%+ on working hardware

**Session Tests:**
- State transitions should be smooth
- Commands should respond within 1 second
- PASS rate: 90%+ (depends on SessionManager config)

**Memory Tests:**
- Free memory should stay >10KB throughout
- No continuous downward trend (indicates leaks)
- Target: 0 failures over 2 hours

**Sync Tests:**
- Latency should be 7.5-20ms typical
- Jitter should be <5ms
- Target: 95% of samples within range

## Troubleshooting

### Connection Issues

**Problem:** Cannot connect to device
- Verify device is powered and advertising
- Check device is within BLE range (1-10 meters)
- Try resetting device (press RESET button)
- Verify no other device is already connected

**Problem:** Connection drops during test
- Check battery level (low battery affects BLE)
- Reduce distance to device
- Eliminate sources of 2.4GHz interference
- Restart test from beginning

### Test Failures

**Problem:** Commands return ERROR or no response
- Verify firmware version matches test expectations (v2.0+)
- Check device is in correct state (e.g., not already in calibration)
- Review serial console output for device-side errors
- Verify command syntax matches protocol spec

**Problem:** Memory test shows declining memory
- This may indicate a memory leak - review firmware changes
- Check for allocations in loops
- Verify gc.collect() is called appropriately
- Use CircuitPython memory profiling to identify leak source

**Problem:** Sync latency too high (>50ms)
- Check Bluetooth connection quality (RSSI)
- Verify no other BLE devices nearby causing interference
- Test with devices closer together
- Check if device is busy with other processing

## Limitations

### What These Tests DON'T Cover

- **Unit tests** - No isolated component testing
- **Automated CI/CD** - Cannot run without hardware
- **Mock testing** - No simulated hardware components
- **Coverage reporting** - No automated coverage metrics
- **Architecture validation** - Only BLE protocol layer tested

### Untested Functionality

The following documented systems have **no test coverage**:

- EventBus and event system
- TherapyStateMachine state transitions
- Pattern generators (RandomPermutation, Sequential, Mirrored)
- Profile management (load/save/validation)
- Configuration system
- Battery monitoring logic
- LED UI controllers
- Hardware multiplexer operations
- Most BLE commands (10 untested)

## Future Testing

To achieve comprehensive test coverage, the project would need:

1. **Unit testing framework** - pytest with CircuitPython mocks
2. **Hardware mocks** - Simulated DRV2605, battery, I2C multiplexer
3. **BLE mocks** - Simulated BLE connections for CI/CD
4. **Automated test suite** - Run tests without human interaction
5. **Coverage reporting** - Track which code paths are tested
6. **Integration tests** - End-to-end therapy session validation

## Contributing

When adding new tests:

1. **Follow existing patterns** - Use same command/response validation approach
2. **Document hardware requirements** - Specify PRIMARY vs SECONDARY, special setup
3. **Include expected results** - Document what PASS looks like
4. **Add timing estimates** - How long does test take to run?
5. **Update this README** - Add new test to table and coverage stats

## Version

**Testing Infrastructure Version:** 1.0 (Hardware Integration Tests)
**Firmware Version Required:** v2.0.0+
**Last Updated:** 2025-11-23
