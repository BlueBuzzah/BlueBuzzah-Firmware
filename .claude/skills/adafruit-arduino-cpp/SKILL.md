---
name: adafruit-arduino-cpp
description: This skill should be used when working on ".cpp" or ".h" files in a PlatformIO project, when the user mentions "Adafruit Feather", "nRF52840", "PlatformIO", "Bluefruit", "BLE firmware", "Arduino C++", "embedded C++", or asks about non-blocking timing, BLE callbacks, I2C multiplexing, memory management, Serial debugging, or embedded anti-patterns. (project)
---

# Adafruit Arduino C++ Development

Expert guidance for writing robust, efficient C++ firmware for Adafruit nRF52840 devices using PlatformIO.

## Hardware Context

**Target**: Adafruit Feather nRF52840 Express
- **MCU**: Nordic nRF52840 (ARM Cortex-M4F @ 64MHz)
- **RAM**: 256KB (~200KB available after BLE stack)
- **Flash**: 1MB (~800KB for user code)
- **Framework**: Arduino via PlatformIO
- **Key Libraries**: Bluefruit (BLE), Adafruit DRV2605, NeoPixel, LittleFS

## Memory Management Patterns

### Pre-Allocate at Startup
Avoid dynamic allocation in loops. Pre-allocate all buffers at startup to prevent heap fragmentation.

```cpp
// GOOD: Static allocation
static char rxBuffer[256];
static StateChangeCallback callbacks[MAX_CALLBACKS];

// BAD: Dynamic allocation in loop
void loop() {
    char* buffer = new char[256];  // NEVER do this
}
```

### Fixed-Size Arrays Over Vectors
Use fixed-size arrays instead of `std::vector` or dynamic containers.

```cpp
struct Pattern {
    uint8_t sequence[MAX_FINGERS];  // Fixed 5 bytes
    float timingMs[MAX_FINGERS];    // Fixed 20 bytes
    uint8_t numFingers;
};
```

### Result Codes Over Exceptions
Embedded systems avoid exception overhead. Use enum result codes.

```cpp
enum class Result : uint8_t {
    OK = 0,
    ERROR_TIMEOUT,
    ERROR_INVALID_PARAM,
    ERROR_HARDWARE,
    ERROR_NOT_INITIALIZED
};

Result doOperation() {
    if (!initialized) return Result::ERROR_NOT_INITIALIZED;
    // ...
    return Result::OK;
}
```

## Timing-Critical Code Patterns

### NEVER Use delay() in Main Loop
`delay()` blocks BLE callbacks and causes disconnects. Use millis()-based state machines.

```cpp
// BAD: Blocking delay
void loop() {
    activateMotor();
    delay(100);        // Blocks BLE stack!
    deactivateMotor();
}

// GOOD: Non-blocking timing
uint32_t activationStart = 0;
bool motorActive = false;

void loop() {
    Bluefruit.update();  // Process BLE events

    if (motorActive && (millis() - activationStart >= 100)) {
        deactivateMotor();
        motorActive = false;
    }
}
```

### millis()-Based State Machine Pattern
Check elapsed time, execute if ready, return immediately.

```cpp
void TherapyEngine::update() {
    if (!_isRunning || _isPaused) return;

    uint32_t now = millis();

    // Check session timeout
    if (_sessionDurationSec > 0) {
        uint32_t elapsed = (now - _sessionStartTime) / 1000;
        if (elapsed >= _sessionDurationSec) {
            stop();
            return;
        }
    }

    executePatternStep();  // Does minimal work per call
}
```

### Timing State Variables
Track activation times to avoid blocking.

```cpp
bool _waitingForInterval;
uint32_t _activationStartTime;
uint32_t _intervalStartTime;
bool _motorActive;
```

## BLE Stack Patterns

### Static Dispatcher for Callbacks
Bluefruit requires C-style function pointers. Use a global instance pointer to bridge to OOP.

```cpp
// Global instance for static callbacks
BLEManager* g_bleManager = nullptr;

// Static callback bridges to instance method
static void _onConnect(uint16_t connHandle) {
    if (g_bleManager) g_bleManager->handleConnect(connHandle);
}

// Set in constructor
BLEManager::BLEManager() {
    g_bleManager = this;
}
```

### EOT-Framed Message Protocol
Use End-of-Transmission character (0x04) to frame messages over BLE UART.

```cpp
#define EOT_CHAR 0x04

void processRxByte(char c) {
    if (c == EOT_CHAR) {
        buffer[bufferIndex] = '\0';
        handleCompleteMessage(buffer);
        bufferIndex = 0;
    } else if (bufferIndex < BUFFER_SIZE - 1) {
        buffer[bufferIndex++] = c;
    }
}
```

### Scanner Flood Prevention
**Critical**: Filter scanner to prevent callback overload in busy BLE environments.

**Important**: 128-bit service UUIDs (like Nordic UART) may NOT be in the advertising packet due to the 31-byte limit. The UUID is often only discoverable after connection via GATT service discovery. Use RSSI filtering + name matching in callback instead.

```cpp
// CORRECT: RSSI filter + name matching (works with 128-bit UUIDs)
Bluefruit.Scanner.clearFilters();
Bluefruit.Scanner.filterRssi(-80);  // Only nearby devices
Bluefruit.Scanner.useActiveScan(true);  // Get scan response for name
Bluefruit.Scanner.start(0);

// In callback, match by name:
if (nameLen > 0 && strcmp(name, targetName) == 0) {
    connectToPrimary(report);
}

// INCORRECT: UUID filter may never match if UUID not in advertising packet
// Bluefruit.Scanner.filterUuid(clientUart.uuid);  // May silently fail!
```

