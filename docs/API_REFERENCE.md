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
- [FreeRTOS Integration](#freertos-integration)
- [Application Layer](#application-layer)
- [LED Controller](#led-controller)
- [Usage Examples](#usage-examples)

---

## Overview

BlueBuzzah v2 provides a clean, layered API for bilateral haptic therapy control. The architecture follows Clean Architecture principles with clear separation between layers:

- **Core**: Types, constants, and fundamental definitions (`types.h`, `config.h`)
- **State**: Explicit state machine for therapy sessions (`state_machine.h`)
- **Hardware**: HapticController, LEDController, battery monitoring (`hardware.h`)
- **Communication**: BLE protocol and message handling (`ble_manager.h`)
- **Therapy**: Pattern generation and therapy execution (`therapy_engine.h`)
- **Application**: Command routing and profile management (`menu_controller.h`, `profile_manager.h`)

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

#define BLE_INTERVAL_MIN_MS 8
#define BLE_INTERVAL_MAX_MS 12
// BLE connection interval range (6-9 BLE units, 8-12ms)

#define SYNC_INTERVAL_MS 1000
// Periodic synchronization interval in milliseconds (SYNC_ADJ messages)

#define COMMAND_TIMEOUT_MS 5000
// General BLE command timeout in milliseconds

#define KEEPALIVE_INTERVAL_MS 1000
// PING/PONG interval in milliseconds (unified keepalive + clock sync, all states)

#define KEEPALIVE_TIMEOUT_MS 6000
// SECONDARY keepalive timeout (6 missed PINGs = connection lost)
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

#### HapticController Class

Hardware control for motors, battery, and I2C multiplexer.

```cpp
// include/hardware.h

#include <Adafruit_DRV2605.h>
#include <Adafruit_TCA9548A.h>

class HapticController {
public:
    bool begin();

    // Motor control
    void activate(uint8_t finger, uint8_t amplitude);
    void deactivate(uint8_t finger);
    void stopAll();
    bool isActive(uint8_t finger) const;

    // DRV2605 configuration
    void setFrequency(uint8_t finger, uint16_t frequencyHz);
    void setActuatorType(bool useLRA);

    // I2C Pre-Selection (Latency Optimization)
    // Moves mux selection off critical path for ~100μs vs ~500μs activation
    bool selectChannelPersistent(uint8_t finger);  // Opens mux channel, keeps it open
    void setFrequencyDirect(uint8_t finger, uint16_t frequencyHz);  // Sets freq on pre-selected channel
    void activatePreSelected(uint8_t finger, uint8_t amplitude);  // Fast activation (~100μs)
    int8_t getPreSelectedFinger() const;  // Returns currently pre-selected finger (-1 if none)

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
    Adafruit_DRV2605 _drv[4];
    bool _motorActive[4];
    int8_t _preSelectedFinger;  // Currently pre-selected finger (-1 if none)

    void configureDRV2605(Adafruit_DRV2605& driver);
};
```

**Usage:**
```cpp
#include "hardware.h"

HapticController hardware;

// Initialize hardware
if (!hardware.begin()) {
    Serial.println(F("[ERROR] Hardware init failed"));
    while (true) { delay(1000); }
}

// Activate motor (standard path)
hardware.activate(0, 100);  // Index finger at ~78% (100/127)
delay(100);
hardware.deactivate(0);

// Fast path using I2C pre-selection (for FreeRTOS motor task)
hardware.selectChannelPersistent(0);  // Pre-select channel
hardware.setFrequencyDirect(0, 250);  // Set frequency
// ... later, when event is due:
hardware.activatePreSelected(0, 100);  // ~100μs vs ~500μs

// Stop all motors
hardware.stopAll();

// Check battery
BatteryStatus status = hardware.getBatteryStatus();
Serial.printf("Battery: %.2fV (%d%%) - %s\n",
    status.voltage, status.percentage, status.status);

if (hardware.isBatteryCritical()) {
    // Emergency shutdown
}
```

---

#### LEDController Class

NeoPixel LED control for visual feedback (defined in `hardware.h`).

```cpp
// include/hardware.h

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
#include "hardware.h"  // LEDController is in hardware.h
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

Core therapy execution engine using a callback-driven architecture. TherapyEngine is decoupled from hardware—it uses callbacks to trigger motor activations, allowing flexible integration with HapticController, ActivationQueue, and SyncProtocol.

```cpp
// include/therapy_engine.h

class TherapyEngine {
public:
    TherapyEngine();  // Default constructor, no parameters

    // =========================================================================
    // CALLBACKS - Set these before starting a session
    // =========================================================================

    // Motor control callbacks
    void setActivateCallback(ActivateCallback callback);      // Called to activate motor
    void setDeactivateCallback(DeactivateCallback callback);  // Called to deactivate motor

    // Event callbacks
    void setCycleCompleteCallback(CycleCompleteCallback callback);      // After each cycle
    void setSetFrequencyCallback(SetFrequencyCallback callback);        // For Custom vCR

    // Sync protocol callbacks (PRIMARY)
    void setMacrocycleStartCallback(MacrocycleStartCallback callback);  // Before macrocycle
    void setSendMacrocycleCallback(SendMacrocycleCallback callback);    // To send batch

    // FreeRTOS scheduling callbacks (PRIMARY motor task)
    void setSchedulingCallbacks(ScheduleActivationCallback scheduleCallback,
                                StartSchedulingCallback startCallback,
                                IsSchedulingCompleteCallback isCompleteCallback);

    // Adaptive lead time callback
    void setGetLeadTimeCallback(GetLeadTimeCallback callback);

    // =========================================================================
    // SESSION CONTROL
    // =========================================================================

    void startSession(
        uint32_t durationSec,
        PatternType patternType = PatternType::RNDP,
        float timeOnMs = 100.0f,
        float timeOffMs = 67.0f,
        float jitterPercent = 0.0f,
        uint8_t numFingers = 4,
        bool mirrorPattern = false,
        uint8_t amplitudeMin = 100,
        uint8_t amplitudeMax = 100,
        bool isTestMode = false
    );

    void update();  // MUST be called frequently in loop()
    void pause();
    void resume();
    void stop();

    // =========================================================================
    // STATUS
    // =========================================================================

    bool isRunning() const;
    bool isPaused() const;
    bool isTestMode() const;
    uint32_t getCyclesCompleted() const;
    uint32_t getElapsedSeconds() const;
    uint32_t getRemainingSeconds() const;
};
```

**Important:** The `update()` method MUST be called frequently in the main loop for therapy to progress.

**Usage:**
```cpp
#include "therapy_engine.h"
#include "hardware.h"

HapticController haptic;
TherapyEngine engine;

// Set up callbacks before starting
engine.setActivateCallback([](uint8_t finger, uint8_t amplitude) {
    haptic.activate(finger, amplitude);
});

engine.setDeactivateCallback([](uint8_t finger) {
    haptic.deactivate(finger);
});

engine.setCycleCompleteCallback([](uint32_t count) {
    Serial.printf("Cycle %lu complete\n", count);
});

// Start session (noisy vCR: mirrored pattern)
engine.startSession(
    7200,                      // 2 hours
    PatternType::RNDP,         // Random permutation
    100.0f,                    // 100ms burst duration
    67.0f,                     // 67ms inter-burst interval
    23.5f,                     // 23.5% jitter
    4,                         // 4 fingers
    true                       // Mirror pattern (noisy vCR)
);

// Main loop - update() is REQUIRED
while (engine.isRunning()) {
    engine.update();
    yield();
}

Serial.printf("Completed %lu cycles\n", engine.getCyclesCompleted());
```

---

### Pattern Generation

Pattern generation for bilateral therapy.

```cpp
// include/therapy_engine.h (private implementation)

void TherapyEngine::generatePattern() {
    // Generate random permutation for PRIMARY device
    for (uint8_t i = 0; i < 4; i++) {
        _primarySequence[i] = i;
    }

    // Fisher-Yates shuffle
    for (uint8_t i = 3; i > 0; i--) {
        uint8_t j = random(0, i + 1);
        uint8_t temp = _primarySequence[i];
        _primarySequence[i] = _primarySequence[j];
        _primarySequence[j] = temp;
    }

    if (_config.mirrorPattern) {
        // Same finger on both devices (noisy vCR)
        memcpy(_secondarySequence, _primarySequence, sizeof(_primarySequence));
    } else {
        // Independent sequences (regular vCR)
        for (uint8_t i = 0; i < 4; i++) {
            _secondarySequence[i] = i;
        }
        for (uint8_t i = 3; i > 0; i--) {
            uint8_t j = random(0, i + 1);
            uint8_t temp = _secondarySequence[i];
            _secondarySequence[i] = _secondarySequence[j];
            _secondarySequence[j] = temp;
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
    bool sendBuzz(uint8_t finger, uint8_t amplitude);
    bool sendDeactivate();
    bool sendPing();  // Unified keepalive + clock sync

    // Command receiving (SECONDARY)
    bool hasCommand() const;
    bool parseCommand(const char* data);

    // Callback registration
    typedef void (*CommandCallback)(const char* command, const char* params);
    void setCommandCallback(CommandCallback cb);

    // Status
    uint32_t getLastKeepaliveTime() const;
    bool isKeepaliveTimeout() const;

private:
    BLEManager& _bleManager;
    DeviceRole _role;
    uint32_t _lastKeepalive;
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
| MACROCYCLE     | P->S      | Batch of 12 motor events  |
| MACROCYCLE_ACK | S->P      | Macrocycle acknowledgment |
| DEACTIVATE     | P->S      | Stop motor activation     |
| PING           | P->S      | Keepalive + clock sync    |
| PONG           | S->P      | Keepalive + clock sync    |

**Examples:**
```
START_SESSION:1|1234567890
MC:42|5000000|12|100,0,100,100,235|67,1,100,100,235|...
PING:1|1234567890
PONG:1|0|1234567900|1234567950
MC_ACK:42
```

**Usage:**
```cpp
#include "sync_protocol.h"

SyncProtocol sync(bleManager, DeviceRole::PRIMARY);

// Callback for received commands (SECONDARY)
void onSyncCommand(const char* command, const char* params) {
    if (strcmp(command, "MACROCYCLE") == 0) {
        // Parse params and execute macrocycle batch
    }
}
sync.setCommandCallback(onSyncCommand);

// Send execute command (PRIMARY)
sync.sendBuzz(2, 100);  // Finger 2, amplitude 100

// Send PING for keepalive + clock sync (PRIMARY, call every 1 second)
sync.sendPing();

// Check keepalive timeout (SECONDARY)
if (sync.isKeepaliveTimeout()) {
    handleConnectionLost();
}
```

---

## FreeRTOS Integration

### Header: `activation_queue.h`

#### ActivationQueue Class

Unified motor event queue for FreeRTOS-based motor control. Manages scheduled motor activations and deactivations with sub-millisecond timing precision.

```cpp
// include/activation_queue.h

enum class MotorEventType : uint8_t {
    ACTIVATE,    // Turn motor ON
    DEACTIVATE   // Turn motor OFF
};

struct MotorEvent {
    uint64_t timeUs;        // Event time (local clock, microseconds)
    uint8_t  finger;        // Motor index (0-3)
    uint8_t  amplitude;     // Intensity (0-100), only used for ACTIVATE
    uint16_t frequencyHz;   // Motor frequency, only used for ACTIVATE
    MotorEventType type;    // ACTIVATE or DEACTIVATE
    volatile bool active;   // Slot in use
};

class ActivationQueue {
public:
    static constexpr uint8_t MAX_EVENTS = 32;  // 12 activations + 12 deactivations + margin

    // Initialize queue
    void begin(HapticController* haptic, TaskHandle_t motorTaskHandle);

    // Clear all scheduled events
    void clear();

    // Add motor activation (auto-enqueues corresponding deactivation)
    bool enqueue(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
                 uint16_t durationMs, uint16_t frequencyHz);

    // Peek at next event without removing
    bool peekNextEvent(MotorEvent& event) const;

    // Get and remove next event
    bool dequeueNextEvent(MotorEvent& event);

    // Get time of next event (UINT64_MAX if empty)
    uint64_t getNextEventTime() const;

    // Query state
    uint8_t eventCount() const;
    bool isEmpty() const;

    // Wake motor task when new event added
    void notifyMotorTask();
};

// Global instance
extern ActivationQueue activationQueue;
```

**Usage Pattern:**
```cpp
// In therapy engine callback:
activationQueue.enqueue(executeTimeUs, finger, amplitude, durationMs, freqHz);
activationQueue.notifyMotorTask();

// Motor task (FreeRTOS) processes events:
MotorEvent event;
while (activationQueue.peekNextEvent(event)) {
    // Sleep until event time, then execute
    activationQueue.dequeueNextEvent(event);
    executeEvent(event);
}
```

---

### Header: `motor_event_buffer.h`

#### MotorEventBuffer Class

Lock-free ring buffer for staging motor events from BLE callbacks (ISR context) to main loop. Uses SPSC (single-producer, single-consumer) model with ARM memory barriers for thread safety.

```cpp
// include/motor_event_buffer.h

struct StagedMotorEvent {
    uint64_t activateTimeUs;   // Absolute activation time
    uint8_t finger;            // Finger index (0-3)
    uint8_t amplitude;         // Amplitude percentage (0-100)
    uint16_t durationMs;       // Duration in milliseconds
    uint16_t frequencyHz;      // Frequency in Hz
    bool isMacrocycleLast;     // True if last event in macrocycle batch
    volatile bool valid;       // Marks slot as ready
};

class MotorEventBuffer {
public:
    static constexpr uint8_t MAX_STAGED = 16;

    // Stage a motor event from ISR/BLE callback (ISR-safe)
    bool stage(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
               uint16_t durationMs, uint16_t frequencyHz, bool isMacrocycleLast = false);

    // Begin a new macrocycle batch (ISR-safe)
    void beginMacrocycle();

    // Check if macrocycle batch is pending
    bool isMacrocyclePending() const;

    // Unstage next event (main loop only)
    bool unstage(StagedMotorEvent& event);

    // Check if events pending (any context)
    bool hasPending() const;
    uint8_t getPendingCount() const;

    // Clear all pending events (main loop only)
    void clear();
};

// Global instance
extern MotorEventBuffer motorEventBuffer;
```

**Usage Pattern:**
```cpp
// In BLE callback (ISR context):
motorEventBuffer.beginMacrocycle();
motorEventBuffer.stage(timeUs, finger, amp, dur, freq, isLast);

// In main loop:
if (motorEventBuffer.isMacrocyclePending()) {
    activationQueue.clear();  // New macrocycle replaces old events
}
StagedMotorEvent event;
while (motorEventBuffer.unstage(event)) {
    activationQueue.enqueue(event.activateTimeUs, event.finger,
                            event.amplitude, event.durationMs, event.frequencyHz);
}
```

---

### Header: `deferred_queue.h`

#### DeferredQueue Class

ISR-safe work queue for deferring operations that aren't safe in callback context (blocking I2C, delays) to main loop execution.

```cpp
// include/deferred_queue.h

enum class DeferredWorkType : uint8_t {
    NONE = 0,
    HAPTIC_PULSE,        // finger, amplitude, duration_ms
    HAPTIC_DOUBLE_PULSE, // finger, amplitude, duration_ms (double pulse)
    HAPTIC_DEACTIVATE,   // finger, 0, 0
    SCANNER_RESTART,     // 0, 0, delay_ms
    LED_FLASH            // r, g, b (packed in params)
};

class DeferredQueue {
public:
    typedef void (*WorkExecutor)(DeferredWorkType type, uint8_t p1, uint8_t p2, uint32_t p3);

    // Enqueue work (ISR-safe)
    bool enqueue(DeferredWorkType type, uint8_t param1 = 0, uint8_t param2 = 0, uint32_t param3 = 0);

    // Process one queued item (main loop)
    bool processOne();

    // Query state
    bool hasPending() const;
    uint8_t getPendingCount() const;
    void clear();

    // Set callback for executing work
    void setExecutor(WorkExecutor executor);
};

// Global instance
extern DeferredQueue deferredQueue;
```

**Usage Pattern:**
```cpp
// In BLE callback (ISR context) - can't do I2C here:
deferredQueue.enqueue(DeferredWorkType::HAPTIC_PULSE, finger, amplitude, durationMs);

// In main loop:
deferredQueue.processOne();  // Executes one item per iteration
```

---

### Header: `latency_metrics.h`

#### LatencyMetrics Struct

Runtime-toggleable latency measurement for execution drift, BLE RTT timing, and sync quality analysis.

```cpp
// include/latency_metrics.h

struct LatencyMetrics {
    // State
    bool enabled;
    bool verboseLogging;

    // Execution drift (actual - scheduled, microseconds)
    int32_t lastDrift_us;
    int32_t minDrift_us;
    int32_t maxDrift_us;
    int64_t totalDrift_us;
    uint32_t sampleCount;
    uint32_t lateCount;     // Count of drift > LATENCY_LATE_THRESHOLD_US
    uint32_t earlyCount;    // Count of negative drift

    // BLE RTT (PRIMARY only)
    uint32_t lastRtt_us;
    uint32_t minRtt_us;
    uint32_t maxRtt_us;
    uint64_t totalRtt_us;
    uint32_t rttSampleCount;

    // Sync quality (from initial probing)
    uint32_t syncProbeCount;
    uint32_t syncMinRtt_us;
    uint32_t syncMaxRtt_us;
    uint32_t syncRttSpread_us;
    int64_t calculatedOffset_us;

    // Methods
    void reset();
    void enable(bool verbose = false);
    void disable();

    void recordExecution(int32_t drift_us);
    void recordRtt(uint32_t rtt_us);
    void recordSyncProbe(uint32_t rtt_us);
    void finalizeSyncProbing(int64_t offset_us);

    int32_t getAverageDrift() const;
    uint32_t getAverageRtt() const;
    uint32_t getJitter() const;
    const char* getSyncConfidence() const;  // "HIGH", "MEDIUM", or "LOW"

    void printReport() const;
};

// Global instance
extern LatencyMetrics latencyMetrics;
```

**Usage Pattern:**
```cpp
// Enable via serial command:
latencyMetrics.enable(true);  // true = verbose logging

// In motor task after execution:
int32_t drift = (int32_t)(actualTimeUs - scheduledTimeUs);
latencyMetrics.recordExecution(drift);

// Check metrics:
Serial.printf("Avg drift: %ld us, Jitter: %lu us\n",
              latencyMetrics.getAverageDrift(), latencyMetrics.getJitter());

// Print full report:
latencyMetrics.printReport();
```

See [LATENCY_METRICS.md](LATENCY_METRICS.md) for detailed metrics interpretation and serial commands.

---

## Application Layer

### Header: `menu_controller.h`

#### MenuController Class

BLE command processing and routing. Coordinates between TherapyEngine, HapticController, ProfileManager, and BLEManager.

```cpp
// include/menu_controller.h

class MenuController {
public:
    MenuController();

    // Initialize with component references (call in setup)
    void begin(
        TherapyEngine* therapyEngine,
        BatteryMonitor* batteryMonitor,
        HapticController* hapticController,
        TherapyStateMachine* stateMachine,
        ProfileManager* profileManager = nullptr,
        BLEManager* bleManager = nullptr
    );

    // Process incoming command, returns response string
    void processCommand(const char* command, char* response, size_t responseSize);

private:
    TherapyEngine* _therapyEngine;
    BatteryMonitor* _batteryMonitor;
    HapticController* _hapticController;
    TherapyStateMachine* _stateMachine;
    ProfileManager* _profileManager;
    BLEManager* _bleManager;

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

MenuController menu;

// In setup(), after initializing other components:
menu.begin(&therapyEngine, &batteryMonitor, &haptic, &stateMachine, &profileManager, &bleManager);

// Process command string
char response[256];
menu.processCommand("SESSION_START:noisy_vcr:7200", response, sizeof(response));
Serial.println(response);
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
#include "menu_controller.h"
#include "profile_manager.h"
#include "sync_protocol.h"

// Global instances
DeviceConfig deviceConfig;
HapticController haptic;
LEDController ledController;
BLEManager bleManager;
StateMachine stateMachine;
ProfileManager profileManager;
TherapyEngine therapyEngine;
SyncProtocol syncProtocol;
MenuController menuController;
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
    if (!haptic.begin()) {
        ledController.indicateFailure();
        while (true) { delay(1000); }
    }

    ledController.begin();
    profileManager.begin();

    // 4. Set up TherapyEngine callbacks (callback-driven architecture)
    therapyEngine.setActivateCallback([](uint8_t finger, uint8_t amplitude) {
        haptic.activate(finger, amplitude);
    });
    therapyEngine.setDeactivateCallback([](uint8_t finger) {
        haptic.deactivate(finger);
    });

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
    static uint32_t lastPing = 0;

    // Update therapy engine (REQUIRED for therapy to progress)
    therapyEngine.update();

    // Send unified keepalive + clock sync PING (every 1s, all states)
    if (millis() - lastPing >= KEEPALIVE_INTERVAL_MS) {
        syncProtocol.sendPing();  // PING provides both keepalive and clock sync
        lastPing = millis();
    }

    // Update LED
    ledController.setTherapyState(stateMachine.currentState());
    ledController.updateBreathing();
}

void runSecondaryLoop() {
    // Check keepalive timeout (6s without PING/MACROCYCLE)
    if (syncProtocol.isKeepaliveTimeout()) {
        haptic.stopAll();
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
#include "types.h"

HapticController haptic;
TherapyEngine engine;

void setup() {
    Serial.begin(115200);

    // Initialize hardware
    if (!haptic.begin()) {
        Serial.println(F("[ERROR] Hardware init failed"));
        while (true) { delay(1000); }
    }

    // Set up callbacks (callback-driven architecture)
    engine.setActivateCallback([](uint8_t finger, uint8_t amplitude) {
        haptic.activate(finger, amplitude);
    });
    engine.setDeactivateCallback([](uint8_t finger) {
        haptic.deactivate(finger);
    });

    // Start session (noisy vCR parameters)
    engine.startSession(
        60,                        // 60 seconds
        PatternType::RNDP,         // Random permutation
        100.0f,                    // 100ms burst duration
        67.0f,                     // 67ms inter-burst interval
        23.5f,                     // 23.5% jitter
        4,                         // 4 fingers
        true                       // Mirror pattern (noisy vCR)
    );

    Serial.println(F("Starting 60-second therapy session"));

    // Run until complete - update() is REQUIRED
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
#include "hardware.h"  // LEDController is in hardware.h

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
#include "types.h"        // DeviceRole, TherapyState, PatternType, etc.
#include "config.h"       // Constants, pin definitions

// Hardware
#include "hardware.h"     // HapticController, LEDController (motors, battery, I2C)

// Communication
#include "ble_manager.h"  // BLE radio and connection management
#include "sync_protocol.h" // PRIMARY-SECONDARY messaging

// Application
#include "state_machine.h"
#include "therapy_engine.h"
#include "menu_controller.h"
#include "profile_manager.h"

// FreeRTOS motor scheduling
#include "activation_queue.h"     // Motor event scheduling queue
#include "motor_event_buffer.h"   // Lock-free ring buffer (BLE→main thread)
#include "deferred_queue.h"       // ISR-safe work queue
#include "latency_metrics.h"      // Performance tracking
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
Result result = hardware.activate(finger, amplitude);
if (result != Result::OK) {
    Serial.printf("[ERROR] activate failed: %d\n", (int)result);
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
