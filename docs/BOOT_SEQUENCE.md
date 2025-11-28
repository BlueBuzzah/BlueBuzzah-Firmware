# BlueBuzzah v2 Boot Sequence

**Comprehensive boot sequence documentation for bilateral haptic therapy system**

Version: 2.0.0
Platform: Arduino C++ on Adafruit Feather nRF52840 Express
Last Updated: 2025-01-11

---

## Table of Contents

- [Overview](#overview)
- [Boot Sequence Requirements](#boot-sequence-requirements)
- [PRIMARY Device Boot Sequence](#primary-device-boot-sequence)
- [SECONDARY Device Boot Sequence](#secondary-device-boot-sequence)
- [LED Indicator Reference](#led-indicator-reference)
- [Connection Requirements](#connection-requirements)
- [Timeout Behavior](#timeout-behavior)
- [Error Handling](#error-handling)
- [Timing Analysis](#timing-analysis)
- [Troubleshooting](#troubleshooting)
- [Implementation Details](#implementation-details)

---

## Overview

The BlueBuzzah boot sequence establishes the necessary BLE connections between PRIMARY, SECONDARY, and optionally a phone app within a **30-second window**. Clear LED feedback provides visual indication of boot progress, success, or failure.

### Boot Sequence Goals

1. **Establish bilateral synchronization** between PRIMARY and SECONDARY devices
2. **Optional phone connection** for app control (not required for therapy)
3. **Visual feedback** via LED at every stage
4. **Safety-first approach** - fail gracefully if connections not established
5. **Automatic fallback** to default therapy if minimum requirements met

### Key Design Principles

- **Time-bounded**: 30-second maximum boot window
- **Safety-critical**: SECONDARY connection is required; phone is optional
- **User-friendly**: Clear LED indicators at each stage
- **Fail-safe**: Therapy only begins when bilateral sync is confirmed
- **Graceful degradation**: Default therapy if phone not connected

---

## Boot Sequence Requirements

### Minimum Requirements

**For PRIMARY Device:**
- Must establish connection with SECONDARY device
- Phone connection is optional

**For SECONDARY Device:**
- Must establish connection with PRIMARY device

**For System:**
- PRIMARY-SECONDARY connection required for therapy
- Sub-10ms bilateral synchronization after boot
- 30-second timeout enforced strictly

### Success Criteria

| Scenario | PRIMARY | SECONDARY | Phone | Result |
|----------|---------|-----------|-------|--------|
| Full Success | ✓ | ✓ | ✓ | App control + default therapy available |
| Partial Success | ✓ | ✓ | ✗ | Automatic default therapy starts |
| PRIMARY Failure | ? | ✗ | ? | Boot fails (slow red LED) |
| SECONDARY Failure | ✗ | ? | ✗ | Boot fails (slow red LED) |

---

## PRIMARY Device Boot Sequence

The PRIMARY device acts as the central coordinator, advertising its presence and waiting for connections from both SECONDARY and phone.

### State Diagram

```mermaid
stateDiagram-v2
    [*] --> Initializing: Power On

    Initializing --> Advertising: BLE Init Complete
    note right of Advertising: LED: Rapid Blue Flash

    Advertising --> WaitingForDevices: Start Advertisement
    note right of WaitingForDevices: Timeout: 30 seconds

    WaitingForDevices --> SecondaryConnected: SECONDARY Connects
    note right of SecondaryConnected: LED: 5x Green Flash

    SecondaryConnected --> WaitingForPhone: No Phone Yet
    note right of WaitingForPhone: LED: Slow Blue Flash

    WaitingForPhone --> BothConnected: Phone Connects
    WaitingForPhone --> TherapyMode: 30s Timeout

    SecondaryConnected --> BothConnected: Phone Connects

    BothConnected --> Ready: All Connected
    TherapyMode --> Ready: Therapy Ready
    note right of Ready: LED: Solid Green

    WaitingForDevices --> Failed: 30s Timeout\n(no SECONDARY)
    note right of Failed: LED: Slow Red Flash

    Failed --> [*]: Halt
    Ready --> ExecutingTherapy: Start Therapy
```

### Detailed Steps

#### 1. Initialization Phase

**Duration**: 1-2 seconds

```cpp
// src/main.cpp

BootResult primaryBootSequence() {
    Serial.println(F("[PRIMARY] Starting boot sequence"));

    // Initialize LED
    ledController.indicateBLEInit();  // Rapid blue flash

    // Initialize BLE
    if (!bleManager.begin(deviceConfig)) {
        ledController.indicateFailure();
        return BootResult::FAILED;
    }

    // Start advertising
    bleManager.startAdvertising();
    // ...
}
```

**LED Indicator**: Rapid blue flash (10Hz)
**Purpose**: Initialize BLE stack and begin advertising

---

#### 2. Connection Wait Phase

**Duration**: 0-30 seconds (depends on connection timing)

```cpp
// src/main.cpp

uint32_t startTime = millis();
bool secondaryConnected = false;
bool phoneConnected = false;

while (millis() - startTime < CONNECTION_TIMEOUT_MS) {
    // Check for new connections (handled via callbacks)
    if (bleManager.hasSecondaryConnection() && !secondaryConnected) {
        secondaryConnected = true;
        ledController.indicateConnectionSuccess();  // 5x green flash

        if (!phoneConnected) {
            ledController.indicateWaitingForPhone();  // Slow blue flash
        }
    }

    if (bleManager.hasPhoneConnection() && !phoneConnected) {
        phoneConnected = true;
    }

    // Small delay to prevent busy loop
    delay(10);
}
```

**LED Indicators**:
- **During wait**: Rapid blue flash continues
- **SECONDARY connects**: 5x green flash (success acknowledgment)
- **Waiting for phone**: Slow blue flash (1Hz)

**Purpose**: Accept connections from SECONDARY and phone within timeout window

---

#### 3. Timeout Evaluation

**At 30 seconds**: Evaluate connection status and determine outcome

```cpp
// Check if timeout reached
if (millis() - startTime >= CONNECTION_TIMEOUT_MS) {
    if (!secondaryConnected) {
        // CRITICAL FAILURE: No SECONDARY
        Serial.println(F("[PRIMARY] ERROR: SECONDARY not connected"));
        ledController.indicateFailure();  // Slow red flash
        return BootResult::FAILED;
    }

    // SECONDARY connected, proceed with or without phone
}
```

**Outcomes**:
- **No SECONDARY**: FAILED (slow red LED, halt)
- **SECONDARY only**: SUCCESS_NO_PHONE (proceed to default therapy)
- **SECONDARY + phone**: SUCCESS_WITH_PHONE (wait for app commands)

---

#### 4. Ready State

**Final LED**: Solid green

```cpp
// Determine final state
ledController.indicateReady();  // Solid green

if (secondaryConnected && phoneConnected) {
    Serial.println(F("[PRIMARY] All connections established"));
    return BootResult::SUCCESS_WITH_PHONE;
} else if (secondaryConnected) {
    Serial.println(F("[PRIMARY] SECONDARY connected, no phone"));
    return BootResult::SUCCESS_NO_PHONE;
}
```

**Purpose**: Signal that device is ready to begin therapy

---

### PRIMARY Timeline Example

```
Time  | State                | LED                    | Connections
------|----------------------|------------------------|------------------
0.0s  | Initializing         | Rapid Blue Flash       | None
1.0s  | Advertising          | Rapid Blue Flash       | None
3.5s  | SECONDARY Connected  | 5x Green Flash         | SECONDARY
4.0s  | Waiting for Phone    | Slow Blue Flash        | SECONDARY
15.0s | Phone Connected      | (transition)           | SECONDARY + Phone
15.5s | Ready                | Solid Green            | SECONDARY + Phone
```

**Alternative Scenario** (No Phone):
```
Time  | State                | LED                    | Connections
------|----------------------|------------------------|------------------
0.0s  | Initializing         | Rapid Blue Flash       | None
1.0s  | Advertising          | Rapid Blue Flash       | None
3.5s  | SECONDARY Connected  | 5x Green Flash         | SECONDARY
4.0s  | Waiting for Phone    | Slow Blue Flash        | SECONDARY
30.0s | Timeout (no phone)   | (transition)           | SECONDARY
30.5s | Ready                | Solid Green            | SECONDARY
```

---

## SECONDARY Device Boot Sequence

The SECONDARY device actively scans for and connects to the PRIMARY device advertising as "BlueBuzzah".

### State Diagram

```mermaid
stateDiagram-v2
    [*] --> Initializing: Power On

    Initializing --> Scanning: BLE Init Complete
    note right of Scanning: LED: Rapid Blue Flash\nTimeout: 30 seconds

    Scanning --> Connected: Found PRIMARY
    note right of Connected: LED: 5x Green Flash

    Connected --> Ready: Connection Confirmed
    note right of Ready: LED: Solid Green

    Scanning --> Failed: 30s Timeout\n(PRIMARY not found)
    note right of Failed: LED: Slow Red Flash

    Failed --> [*]: Halt
    Ready --> AwaitingCommands: Wait for PRIMARY
```

### Detailed Steps

#### 1. Initialization Phase

**Duration**: 1-2 seconds

```cpp
// src/main.cpp

BootResult secondaryBootSequence() {
    Serial.println(F("[SECONDARY] Starting boot sequence"));

    // Initialize LED
    ledController.indicateBLEInit();  // Rapid blue flash

    // Initialize BLE
    if (!bleManager.begin(deviceConfig)) {
        ledController.indicateFailure();
        return BootResult::FAILED;
    }

    // ...
}
```

**LED Indicator**: Rapid blue flash (10Hz)
**Purpose**: Initialize BLE stack for scanning

---

#### 2. Scanning Phase

**Duration**: 0-30 seconds (until PRIMARY found or timeout)

```cpp
uint32_t startTime = millis();
bool connected = false;

while (millis() - startTime < CONNECTION_TIMEOUT_MS) {
    // Scan for PRIMARY device
    if (bleManager.scanAndConnect("BlueBuzzah", 1000)) {
        connected = true;

        // Success indicators
        ledController.indicateConnectionSuccess();  // 5x green flash
        delay(500);  // Brief pause after success flash
        ledController.indicateReady();  // Solid green

        Serial.println(F("[SECONDARY] Connected to PRIMARY"));
        return BootResult::SUCCESS;
    }

    // Small delay between scan attempts
    delay(100);
}
```

**LED Indicator**: Rapid blue flash (continues during scanning)
**Purpose**: Find and connect to PRIMARY device

---

#### 3. Connection Success

**Duration**: < 1 second

**LED Sequence**:
1. 5x green flash (success acknowledgment, synchronized with PRIMARY)
2. Brief pause (0.5s)
3. Solid green (ready state)

```cpp
if (connected) {
    ledController.indicateConnectionSuccess();  // 5x green flash
    delay(500);
    ledController.indicateReady();  // Solid green
    return BootResult::SUCCESS;
}
```

**Purpose**: Confirm connection establishment to user

---

#### 4. Timeout Failure

**At 30 seconds**: If PRIMARY not found

```cpp
// Timeout - connection failed
Serial.println(F("[SECONDARY] ERROR: PRIMARY not found within timeout"));
ledController.indicateFailure();  // Slow red flash
return BootResult::FAILED;
```

**LED Indicator**: Slow red flash (1Hz)
**Purpose**: Indicate boot failure, require restart

---

### SECONDARY Timeline Example

**Successful Boot**:
```
Time  | State                | LED                    | Status
------|----------------------|------------------------|------------------
0.0s  | Initializing         | Rapid Blue Flash       | BLE init
1.0s  | Scanning             | Rapid Blue Flash       | Looking for PRIMARY
3.5s  | Found PRIMARY        | Rapid Blue Flash       | Attempting connection
4.0s  | Connected            | 5x Green Flash         | Connection success
4.5s  | Ready                | Solid Green            | Awaiting commands
```

**Failed Boot** (PRIMARY not found):
```
Time  | State                | LED                    | Status
------|----------------------|------------------------|------------------
0.0s  | Initializing         | Rapid Blue Flash       | BLE init
1.0s  | Scanning             | Rapid Blue Flash       | Looking for PRIMARY
...   | Scanning             | Rapid Blue Flash       | Still searching
30.0s | Timeout              | (transition)           | PRIMARY not found
30.5s | Failed               | Slow Red Flash         | Boot failed
```

---

## LED Indicator Reference

### Complete LED Color and Pattern Guide

| State | Color | Pattern | Frequency | Meaning |
|-------|-------|---------|-----------|---------|
| **BLE Initialization** | Blue | Rapid Flash | 10 Hz | BLE stack initializing |
| **Connection Success** | Green | 5x Flash | 5 Hz | PRIMARY-SECONDARY connected |
| **Waiting for Phone** | Blue | Slow Flash | 1 Hz | SECONDARY connected, waiting for phone |
| **Ready** | Green | Solid | - | Boot complete, ready for therapy |
| **Connection Failed** | Red | Slow Flash | 1 Hz | Boot failed, restart required |

### LED Pattern Details

#### Rapid Blue Flash (BLE Init)
```cpp
// Pattern: 100ms on, 100ms off
ledController.rapidFlash(LED_BLUE, 10.0f);
```
- **Duration**: Until connection attempt completes
- **Purpose**: Indicates active BLE operations
- **Devices**: Both PRIMARY and SECONDARY

---

#### 5x Green Flash (Connection Success)
```cpp
// Pattern: 5 flashes, 100ms on, 100ms off each
ledController.flashCount(LED_GREEN, 5);
```
- **Duration**: ~1 second (5 × 200ms)
- **Purpose**: Confirms PRIMARY-SECONDARY connection
- **Devices**: Both PRIMARY and SECONDARY (synchronized)
- **Timing**: Synchronized within sub-10ms

---

#### Slow Blue Flash (Waiting for Phone)
```cpp
// Pattern: 500ms on, 500ms off
ledController.slowFlash(LED_BLUE);
```
- **Duration**: Until phone connects or 30s timeout
- **Purpose**: Indicates waiting for phone connection
- **Devices**: PRIMARY only

---

#### Solid Green (Ready)
```cpp
// Pattern: Continuous solid green
ledController.setColor(LED_GREEN);
```
- **Duration**: Until therapy starts
- **Purpose**: Boot complete, system ready
- **Devices**: Both PRIMARY and SECONDARY
- **Transition**: Changes to breathing green when therapy starts

---

#### Slow Red Flash (Failure)
```cpp
// Pattern: 500ms on, 500ms off
ledController.slowFlash(LED_RED);
```
- **Duration**: Continuous (until power cycle)
- **Purpose**: Boot sequence failed
- **Devices**: Both PRIMARY and SECONDARY (when applicable)
- **Recovery**: Requires device restart

---

### LED Pattern Timing Diagram

```
PRIMARY Boot (Success with Phone):
0s     5s     10s    15s    20s    25s    30s
|------|------|------|------|------|------|
[Rapid Blue Flash................]
                 ^
                 SECONDARY connects
                 [5x Green]
                          [Slow Blue Flash...]
                                         ^
                                         Phone connects
                                         [Solid Green]

PRIMARY Boot (Success without Phone):
0s     5s     10s    15s    20s    25s    30s
|------|------|------|------|------|------|
[Rapid Blue Flash................]
                 ^
                 SECONDARY connects
                 [5x Green]
                          [Slow Blue Flash.............]
                                                      ^
                                                      Timeout
                                                      [Solid Green]

SECONDARY Boot (Success):
0s     5s     10s    15s    20s    25s    30s
|------|------|------|------|------|------|
[Rapid Blue Flash...]
                 ^
                 Found PRIMARY
                 [5x Green]
                          [Solid Green...............]

PRIMARY/SECONDARY Boot (Failure):
0s     5s     10s    15s    20s    25s    30s
|------|------|------|------|------|------|
[Rapid Blue Flash................................]
                                              ^
                                              Timeout (no connection)
                                              [Slow Red Flash....]
```

---

## Connection Requirements

### BLE Advertisement Parameters (PRIMARY)

```cpp
// src/ble_manager.cpp

void BLEManager::startAdvertising() {
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(_bleuart);
    Bluefruit.Advertising.addName();

    Bluefruit.Advertising.setInterval(800, 800);  // 500ms interval (in 0.625ms units)
    Bluefruit.Advertising.setFastTimeout(30);     // Advertise for 30 seconds
    Bluefruit.Advertising.start(30);              // 30 second timeout
}
```

**Rationale**:
- 500ms interval balances discoverability and power consumption
- Advertises for full 30-second boot window
- Name "BlueBuzzah" identifies device to SECONDARY and phone

---

### BLE Scanning Parameters (SECONDARY)

```cpp
// src/ble_manager.cpp

bool BLEManager::scanAndConnect(const char* targetName, uint32_t timeoutMs) {
    Bluefruit.Scanner.setRxCallback(scanCallback);
    Bluefruit.Scanner.filterRssi(-80);
    Bluefruit.Scanner.setInterval(160, 80);  // 100ms interval, 50ms window
    Bluefruit.Scanner.start(timeoutMs / 1000);

    // Wait for callback or timeout
    // ...
}
```

**Rationale**:
- 1-second scan periods provide good discovery rate
- 100ms interval between scans avoids excessive power use
- Active scanning gets additional device information
- 30-second timeout matches boot window

---

### Connection Validation

Both devices validate connections after establishment:

```cpp
// src/ble_manager.cpp

bool BLEManager::validateConnection(uint16_t connHandle) {
    BLEConnection* conn = Bluefruit.Connection(connHandle);

    // Check connection parameters
    uint16_t intervalMs = conn->getConnectionInterval() * 1.25;
    if (intervalMs > 10) {
        Serial.printf("[WARN] High connection interval (%dms)\n", intervalMs);
    }

    // Verify service discovery
    if (!_bleuart.discover(connHandle)) {
        Serial.println(F("[ERROR] UART service not found"));
        return false;
    }

    return true;
}
```

---

### Initial Time Synchronization

After PRIMARY-SECONDARY connection, initial time sync is established:

```cpp
// src/sync_protocol.cpp

bool SyncProtocol::establishInitialSync() {
    uint32_t primaryTime = millis();

    // Send FIRST_SYNC message
    char syncMsg[32];
    snprintf(syncMsg, sizeof(syncMsg), "FIRST_SYNC:%lu\n", primaryTime);
    _bleManager.sendToSecondary(syncMsg);

    // Wait for ACK with SECONDARY timestamp
    char response[64];
    if (!receiveWithTimeout(response, sizeof(response), 2000)) {
        return false;
    }

    // Parse response and calculate offset
    uint32_t secondaryTime = 0;
    if (sscanf(response, "ACK:%lu", &secondaryTime) == 1) {
        _timeOffset = calculateOffset(primaryTime, secondaryTime);
        Serial.printf("[SYNC] Initial offset: %ldms\n", _timeOffset);
        return true;
    }

    return false;
}
```

This ensures sub-10ms bilateral synchronization before therapy begins.

---

## Timeout Behavior

### 30-Second Boot Window

The 30-second boot window is **strictly enforced** and begins immediately after BLE initialization completes.

```cpp
uint32_t startTime = millis();
const uint32_t CONNECTION_TIMEOUT_MS = 30000;

while (millis() - startTime < CONNECTION_TIMEOUT_MS) {
    // Connection attempts
    // ...
}
```

---

### Timeout Scenarios

#### PRIMARY Timeout Scenarios

| Scenario | Connections | Timeout Action | LED | Result |
|----------|-------------|----------------|-----|--------|
| No devices | None | Fail | Slow Red | FAILED |
| SECONDARY only | SECONDARY | Proceed | Solid Green | SUCCESS_NO_PHONE |
| SECONDARY + Phone | Both | Proceed | Solid Green | SUCCESS_WITH_PHONE |

**Code**:
```cpp
if (timeoutReached) {
    if (!secondaryConnected) {
        ledController.indicateFailure();
        return BootResult::FAILED;
    } else {
        ledController.indicateReady();
        return phoneConnected ? BootResult::SUCCESS_WITH_PHONE
                              : BootResult::SUCCESS_NO_PHONE;
    }
}
```

---

#### SECONDARY Timeout Scenarios

| Scenario | Connection | Timeout Action | LED | Result |
|----------|------------|----------------|-----|--------|
| PRIMARY found | Connected | N/A (success before timeout) | Solid Green | SUCCESS |
| PRIMARY not found | None | Fail | Slow Red | FAILED |

**Code**:
```cpp
if (timeoutReached) {
    if (!connected) {
        ledController.indicateFailure();
        return BootResult::FAILED;
    }
}
```

---

### Timeout Extension

**The boot window is NOT extended** even if connections are partially established. This ensures predictable boot behavior.

**Rationale**:
- Predictable user experience
- Prevents indefinite waiting
- Encourages reliable hardware placement
- Allows automatic default therapy if phone not available

---

## Error Handling

### Connection Failure Modes

#### 1. No SECONDARY Connection (PRIMARY)

**Symptom**: PRIMARY times out with no SECONDARY connection

**Causes**:
- SECONDARY not powered on
- SECONDARY out of range
- BLE interference
- Hardware failure

**LED**: Slow red flash

**Recovery**: Power cycle both devices

```cpp
if (!secondaryConnected) {
    Serial.println(F("[PRIMARY] CRITICAL: SECONDARY not connected"));
    Serial.println(F("[PRIMARY] Cannot proceed without bilateral synchronization"));
    ledController.indicateFailure();
    return BootResult::FAILED;
}
```

---

#### 2. No PRIMARY Found (SECONDARY)

**Symptom**: SECONDARY times out without finding PRIMARY

**Causes**:
- PRIMARY not powered on
- PRIMARY out of range
- BLE interference
- Hardware failure

**LED**: Slow red flash

**Recovery**: Power cycle both devices

```cpp
if (!connected) {
    Serial.println(F("[SECONDARY] CRITICAL: PRIMARY not found"));
    Serial.println(F("[SECONDARY] Ensure PRIMARY is powered on and in range"));
    ledController.indicateFailure();
    return BootResult::FAILED;
}
```

---

#### 3. Connection Drops During Boot

**Symptom**: Connection established but drops before boot completes

**Causes**:
- BLE interference
- Devices moved out of range
- Low battery
- Hardware issue

**Detection**:
```cpp
void monitorConnectionDuringBoot() {
    if (!bleManager.isSecondaryConnected()) {
        Serial.println(F("[PRIMARY] WARNING: SECONDARY connection lost during boot"));
        ledController.indicateFailure();
        return BootResult::FAILED;
    }
}
```

**Recovery**: Power cycle both devices

---

#### 4. Invalid Connection Parameters

**Symptom**: Connection established but parameters not suitable for therapy

**Causes**:
- BLE negotiation failure
- Incompatible BLE settings
- Firmware version mismatch

**Detection**:
```cpp
bool validateConnectionParams(BLEConnection* conn) {
    uint16_t interval = conn->getConnectionInterval();
    uint16_t latency = conn->getSlaveLatency();
    uint16_t timeout = conn->getSupervisionTimeout();

    if (interval > 8) {  // Need < 10ms for sub-10ms sync (interval in 1.25ms units)
        Serial.printf("[ERROR] Connection interval too high (%dms)\n",
                      (int)(interval * 1.25));
        return false;
    }

    if (latency > 0) {  // Need zero latency for real-time sync
        Serial.printf("[ERROR] Slave latency not zero (%d)\n", latency);
        return false;
    }

    return true;
}
```

**Recovery**: Retry connection or power cycle

---

### Error Logging

All boot errors are logged with detailed context:

```cpp
void logBootError(const char* errorType, uint32_t timestamp) {
    Serial.printf("[BOOT ERROR] %.2fs - %s\n",
                  timestamp / 1000.0f, errorType);

    BatteryStatus battery = hardware.getBatteryStatus();
    Serial.printf("  battery_voltage: %.2fV\n", battery.voltage);
    Serial.printf("  firmware_version: %s\n", FIRMWARE_VERSION);
}
```

**Example Log**:
```
[BOOT ERROR] 30.15s - SECONDARY_NOT_FOUND
  timeout: 30.0s
  battery_voltage: 3.7V
  firmware_version: 2.0.0
```

---

## Timing Analysis

### Boot Sequence Performance Targets

| Metric | Target | Typical | Maximum |
|--------|--------|---------|---------|
| BLE Init | < 2s | 1.5s | 3s |
| Connection Time | < 5s | 3s | 30s |
| Initial Sync | < 1s | 0.5s | 2s |
| Total Boot (success) | < 10s | 5s | 30s |
| Total Boot (failure) | 30s | 30s | 30s |

---

### Typical Boot Timeline (Success)

```
Event                          | Time    | Duration | Cumulative
-------------------------------|---------|----------|------------
Power On                       | 0.0s    | -        | 0.0s
Arduino Boot                   | 0.0s    | 0.3s     | 0.3s
BLE Stack Init                 | 0.3s    | 1.0s     | 1.3s
Start Advertising/Scanning     | 1.3s    | 0.1s     | 1.4s
Device Discovery               | 1.4s    | 1.5s     | 2.9s
Connection Establishment       | 2.9s    | 0.5s     | 3.4s
Service Discovery              | 3.4s    | 0.3s     | 3.7s
Initial Sync                   | 3.7s    | 0.5s     | 4.2s
LED Success Indicators         | 4.2s    | 1.0s     | 5.2s
Boot Complete                  | 5.2s    | -        | 5.2s
```

**Total Boot Time (Typical Success)**: ~5.2 seconds

---

### Worst-Case Boot Timeline (Failure)

```
Event                          | Time    | Duration | Cumulative
-------------------------------|---------|----------|------------
Power On                       | 0.0s    | -        | 0.0s
Arduino Boot                   | 0.0s    | 0.3s     | 0.3s
BLE Stack Init                 | 0.3s    | 2.0s     | 2.3s
Start Advertising/Scanning     | 2.3s    | 0.1s     | 2.4s
Scanning (no device found)     | 2.4s    | 27.6s    | 30.0s
Timeout Reached                | 30.0s   | -        | 30.0s
LED Failure Indication         | 30.0s   | 0.5s     | 30.5s
Boot Failed                    | 30.5s   | -        | 30.5s
```

**Total Boot Time (Failure)**: 30.5 seconds

---

### BLE Connection Latency

**Connection Interval**: 7.5ms (BLE_INTERVAL)
**Supervision Timeout**: 100ms (BLE_TIMEOUT)
**Slave Latency**: 0 (BLE_LATENCY)

**Effective Latency**:
- Minimum: 7.5ms (one connection interval)
- Maximum: 15ms (two connection intervals)
- Average: ~10ms

**Sub-10ms Synchronization**: Achieved through:
1. 7.5ms BLE connection interval
2. Time offset calculation and compensation
3. Predictive timing adjustment
4. Periodic resynchronization (SYNC_ADJ every macrocycle)

---

## Troubleshooting

### Problem: Devices Stuck on Blue Flashing LED

**Symptoms**:
- LED continues rapid blue flash indefinitely
- No connection established
- Both devices powered on

**Possible Causes**:
1. Devices out of range (> 1-2 meters)
2. BLE interference (Wi-Fi, other devices)
3. One device not actually powered on
4. Settings file missing or corrupt
5. Hardware failure

**Solutions**:

1. **Verify Both Devices Powered On**:
   - Check that both LEDs are flashing blue
   - Verify battery voltage (> 3.4V)
   - Check USB connections if using USB power

2. **Reduce Distance**:
   - Place devices within 0.5 meters
   - Remove obstructions between devices
   - Avoid metal surfaces

3. **Check Settings Files**:
   - Use serial monitor to verify device role is correct
   - Check that settings.json exists in LittleFS

4. **Reduce BLE Interference**:
   - Move away from Wi-Fi routers
   - Turn off nearby Bluetooth devices
   - Avoid 2.4GHz congested areas

5. **Power Cycle Both Devices**:
   - Turn off both devices
   - Wait 5 seconds
   - Power on simultaneously

---

### Problem: Devices Show Red Flashing LED After 30 Seconds

**Symptoms**:
- LED shows slow red flash after timeout
- Boot sequence failed
- Devices not communicating

**Possible Causes**:
1. PRIMARY and SECONDARY roles not configured correctly
2. Devices not powered on simultaneously
3. Critical BLE hardware failure
4. Severe BLE interference

**Solutions**:

1. **Verify Device Roles**:
   - Check serial monitor for device role output
   - PRIMARY should show `[PRIMARY]` tag
   - SECONDARY should show `[SECONDARY]` tag

2. **Reflash Firmware**:
   ```bash
   pio run -t upload
   ```

3. **Test Hardware**:
   - Check serial monitor for I2C errors
   - Verify BLE initialization succeeds

---

### Problem: PRIMARY Shows Green, SECONDARY Shows Red

**Symptoms**:
- PRIMARY LED is solid green
- SECONDARY LED is slow red flash
- Unilateral connection (one-way)

**Possible Causes**:
1. SECONDARY powered on late (after PRIMARY timeout)
2. Connection established but immediately dropped
3. Asymmetric hardware failure

**Solutions**:

1. **Synchronize Power-On**:
   - Turn off both devices
   - Power on both simultaneously
   - PRIMARY should see SECONDARY within 5 seconds

2. **Check SECONDARY BLE**:
   - Check serial monitor for scan results
   - Verify "BlueBuzzah" device is found

---

### Problem: Boot Succeeds But Therapy Fails

**Symptoms**:
- Boot completes successfully (green LED)
- Therapy starts but motors don't activate
- Or therapy immediately errors

**Possible Causes**:
1. Haptic hardware not initialized
2. I2C communication failure
3. Bilateral synchronization lost
4. Initial sync failed

**Solutions**:

1. **Test Haptic Hardware**:
   - Enter calibration mode
   - Test each finger individually

2. **Verify I2C Communication**:
   - Check serial monitor for I2C addresses
   - Should see 0x70 (multiplexer), 0x5A (DRV2605 per channel)

3. **Check Sync Protocol**:
   - Verify time offset was established
   - Check for SYNC errors in serial output

---

### Problem: Intermittent Boot Failures

**Symptoms**:
- Boot sometimes succeeds, sometimes fails
- No consistent pattern
- Hardware seems functional

**Possible Causes**:
1. Environmental BLE interference varies
2. Battery voltage borderline low
3. Timing-dependent race condition
4. Memory fragmentation

**Solutions**:

1. **Monitor Battery Voltage**:
   - Check serial output for battery status
   - Should be > 3.5V for reliable operation

2. **Enable Debug Logging**:
   - Add Serial.print statements to track boot progress

3. **Increase Boot Timeout** (temporary diagnostic):
   - Modify CONNECTION_TIMEOUT_MS in config.h

---

## Implementation Details

### Boot Sequence in main.cpp

The boot sequence is orchestrated in `main.cpp`:

```cpp
// src/main.cpp

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    // 1. Initialize filesystem
    if (!LittleFS.begin()) {
        Serial.println(F("[ERROR] LittleFS mount failed"));
    }

    // 2. Load configuration
    deviceConfig = loadDeviceConfig();
    Serial.printf("[INFO] Role: %s\n", deviceConfig.deviceTag);

    // 3. Initialize hardware
    if (!hardware.begin()) {
        ledController.indicateFailure();
        while (true) { delay(1000); }
    }

    ledController.begin();

    // 4. Execute boot sequence based on role
    if (deviceConfig.role == DeviceRole::PRIMARY) {
        bootResult = primaryBootSequence();
    } else {
        bootResult = secondaryBootSequence();
    }

    if (bootResult == BootResult::FAILED) {
        ledController.indicateFailure();
        while (true) { delay(1000); }
    }

    ledController.indicateReady();
    Serial.println(F("[INFO] Boot complete, entering main loop"));
}
```

---

### LED Controller Class

```cpp
// include/led_controller.h

class LEDController {
public:
    void begin();

    void indicateBLEInit();           // Rapid blue flash
    void indicateConnectionSuccess(); // 5x green flash
    void indicateWaitingForPhone();   // Slow blue flash
    void indicateReady();             // Solid green
    void indicateFailure();           // Slow red flash

private:
    Adafruit_NeoPixel _pixel;
    void rapidFlash(uint32_t color, float frequency);
    void slowFlash(uint32_t color);
    void flashCount(uint32_t color, uint8_t count);
    void setColor(uint32_t color);
};
```

---

## Best Practices

### For Reliable Boot

1. **Power on devices simultaneously** (within 1-2 seconds)
2. **Keep devices close** (< 1 meter) during boot
3. **Ensure good battery charge** (> 3.5V)
4. **Avoid BLE interference** (Wi-Fi routers, other Bluetooth devices)
5. **Use clean power source** (good battery or stable USB)
6. **Monitor LED feedback** for diagnostic information

### For Troubleshooting

1. **Check LED patterns** first - they tell the story
2. **Monitor serial console** for detailed logs
3. **Test one component at a time** (BLE, haptic, etc.)
4. **Use calibration mode** to verify hardware
5. **Check settings files** before assuming hardware failure

### For Development

1. **Use mock implementations** for testing without hardware
2. **Enable debug logging** for detailed diagnostics
3. **Monitor timing** with millis() measurements
4. **Test timeout scenarios** explicitly
5. **Validate all LED patterns** visually

---

## Summary

The BlueBuzzah boot sequence is a **safety-critical** operation that establishes bilateral synchronization between PRIMARY and SECONDARY devices. Key points:

- **30-second timeout** strictly enforced
- **Clear LED feedback** at every stage
- **Fail-safe design** - therapy only starts with confirmed bilateral sync
- **Graceful degradation** - default therapy if phone not connected
- **Comprehensive error handling** with recovery guidance

The boot sequence ensures that therapy can only begin when:
1. PRIMARY and SECONDARY are connected
2. Bilateral time synchronization is established
3. All hardware is initialized and validated
4. User has clear visual confirmation of system readiness

---

## Additional Resources

- [API Reference](API_REFERENCE.md) - Complete API documentation
- [Architecture Guide](ARCHITECTURE.md) - System design and patterns

---

**Platform**: Arduino C++ with PlatformIO
**Last Updated**: 2025-01-11
**Document Version**: 2.0.0