### Scanner Health Checks
Scanner mysteriously stops sometimes. Implement periodic health checks.

```cpp
if (millis() - lastScanCheck >= 5000) {
    if (!Bluefruit.Scanner.isRunning() && getConnectionCount() == 0) {
        startScanning(targetName);  // Auto-restart
    }
    lastScanCheck = millis();
}
```

## Hardware Abstraction Patterns

### I2C Multiplexer Channel Management
**Always close channels after operations** to prevent I2C bus conflicts.

```cpp
Result HapticController::activate(uint8_t finger, uint8_t amplitude) {
    if (!selectChannel(finger)) {
        return Result::ERROR_HARDWARE;
    }

    _drv[finger].setRealtimeValue(amplitudeToRTP(amplitude));

    closeChannels();  // CRITICAL: Always close
    return Result::OK;
}
```

### Adaptive I2C Timing
Different I2C paths need different settling times. Discover empirically.

```cpp
// Channel 4 (longer path) needs more time
if (finger == FINGER_PINKY) {
    delay(I2C_INIT_DELAY_CH4_MS);  // 10ms
} else {
    delay(I2C_INIT_DELAY_MS);      // 5ms
}
```

### Retry Logic with Backoff
I2C operations can fail transiently. Implement retry logic.

```cpp
for (uint8_t attempt = 0; attempt < I2C_RETRY_COUNT; attempt++) {
    if (attempt > 0) delay(I2C_RETRY_DELAY_MS);

    if (!selectChannel(finger)) continue;
    if (_drv[finger].begin()) {
        configureDRV2605(_drv[finger]);
        closeChannels();
        return true;
    }
    closeChannels();
}
return false;  // All attempts failed
```

## State Machine Best Practices

### Deterministic Transition Table
Explicit transitions only. Invalid transitions return current state.

```cpp
TherapyState determineNextState(StateTrigger trigger) {
    switch (trigger) {
        case StateTrigger::CONNECTED:
            if (_currentState == TherapyState::IDLE) {
                return TherapyState::READY;
            }
            break;
        case StateTrigger::START:
            if (_currentState == TherapyState::READY) {
                return TherapyState::RUNNING;
            }
            break;
        // ... more transitions
    }
    return _currentState;  // No valid transition
}
```

### Battery Critical Override
Battery critical can force state change from ANY state.

```cpp
if (trigger == StateTrigger::BATTERY_CRITICAL) {
    return TherapyState::BATTERY_CRITICAL;  // From any state
}
```

### Fixed Callback Registration
Pre-allocate callback slots to avoid dynamic allocation.

```cpp
StateChangeCallback _callbacks[MAX_STATE_CALLBACKS];  // Fixed array
uint8_t _callbackCount = 0;

bool registerCallback(StateChangeCallback cb) {
    if (_callbackCount >= MAX_STATE_CALLBACKS) return false;
    _callbacks[_callbackCount++] = cb;
    return true;
}
```

## Anti-Patterns to Avoid

| Anti-Pattern | Problem | Solution |
|--------------|---------|----------|
| `delay()` in loop | Blocks BLE callbacks | millis()-based timing |
| Unfiltered scanner | Callback flood in busy areas | RSSI filter + name matching |
| UUID filter for 128-bit | Filter never matches | Use RSSI + name in callback |
| `%llu` in printf | ARM doesn't support it | Split into two `%lu` values |
| No channel close | I2C bus conflicts | Always close after operations |
| `new`/`malloc` in loop | Heap fragmentation | Pre-allocate at startup |
| Exceptions | Runtime overhead | Result codes |
| Implicit state changes | Unpredictable behavior | Explicit transition table |
| Single I2C timing | Different paths fail | Adaptive delays |

## Debugging Techniques

### Serial Output Pattern
Use structured logging with timing markers.

```cpp
Serial.print("[");
Serial.print(millis());
Serial.print("] STATE: ");
Serial.println(stateToString(currentState));
```

### LED Status Codes
Use NeoPixel for visual state indication.

```cpp
void setStatusLED(TherapyState state) {
    switch (state) {
        case IDLE:      pixel.setPixelColor(0, 0, 0, 255);   break;  // Blue
        case READY:     pixel.setPixelColor(0, 0, 255, 0);   break;  // Green
        case RUNNING:   pixel.setPixelColor(0, 255, 255, 0); break;  // Yellow
        case ERROR:     pixel.setPixelColor(0, 255, 0, 0);   break;  // Red
    }
    pixel.show();
}
```

## PlatformIO Quick Reference

```bash
pio run                    # Compile
pio run -t upload          # Flash firmware
pio device monitor         # Serial monitor (115200)
pio test -e native         # Run unit tests
pio test -e native_coverage # Tests with coverage
pio run -t clean           # Clean build
```

## Additional Resources

Detailed reference documentation available in `references/`:

- **`ble-patterns.md`** - Complete BLE callback patterns, connection handling, message framing
- **`timing-patterns.md`** - Non-blocking timing templates, session management
- **`debugging.md`** - Serial debugging, LED codes, I2C diagnostics, memory monitoring
- **`platformio-commands.md`** - Full CLI reference, test environments, coverage workflows
