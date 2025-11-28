# BlueBuzzah v2 Testing Guide

**Hardware integration testing guide for BlueBuzzah v2 firmware**

Version: 2.0.0
Last Updated: 2025-11-23

---

## Table of Contents

- [Overview](#overview)
- [Test Infrastructure](#test-infrastructure)
- [Hardware Requirements](#hardware-requirements)
- [Running Tests](#running-tests)
- [Test Coverage](#test-coverage)
- [Test Files](#test-files)
- [BLE Protocol Testing](#ble-protocol-testing)
- [Memory Testing](#memory-testing)
- [Synchronization Testing](#synchronization-testing)
- [Troubleshooting](#troubleshooting)
- [Test Results Interpretation](#test-results-interpretation)
- [Contributing](#contributing)

---

## Overview

BlueBuzzah v2 uses **manual hardware integration tests** to validate firmware functionality on actual Feather nRF52840 devices. The testing approach emphasizes:

- **Real hardware validation** - Tests run on actual CircuitPython devices
- **BLE protocol verification** - Validates command/response behavior
- **Memory monitoring** - Ensures CircuitPython memory stability
- **Synchronization accuracy** - Measures multi-device timing
- **Manual execution** - Requires human setup and intervention

### What This Testing Does

✓ Validates BLE command protocol (8 out of 18 commands)
✓ Verifies haptic motor calibration workflow
✓ Tests therapy session management commands
✓ Monitors memory usage over extended periods
✓ Measures synchronization latency between devices

### What This Testing Does NOT Do

✗ Unit testing of individual components
✗ Automated CI/CD integration
✗ Mock-based testing without hardware
✗ Code coverage reporting
✗ Automated regression testing

---

## Test Infrastructure

### Current Test Files

| File | Purpose | Duration | Hardware Required |
|------|---------|----------|-------------------|
| `calibrate_commands_test.py` | Motor calibration validation | 5-10 min | 1 device (PRIMARY or SECONDARY) |
| `session_commands_test.py` | Session management commands | 3-5 min | 1 device (PRIMARY) |
| `memory_stress_test.py` | Memory leak detection | 2+ hours | 1 device |
| `sync_accuracy_test.py` | Latency measurement | 2-5 min | 1-2 devices |

### Testing Architecture

```
Host Computer (Python)
    │
    ├─ BLE Connection
    │
    └─ BlueBuzzah Device (CircuitPython)
        ├─ BLE UART Service
        ├─ Command Handler
        ├─ Hardware Controllers
        └─ Therapy Engine
```

Tests send BLE commands and validate responses - no mock infrastructure exists.

---

## Hardware Requirements

### Required Equipment

1. **BlueBuzzah Device(s)**
   - Adafruit Feather nRF52840 Express
   - BlueBuzzah v2 firmware installed
   - Charged battery or USB power
   - BLE advertising active

2. **Host Computer**
   - Bluetooth 4.0+ (BLE capable)
   - Python 3.9+
   - BLE library (`adafruit-blinka-bleio` or `bleak`)

3. **Optional**
   - Second BlueBuzzah device for sync testing
   - USB cable for serial console monitoring
   - Logic analyzer for timing verification

### Device Preparation

1. Flash BlueBuzzah v2 firmware to device
2. Configure device as PRIMARY or SECONDARY (as needed)
3. Verify BLE advertising (LED should pulse)
4. Note device name (e.g., "BlueBuzzah-PRIMARY")
5. Ensure battery >50% or connect USB power

---

## Running Tests

### Installation

**Install BLE library:**

```bash
# Option 1: Adafruit BLE
pip install adafruit-blinka-bleio

# Option 2: Bleak (cross-platform)
pip install bleak
```

### Connection Setup

**Establish BLE connection** before running any test:

```python
from adafruit_ble import BLERadio
from adafruit_ble.advertising.standard import ProvideServicesAdvertisement
from adafruit_ble.services.nordic import UARTService

# Initialize BLE radio
ble = BLERadio()
uart_connection = None

# Scan for BlueBuzzah device
print("Scanning for BlueBuzzah...")
for advertisement in ble.start_scan(ProvideServicesAdvertisement):
    if "BlueBuzzah" in advertisement.complete_name:
        print(f"Found: {advertisement.complete_name}")
        uart_connection = ble.connect(advertisement)
        break

ble.stop_scan()

# Get UART service
if uart_connection and uart_connection.connected:
    uart_service = uart_connection[UARTService]
    print("✓ Connected successfully")
else:
    print("✗ Connection failed")
    exit(1)
```

### Running Individual Tests

**Calibration Test:**

```python
import tests.calibrate_commands_test as calibrate_test

# Run all calibration tests
calibrate_test.run_all_tests(uart_service)
```

**Session Test:**

```python
import tests.session_commands_test as session_test

# Run session management tests (requires PRIMARY device)
session_test.run_all_tests(uart_service)
```

**Memory Stress Test:**

```python
import tests.memory_stress_test as mem_test

# Run 2-hour memory monitoring
mem_test.run_stress_test(uart_service, duration_hours=2)
```

**Sync Accuracy Test:**

```python
import tests.sync_accuracy_test as sync_test

# Measure sync latency (100 trials)
sync_test.measure_sync_latency(uart_service, trials=100)
```

---

## Test Coverage

### BLE Protocol Commands

**Commands Tested (8/18 - 44% coverage):**

| Command | Tested | Test File |
|---------|--------|-----------|
| CALIBRATE_START | ✓ | calibrate_commands_test.py |
| CALIBRATE_BUZZ | ✓ | calibrate_commands_test.py |
| CALIBRATE_STOP | ✓ | calibrate_commands_test.py |
| SESSION_START | ✓ | session_commands_test.py |
| SESSION_PAUSE | ✓ | session_commands_test.py |
| SESSION_RESUME | ✓ | session_commands_test.py |
| SESSION_STOP | ✓ | session_commands_test.py |
| SESSION_STATUS | ✓ | session_commands_test.py |

**Commands NOT Tested (10/18 - 56%):**

| Command | Status | Impact |
|---------|--------|--------|
| INFO | Not tested | Device info retrieval untested |
| BATTERY | Not tested | Battery monitoring untested |
| PING | Not tested | Connection health untested |
| PROFILE_LIST | Not tested | Profile management untested |
| PROFILE_LOAD | Not tested | Profile loading untested |
| PROFILE_GET | Not tested | Profile retrieval untested |
| PROFILE_CUSTOM | Not tested | Custom profiles untested |
| PARAM_SET | Not tested | Parameter adjustment untested |
| HELP | Not tested | Command help untested |
| RESTART | Not tested | Device restart untested |

### Functional Coverage

**Tested Functionality:**

- ✓ BLE command/response protocol with EOT terminators
- ✓ Haptic motor activation (8 motors)
- ✓ Intensity scaling (25%, 50%, 75%, 100%)
- ✓ Session state transitions (START → PAUSE → RESUME → STOP)
- ✓ Memory monitoring over extended periods
- ✓ Sync command latency measurement

**Untested Functionality:**

- ✗ Event system (EventBus, event handlers)
- ✗ State machine transitions (TherapyStateMachine)
- ✗ Pattern generators (RandomPermutation, Sequential, Mirrored)
- ✗ Profile management (load, save, validate)
- ✗ Configuration system
- ✗ Battery monitoring logic
- ✗ LED UI controllers
- ✗ I2C multiplexer operations
- ✗ Error recovery mechanisms
- ✗ Hardware fault detection

**Estimated Coverage:** ~15-20% of total firmware functionality

---

## Test Files

### calibrate_commands_test.py

**Purpose:** Validate haptic motor calibration workflow

**Test Sequence:**
1. Send `CALIBRATE_START` → Verify enters calibration mode
2. For each motor (0-7):
   - Send `CALIBRATE_BUZZ:motor:intensity` at 25%, 50%, 75%, 100%
   - Verify `BUZZ:COMPLETE` response
   - Confirm motor activates (human verification)
3. Send `CALIBRATE_STOP` → Verify exits calibration mode

**Success Criteria:**
- All commands return valid responses within 2 seconds
- Responses include EOT terminator (`\x04`)
- Motors activate with perceptible intensity differences
- No ERROR responses

**Typical Output:**
```
Test 1: CALIBRATE_START
----------------------------------------
    Response: CALIBRATION:STARTED
    ✓ PASS: Entered calibration mode

Test 2: CALIBRATE_BUZZ (Motor 0, 100%)
----------------------------------------
    Response: BUZZ:COMPLETE
    ✓ PASS: Motor 0 responded

...

SUMMARY: 25/25 tests PASSED
```

---

### session_commands_test.py

**Purpose:** Validate therapy session management commands

**Test Sequence:**
1. `SESSION_START` → Start therapy session
2. Wait 5 seconds
3. `SESSION_STATUS` → Verify status reports "RUNNING"
4. `SESSION_PAUSE` → Pause session
5. `SESSION_STATUS` → Verify status reports "PAUSED"
6. `SESSION_RESUME` → Resume session
7. `SESSION_STATUS` → Verify status reports "RUNNING"
8. `SESSION_STOP` → Stop session, get statistics

**Success Criteria:**
- State transitions occur correctly
- STATUS responses match expected state
- STOP command returns session statistics
- All responses within 5 seconds

**Typical Output:**
```
Test 1: SESSION_START
----------------------------------------
    Response: SESSION:STARTED
    ✓ PASS: Session started successfully

Test 2: SESSION_STATUS
----------------------------------------
    Response: STATUS:RUNNING
    ✓ PASS: Status correct

...

SUMMARY: 8/8 tests PASSED
```

---

### memory_stress_test.py

**Purpose:** Detect memory leaks during extended operation

**Test Sequence:**
1. Establish baseline memory reading
2. Start therapy session
3. Monitor `gc.mem_free()` every 60 seconds
4. Run for 2+ hours (configurable)
5. Log memory trends and minimum free memory

**Success Criteria:**
- Free memory stays above 10KB throughout
- No continuous downward trend (leak indicator)
- System remains stable and responsive

**Typical Output:**
```
Memory Stress Test - 2 hour duration
========================================
Baseline: 92,384 bytes free

00:15:00 - 89,472 bytes free (-2.9 KB)
00:30:00 - 91,136 bytes free (+1.7 KB)
00:45:00 - 88,832 bytes free (-2.3 KB)
...
02:00:00 - 90,240 bytes free (-2.1 KB)

RESULT: PASS
Minimum free memory: 85,760 bytes (>10KB threshold)
No downward trend detected
```

---

### sync_accuracy_test.py

**Purpose:** Measure synchronization command latency

**Test Sequence:**
1. Send `EXECUTE_BUZZ:motor:intensity` command
2. Record send timestamp
3. Wait for `BUZZ_COMPLETE` response
4. Record receive timestamp
5. Calculate round-trip latency
6. Repeat for 100 trials (configurable)
7. Calculate statistics (mean, median, min, max, jitter)

**Success Criteria:**
- Mean latency: 7.5-20ms (target range)
- Max latency: <50ms
- Jitter (std dev): <5ms
- 95% of samples within acceptable range

**Typical Output:**
```
Sync Latency Measurement
========================================
Trials: 100

Results:
  Mean:   12.3 ms
  Median: 11.8 ms
  Min:     8.1 ms
  Max:    18.7 ms
  Jitter:  2.4 ms (std dev)

✓ PASS: 98/100 samples (98%) within 7.5-20ms range
✓ PASS: Jitter < 5ms threshold
✓ PASS: Max latency < 50ms
```

---

## BLE Protocol Testing

### Command Format

All tests use the BLE Protocol v2.0 format:

**Request:**
```
COMMAND_NAME:ARG1:ARG2\x04
```

**Response:**
```
KEY:VALUE\x04
```

The `\x04` (EOT) terminator is **mandatory** for all messages.

### Response Validation

Tests validate:
1. **Presence of EOT** - Response must contain `\x04`
2. **Response timing** - Must arrive within timeout (2-5 seconds)
3. **Format** - Expects `KEY:VALUE` structure
4. **Success keywords** - Looks for "OK", "STARTED", "COMPLETE", "STOPPED"
5. **Error detection** - Checks for "ERROR", "FAIL", "INVALID"

### Protocol Timing

Per BLE_PROTOCOL.md specification:

- **Minimum inter-command delay:** 100ms
- **Command timeout:** 2-5 seconds (depends on command complexity)
- **Response time:** <1 second for most commands
- **Session commands:** Up to 5 seconds (involve state changes)

---

## Memory Testing

### CircuitPython Memory Constraints

- **Total RAM:** 256 KB
- **Available to CircuitPython:** ~130 KB
- **With BLE active:** ~90 KB
- **Safe minimum:** 10 KB free

### Memory Monitoring

The memory stress test checks for:

1. **Baseline stability** - Memory should stabilize after initialization
2. **Leak detection** - No continuous downward trend
3. **Minimum threshold** - Never drop below 10 KB
4. **GC effectiveness** - Memory should recover after `gc.collect()`

### Memory Failure Indicators

⚠️ **Warning signs:**
- Continuous downward trend over hours
- Free memory approaching 10 KB
- `MemoryError` exceptions during test
- Increasing GC frequency without memory recovery

### Memory Optimization

If memory issues detected, see CLAUDE.md for CircuitPython memory optimization guidelines:

- Use `const()` for numeric constants
- Pre-allocate buffers at startup
- Avoid string concatenation in loops
- Import only needed items from modules
- Call `gc.collect()` after large operations

---

## Synchronization Testing

### Multi-Device Coordination

BlueBuzzah uses command-driven synchronization (SYNCHRONIZATION_PROTOCOL.md):

**Synchronization Flow:**
```
PRIMARY → EXECUTE_BUZZ:motor:intensity → SECONDARY
PRIMARY ← BUZZ_COMPLETE ← SECONDARY
```

### Latency Budget

Per protocol specification:

| Component | Budget | Typical |
|-----------|--------|---------|
| BLE transmission | 5-10 ms | 7.5 ms |
| Command processing | 1-3 ms | 2 ms |
| Motor activation | 2-5 ms | 3 ms |
| Response generation | 0.5-1 ms | 0.8 ms |
| **Total round-trip** | **10-20 ms** | **13.3 ms** |

### Jitter Analysis

The sync test measures jitter (latency variation):

- **Good:** <2ms standard deviation
- **Acceptable:** 2-5ms standard deviation
- **Poor:** >5ms standard deviation (investigate BLE interference)

---

## Troubleshooting

### Connection Issues

**Problem:** Cannot connect to device

**Solutions:**
1. Verify device is powered on and advertising
2. Check LED indicates BLE active (should pulse)
3. Restart device (press RESET button)
4. Ensure device not already connected to another host
5. Reduce distance to <2 meters
6. Check host Bluetooth is enabled

**Problem:** Connection drops mid-test

**Solutions:**
1. Check battery level (low battery degrades BLE)
2. Eliminate 2.4GHz interference sources (WiFi, microwaves)
3. Reduce distance between devices
4. Verify firmware not crashing (check serial console)

### Test Failures

**Problem:** Commands timeout (no response)

**Root Causes:**
- Device crashed - check serial console for errors
- BLE connection lost - verify connection still active
- Device in wrong state - ensure preconditions met
- Command queue full - add delay between commands

**Problem:** ERROR responses

**Root Causes:**
- Invalid command syntax - verify format matches protocol spec
- Device not in correct state - check state requirements
- SessionManager disabled - some commands require it enabled
- Hardware fault - check serial console for hardware errors

**Problem:** Inconsistent motor activation

**Root Causes:**
- DRV2605 initialization failed - check I2C connections
- Multiplexer channel selection issue - verify multiplexer wiring
- Motor disconnected - check physical connections
- Amplitude too low - increase intensity parameter

### Memory Test Issues

**Problem:** Memory declining during test

**Action Plan:**
1. Stop test immediately
2. Review recent firmware changes
3. Use CircuitPython memory profiling skill
4. Check for loop allocations
5. Verify `gc.collect()` placement
6. Review string operations

**Problem:** MemoryError during test

**Immediate Action:**
1. Note timestamp and operation when error occurred
2. Check device serial console for stack trace
3. Review code path that caused error
4. Identify allocation that exceeded available memory
5. Apply memory optimization techniques

---

## Test Results Interpretation

### PASS Criteria

A test is considered **PASSED** if:
- All commands return valid responses
- Responses arrive within timeout period
- Response format matches protocol specification
- No ERROR or FAIL keywords in responses
- Hardware behaves as expected (for calibration tests)

### FAIL Criteria

A test is considered **FAILED** if:
- Command times out with no response
- Response contains ERROR or FAIL keyword
- Response format is invalid (missing EOT, wrong structure)
- Hardware does not behave as expected

### UNKNOWN Results

Some tests may return **UNKNOWN** if:
- Response format is unexpected but not clearly an error
- Response timing is borderline
- Human verification required (for calibration tests)

### Success Rates

**Expected success rates on working hardware:**

- Calibration tests: 95-100%
- Session tests: 90-100% (depends on SessionManager config)
- Memory tests: 100% (should never fail on stable firmware)
- Sync tests: 95-100% (some variation from BLE jitter)

---

## Contributing

### Adding New Tests

When creating new hardware integration tests:

1. **Follow existing patterns:**
   - Use same command/response validation approach
   - Include EOT terminators in all messages
   - Implement timeout handling
   - Print clear test output

2. **Document requirements:**
   - Specify hardware needed (PRIMARY vs SECONDARY)
   - List command dependencies
   - Define success criteria
   - Estimate execution time

3. **Update documentation:**
   - Add test to tests/README.md table
   - Update coverage statistics in this file
   - Document new commands tested
   - Add troubleshooting section if needed

4. **Test file structure:**
   ```python
   """Test description and usage instructions."""

   class TestClassName:
       def __init__(self, ble_conn):
           self.ble = ble_conn
           self.test_results = {}

       def send_command(self, command):
           """Send command with EOT, wait for response."""
           # Implementation

       def test_feature_name(self):
           """Test specific feature."""
           # Test implementation

   def run_all_tests(ble_conn):
       """Run complete test suite."""
       # Test execution
   ```

### Test Naming Conventions

- **File:** `feature_commands_test.py` or `feature_test.py`
- **Class:** `FeatureCommandsTester` or `FeatureTester`
- **Methods:** `test_specific_behavior()`
- **Runner:** `run_all_tests(ble_conn)`

### Documentation Requirements

Every test file must include:
- Module docstring with purpose and usage
- Hardware requirements
- Expected results
- Example output
- Troubleshooting tips (if applicable)

---

## Future Testing Improvements

To achieve comprehensive automated testing, the project would need:

### Short-Term (Manual Testing)
- Add tests for remaining 10 BLE commands
- Create multi-device sync test suite
- Add battery monitoring validation
- Test error recovery scenarios

### Medium-Term (Test Infrastructure)
- Develop CircuitPython unit test framework
- Create hardware mock library
- Implement automated BLE connection handling
- Add test result logging and analysis

### Long-Term (Full Automation)
- Build pytest-based unit test suite
- Create CI/CD integration
- Implement code coverage reporting
- Develop automated regression testing
- Create test fixtures for all components
- Build mock BLE services for isolated testing

---

## Related Documentation

- **tests/README.md** - Detailed test file documentation
- **docs/BLE_PROTOCOL.md** - BLE protocol specification
- **docs/SYNCHRONIZATION_PROTOCOL.md** - Sync protocol details
- **docs/API_REFERENCE.md** - Complete API documentation
- **CLAUDE.md** - CircuitPython development guidelines

---

**Version:** 2.0.0 (Hardware Integration Testing)
**Last Updated:** 2025-11-23
**Test Coverage:** 8/18 BLE commands (44%), ~15-20% functional coverage
