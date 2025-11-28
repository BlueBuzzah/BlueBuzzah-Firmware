# BlueBuzzah v2 API Reference

**Comprehensive API documentation for BlueBuzzah v2 bilateral haptic therapy system**

Version: 2.0.0
Platform: Arduino C++ on Adafruit Feather nRF52840 Express
Build System: PlatformIO with Adafruit nRF52 BSP
Last Updated: 2025-01-11

---

## Table of Contents

- [Overview](#overview)
- [Core Types and Constants](#core-types-and-constants)
- [Configuration System](#configuration-system)
- [State Machine](#state-machine)
- [Hardware Abstraction](#hardware-abstraction)
- [BLE Communication](#ble-communication)
- [Therapy Engine](#therapy-engine)
- [Synchronization Protocol](#synchronization-protocol)
- [Application Layer](#application-layer)
- [LED Controller](#led-controller)
- [Usage Examples](#usage-examples)

---

## Overview

BlueBuzzah v2 provides a clean, layered API for bilateral haptic therapy control. The architecture follows Clean Architecture principles with clear separation between layers:

- **Core**: Types, constants, and fundamental definitions (`types.h`, `config.h`)
- **State**: Explicit state machine for therapy sessions (`state_machine.h`)
- **Hardware**: Hardware abstraction for haptic controllers, battery, etc. (`hardware.h`)
- **Communication**: BLE protocol and message handling (`ble_manager.h`)
- **Therapy**: Pattern generation and therapy execution (`therapy_engine.h`)
- **Application**: High-level use cases and workflows (`session_manager.h`, `menu_controller.h`)

---

## Core Types and Constants

### Header: `types.h`

#### DeviceRole

Device role in the bilateral therapy system.

```cpp
// include/types.h

enum class DeviceRole : uint8_t {
    PRIMARY,    // Initiates therapy, controls timing
    SECONDARY   // Follows PRIMARY commands
};

// Helper functions
inline bool isPrimary(DeviceRole role) {
    return role == DeviceRole::PRIMARY;
}

inline bool isSecondary(DeviceRole role) {
    return role == DeviceRole::SECONDARY;
}
```

**Usage:**
```cpp
#include "types.h"

DeviceRole role = DeviceRole::PRIMARY;
if (isPrimary(role)) {
    startAdvertising();
} else {
    scanForPrimary();
}
```

---

#### TherapyState

Therapy session state machine states.

```cpp
// include/types.h

enum class TherapyState : uint8_t {
    IDLE,               // No active session, waiting for commands
    CONNECTING,         // Establishing BLE connections during boot
    READY,              // Connected and ready to start therapy
    RUNNING,            // Actively delivering therapy
    PAUSED,             // Therapy temporarily suspended by user
    STOPPING,           // Graceful shutdown in progress
    ERROR,              // Unrecoverable error occurred
    LOW_BATTERY,        // Battery below warning threshold (<3.4V)
    CRITICAL_BATTERY,   // Battery critically low (<3.3V), immediate shutdown
    CONNECTION_LOST,    // PRIMARY-SECONDARY connection lost during therapy
    PHONE_DISCONNECTED  // Phone connection lost (informational, therapy continues)
};
```

**State Descriptions:**

| State | Description |
|-------|-------------|
| `IDLE` | No active session, waiting for commands |
| `CONNECTING` | Establishing BLE connections during boot |
| `READY` | Connected and ready to start therapy |
| `RUNNING` | Actively delivering therapy |
| `PAUSED` | Therapy temporarily suspended by user |
| `STOPPING` | Graceful shutdown in progress |
| `ERROR` | Unrecoverable error occurred |
| `LOW_BATTERY` | Battery below warning threshold (<3.4V) |
| `CRITICAL_BATTERY` | Battery critically low (<3.3V), immediate shutdown |
| `CONNECTION_LOST` | PRIMARY-SECONDARY connection lost during therapy |
| `PHONE_DISCONNECTED` | Phone connection lost (informational, therapy continues) |

**State Transitions:**
```
IDLE -> CONNECTING -> READY -> RUNNING <-> PAUSED -> STOPPING -> IDLE
                        |                           ^
                        v                           |
                      ERROR/LOW_BATTERY/CRITICAL_BATTERY/CONNECTION_LOST
```

**Note:** `PHONE_DISCONNECTED` is an informational state - therapy continues normally when the phone disconnects. Only `CONNECTION_LOST` (PRIMARY-SECONDARY) stops therapy.

**Helper Functions:**

```cpp
// include/types.h

inline bool isActiveState(TherapyState state) {
    return state == TherapyState::RUNNING || state == TherapyState::PAUSED;
}

inline bool isErrorState(TherapyState state) {
    return state == TherapyState::ERROR ||
           state == TherapyState::LOW_BATTERY ||
           state == TherapyState::CRITICAL_BATTERY ||
           state == TherapyState::CONNECTION_LOST;
}

inline bool canStartTherapy(TherapyState state) {
    return state == TherapyState::READY;
}

inline bool canPause(TherapyState state) {
    return state == TherapyState::RUNNING;
}

inline bool canResume(TherapyState state) {
    return state == TherapyState::PAUSED;
}
```

**Usage:**
```cpp
#include "types.h"

TherapyState state = TherapyState::RUNNING;
if (isActiveState(state)) {
    executeTherapyCycle();
} else if (canStartTherapy(state)) {
    startNewSession();
}
```

---

#### BootResult

Boot sequence outcome enumeration.

```cpp
// include/types.h

enum class BootResult : uint8_t {
    FAILED,             // Boot sequence failed
    SUCCESS_NO_PHONE,   // Connected to glove but no phone
    SUCCESS_WITH_PHONE, // Connected to both glove and phone
    SUCCESS             // For SECONDARY (only needs PRIMARY connection)
};

inline bool isSuccess(BootResult result) {
    return result != BootResult::FAILED;
}

inline bool hasPhone(BootResult result) {
    return result == BootResult::SUCCESS_WITH_PHONE;
}
```

**Usage:**
```cpp
#include "types.h"

BootResult result = executeBootSequence();
if (isSuccess(result)) {
    if (hasPhone(result)) {
        waitForPhoneCommands();
    } else {
        startDefaultTherapy();
    }
}
```

---

#### BatteryStatus

Battery status information struct.

```cpp
// include/types.h

struct BatteryStatus {
    float voltage;       // Current voltage in volts
    uint8_t percentage;  // Battery percentage 0-100
    const char* status;  // Status string: "OK", "LOW", or "CRITICAL"
    bool isLow;          // True if below LOW_VOLTAGE threshold
    bool isCritical;     // True if below CRITICAL_VOLTAGE threshold

    bool isOk() const {
        return !isLow && !isCritical;
    }

    bool requiresAction() const {
        return isLow || isCritical;
    }
};
```

**Usage:**
```cpp
#include "types.h"
#include "hardware.h"

BatteryStatus battery = hardware.getBatteryStatus();
if (battery.isCritical) {
    shutdownImmediately();
} else if (battery.isLow) {
    showWarningLED();
}
Serial.printf("Battery: %.2fV (%d%%)\n", battery.voltage, battery.percentage);
```

---

#### SessionInfo

Therapy session information struct.

```cpp
// include/types.h

struct SessionInfo {
    char sessionId[16];        // Unique session identifier
    uint32_t startTimeMs;      // Session start timestamp (millis())
    uint32_t durationSec;      // Total session duration in seconds
    uint32_t elapsedSec;       // Elapsed time in seconds, excluding pauses
    char profileName[32];      // Therapy profile name
    TherapyState state;        // Current therapy state

    uint8_t progressPercent() const {
        if (durationSec == 0) return 0;
        return (uint8_t)((elapsedSec * 100UL) / durationSec);
    }

    uint32_t remainingSec() const {
        return (elapsedSec < durationSec) ? (durationSec - elapsedSec) : 0;
    }

    bool isComplete() const {
        return elapsedSec >= durationSec;
    }
};
```

**Usage:**
```cpp
#include "types.h"

SessionInfo session;
strncpy(session.sessionId, "session_001", sizeof(session.sessionId));
session.startTimeMs = millis();
session.durationSec = 7200;
session.elapsedSec = 3600;
strncpy(session.profileName, "noisy_vcr", sizeof(session.profileName));
session.state = TherapyState::RUNNING;

Serial.printf("Progress: %d%%\n", session.progressPercent());
Serial.printf("Remaining: %lu s\n", session.remainingSec());
```

---

### Header: `config.h`

System-wide constants for timing, hardware, battery thresholds, and more.

#### Firmware Version

```cpp
// include/config.h

#define FIRMWARE_VERSION "2.0.0"
```
Current firmware version following semantic versioning.

---

#### Timing Constants

```cpp
// include/config.h

#define STARTUP_WINDOW_SEC 30
// Boot sequence connection window in seconds

#define CONNECTION_TIMEOUT_SEC 30
// BLE connection establishment timeout in seconds

#define BLE_INTERVAL_MS 7.5f
// BLE connection interval in milliseconds for sub-10ms synchronization

#define SYNC_INTERVAL_MS 1000
// Periodic synchronization interval in milliseconds (SYNC_ADJ messages)

#define COMMAND_TIMEOUT_MS 5000
// General BLE command timeout in milliseconds

#define HEARTBEAT_INTERVAL_MS 2000
// Heartbeat message interval in milliseconds

#define HEARTBEAT_TIMEOUT_MS 6000
// Heartbeat timeout (3 missed heartbeats = connection lost)
```

---

#### Hardware Constants

```cpp
// include/config.h

#define I2C_MULTIPLEXER_ADDR 0x70
// TCA9548A I2C multiplexer address

#define DRV2605_DEFAULT_ADDR 0x5A
// DRV2605 haptic driver I2C address

#define I2C_FREQUENCY 400000
// I2C bus frequency in Hz (400 kHz Fast Mode)

#define MAX_ACTUATORS 5
// Maximum number of haptic actuators per device (4 used in practice)

#define MAX_AMPLITUDE 127
// Maximum haptic amplitude (DRV2605 RTP mode: 0-127)
```

---

#### Pin Assignments

```cpp
// include/config.h

#define NEOPIXEL_PIN 8
// NeoPixel LED data pin (D8 on Feather nRF52840)

#define BATTERY_PIN A6
// Battery voltage monitoring analog pin

// I2C uses default Wire (SDA/SCL pins)
```

---

#### LED Colors

```cpp
// include/config.h

#define LED_BLUE    0x0000FF  // BLE operations
#define LED_GREEN   0x00FF00  // Success/Normal
#define LED_RED     0xFF0000  // Error/Critical
#define LED_WHITE   0xFFFFFF  // Special indicators
#define LED_YELLOW  0xFFFF00  // Paused
#define LED_ORANGE  0xFF8000  // Low battery
#define LED_OFF     0x000000  // LED off
```

---

#### Battery Thresholds

```cpp
// include/config.h

#define CRITICAL_VOLTAGE 3.3f
// Critical battery voltage (immediate shutdown)

#define LOW_VOLTAGE 3.4f
// Low battery warning voltage (warning, therapy continues)

#define BATTERY_CHECK_INTERVAL_MS 60000
// Battery voltage check interval in milliseconds during therapy
```

---

## Configuration System

### Header: `config.h` / `profile_manager.h`

#### DeviceConfig

Device configuration loaded from LittleFS.

```cpp
// include/config.h

struct DeviceConfig {
    DeviceRole role;
    const char* bleName;
    const char* deviceTag;
};

DeviceConfig loadDeviceConfig();
```

**Usage:**
```cpp
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

DeviceConfig loadDeviceConfig() {
    DeviceConfig config;

    if (!LittleFS.begin()) {
        // Default to PRIMARY if no filesystem
        config.role = DeviceRole::PRIMARY;
        config.bleName = "BlueBuzzah";
        config.deviceTag = "[PRIMARY]";
        return config;
    }

    File file = LittleFS.open("/settings.json", "r");
    if (!file) {
        config.role = DeviceRole::PRIMARY;
        config.bleName = "BlueBuzzah";
        config.deviceTag = "[PRIMARY]";
        return config;
    }

    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();

    const char* roleStr = doc["deviceRole"] | "Primary";
    config.role = (strcmp(roleStr, "Primary") == 0)
        ? DeviceRole::PRIMARY
        : DeviceRole::SECONDARY;
    config.bleName = "BlueBuzzah";
    config.deviceTag = (config.role == DeviceRole::PRIMARY)
        ? "[PRIMARY]" : "[SECONDARY]";

    return config;
}
```

---

#### TherapyConfig

Therapy-specific configuration.

```cpp
// include/types.h

struct TherapyConfig {
    char profileName[32];         // Profile identifier
    uint16_t burstDurationMs;     // Burst duration in ms
    uint16_t interBurstIntervalMs;// Interval between bursts in ms
    uint8_t burstsPerCycle;       // Number of bursts per cycle
    char patternType[16];         // "random", "sequential", or "mirrored"
    uint16_t frequencyHz;         // Haptic frequency in Hz
    uint8_t amplitudePercent;     // Amplitude 0-100
    uint8_t jitterPercent;        // Timing jitter percentage (0-100, stored as x10)
    bool mirrorPattern;           // Bilateral mirroring mode
                                  // true: Same finger on both hands (noisy vCR)
                                  // false: Independent sequences per hand (regular vCR)
};

// Default profiles
TherapyConfig getDefaultNoisyVCR();
TherapyConfig getDefaultRegularVCR();
TherapyConfig getDefaultHybridVCR();
```

**Usage:**
```cpp
#include "types.h"

// Use default profile
TherapyConfig config = getDefaultNoisyVCR();

// Custom profile
TherapyConfig custom;
strncpy(custom.profileName, "custom_research", sizeof(custom.profileName));
custom.burstDurationMs = 100;
custom.interBurstIntervalMs = 668;
custom.burstsPerCycle = 3;
strncpy(custom.patternType, "random", sizeof(custom.patternType));
custom.frequencyHz = 175;
custom.amplitudePercent = 100;
custom.jitterPercent = 235;  // 23.5% stored as integer
custom.mirrorPattern = true; // true for noisy vCR
```

---

### Class: ProfileManager

Loads and validates configuration from JSON files.

```cpp
// include/profile_manager.h

class ProfileManager {
public:
    bool begin();
    bool loadProfile(const char* name, TherapyConfig& config);
    bool saveProfile(const char* name, const TherapyConfig& config);
    bool deleteProfile(const char* name);
    void listProfiles(char* buffer, size_t bufferSize);
    TherapyConfig getDefaultProfile();

private:
    bool validateConfig(const TherapyConfig& config);
};
```

**Usage:**
```cpp
#include "profile_manager.h"

ProfileManager profileManager;
profileManager.begin();

// List available profiles
char profiles[256];
profileManager.listProfiles(profiles, sizeof(profiles));
Serial.printf("Available profiles: %s\n", profiles);

// Load profile
TherapyConfig config;
if (profileManager.loadProfile("noisy_vcr", config)) {
    Serial.printf("Loaded profile: %s\n", config.profileName);
}

// Save custom profile
profileManager.saveProfile("custom_research", config);
```

---

## State Machine

### Header: `state_machine.h`

#### StateTrigger Enum

```cpp
// include/types.h

enum class StateTrigger : uint8_t {
    CONNECTED,
    START_SESSION,
    PAUSE_SESSION,
    RESUME_SESSION,
    STOP_SESSION,
    SESSION_COMPLETE,
    BATTERY_WARNING,
    BATTERY_CRITICAL,
    BATTERY_OK,
    DISCONNECTED,
    RECONNECTED,
    RECONNECT_FAILED,
    PHONE_LOST,
    PHONE_RECONNECTED,
    PHONE_TIMEOUT,
    ERROR_OCCURRED,
    EMERGENCY_STOP,
    RESET,
    STOPPED,
    FORCED_SHUTDOWN
};
```

#### StateMachine Class

Explicit state machine for therapy session management.

```cpp
// include/state_machine.h

class StateMachine {
public:
    StateMachine();

    TherapyState currentState() const;
    bool transition(StateTrigger trigger);
    bool canTransition(StateTrigger trigger) const;
    void forceState(TherapyState newState);
    void reset();

    // Callback registration
    typedef void (*StateChangeCallback)(TherapyState from, TherapyState to);
    void setCallback(StateChangeCallback callback);

    // Utility
    const char* stateToString() const;
    static const char* stateToString(TherapyState state);

private:
    TherapyState _currentState;
    StateChangeCallback _callback;
    bool validateTransition(StateTrigger trigger) const;
};
```

**Usage:**
```cpp
#include "state_machine.h"

// Create state machine
StateMachine stateMachine;

// Add observer for state changes
void onStateChange(TherapyState from, TherapyState to) {
    Serial.printf("State: %s -> %s\n",
        StateMachine::stateToString(from),
        StateMachine::stateToString(to));
}
stateMachine.setCallback(onStateChange);

// Check current state
Serial.printf("Current state: %s\n", stateMachine.stateToString());

// Attempt transition
if (stateMachine.canTransition(StateTrigger::START_SESSION)) {
    if (stateMachine.transition(StateTrigger::START_SESSION)) {
        Serial.println(F("Session started successfully"));
    }
}

// Reset state machine
stateMachine.reset();
```

---

## Hardware Abstraction

### Header: `hardware.h`

#### HardwareController Class

Hardware control for motors, battery, and I2C multiplexer.

```cpp
// include/hardware.h

#include <Adafruit_DRV2605.h>
#include <Adafruit_TCA9548A.h>

class HardwareController {
public:
    bool begin();

    // Motor control
    void buzzFinger(uint8_t finger, uint8_t amplitude);
    void stopFinger(uint8_t finger);
    void stopAllMotors();
    bool isMotorActive(uint8_t finger) const;

    // DRV2605 configuration
    void setFrequency(uint8_t finger, uint16_t frequencyHz);
    void setActuatorType(bool useLRA);

    // Battery monitoring
    float getBatteryVoltage();
    uint8_t getBatteryPercentage();
    BatteryStatus getBatteryStatus();
    bool isBatteryLow();
    bool isBatteryCritical();

    // I2C multiplexer
    void selectChannel(uint8_t channel);
    void deselectAll();

private:
    Adafruit_TCA9548A _tca;
    Adafruit_DRV2605 _drv[5];
    bool _motorActive[5];

    void configureDRV2605(Adafruit_DRV2605& driver);
};
```

**Usage:**
```cpp
#include "hardware.h"

HardwareController hardware;

// Initialize hardware
if (!hardware.begin()) {
    Serial.println(F("[ERROR] Hardware init failed"));
    while (true) { delay(1000); }
}

// Activate motor
hardware.buzzFinger(0, 100);  // Thumb at ~78% (100/127)
delay(100);
hardware.stopFinger(0);

// Stop all motors
hardware.stopAllMotors();

// Check battery
BatteryStatus status = hardware.getBatteryStatus();
Serial.printf("Battery: %.2fV (%d%%) - %s\n",
    status.voltage, status.percentage, status.status);

if (hardware.isBatteryCritical()) {
    // Emergency shutdown
}
```

---

#### LED Controller

NeoPixel LED control for visual feedback.

```cpp
// include/led_controller.h

#include <Adafruit_NeoPixel.h>

class LEDController {
public:
    LEDController();
    bool begin();

    // Basic control
    void setColor(uint32_t color);
    void off();

    // Animation patterns
    void rapidFlash(uint32_t color, uint8_t count = 5);
    void slowFlash(uint32_t color);
    void breathe(uint32_t color);
    void flashCount(uint32_t color, uint8_t count);

    // State-based patterns
    void setTherapyState(TherapyState state);
    void updateBreathing();  // Call at ~20Hz during RUNNING

    // Boot sequence patterns
    void indicateBLEInit();
    void indicateConnectionSuccess();
    void indicateWaitingForPhone();
    void indicateReady();
    void indicateFailure();
    void indicateConnectionLost();

private:
    Adafruit_NeoPixel _pixel;
    uint32_t _currentColor;
    uint32_t _lastUpdate;
    float _breathPhase;
};
```

**Usage:**
```cpp
#include "led_controller.h"
#include "config.h"

LEDController led;
led.begin();

// Solid green
led.setColor(LED_GREEN);

// Flash red 5 times
led.flashCount(LED_RED, 5);

// State-based LED
led.setTherapyState(TherapyState::RUNNING);

// Update breathing effect (call in loop at ~20Hz)
while (therapyRunning) {
    led.updateBreathing();
    delay(50);
}

// Turn off
led.off();
```

---

## BLE Communication

### Header: `ble_manager.h`

#### BLEManager Class

BLE communication management using Bluefruit library.

```cpp
// include/ble_manager.h

#include <bluefruit.h>

class BLEManager {
public:
    bool begin(const DeviceConfig& config);

    // Connection management
    void startAdvertising();
    bool scanAndConnect(const char* targetName, uint32_t timeoutMs);
    void disconnect(uint16_t connHandle);
    bool isConnected() const;

    // Connection handles
    uint16_t getPhoneHandle() const;
    uint16_t getSecondaryHandle() const;   // PRIMARY only
    uint16_t getPrimaryHandle() const;     // SECONDARY only

    // Data transmission
    bool sendToPhone(const char* data);
    bool sendToSecondary(const char* data);  // PRIMARY only
    bool sendToPrimary(const char* data);    // SECONDARY only

    // Callback registration
    typedef void (*ConnectCallback)(uint16_t connHandle);
    typedef void (*DisconnectCallback)(uint16_t connHandle, uint8_t reason);
    typedef void (*RxCallback)(uint16_t connHandle, const char* data);

    void setConnectCallback(ConnectCallback cb);
    void setDisconnectCallback(DisconnectCallback cb);
    void setRxCallback(RxCallback cb);

private:
    BLEUart _bleuart;
    uint16_t _phoneHandle;
    uint16_t _secondaryHandle;
    uint16_t _primaryHandle;
    DeviceRole _role;

    static void connectCallbackStatic(uint16_t connHandle);
    static void disconnectCallbackStatic(uint16_t connHandle, uint8_t reason);
    static void rxCallbackStatic(uint16_t connHandle);
};
```

**Usage:**
```cpp
#include "ble_manager.h"

BLEManager bleManager;

// Callbacks
void onConnect(uint16_t connHandle) {
    Serial.printf("[BLE] Connected: %d\n", connHandle);
}

void onDisconnect(uint16_t connHandle, uint8_t reason) {
    Serial.printf("[BLE] Disconnected: %d, reason: 0x%02X\n", connHandle, reason);
}

void onRxData(uint16_t connHandle, const char* data) {
    Serial.printf("[BLE] RX from %d: %s\n", connHandle, data);
}

// Setup
bleManager.setConnectCallback(onConnect);
bleManager.setDisconnectCallback(onDisconnect);
bleManager.setRxCallback(onRxData);
bleManager.begin(deviceConfig);

// PRIMARY: Start advertising
bleManager.startAdvertising();

// SECONDARY: Scan and connect
bleManager.scanAndConnect("BlueBuzzah", 30000);

// Send data
bleManager.sendToPhone("OK:Command received\n\x04");

// Check connection
if (bleManager.isConnected()) {
    Serial.println(F("Connected"));
}
```

---

### BLE Protocol Commands

```cpp
// Command types (string-based protocol)

// Device Information
// INFO - Get device information
// BATTERY - Get battery status
// PING - Connection test

// Profile Management
// PROFILE_LIST - List available profiles
// PROFILE_LOAD:<name> - Load profile
// PROFILE_GET - Get current profile
// PROFILE_CUSTOM:<json> - Set custom profile

// Session Control
// SESSION_START:<profile>:<duration_sec>
// SESSION_PAUSE
// SESSION_RESUME
// SESSION_STOP
// SESSION_STATUS

// Parameter Adjustment
// PARAM_SET:<param>:<value>

// Calibration
// CALIBRATE_START
// CALIBRATE_BUZZ:<finger>:<amplitude>:<duration_ms>
// CALIBRATE_STOP

// System
// HELP
// RESTART
// SET_ROLE:<PRIMARY|SECONDARY>
```

---

## Therapy Engine

### Header: `therapy_engine.h`

#### TherapyEngine Class

Core therapy execution engine.

```cpp
// include/therapy_engine.h

class TherapyEngine {
public:
    TherapyEngine(HardwareController& hardware, StateMachine& stateMachine);

    // Session control
    bool startSession(const TherapyConfig& config, uint32_t durationSec);
    void pauseSession();
    void resumeSession();
    void stopSession();

    // Main update (call in loop)
    void update();

    // Status
    bool isRunning() const;
    bool isPaused() const;
    const SessionInfo& getSessionInfo() const;
    uint32_t getCyclesCompleted() const;

    // Callbacks
    typedef void (*CycleCompleteCallback)(uint32_t cycleCount);
    typedef void (*SessionCompleteCallback)(const SessionInfo& info);

    void setCycleCompleteCallback(CycleCompleteCallback cb);
    void setSessionCompleteCallback(SessionCompleteCallback cb);

private:
    HardwareController& _hardware;
    StateMachine& _stateMachine;
    TherapyConfig _config;
    SessionInfo _session;

    uint32_t _cycleCount;
    uint32_t _lastCycleTime;
    uint32_t _pauseStartTime;
    uint32_t _totalPauseTime;

    uint8_t _leftSequence[4];
    uint8_t _rightSequence[4];
    uint8_t _currentBurst;

    void generatePattern();
    void executeBurst(uint8_t burstIndex);
    uint16_t calculateJitter();
};
```

**Usage:**
```cpp
#include "therapy_engine.h"

HardwareController hardware;
StateMachine stateMachine;
TherapyEngine engine(hardware, stateMachine);

// Callbacks
void onCycleComplete(uint32_t count) {
    Serial.printf("Cycle %lu complete\n", count);
}

void onSessionComplete(const SessionInfo& info) {
    Serial.printf("Session complete: %lu cycles\n", info.elapsedSec);
}

engine.setCycleCompleteCallback(onCycleComplete);
engine.setSessionCompleteCallback(onSessionComplete);

// Start session
TherapyConfig config = getDefaultNoisyVCR();
engine.startSession(config, 7200);  // 2 hours

// Main loop
while (engine.isRunning()) {
    engine.update();  // Call frequently
    yield();
}
```

---

### Pattern Generation

Pattern generation for bilateral therapy.

```cpp
// include/therapy_engine.h (private implementation)

void TherapyEngine::generatePattern() {
    // Generate random permutation for left hand
    for (uint8_t i = 0; i < 4; i++) {
        _leftSequence[i] = i;
    }

    // Fisher-Yates shuffle
    for (uint8_t i = 3; i > 0; i--) {
        uint8_t j = random(0, i + 1);
        uint8_t temp = _leftSequence[i];
        _leftSequence[i] = _leftSequence[j];
        _leftSequence[j] = temp;
    }

    if (_config.mirrorPattern) {
        // Same finger on both hands (noisy vCR)
        memcpy(_rightSequence, _leftSequence, sizeof(_leftSequence));
    } else {
        // Independent sequences (regular vCR)
        for (uint8_t i = 0; i < 4; i++) {
            _rightSequence[i] = i;
        }
        for (uint8_t i = 3; i > 0; i--) {
            uint8_t j = random(0, i + 1);
            uint8_t temp = _rightSequence[i];
            _rightSequence[i] = _rightSequence[j];
            _rightSequence[j] = temp;
        }
    }
}
```

**Bilateral Mirroring:**

| vCR Type        | mirrorPattern | Behavior                              |
|-----------------|---------------|---------------------------------------|
| **Noisy vCR**   | `true`        | Same finger activated on both hands   |
| **Regular vCR** | `false`       | Independent random sequences per hand |

---

## Synchronization Protocol

### Header: `sync_protocol.h`

#### SyncProtocol Class

Time synchronization between PRIMARY and SECONDARY devices.

```cpp
// include/sync_protocol.h

class SyncProtocol {
public:
    SyncProtocol(BLEManager& bleManager, DeviceRole role);

    // Command sending (PRIMARY)
    bool sendStartSession(const TherapyConfig& config, uint32_t durationSec);
    bool sendPauseSession();
    bool sendResumeSession();
    bool sendStopSession();
    bool sendExecuteBuzz(uint8_t leftFinger, uint8_t rightFinger, uint8_t amplitude);
    bool sendDeactivate();
    bool sendHeartbeat();

    // Command receiving (SECONDARY)
    bool hasCommand() const;
    bool parseCommand(const char* data);

    // Callback registration
    typedef void (*CommandCallback)(const char* command, const char* params);
    void setCommandCallback(CommandCallback cb);

    // Status
    uint32_t getLastHeartbeatTime() const;
    bool isHeartbeatTimeout() const;

private:
    BLEManager& _bleManager;
    DeviceRole _role;
    uint32_t _lastHeartbeat;
    CommandCallback _callback;

    void formatMessage(char* buffer, size_t size,
                       const char* command, const char* params);
};
```

**Message Format:**
```
SYNC:<command>:<key1>|<val1>|<key2>|<val2>|...
```

**SYNC Commands:**

| Command        | Direction | Description               |
|----------------|-----------|---------------------------|
| START_SESSION  | P->S      | Begin therapy with config |
| PAUSE_SESSION  | P->S      | Pause current session     |
| RESUME_SESSION | P->S      | Resume paused session     |
| STOP_SESSION   | P->S      | Stop session              |
| EXECUTE_BUZZ   | P->S      | Trigger motor activation  |
| DEACTIVATE     | P->S      | Stop motor activation     |
| HEARTBEAT      | P->S      | Connection keepalive      |

**Examples:**
```
SYNC:START_SESSION:duration_sec|7200|pattern_type|rndp|jitter_percent|235
SYNC:EXECUTE_BUZZ:left_finger|2|right_finger|2|amplitude|100|timestamp|123456
SYNC:HEARTBEAT:ts|1234567890
```

**Usage:**
```cpp
#include "sync_protocol.h"

SyncProtocol sync(bleManager, DeviceRole::PRIMARY);

// Callback for received commands (SECONDARY)
void onSyncCommand(const char* command, const char* params) {
    if (strcmp(command, "EXECUTE_BUZZ") == 0) {
        // Parse params and execute buzz
    }
}
sync.setCommandCallback(onSyncCommand);

// Send execute command (PRIMARY)
sync.sendExecuteBuzz(2, 2, 100);  // Finger 2, amplitude 100

// Send heartbeat (PRIMARY, call every 2 seconds)
sync.sendHeartbeat();

// Check heartbeat timeout (SECONDARY)
if (sync.isHeartbeatTimeout()) {
    handleConnectionLost();
}
```

---

## Application Layer

### Header: `session_manager.h`

#### SessionManager Class

High-level session lifecycle management.

```cpp
// include/session_manager.h

class SessionManager {
public:
    SessionManager(TherapyEngine& engine, ProfileManager& profiles,
                   StateMachine& stateMachine);

    // Session control
    bool startSession(const char* profileName, uint32_t durationSec);
    void pauseSession();
    void resumeSession();
    void stopSession();

    // Status
    const SessionInfo* getCurrentSession() const;
    bool hasActiveSession() const;
    void getStatus(char* buffer, size_t size) const;

private:
    TherapyEngine& _engine;
    ProfileManager& _profiles;
    StateMachine& _stateMachine;
    SessionInfo _currentSession;
    bool _hasSession;
};
```

**Usage:**
```cpp
#include "session_manager.h"

SessionManager sessionManager(engine, profiles, stateMachine);

// Start session
if (sessionManager.startSession("noisy_vcr", 7200)) {
    Serial.println(F("Session started"));
}

// Pause session
sessionManager.pauseSession();

// Resume session
sessionManager.resumeSession();

// Get status
char status[256];
sessionManager.getStatus(status, sizeof(status));
Serial.println(status);

// Stop session
sessionManager.stopSession();
```

---

### Header: `menu_controller.h`

#### MenuController Class

BLE command processing and routing.

```cpp
// include/menu_controller.h

class MenuController {
public:
    MenuController(SessionManager& session, HardwareController& hardware,
                   ProfileManager& profiles, BLEManager& ble);

    // Process incoming command, returns response string
    void processCommand(const char* command, char* response, size_t responseSize);

private:
    SessionManager& _session;
    HardwareController& _hardware;
    ProfileManager& _profiles;
    BLEManager& _ble;

    // Command handlers
    void handleInfo(char* response, size_t size);
    void handleBattery(char* response, size_t size);
    void handleSessionStart(const char* params, char* response, size_t size);
    void handleSessionPause(char* response, size_t size);
    void handleSessionResume(char* response, size_t size);
    void handleSessionStop(char* response, size_t size);
    void handleSessionStatus(char* response, size_t size);
    void handleCalibrateBuzz(const char* params, char* response, size_t size);
    void handleProfileList(char* response, size_t size);
    void handleProfileLoad(const char* params, char* response, size_t size);
    void handleHelp(char* response, size_t size);
};
```

**Usage:**
```cpp
#include "menu_controller.h"

MenuController menu(sessionManager, hardware, profiles, bleManager);

// Process command string
char response[256];
menu.processCommand("SESSION_START:noisy_vcr:7200", response, sizeof(response));
Serial.println(response);
```

---

### Header: `calibration_controller.h`

#### CalibrationController Class

Calibration mode for individual finger testing.

```cpp
// include/calibration_controller.h

class CalibrationController {
public:
    CalibrationController(HardwareController& hardware);

    void startCalibration();
    void stopCalibration();
    bool isCalibrating() const;

    void testFinger(uint8_t finger, uint8_t amplitude, uint16_t durationMs);
    void testAllFingers(uint8_t amplitude, uint16_t durationMs);

private:
    HardwareController& _hardware;
    bool _isCalibrating;
};
```

**Usage:**
```cpp
#include "calibration_controller.h"

CalibrationController calibration(hardware);

// Start calibration mode
calibration.startCalibration();

// Test individual finger
calibration.testFinger(0, 100, 200);  // Thumb, amplitude 100, 200ms

// Test all fingers sequentially
calibration.testAllFingers(100, 100);

// Stop calibration
calibration.stopCalibration();
```

---

## LED Controller

LED patterns for boot sequence and therapy states.

### Boot Sequence Patterns

| Function                      | Pattern              | Color   |
|-------------------------------|----------------------|---------|
| `indicateBLEInit()`           | Rapid flash (10Hz)   | Blue    |
| `indicateConnectionSuccess()` | 5x flash             | Green   |
| `indicateWaitingForPhone()`   | Slow flash (1Hz)     | Blue    |
| `indicateReady()`             | Solid                | Green   |
| `indicateFailure()`           | Slow flash (0.5Hz)   | Red     |
| `indicateConnectionLost()`    | Rapid flash (5Hz)    | Orange  |

### Therapy State Patterns

| State                | Pattern        | Color  |
|----------------------|----------------|--------|
| `RUNNING`            | Breathing      | Green  |
| `PAUSED`             | Slow pulse     | Yellow |
| `STOPPING`           | Fade out       | Green  |
| `LOW_BATTERY`        | Breathing      | Orange |
| `CRITICAL_BATTERY`   | Rapid flash    | Red    |
| `CONNECTION_LOST`    | Rapid flash    | Orange |
| `ERROR`              | Solid          | Red    |

---

## Usage Examples

### Complete System Initialization

```cpp
// main.cpp

#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "types.h"
#include "hardware.h"
#include "ble_manager.h"
#include "therapy_engine.h"
#include "state_machine.h"
#include "led_controller.h"
#include "menu_controller.h"
#include "profile_manager.h"
#include "session_manager.h"
#include "sync_protocol.h"

// Global instances
DeviceConfig deviceConfig;
HardwareController hardware;
BLEManager bleManager;
StateMachine stateMachine;
LEDController ledController;
ProfileManager profileManager;
TherapyEngine* therapyEngine;
SessionManager* sessionManager;
MenuController* menuController;
SyncProtocol* syncProtocol;
BootResult bootResult;

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
    profileManager.begin();

    // 4. Create engine instances
    therapyEngine = new TherapyEngine(hardware, stateMachine);
    sessionManager = new SessionManager(*therapyEngine, profileManager, stateMachine);
    syncProtocol = new SyncProtocol(bleManager, deviceConfig.role);
    menuController = new MenuController(*sessionManager, hardware, profileManager, bleManager);

    // 5. Initialize BLE
    bleManager.begin(deviceConfig);

    // 6. Execute boot sequence
    bootResult = executeBootSequence();

    if (bootResult == BootResult::FAILED) {
        ledController.indicateFailure();
        while (true) { delay(1000); }
    }

    ledController.indicateReady();
    Serial.println(F("[INFO] Boot complete, entering main loop"));
}

void loop() {
    if (deviceConfig.role == DeviceRole::PRIMARY) {
        runPrimaryLoop();
    } else {
        runSecondaryLoop();
    }
}

void runPrimaryLoop() {
    static uint32_t lastHeartbeat = 0;

    // Update therapy engine
    therapyEngine->update();

    // Send heartbeat every 2 seconds during therapy
    if (stateMachine.currentState() == TherapyState::RUNNING) {
        if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
            syncProtocol->sendHeartbeat();
            lastHeartbeat = millis();
        }
    }

    // Update LED
    ledController.setTherapyState(stateMachine.currentState());
    ledController.updateBreathing();
}

void runSecondaryLoop() {
    // Check heartbeat timeout
    if (syncProtocol->isHeartbeatTimeout()) {
        hardware.stopAllMotors();
        stateMachine.forceState(TherapyState::CONNECTION_LOST);
        ledController.indicateConnectionLost();
    }

    // Update LED
    ledController.updateBreathing();
}
```

---

### Simple Therapy Execution

```cpp
#include "hardware.h"
#include "therapy_engine.h"
#include "state_machine.h"
#include "types.h"

HardwareController hardware;
StateMachine stateMachine;

void setup() {
    Serial.begin(115200);

    // Initialize hardware
    if (!hardware.begin()) {
        Serial.println(F("[ERROR] Hardware init failed"));
        while (true) { delay(1000); }
    }

    // Create engine
    TherapyEngine engine(hardware, stateMachine);

    // Start session with default profile
    TherapyConfig config = getDefaultNoisyVCR();
    engine.startSession(config, 60);  // 60 seconds

    Serial.println(F("Starting 60-second therapy session"));

    // Run until complete
    while (engine.isRunning()) {
        engine.update();
        yield();
    }

    Serial.printf("Completed %lu cycles\n", engine.getCyclesCompleted());
}

void loop() {
    // Nothing - single execution
}
```

---

### State Machine Integration

```cpp
#include "state_machine.h"
#include "led_controller.h"

StateMachine stateMachine;
LEDController ledController;

void onStateChange(TherapyState from, TherapyState to) {
    Serial.printf("State: %s -> %s\n",
        StateMachine::stateToString(from),
        StateMachine::stateToString(to));
    ledController.setTherapyState(to);
}

void setup() {
    Serial.begin(115200);
    ledController.begin();

    // Register callback
    stateMachine.setCallback(onStateChange);

    // Perform transitions
    stateMachine.transition(StateTrigger::CONNECTED);
    stateMachine.transition(StateTrigger::START_SESSION);
    delay(1000);
    stateMachine.transition(StateTrigger::PAUSE_SESSION);
    delay(1000);
    stateMachine.transition(StateTrigger::RESUME_SESSION);
    delay(1000);
    stateMachine.transition(StateTrigger::STOP_SESSION);
}

void loop() {
    ledController.updateBreathing();
    delay(50);
}
```

---

## Common Include Patterns

```cpp
// Core types
#include "types.h"        // DeviceRole, TherapyState, TherapyConfig, etc.
#include "config.h"       // Constants, pin definitions

// Hardware
#include "hardware.h"     // HardwareController (motors, battery, I2C)
#include "led_controller.h"

// Communication
#include "ble_manager.h"  // BLE radio and connection management
#include "sync_protocol.h" // PRIMARY-SECONDARY messaging

// Application
#include "state_machine.h"
#include "therapy_engine.h"
#include "session_manager.h"
#include "menu_controller.h"
#include "profile_manager.h"
#include "calibration_controller.h"
```

---

## Error Handling

Arduino C++ typically disables exceptions. Use return codes:

```cpp
// Result enum
enum class Result : uint8_t {
    OK,
    ERROR_TIMEOUT,
    ERROR_INVALID_PARAM,
    ERROR_NOT_CONNECTED,
    ERROR_HARDWARE
};

// Example usage
Result result = hardware.buzzFinger(finger, amplitude);
if (result != Result::OK) {
    Serial.printf("[ERROR] buzzFinger failed: %d\n", (int)result);
}

// BLE error response format
void sendErrorResponse(const char* error) {
    char response[64];
    snprintf(response, sizeof(response), "ERROR:%s\n\x04", error);
    bleManager.sendToPhone(response);
}
```

---

## Testing

BlueBuzzah v2 uses **PlatformIO test framework** for validation.

**Test Commands:**
```bash
pio test                    # Run all tests
pio test -e native          # Run native tests (no hardware)
pio test -e feather_nrf52840 # Run on-device tests
```

**Test Coverage:**
- State machine transitions
- Pattern generation
- SYNC protocol parsing
- BLE command handling
- Battery status calculation

See [Testing Guide](TESTING.md) for detailed procedures.

---

## Version Information

**API Version**: 2.0.0
**Protocol Version**: 2.0.0
**Firmware Version**: 2.0.0
**Platform**: Arduino C++ with PlatformIO

---

## Additional Resources

- [Architecture Guide](ARCHITECTURE.md) - System design and patterns
- [Arduino Firmware Architecture](ARDUINO_FIRMWARE_ARCHITECTURE.md) - Module structure and C++ patterns
- [Boot Sequence](BOOT_SEQUENCE.md) - Boot process and LED indicators
- [Therapy Engine](THERAPY_ENGINE.md) - Pattern generation and timing
- [Testing Guide](TESTING.md) - Test framework and procedures
- [Synchronization Protocol](SYNCHRONIZATION_PROTOCOL.md) - Device sync details
- [BLE Protocol](BLE_PROTOCOL.md) - Command reference for mobile apps

---

**Last Updated**: 2025-01-11
**Document Version**: 2.0.0
