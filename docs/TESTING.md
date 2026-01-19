# BlueBuzzah v2 Testing Guide

**Testing guide for BlueBuzzah v2 firmware**

Version: 2.0.0
Last Updated: 2026-01-19

---

## Table of Contents

- [Overview](#overview)
- [Test Infrastructure](#test-infrastructure)
- [Running Tests](#running-tests)
- [Test Coverage](#test-coverage)
- [Native Unit Tests](#native-unit-tests)
- [Hardware Integration Testing](#hardware-integration-testing)
- [BLE Protocol Testing](#ble-protocol-testing)
- [Memory Testing](#memory-testing)
- [Synchronization Testing](#synchronization-testing)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)

---

## Overview

BlueBuzzah v2 uses a **dual testing approach**:

1. **PlatformIO Native Unit Tests** - Automated tests that run on the host computer with mocked Arduino APIs
2. **Hardware Integration Tests** - Manual tests that run on actual Feather nRF52840 devices

### What This Testing Does

✓ Unit testing of individual components (PlatformIO native)
✓ Validates BLE command protocol handling
✓ Tests therapy engine state machine transitions
✓ Verifies synchronization protocol logic
✓ Tests motor event buffer thread safety
✓ Validates profile management
✓ Code coverage reporting (via `native_coverage` environment)

### Testing Limitations

- Hardware-in-the-loop tests require manual setup
- No automated CI/CD integration yet
- BLE communication testing requires real hardware

---

## Test Infrastructure

### PlatformIO Test Environments

| Environment | Purpose | Command |
|-------------|---------|---------|
| `native` | Fast unit tests (no coverage) | `pio test -e native` |
| `native_coverage` | Unit tests + coverage (macOS/clang) | `pio test -e native_coverage` |
| `native_coverage_gcc` | Unit tests + coverage (Linux/GCC) | `pio test -e native_coverage_gcc` |

### Native Unit Test Files

| Test Suite | Module Tested | Purpose |
|------------|---------------|---------|
| `test_latency_metrics` | `latency_metrics.cpp` | RTT tracking, drift calculation |
| `test_profile_manager` | `profile_manager.cpp` | Profile load/save, validation |
| `test_types` | `types.h` | Struct packing, enum values |
| `test_menu_controller` | `menu_controller.cpp` | Command parsing, response format |
| `test_motor_event_buffer` | `motor_event_buffer.cpp` | Lock-free buffer, thread safety |
| `test_state_machine` | `state_machine.cpp` | Therapy FSM transitions |
| `test_therapy_engine` | `therapy_engine.cpp` | Pattern generation, callbacks |
| `test_sync_protocol` | `sync_protocol.cpp` | PTP clock sync, offset calculation |

### Mock Infrastructure

The `test/mocks/` directory provides Arduino API mocks:

```
test/mocks/
├── include/
│   ├── Arduino.h       # millis(), micros(), delay(), Serial
│   ├── bluefruit.h     # BLE stubs
│   └── ...
└── src/
    └── Arduino.cpp     # Mock implementations
```

---

## Running Tests

### Native Unit Tests

**Run all unit tests:**

```bash
pio test -e native
```

**Run with code coverage (macOS):**

```bash
pio test -e native_coverage
```

**Run with code coverage (Linux):**

```bash
pio test -e native_coverage_gcc
```

**Run a specific test suite:**

```bash
pio test -e native -f test_sync_protocol
pio test -e native -f test_therapy_engine
```

**Expected output (success):**

```
test/test_sync_protocol/test_sync_protocol.cpp:217:test_initial_state:PASS
test/test_sync_protocol/test_sync_protocol.cpp:218:test_ping_pong_sequence:PASS
test/test_sync_protocol/test_sync_protocol.cpp:219:test_offset_calculation:PASS
...
-----------------------
8 Tests 0 Failures 0 Ignored
OK
```

### Test Output Locations

Coverage reports are generated in:

```
.pio/test_results/native_coverage/
├── coverage.info
└── html/
    └── index.html  # Open in browser for visual coverage
```

---

## Test Coverage

### Native Unit Tests - Module Coverage

| Module | Test Suite | Key Tests |
|--------|------------|-----------|
| `sync_protocol.cpp` | `test_sync_protocol` | PTP offset calculation, drift tracking, warm-start recovery |
| `therapy_engine.cpp` | `test_therapy_engine` | Pattern generation, callback invocation, session timing |
| `state_machine.cpp` | `test_state_machine` | FSM transitions, state validation |
| `menu_controller.cpp` | `test_menu_controller` | Command parsing, response formatting |
| `motor_event_buffer.cpp` | `test_motor_event_buffer` | Lock-free operations, overflow handling |
| `profile_manager.cpp` | `test_profile_manager` | Profile serialization, validation |
| `latency_metrics.cpp` | `test_latency_metrics` | RTT calculation, drift statistics |
| `types.h` | `test_types` | Struct packing, enum values |

### Tested Functionality

- ✓ IEEE 1588 PTP clock offset calculation
- ✓ Drift rate tracking and compensation
- ✓ Warm-start sync recovery logic
- ✓ Therapy state machine transitions
- ✓ Pattern generation (random permutation, sequential)
- ✓ Motor event buffer thread safety
- ✓ Profile serialization/deserialization
- ✓ Command parsing and response formatting
- ✓ Latency metrics aggregation

### Not Tested (Requires Hardware)

- ✗ Actual BLE communication
- ✗ I2C multiplexer operations
- ✗ DRV2605 motor driver
- ✗ LED controller patterns
- ✗ Battery ADC readings
- ✗ Real-time timing accuracy

---

## Native Unit Tests

### test_sync_protocol

Tests the IEEE 1588 PTP-style clock synchronization:

```cpp
void test_offset_calculation() {
    // Verify 4-timestamp offset calculation
    // offset = ((T2 - T1) + (T3 - T4)) / 2
    syncProtocol.handlePingResponse(seq, 0, T2, T3);
    TEST_ASSERT_INT_WITHIN(100, expectedOffset, syncProtocol.getClockOffset());
}

void test_drift_rate_tracking() {
    // Verify drift compensation over time
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedDrift, syncProtocol.getDriftRate());
}

void test_warm_start_recovery() {
    // Verify cached sync state is used after brief disconnect
    syncProtocol.disconnect();
    delay(5000);  // Less than SYNC_WARM_START_VALIDITY_MS
    syncProtocol.reconnect();
    TEST_ASSERT_TRUE(syncProtocol.isWarmStart());
}
```

### test_therapy_engine

Tests pattern generation and callback invocation:

```cpp
void test_callback_invocation() {
    bool activateCalled = false;
    engine.setActivateCallback([&](int finger) { activateCalled = true; });
    engine.update();
    TEST_ASSERT_TRUE(activateCalled);
}

void test_pattern_generation() {
    // Verify random permutation covers all fingers
    TEST_ASSERT_EQUAL(8, uniqueFingersActivated);
}
```

### test_motor_event_buffer

Tests lock-free buffer operations:

```cpp
void test_overflow_handling() {
    // Fill buffer beyond capacity
    for (int i = 0; i < BUFFER_SIZE + 10; i++) {
        buffer.push(event);
    }
    // Verify oldest events dropped, newest preserved
    TEST_ASSERT_EQUAL(BUFFER_SIZE, buffer.count());
}

void test_concurrent_access() {
    // Verify ISR-safe push with main-loop pop
    // (simulated with interleaved operations)
}
```

---

## Hardware Integration Testing

For testing that requires actual hardware, use manual verification:

### Hardware Requirements

1. **BlueBuzzah Device(s)**
   - Adafruit Feather nRF52840 Express
   - BlueBuzzah v2 firmware installed
   - Charged battery or USB power

2. **Host Computer**
   - USB cable for serial console
   - PlatformIO installed

3. **Optional**
   - Second BlueBuzzah for sync testing
   - Logic analyzer for timing verification

### Manual Test Procedures

**Motor Calibration Test:**
```bash
# Connect via serial monitor
pio device monitor

# Enter calibration mode
CALIBRATE_START

# Test each finger (0-7)
CALIBRATE_BUZZ:0:80:500
CALIBRATE_BUZZ:1:80:500
# ... verify each motor vibrates

CALIBRATE_STOP
```

**Latency Metrics Test:**
```bash
# Enable latency tracking
LATENCY_ON_VERBOSE

# Start therapy session via app or serial

# Check metrics
GET_LATENCY

# Disable and see final report
LATENCY_OFF
```

See [LATENCY_METRICS.md](LATENCY_METRICS.md) for detailed metrics interpretation.

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

### Arduino/nRF52840 Memory Budget

- **Total RAM:** 256 KB
- **Available after BLE stack:** ~200 KB
- **Typical firmware usage:** ~50-80 KB
- **Safe headroom:** >100 KB free

### Memory Monitoring

The memory stress test checks for:

1. **Baseline stability** - Memory should stabilize after initialization
2. **Leak detection** - No continuous downward trend
3. **Stack overflow** - Monitor stack usage with `dbgMemInfo()`
4. **Heap fragmentation** - Check for allocation failures

### Memory Failure Indicators

Warning signs:
- Continuous downward trend over hours
- Hard faults or crashes during extended operation
- Allocation failures in serial output
- Watchdog resets

### Memory Optimization

Best practices for Arduino C++ firmware:

- Use `static` allocation where possible
- Pre-allocate buffers at startup
- Avoid `String` concatenation in loops (use `snprintf`)
- Use `F()` macro for string literals to keep them in flash
- Prefer stack allocation for small temporary buffers

---

## Synchronization Testing

### Multi-Device Coordination

BlueBuzzah uses scheduled execution synchronization (SYNCHRONIZATION_PROTOCOL.md):

**Synchronization Flow:**
```
PRIMARY → MACROCYCLE:seq:baseTime:12|events... → SECONDARY
SECONDARY: Execute buzz at scheduled time
```

### Latency Budget

Per protocol specification:

| Component | Budget | Typical |
|-----------|--------|---------|
| BLE transmission | 5-10 ms | 7.5 ms |
| Command processing | 1-3 ms | 2 ms |
| Motor activation | 2-5 ms | 3 ms |
| **Total one-way** | **8-18 ms** | **12.5 ms** |

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
- StateMachine disabled - some commands require it enabled
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
3. Use `dbgMemInfo()` for heap/stack analysis
4. Check for loop allocations
5. Review `String` usage and avoid concatenation in loops
6. Use `F()` macro for flash-based string literals

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
- Session tests: 90-100% (depends on StateMachine config)
- Memory tests: 100% (should never fail on stable firmware)
- Sync tests: 95-100% (some variation from BLE jitter)

---

## Contributing

### Adding New Native Tests

When adding new PlatformIO native unit tests:

1. **Create test directory:**
   ```
   test/test_module_name/
   └── test_module_name.cpp
   ```

2. **Test file structure:**
   ```cpp
   #include <unity.h>
   #include "module_under_test.h"

   void setUp() {
       // Reset state before each test
   }

   void tearDown() {
       // Cleanup after each test
   }

   void test_specific_behavior() {
       // Arrange
       ModuleUnderTest module;

       // Act
       auto result = module.doSomething();

       // Assert
       TEST_ASSERT_EQUAL(expected, result);
   }

   int main(int argc, char **argv) {
       UNITY_BEGIN();
       RUN_TEST(test_specific_behavior);
       return UNITY_END();
   }
   ```

3. **Run new tests:**
   ```bash
   pio test -e native -f test_module_name
   ```

### Test Naming Conventions

- **Directory:** `test/test_module_name/`
- **File:** `test_module_name.cpp`
- **Functions:** `test_specific_behavior()`, `test_edge_case()`

### Mock Requirements

If your test needs Arduino APIs, ensure they're mocked in `test/mocks/`:
- Add declarations to `test/mocks/include/`
- Add implementations to `test/mocks/src/`

---

## Future Testing Improvements

### Completed ✓

- ✓ PlatformIO native unit test framework
- ✓ Arduino API mocks for host testing
- ✓ Code coverage reporting

### Planned

- CI/CD integration (GitHub Actions)
- Automated regression testing on PRs
- Hardware-in-the-loop test framework
- BLE communication mock library

---

## Related Documentation

- **docs/BLE_PROTOCOL.md** - BLE protocol specification
- **docs/SYNCHRONIZATION_PROTOCOL.md** - Sync protocol details
- **docs/LATENCY_METRICS.md** - Performance measurement guide
- **docs/API_REFERENCE.md** - Complete API documentation

---

**Version:** 2.0.0
**Last Updated:** 2026-01-19
**Test Suites:** 8 native unit test suites
