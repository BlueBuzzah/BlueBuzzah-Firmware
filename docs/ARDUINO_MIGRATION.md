# BlueBuzzah Firmware Migration Plan

## CircuitPython → Arduino + Adafruit nRF52 BSP

**Version:** 1.0.0  
**Created:** 2024-11-27  
**Target Platform:** Adafruit Feather nRF52840 Express  
**Reason for Migration:** Memory constraints in CircuitPython preventing feature completion

---

## Project Locations

| Component                            | Path                    |
| ------------------------------------ | ----------------------- |
| **Arduino project (TARGET)**         | `./BlueBuzzah-Arduino/` |
| **CircuitPython source (REFERENCE)** | `./src/`                |
| **CircuitPython docs (REFERENCE)**   | `./docs/`               |

**Important:** The Arduino project lives inside the existing CircuitPython repository. When migrating:

- Read source files from `./src/*.py`
- Write C++ files to `./BlueBuzzah-Arduino/src/` and `./BlueBuzzah-Arduino/include/`
- Reference documentation in `./docs/` for architecture details

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Current Architecture Overview](#current-architecture-overview)
3. [Target Architecture](#target-architecture)
4. [Dependencies](#dependencies)
5. [Work Packages](#work-packages)
6. [File-by-File Migration Guide](#file-by-file-migration-guide)
7. [BLE API Translation Reference](#ble-api-translation-reference)
8. [Sync Protocol Specification](#sync-protocol-specification)
9. [Testing Checkpoints](#testing-checkpoints)
10. [Risk Mitigation](#risk-mitigation)

---

## Executive Summary

This document provides a complete migration plan for porting BlueBuzzah firmware from CircuitPython to Arduino C++ using the Adafruit nRF52 Board Support Package (BSP). The migration is driven by memory constraints—CircuitPython's interpreter consumes ~100-150KB RAM before application code runs, leaving insufficient headroom for the full feature set.

**Expected outcome:**

- RAM usage reduced from ~200KB to ~25KB
- Full feature implementation possible
- Improved timing precision for therapy execution
- Maintainable codebase with extensive library support

**Estimated effort:** 15-20 hours across 5 work packages

---

## Current Architecture Overview

### Source Files to Migrate

| File          | Lines | Purpose                                      | Migration Complexity |
| ------------- | ----- | -------------------------------------------- | -------------------- |
| `app.py`      | 1530  | Main orchestrator, boot sequence, main loops | High (largest file)  |
| `hardware.py` | 637   | DRV2605, I2C mux, battery, NeoPixel          | Low                  |
| `ble.py`      | 454   | BLE radio, UART service, connections         | Medium               |
| `therapy.py`  | 697   | Pattern generation, therapy execution        | Medium               |
| `sync.py`     | 250   | PRIMARY↔SECONDARY protocol                   | Low                  |
| `state.py`    | ~200  | Therapy state machine (11 states)            | Low                  |
| `menu.py`     | ~400  | BLE command processing                       | Low                  |
| `profiles.py` | ~300  | Therapy profile management                   | Low                  |
| `led.py`      | ~250  | LED animations and states                    | Low                  |

### Hardware Configuration

```
Adafruit Feather nRF52840 Express
├── NeoPixel LED (board.NEOPIXEL)
├── Battery Monitor (board.VOLTAGE_MONITOR / A6)
└── I2C Bus (400kHz)
    └── TCA9548A Multiplexer @ 0x70
        ├── Channel 0: DRV2605 (Thumb) @ 0x5A
        ├── Channel 1: DRV2605 (Index) @ 0x5A
        ├── Channel 2: DRV2605 (Middle) @ 0x5A
        ├── Channel 3: DRV2605 (Ring) @ 0x5A
        └── Channel 4: DRV2605 (Pinky) @ 0x5A
```

### BLE Architecture

- **PRIMARY device:** Advertises as "BlueBuzzah", accepts connections from SECONDARY + phone
- **SECONDARY device:** Scans for "BlueBuzzah", connects to PRIMARY
- **Service:** Nordic UART Service (NUS) for bidirectional communication
- **Framing:** Messages terminated with EOT (0x04)

---

## Target Architecture

### Project Structure

The Arduino project is located at `./BlueBuzzah-Arduino/` within the existing repository:

```
docs/
├── ARDUINO_FIRMWARE_ARCHITECTURE.md
└── ARDUINO_MIGRATION.md          # This file
BlueBuzzah-CircuitPython/                  # Existing repo root
├── src/                               # CircuitPython source (REFERENCE)
│   ├── app.py
│   ├── hardware.py
│   ├── ble.py
│   ├── therapy.py
│   ├── sync.py
│   └── ...
├── docs/                              # Architecture docs (REFERENCE)
│   ├── FIRMWARE_ARCHITECTURE.md
│   └── ...
BlueBuzzah-Arduino/                # Arduino project (TARGET)
├── platformio.ini                 # PlatformIO configuration
├── include/
│   ├── config.h                   # Pin definitions, constants
│   ├── types.h                    # Enums, structs (DeviceRole, TherapyState, etc.)
│   ├── hardware.h                 # Hardware class declarations
│   ├── ble_manager.h              # BLE management class
│   ├── therapy_engine.h           # Therapy execution class
│   ├── sync_protocol.h            # Sync command handling
│   ├── state_machine.h            # Therapy state machine
│   ├── menu_controller.h          # Command processing
│   ├── profile_manager.h          # Profile handling
│   └── led_controller.h           # LED animations
├── src/
│   ├── main.cpp                   # Entry point, setup(), loop()
│   ├── hardware.cpp               # Hardware implementations
├── ble_manager.cpp            # BLE implementations
├── therapy_engine.cpp         # Therapy implementations
├── sync_protocol.cpp          # Sync implementations
├── state_machine.cpp          # State machine implementations
├── menu_controller.cpp        # Menu implementations
├── profile_manager.cpp        # Profile implementations
├── led_controller.cpp         # LED implementations
└── data/
    └── settings.json              # Device configuration (role, etc.)
```

### PlatformIO Configuration

The `./BlueBuzzah-Arduino/platformio.ini` should contain:

```ini
; platformio.ini
[env:adafruit_feather_nrf52840]
platform = nordicnrf52
board = adafruit_feather_nrf52840
framework = arduino

lib_deps =
    adafruit/Adafruit DRV2605 Library@^1.2.4
    adafruit/Adafruit NeoPixel@^1.12.0
    adafruit/Adafruit BusIO@^1.16.1
    bblanchon/ArduinoJson@^7.0.0
    wifwaf/TCA9548A@^1.1.4

monitor_speed = 115200
monitor_filters = time

build_flags =
    -DCFG_DEBUG=0
    -DNRF52840_XXAA
    -w

upload_protocol = nrfutil
board_build.filesystem = littlefs
```

**Note:** The Bluefruit BLE library is built into the nRF52 platform and does not need to be added to `lib_deps`. Just `#include <bluefruit.h>` in your code.

---

## Dependencies

### Arduino Libraries Required

| Library           | PlatformIO Name                     | Purpose          | CircuitPython Equivalent |
| ----------------- | ----------------------------------- | ---------------- | ------------------------ |
| Bluefruit         | (built-in)                          | BLE stack        | `adafruit_ble`           |
| Adafruit DRV2605  | `adafruit/Adafruit DRV2605 Library` | Haptic driver    | `adafruit_drv2605`       |
| TCA9548A          | `wifwaf/TCA9548A`                   | I2C multiplexer  | Manual I2C writes        |
| Adafruit NeoPixel | `adafruit/Adafruit NeoPixel`        | LED control      | `neopixel`               |
| ArduinoJson       | `bblanchon/ArduinoJson`             | JSON parsing     | `json`                   |
| Wire              | (built-in)                          | I2C bus          | `busio`                  |
| LittleFS          | (built-in)                          | Flash filesystem | CIRCUITPY filesystem     |

**Note:** Libraries marked "(built-in)" come with the nRF52 platform and don't need to be added to `lib_deps`.

---

## Work Packages

### WP1: Project Scaffolding & Hardware Layer (3-4 hours)

**Goal:** Establish project structure, port hardware abstractions, verify basic functionality.

**Tasks:**

1. Create PlatformIO project with correct board configuration
2. Define `config.h` with pin mappings and constants
3. Define `types.h` with all enums and structs
4. Port `I2CMultiplexer` class (simplified—use `Adafruit_TCA9548A`)
5. Port `DRV2605Controller` class
6. Port `BatteryMonitor` class
7. Port `LEDController` class (NeoPixel wrapper)
8. Create test sketch to verify each component

**Validation checkpoint:**

- [ ] All 5 DRV2605 drivers initialize successfully
- [ ] Battery voltage reads correctly
- [ ] NeoPixel LED can display colors
- [ ] Serial output shows initialization success

**Key code translations:**

```cpp
// CircuitPython: self.multiplexer.select_channel(finger)
// Arduino:
tca.select(channel);

// CircuitPython: self._write_register(self.REG_RTP_INPUT, rtp_value)
// Arduino:
drv.setRealtimeValue(rtp_value);

// CircuitPython: self.pin.value (ADC)
// Arduino:
analogRead(BATTERY_PIN);
```

---

### WP2: BLE Layer (4-5 hours)

**Goal:** Implement BLE communication with multi-connection support.

**Tasks:**

1. Create `BLEManager` class with Bluefruit API
2. Implement peripheral mode (advertising) for PRIMARY
3. Implement central mode (scanning) for SECONDARY
4. Implement UART service with EOT framing
5. Handle multi-connection (phone + SECONDARY on PRIMARY)
6. Port connection state tracking

**Critical implementation notes:**

The Bluefruit library uses **callbacks** for BLE events, unlike CircuitPython's polling model:

```cpp
// Setup callbacks in setup()
Bluefruit.Periph.setConnectCallback(connect_callback);
Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
bleuart.setRxCallback(rx_callback);

// Connection callback
void connect_callback(uint16_t conn_handle) {
    // Store connection, identify if phone or SECONDARY
}

// RX callback - called when data received
void rx_callback(uint16_t conn_handle) {
    // Read and process incoming data
}
```

**Multi-connection handling:**

```cpp
// PRIMARY supports 2 connections: phone + SECONDARY
Bluefruit.begin(2, 0);  // 2 peripheral connections, 0 central

// SECONDARY connects to PRIMARY as central
Bluefruit.begin(0, 1);  // 0 peripheral, 1 central connection
```

**Validation checkpoint:**

- [ ] PRIMARY advertises as "BlueBuzzah"
- [ ] Phone can connect and send/receive UART data
- [ ] SECONDARY can scan, find, and connect to PRIMARY
- [ ] EOT framing works correctly (messages delimited)

---

### WP3: Sync Protocol & Therapy Engine (4-5 hours)

**Goal:** Port pattern generation and bilateral synchronization.

**Tasks:**

1. Port `SyncCommand` class with serialize/deserialize
2. Port `SimpleSyncProtocol` for timestamp handling
3. Port pattern generation functions (`generate_random_permutation`, etc.)
4. Port `TherapyEngine` class with timing logic
5. Implement callback system for motor activation

**Pattern generation translation:**

```cpp
// CircuitPython: random.shuffle() not available, used _shuffle_in_place()
// Arduino: Same Fisher-Yates algorithm

void shuffleInPlace(uint8_t* arr, size_t n) {
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = random(0, i + 1);
        uint8_t temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}
```

**Timing translation:**

```cpp
// CircuitPython: time.monotonic() returns float seconds
// Arduino: millis() returns unsigned long milliseconds

// CircuitPython:
elapsed_ms = (time.monotonic() - self._activation_start_time) * 1000

// Arduino:
unsigned long elapsed_ms = millis() - _activationStartTime;
```

**Validation checkpoint:**

- [ ] Patterns generate correctly with proper randomization
- [ ] Therapy engine executes patterns with correct timing
- [ ] SYNC commands serialize/deserialize correctly
- [ ] PRIMARY sends EXECUTE_BUZZ, SECONDARY receives and executes

---

### WP4: State Machine & Application Logic (3-4 hours)

**Goal:** Port state machine and main application orchestration.

**Tasks:**

1. Port `TherapyStateMachine` with 11 states and transitions
2. Port boot sequence logic (PRIMARY and SECONDARY variants)
3. Port main loop logic with role-based behavior
4. Port session management (start, pause, resume, stop)
5. Port heartbeat protocol
6. Port battery monitoring integration

**State machine translation:**

```cpp
// types.h
enum class TherapyState {
    IDLE,
    CONNECTING,
    READY,
    RUNNING,
    PAUSED,
    STOPPING,
    ERROR,
    LOW_BATTERY,
    CRITICAL_BATTERY,
    CONNECTION_LOST,
    PHONE_DISCONNECTED
};

enum class StateTrigger {
    CONNECTED,
    START_SESSION,
    PAUSE_SESSION,
    RESUME_SESSION,
    STOP_SESSION,
    // ... etc
};

// Simple transition function
bool StateMachine::transition(StateTrigger trigger) {
    switch (_currentState) {
        case TherapyState::IDLE:
            if (trigger == StateTrigger::CONNECTED) {
                _currentState = TherapyState::READY;
                return true;
            }
            break;
        // ... other transitions
    }
    return false;
}
```

**Validation checkpoint:**

- [ ] Boot sequence completes for both PRIMARY and SECONDARY
- [ ] State transitions work correctly
- [ ] Heartbeat sent/received during therapy
- [ ] Timeout handling works (heartbeat, connection)

---

### WP5: Menu System & Profiles (2-3 hours)

**Goal:** Port command processing and profile management.

**Tasks:**

1. Port `MenuController` command parsing
2. Port `ProfileManager` with ArduinoJson
3. Implement settings.json reading from LittleFS
4. Port calibration controller
5. Final integration testing

**JSON handling with ArduinoJson:**

```cpp
#include <ArduinoJson.h>
#include <LittleFS.h>

bool loadSettings(DeviceConfig& config) {
    File file = LittleFS.open("/settings.json", "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) return false;

    const char* role = doc["deviceRole"] | "Primary";
    config.role = (strcmp(role, "Primary") == 0)
        ? DeviceRole::PRIMARY
        : DeviceRole::SECONDARY;

    return true;
}
```

**Validation checkpoint:**

- [ ] Phone commands processed correctly
- [ ] Profile loading/saving works
- [ ] Settings.json read correctly
- [ ] Full therapy session completes (both gloves)

---

## File-by-File Migration Guide

### hardware.py → hardware.h / hardware.cpp

| Python Class/Function | C++ Equivalent                                       |
| --------------------- | ---------------------------------------------------- |
| `BoardConfig`         | `struct BoardConfig` + pin `#define`s                |
| `I2CMultiplexer`      | Use `Adafruit_TCA9548A` directly                     |
| `DRV2605Controller`   | `class HapticController` wrapping `Adafruit_DRV2605` |
| `BatteryMonitor`      | `class BatteryMonitor`                               |
| `LEDPin`              | `class LEDController` wrapping `Adafruit_NeoPixel`   |

### ble.py → ble_manager.h / ble_manager.cpp

| Python Class/Method      | C++ Equivalent                           |
| ------------------------ | ---------------------------------------- |
| `BLE.__init__()`         | `BLEManager::begin()`                    |
| `BLE.advertise()`        | `Bluefruit.Advertising.start()`          |
| `BLE.scan_and_connect()` | `Bluefruit.Scanner.start()` + callbacks  |
| `BLE.send()`             | `bleuart.write()` + EOT                  |
| `BLE.receive()`          | Callback-based with buffer + EOT parsing |
| `BLEConnection`          | `struct BLEConnection`                   |

### therapy.py → therapy_engine.h / therapy_engine.cpp

| Python Class/Function           | C++ Equivalent                                 |
| ------------------------------- | ---------------------------------------------- |
| `Pattern`                       | `struct Pattern`                               |
| `generate_random_permutation()` | `Pattern generateRandomPermutation()`          |
| `generate_sequential_pattern()` | `Pattern generateSequentialPattern()`          |
| `TherapyEngine`                 | `class TherapyEngine`                          |
| `TherapyEngine.update()`        | `TherapyEngine::update()` (call from `loop()`) |

### sync.py → sync_protocol.h / sync_protocol.cpp

| Python Class/Function | C++ Equivalent                |
| --------------------- | ----------------------------- |
| `SyncCommand`         | `struct SyncCommand`          |
| `_serialize_data()`   | `String serializeData(Map)`   |
| `_deserialize_data()` | `Map deserializeData(String)` |
| `send_execute_buzz()` | `void sendExecuteBuzz()`      |
| `SimpleSyncProtocol`  | `class SyncProtocol`          |

### app.py → main.cpp

| Python Method                      | C++ Equivalent                   |
| ---------------------------------- | -------------------------------- |
| `BlueBuzzahApplication.__init__()` | `setup()` + global instances     |
| `BlueBuzzahApplication.run()`      | `loop()`                         |
| `_primary_boot_sequence()`         | `primaryBootSequence()`          |
| `_secondary_boot_sequence()`       | `secondaryBootSequence()`        |
| `_run_primary_loop()`              | Code in `loop()` with role check |
| `_run_secondary_loop()`            | Code in `loop()` with role check |

---

## BLE API Translation Reference

### Initialization

```python
# CircuitPython
from adafruit_ble import BLERadio
from adafruit_ble.services.nordic import UARTService

self.ble = BLERadio()
self.uart_service = UARTService()
```

```cpp
// Arduino
#include <bluefruit.h>

BLEUart bleuart;

void setup() {
    Bluefruit.begin();
    Bluefruit.setTxPower(4);
    bleuart.begin();
}
```

### Advertising

```python
# CircuitPython
from adafruit_ble.advertising.standard import ProvideServicesAdvertisement

advertisement = ProvideServicesAdvertisement(uart_service)
ble.start_advertising(advertisement)
```

```cpp
// Arduino
void startAdvertising() {
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(bleuart);
    Bluefruit.Advertising.addName();

    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);  // in units of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);  // 0 = Don't stop advertising
}
```

### Scanning (SECONDARY/Central mode)

```python
# CircuitPython
for advertisement in ble.start_scan(ProvideServicesAdvertisement, timeout=5):
    if advertisement.complete_name == "BlueBuzzah":
        connection = ble.connect(advertisement)
```

```cpp
// Arduino
BLEClientUart clientUart;

void setup() {
    Bluefruit.begin(0, 1);  // 0 peripheral, 1 central
    clientUart.begin();

    Bluefruit.Scanner.setRxCallback(scan_callback);
    Bluefruit.Scanner.start(0);
}

void scan_callback(ble_gap_evt_adv_report_t* report) {
    if (Bluefruit.Scanner.checkReportForService(report, clientUart)) {
        // Check name
        uint8_t buffer[32];
        if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, buffer, sizeof(buffer))) {
            if (strcmp((char*)buffer, "BlueBuzzah") == 0) {
                Bluefruit.Central.connect(report);
            }
        }
    }
}
```

### Sending Data

```python
# CircuitPython
message_with_eot = message + '\x04'
uart_service.write(message_with_eot.encode('utf-8'))
```

```cpp
// Arduino
void sendMessage(const char* message) {
    bleuart.print(message);
    bleuart.write(0x04);  // EOT terminator
}
```

### Receiving Data

```python
# CircuitPython (polling)
if uart_service.in_waiting:
    data = uart_service.read()
```

```cpp
// Arduino (callback-based)
void rx_callback(uint16_t conn_handle) {
    while (bleuart.available()) {
        char c = bleuart.read();
        if (c == 0x04) {
            // Message complete, process buffer
            processMessage(rxBuffer);
            rxBufferIndex = 0;
        } else {
            rxBuffer[rxBufferIndex++] = c;
        }
    }
}
```

---

## Sync Protocol Specification

### Message Format

All SYNC messages use this format:

```
SYNC:<COMMAND_TYPE>:<key1>|<value1>|<key2>|<value2>...\x04
```

### Command Types

| Command          | Direction | Data Fields                                                        | Description              |
| ---------------- | --------- | ------------------------------------------------------------------ | ------------------------ |
| `START_SESSION`  | P→S       | `duration_sec`, `pattern_type`, `jitter_percent`, `mirror_pattern` | Begin therapy session    |
| `PAUSE_SESSION`  | P→S       | (none)                                                             | Pause current session    |
| `RESUME_SESSION` | P→S       | (none)                                                             | Resume paused session    |
| `STOP_SESSION`   | P→S       | (none)                                                             | Stop session             |
| `EXECUTE_BUZZ`   | P→S       | `left_finger`, `right_finger`, `amplitude`, `seq`, `timestamp`     | Trigger motor activation |
| `DEACTIVATE`     | P→S       | `left_finger`, `right_finger`                                      | Stop motor activation    |
| `HEARTBEAT`      | P→S       | `ts`                                                               | Connection keepalive     |

### Example Messages

```
SYNC:START_SESSION:duration_sec|7200|pattern_type|rndp|jitter_percent|235|mirror_pattern|1\x04

SYNC:EXECUTE_BUZZ:left_finger|2|right_finger|2|amplitude|100|seq|42|timestamp|1234567890\x04

SYNC:HEARTBEAT:ts|1234567890\x04
```

### Timing Parameters

| Parameter            | Value      | Notes                                      |
| -------------------- | ---------- | ------------------------------------------ |
| Heartbeat interval   | 2 seconds  | PRIMARY sends during therapy               |
| Heartbeat timeout    | 6 seconds  | SECONDARY detects connection loss          |
| SYNC command timeout | 10 seconds | Warns if no commands during active session |
| BLE receive timeout  | 10ms       | Non-blocking poll interval                 |

---

## Testing Checkpoints

### Checkpoint 1: Hardware Verification

```
[ ] Serial output shows "DRV2605 initialized" for fingers 0-4
[ ] Battery voltage displays (e.g., "Battery: 4.12V, 95%")
[ ] NeoPixel shows blue, green, red on command
[ ] Each finger buzzes when tested individually
```

### Checkpoint 2: BLE Communication

```
[ ] nRF Connect app sees "BlueBuzzah" advertisement
[ ] Can connect and exchange UART data with phone
[ ] SECONDARY finds and connects to PRIMARY
[ ] EOT framing works (multi-message packets handled)
```

### Checkpoint 3: Bilateral Sync

```
[ ] PRIMARY sends EXECUTE_BUZZ
[ ] SECONDARY receives and activates correct fingers
[ ] Timing difference < 20ms between gloves
[ ] Heartbeat maintains connection during 5-minute test
```

### Checkpoint 4: Full Session

```
[ ] Boot sequence completes on both devices
[ ] 5-minute therapy session runs without errors
[ ] Pause/resume works correctly
[ ] Phone commands processed (if connected)
[ ] Graceful shutdown on stop command
```

### Checkpoint 5: Error Handling

```
[ ] Heartbeat timeout triggers recovery on SECONDARY
[ ] Low battery warning at threshold
[ ] Critical battery triggers shutdown
[ ] Reconnection after brief disconnect
```

---

## Risk Mitigation

### Risk: BLE Multi-Connection Complexity

**Mitigation:** Start with single connection (phone only), then add SECONDARY connection. Use Bluefruit examples as reference:

- `examples/Peripheral/bleuart`
- `examples/Central/central_bleuart`
- `examples/Peripheral/bleuart_multi` (multiple connections)

### Risk: DRV2605 Channel 4 Initialization

**Mitigation:** Port the existing retry logic with extended delays:

```cpp
// Extra delay for channel 4 (longer I2C path)
if (finger == 4) {
    delay(10);  // 10ms vs 5ms for others
}
```

### Risk: Timing Precision for Therapy

**Mitigation:** Use `micros()` for sub-millisecond precision where needed. Arduino's single-threaded model actually improves timing consistency vs CircuitPython's cooperative multitasking.

### Risk: Flash Storage for Settings

**Mitigation:** Use LittleFS (built into Adafruit BSP):

```cpp
#include <LittleFS.h>

void setup() {
    LittleFS.begin();
}
```

---

## Memory Budget

| Component           | Estimated RAM | Notes                           |
| ------------------- | ------------- | ------------------------------- |
| Bluefruit BLE stack | 15KB          | Fixed overhead                  |
| UART buffers        | 1KB           | RX + TX buffers                 |
| Pattern storage     | 500B          | Current + next pattern          |
| State machine       | 200B          | State + transitions             |
| Application logic   | 3KB           | All classes combined            |
| Stack               | 4KB           | Conservative estimate           |
| **Total**           | **~24KB**     |                                 |
| **Available**       | **256KB**     | nRF52840 SRAM                   |
| **Headroom**        | **232KB**     | 10x current CircuitPython usage |

---

## Quick Reference: Key Differences

| Aspect            | CircuitPython                      | Arduino                       |
| ----------------- | ---------------------------------- | ----------------------------- |
| Entry point       | `main.py` → `app.run()`            | `setup()` + `loop()`          |
| Time              | `time.monotonic()` (float seconds) | `millis()` (unsigned long ms) |
| Precise time      | `time.monotonic_ns()`              | `micros()`                    |
| BLE events        | Polling                            | Callbacks                     |
| I2C locking       | `i2c.try_lock()` / `unlock()`      | Not needed (single-threaded)  |
| Memory management | `gc.collect()`                     | Automatic (no GC)             |
| String formatting | `"{}".format(x)` or f-strings      | `String` class or `sprintf()` |
| JSON              | `json.load()`                      | `ArduinoJson` library         |
| File storage      | CIRCUITPY filesystem               | LittleFS                      |
| Random            | `random.randint(a, b)`             | `random(min, max)`            |

---

## Getting Started

The PlatformIO project is already created at `./BlueBuzzah-Arduino/`.

### To begin migration:

1. **Reference the CircuitPython source:**

   - Source files: `./src/*.py`
   - Architecture docs: `./docs/FIRMWARE_ARCHITECTURE.md`

2. **Write Arduino code to:**

   - Headers: `./BlueBuzzah-Arduino/include/`
   - Source: `./BlueBuzzah-Arduino/src/`
   - Data files: `./BlueBuzzah-Arduino/data/`

3. **Start with WP1:** Create `config.h`, `types.h`, and port `hardware.cpp`

4. **Build and test:**

   ```bash
   cd BlueBuzzah-Arduino
   pio run              # Build
   pio run -t upload    # Upload to device
   pio device monitor   # Serial monitor
   ```

5. **Test incrementally:** Each work package has validation checkpoints

6. **Upload filesystem (for settings.json):**
   ```bash
   pio run -t uploadfs
   ```

### File Mapping Quick Reference

| CircuitPython Source      | Arduino Target                                                             |
| ------------------------- | -------------------------------------------------------------------------- |
| `./src/app.py`            | `./BlueBuzzah-Arduino/src/main.cpp`                                        |
| `./src/hardware.py`       | `./BlueBuzzah-Arduino/src/hardware.cpp` + `include/hardware.h`             |
| `./src/ble.py`            | `./BlueBuzzah-Arduino/src/ble_manager.cpp` + `include/ble_manager.h`       |
| `./src/therapy.py`        | `./BlueBuzzah-Arduino/src/therapy_engine.cpp` + `include/therapy_engine.h` |
| `./src/sync.py`           | `./BlueBuzzah-Arduino/src/sync_protocol.cpp` + `include/sync_protocol.h`   |
| `./src/state.py`          | `./BlueBuzzah-Arduino/src/state_machine.cpp` + `include/state_machine.h`   |
| `./src/core/types.py`     | `./BlueBuzzah-Arduino/include/types.h`                                     |
| `./src/core/constants.py` | `./BlueBuzzah-Arduino/include/config.h`                                    |

---

**Document Maintenance:**

Update this document when:

- Completing a work package (mark checkpoints)
- Discovering new migration challenges
- Finding better Arduino API equivalents

**Last Updated:** 2024-11-27
