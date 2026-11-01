# BlueBuzzah Firmware API Reference

**Comprehensive API documentation for the BlueBuzzah bilateral haptic therapy system**

Platform: Arduino C++ on Adafruit Feather nRF52840 Express (v2, 4 motors) and Seeed XIAO ESP32-S3 PentaBuzzer (v3, 5 motors)
Build System: PlatformIO (`adafruit_feather_nrf52840`, `pentabuzzer_esp32s3`)

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

The BlueBuzzah firmware provides a clean, layered API for bilateral haptic therapy control. The architecture follows Clean Architecture principles with clear separation between layers:

- **Core**: Types, constants, and fundamental definitions (`types.h`, `config.h`)
- **State**: Explicit state machine for therapy sessions (`state_machine.h`)
- **Hardware**: HapticController, BatteryMonitor, LEDController (`hardware.h`)
- **Communication**: BLE protocol and message handling (`ble_manager.h`), PRIMARY↔SECONDARY sync (`sync_protocol.h`)
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
    PRIMARY = 0,    // Orchestrates therapy, connects to phone
    SECONDARY = 1   // Follows PRIMARY commands
};

// Helper functions
inline const char* deviceRoleToString(DeviceRole role);  // "PRIMARY" / "SECONDARY"
inline const char* deviceRoleToTag(DeviceRole role);     // "[PRIMARY]" / "[SECONDARY]"
```

**Usage:**
```cpp
#include "types.h"
#include "ble_manager.h"

DeviceRole role = profiles.getDeviceRole();
Serial.printf("[ROLE] Device configured as: %s\n", deviceRoleToString(role));

ble.begin(role, BLE_NAME);            // PRIMARY starts advertising inside begin()
if (role == DeviceRole::SECONDARY) {
    ble.startScanning(BLE_NAME);      // SECONDARY scans for the PRIMARY
}
```

---

#### TherapyState

Therapy session state machine states.

```cpp
// include/types.h

enum class TherapyState : uint8_t {
    IDLE = 0,           // No active session, waiting for commands
    CONNECTING,         // Establishing BLE connections during boot
    READY,              // Connected and ready to start therapy
    RUNNING,            // Actively delivering therapy
    PAUSED,             // Therapy temporarily suspended by user
    STOPPING,           // Graceful shutdown in progress
    ERROR,              // Unrecoverable error occurred
    LOW_BATTERY,        // Session running on a low battery (BATTERY_LOW_VOLTAGE)
    CRITICAL_BATTERY,   // Battery critical: session stopped until the glove charges
    CONNECTION_LOST,    // PRIMARY-SECONDARY connection lost (from READY, RUNNING, LOW_BATTERY or PAUSED)
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
| `ERROR` | Boot hardware fault (no motor driver, no motor task, or v3 fewer than 4 motors); motors stopped and sessions refused until reboot |
| `LOW_BATTERY` | Session running on a low battery (two readings <3.8V, `BATTERY_LOW_VOLTAGE`); behaves like `RUNNING` |
| `CRITICAL_BATTERY` | Battery critical (two readings <3.3V): motors stopped until the glove charges to ≥3.8V, then `IDLE` |
| `CONNECTION_LOST` | PRIMARY-SECONDARY connection lost (from READY, RUNNING, LOW_BATTERY or PAUSED) |
| `PHONE_DISCONNECTED` | Phone connection lost (informational, therapy continues) |

**State Transitions** (`TherapyStateMachine::determineNextState()`, `src/state_machine.cpp`):
```
IDLE -> READY -> RUNNING <-> PAUSED -> STOPPING -> IDLE
         |          |          |
         v          v          v
       CONNECTION_LOST (peer glove lost) -> READY on reconnect,
                                            IDLE after CONNECTION_LOST_TIMEOUT_MS (30 s)
```

`CONNECTING` exists in the enum but is never entered: `CONNECTED` moves `IDLE` straight to `READY`. `RUNNING` becomes `LOW_BATTERY` (`BATTERY_WARNING`) while this glove's battery is low and returns on `BATTERY_OK`; `LOW_BATTERY` pauses, stops, completes, and loses the peer or phone exactly like `RUNNING`. A critical battery enters `CRITICAL_BATTERY` (`BATTERY_CRITICAL`) from any state except `ERROR` or during a bench `TEST`, and `BATTERY_OK` returns it to `IDLE` once charged. `setup()` fires `ERROR_OCCURRED` on a boot hardware fault; nothing leaves `ERROR` until reboot. `reconcileBatteryState()` (`src/main.cpp`) fires these every loop pass.

**Note:** `PHONE_DISCONNECTED` is an informational state - therapy continues normally when the phone disconnects. Only `CONNECTION_LOST` (PRIMARY-SECONDARY), `CRITICAL_BATTERY` and `ERROR` stop therapy.

**Helper Functions:**

```cpp
// include/types.h

inline const char* therapyStateToString(TherapyState state);  // "IDLE", "RUNNING", ...

inline bool isActiveState(TherapyState state) {
    return state == TherapyState::RUNNING ||
           state == TherapyState::PAUSED ||
           state == TherapyState::LOW_BATTERY;
}

inline bool isErrorState(TherapyState state) {
    return state == TherapyState::ERROR ||
           state == TherapyState::CRITICAL_BATTERY ||
           state == TherapyState::CONNECTION_LOST;
}
```

**Usage:**
```cpp
#include "types.h"

TherapyState state = stateMachine.getCurrentState();
Serial.printf("State: %s\n", therapyStateToString(state));
if (isErrorState(state)) {
    haptic.stopAll();
}
```

---

#### BootResult

Boot sequence outcome enumeration.

```cpp
// include/types.h

enum class BootResult : uint8_t {
    FAILED = 0,         // Boot sequence failed
    SUCCESS_NO_PHONE,   // Connected to glove but no phone
    SUCCESS_WITH_PHONE, // Connected to both glove and phone
    SUCCESS             // For SECONDARY (only needs PRIMARY connection)
};

inline bool isBootSuccess(BootResult result) {
    return result != BootResult::FAILED;
}
```

`BootResult` and `isBootSuccess()` are declared in `types.h` but not used by `src/`; the boot flow in `src/main.cpp` `setup()` tracks `hardwareReady` / `bleReady` instead (see [BOOT_SEQUENCE.md](BOOT_SEQUENCE.md)).

---

#### BatteryStatus

Battery status information struct.

```cpp
// include/types.h

struct BatteryStatus {
    float voltage;       // Current voltage in volts
    uint8_t percentage;  // Battery percentage 0-100
    bool isLow;          // True if below BATTERY_LOW_VOLTAGE threshold
    bool isCritical;     // True if below BATTERY_CRITICAL_VOLTAGE threshold

    const char* statusString() const;  // "OK", "LOW", or "CRITICAL"

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

BatteryMonitor battery;
battery.begin();

BatteryStatus status = battery.getStatus();
Serial.printf("[BATTERY] %.2fV | %d%% | Status: %s\n",
              status.voltage, status.percentage, status.statusString());
```

---

### Header: `config.h`

System-wide constants for timing, hardware, battery thresholds, and more.

#### Firmware Version

```cpp
// include/config.h

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "2.0.0"
#endif
```
Firmware version following semantic versioning. The `config.h` value is a default that local builds report; release builds (`.github/workflows/release.yml`) override it with the release tag (leading `v` stripped) via `-DFIRMWARE_VERSION`, and pass the same value as `BLUEBUZZAH_VERSION` for the v3 package manifest.

---

#### Timing Constants

```cpp
// include/config.h

#define STARTUP_WINDOW_MS 30000
// Boot sequence connection window in milliseconds

#define CONNECTION_TIMEOUT_MS 30000
// BLE connection establishment timeout in milliseconds

#define BLE_INTERVAL_MIN_MS 7.5f
#define BLE_INTERVAL_MAX_MS 10
// BLE connection interval range (6-8 BLE units, 7.5-10ms)

#define BLE_PHONE_INTERVAL_MIN_UNITS 24
#define BLE_PHONE_INTERVAL_MAX_UNITS 40
// Phone link connection interval request (1.25ms units, 30-50ms, no peripheral latency);
// the glove link keeps the BLE_INTERVAL_MIN_MS/MAX_MS range

#define COMMAND_TIMEOUT_MS 5000
// General BLE command timeout in milliseconds

#define KEEPALIVE_INTERVAL_MS 1000
// Idle PING/PONG interval in milliseconds (unified keepalive + clock sync)

#define SYNC_ACTIVE_INTERVAL_MS 250
// PING/PONG interval while therapy is running

#define SYNC_ACQUIRE_INTERVAL_MS 70
// PING/PONG interval until clock sync is valid (cold or warm start); overrides the two above

#define KEEPALIVE_TIMEOUT_MS 6000
// SECONDARY keepalive timeout (6 missed PINGs = connection lost)
```

---

#### Hardware Constants

```cpp
// include/board_config.h

#define TCA9548A_ADDRESS  0x70
// TCA9548A I2C multiplexer address (both boards)

#define DRV2605_ADDRESS   0x5A
// DRV2605 haptic driver I2C address (both boards)

#define MAX_ACTUATORS 4   // BOARD_BLUEBUZZAH_NRF52 (Feather nRF52840)
#define MAX_ACTUATORS 5   // BOARD_PENTABUZZER_ESP32S3 (adds the thumb, index 4)
// Haptic actuators per device, selected by the board macro

// include/config.h

#define I2C_FREQUENCY 400000
// I2C bus frequency in Hz (400 kHz Fast Mode)

#define MAX_AMPLITUDE 100
// Maximum amplitude percentage (0-100)

#define DRV2605_MAX_RTP 127
// DRV2605 RTP register max value (amplitude % is scaled to 0-127)
```

---

#### Pin Assignments

```cpp
// include/config.h (per-board values from include/board_config.h)

#define NEOPIXEL_PIN NEOPIXEL_PIN_OVERRIDE
// NeoPixel LED data pin: PIN_NEOPIXEL on the Feather, GPIO4 on PentaBuzzer

#if BATTERY_SENSE_ADC
#define BATTERY_PIN BATTERY_PIN_OVERRIDE
#endif
// Battery voltage ADC pin: PIN_VBAT on the Feather. PentaBuzzer has no battery
// ADC; it reads VBAT from the DRV2605 VBAT register instead

// I2C uses default Wire pins on the Feather; PentaBuzzer sets
// SDA_PIN_OVERRIDE 5 / SCL_PIN_OVERRIDE 6
```

---

#### LED Colors

Status colors are `RGBColor` constants in the `Colors` namespace (`include/types.h`),
passed to `LEDController::setPattern()`:

```cpp
// include/types.h
namespace Colors {
    const RGBColor OFF(0, 0, 0);          // LED off
    const RGBColor RED(255, 0, 0);        // Error / critical
    const RGBColor GREEN(0, 255, 0);      // Ready / running
    const RGBColor BLUE(0, 0, 255);       // Idle / connecting
    const RGBColor WHITE(255, 255, 255);  // Test/diagnostic flash
    const RGBColor YELLOW(255, 255, 0);   // Paused / stopping
    const RGBColor ORANGE(255, 128, 0);   // Low battery / unconfigured
    const RGBColor PURPLE(128, 0, 255);   // Connection lost (peer glove)
    const RGBColor CYAN(0, 255, 255);     // Hardware-init success (boot)
}
```

---

#### Battery Thresholds

```cpp
// include/config.h

#define BATTERY_CRITICAL_VOLTAGE 3.3f
// Critical battery voltage (immediate shutdown)

#define BATTERY_CRITICAL_CONFIRM_READINGS 2
// Consecutive readings below a threshold before the level turns low or critical

#define BATTERY_LOW_HYSTERESIS_V 0.10f
// Low clears at BATTERY_LOW_VOLTAGE + this; critical clears at BATTERY_LOW_VOLTAGE

#define BATTERY_CHECK_INTERVAL_MS 60000
// Battery voltage check interval in milliseconds

// include/board_config.h (per board)
#define BATTERY_LOW_VOLTAGE 3.80f
// Low battery: can't finish a 2-hour session (orange LED, sessions refused)
```

---

## Configuration System

### Header: `types.h` / `profile_manager.h`

#### DeviceConfig

Device identity struct.

```cpp
// include/types.h

struct DeviceConfig {
    DeviceRole role;
    char bleName[32];
    char deviceTag[16];

    DeviceConfig();             // PRIMARY, "BlueBuzzah", "[PRIMARY]"
    bool isPrimary() const;
    bool isSecondary() const;
};
```

`DeviceConfig` is declared but not used by `src/`. The device role is persisted by `ProfileManager` in the binary settings file (`SETTINGS_FILE "/settings.bin"`) and read back with `ProfileManager::hasStoredRole()` / `getDeviceRole()`. A device with no stored role blinks orange and waits for `SET_ROLE:PRIMARY|SECONDARY` over USB serial, then reboots.

**Usage:**
```cpp
#include "profile_manager.h"

ProfileManager profiles;
profiles.begin();

if (!profiles.hasStoredRole()) {
    waitForRoleConfiguration();  // src/main.cpp - never returns, reboots after SET_ROLE
}
DeviceRole role = profiles.getDeviceRole();
```

---

#### TherapyConfig

Therapy session configuration struct.

```cpp
// include/types.h

struct TherapyConfig {
    uint32_t durationSec;       // Session duration in seconds (default 7200)
    uint8_t amplitude;          // Motor amplitude (0-100, default 100)
    uint16_t frequencyHz;       // LRA frequency in Hz (default 250)
    uint16_t timeOnMs;          // Vibration on time in ms (default 100)
    uint16_t timeOffMs;         // Vibration off time in ms (default 67)
    uint8_t jitterPercent;      // Timing jitter (0-100, default 0)
    uint8_t numFingers;         // Number of fingers to use (default MAX_ACTUATORS)
    bool mirrorPattern;         // True for noisy vCR, false for regular vCR (default false)
    ActuatorType actuatorType;  // LRA or ERM (default LRA)
};
```

`TherapyConfig` is declared but not used by `src/`. Session parameters come from the active `TherapyProfile` (below), which `MenuController` passes to `TherapyEngine::startSession()`.

---

### Class: ProfileManager

Built-in therapy profiles, the user-editable Custom profile, and persistent device settings (role, profile, LED and debug flags). Storage goes through `fs_backend` (InternalFS on nRF, LittleFS on ESP32) as packed binary files: `/settings.bin` (`SettingsData`) and `/custom.bin` (`CustomOverrideData`).

```cpp
// include/profile_manager.h

struct TherapyProfile {
    char name[PROFILE_NAME_MAX];
    char description[PROFILE_DESC_MAX];
    ActuatorType actuatorType;
    uint16_t frequencyHz;
    float timeOnMs;
    float timeOffMs;
    float jitterPercent;
    uint8_t amplitudeMin;
    uint8_t amplitudeMax;
    uint16_t sessionDurationMin;
    char patternType[PATTERN_TYPE_MAX];  // "rndp", "sequential", "mirrored"
    bool mirrorPattern;
    uint8_t numFingers;
    bool isDefault;
    bool frequencyRandomization;
    uint16_t frequencyMin;
    uint16_t frequencyMax;
};

constexpr uint8_t CUSTOM_PROFILE_ID = 4;

class ProfileManager {
public:
    ProfileManager();
    bool begin(bool loadFromStorage = true);

    // Profile access (IDs are 1-based)
    const char** getProfileNames(uint8_t* count);
    bool loadProfile(uint8_t profileId);
    bool loadProfileByName(const char* name);
    const TherapyProfile* getCurrentProfile() const;
    const char* getCurrentProfileName() const;
    uint8_t getProfileCount() const;
    uint8_t getCurrentProfileId() const;

    // Parameter modification (validated against config.h PARAM_* bounds)
    bool setParameter(const char* paramName, const char* value);
    void resetToDefaults();

    // Persistence
    bool saveSettings();
    bool loadSettings();
    bool saveCustomOverride();
    bool loadCustomOverride();
    bool clearCustomOverride();
    bool isStorageAvailable() const;

    // Device role
    DeviceRole getDeviceRole() const;
    void setDeviceRole(DeviceRole role);
    bool hasStoredRole() const;

    // Therapy LED / debug mode flags
    bool getTherapyLedOff() const;
    void setTherapyLedOff(bool ledOff);
    bool getDebugMode() const;
    void setDebugMode(bool debug);
};
```

**Built-in profiles:**

| ID | Name          |
|----|---------------|
| 1  | `regular_vcr` |
| 2  | `noisy_vcr`   |
| 3  | `hybrid_vcr`  |
| 4  | `custom_vcr` (`CUSTOM_PROFILE_ID`, user-editable) |
| 5  | `gentle`      |
| 6  | `quick_test`  |

**Usage:**
```cpp
#include "profile_manager.h"

ProfileManager profiles;
profiles.begin();

// List available profiles
uint8_t count = 0;
const char** names = profiles.getProfileNames(&count);
for (uint8_t i = 0; i < count; i++) {
    Serial.printf("%d: %s\n", i + 1, names[i]);
}

// Load profile and persist the selection
if (profiles.loadProfile(2)) {  // noisy_vcr
    profiles.saveSettings();
    Serial.printf("Loaded profile: %s\n", profiles.getCurrentProfileName());
}

const TherapyProfile* p = profiles.getCurrentProfile();
```

---

## State Machine

### Header: `state_machine.h`

#### StateTrigger Enum

```cpp
// include/types.h

enum class StateTrigger : uint8_t {
    CONNECTED = 0,
    DISCONNECTED,
    START_SESSION,
    PAUSE_SESSION,
    RESUME_SESSION,
    STOP_SESSION,
    SESSION_COMPLETE,
    BATTERY_WARNING,
    BATTERY_CRITICAL,
    BATTERY_OK,
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

inline const char* stateTriggerToString(StateTrigger trigger);
```

#### TherapyStateMachine Class

Explicit state machine for therapy session management.

```cpp
// include/state_machine.h

#define MAX_STATE_CALLBACKS 4

struct StateTransition {
    TherapyState fromState;
    TherapyState toState;
    StateTrigger trigger;
    const char* reason;  // Optional reason for forced transitions
};

typedef void (*StateChangeCallback)(const StateTransition& transition);

class TherapyStateMachine {
public:
    TherapyStateMachine();

    void begin(TherapyState initialState = TherapyState::IDLE);

    // State access (atomic, safe from any context)
    TherapyState getCurrentState() const;
    TherapyState getPreviousState() const;

    bool transition(StateTrigger trigger);  // false if no valid transition
    void forceState(TherapyState state, const char* reason = nullptr);
    void reset();                           // back to IDLE

    // Callback registration (up to MAX_STATE_CALLBACKS)
    bool onStateChange(StateChangeCallback callback);
    void clearCallbacks();

    // State checks
    bool isActive() const;   // isActiveState()
    bool isError() const;    // isErrorState()
    bool isRunning() const;  // RUNNING or LOW_BATTERY
    bool isPaused() const;
    bool isReady() const;
    bool isIdle() const;
};
```

**Usage:**
```cpp
#include "state_machine.h"

TherapyStateMachine stateMachine;
stateMachine.begin(TherapyState::IDLE);

// Add observer for state changes
void onStateChange(const StateTransition& t) {
    Serial.printf("State: %s -> %s\n",
        therapyStateToString(t.fromState),
        therapyStateToString(t.toState));
}
stateMachine.onStateChange(onStateChange);

// Check current state
Serial.printf("Current state: %s\n", therapyStateToString(stateMachine.getCurrentState()));

// Attempt transition
if (stateMachine.transition(StateTrigger::START_SESSION)) {
    Serial.println(F("Session started successfully"));
}

// Reset state machine
stateMachine.reset();
```

---

## Hardware Abstraction

### Header: `hardware.h`

#### HapticController Class

Hardware control for the `MAX_ACTUATORS` DRV2605 drivers behind the TCA9548A I2C multiplexer.

```cpp
// include/hardware.h

#include <TCA9548A.h>
#include <Adafruit_DRV2605.h>

class HapticController {
public:
    HapticController();
    bool begin();                                   // true if at least one finger initialized
    bool initializeFinger(uint8_t finger);

    // Motor control (amplitude is a percentage, 0-100)
    Result activate(uint8_t finger, uint8_t amplitude);
    Result deactivate(uint8_t finger);
    void stopAll();
    void emergencyStop();
    bool isActive(uint8_t finger) const;
    bool anyMotorActive() const;
    bool isEnabled(uint8_t finger) const;           // driver initialized successfully
    uint8_t getEnabledCount() const;

    // DRV2605 configuration
    Result setFrequency(uint8_t finger, uint16_t frequencyHz);

    // I2C Pre-Selection (Latency Optimization)
    // Channel and frequency are set BEFORE the activation time, leaving only the RTP write
    bool selectChannelPersistent(uint8_t finger);                     // Opens mux channel, keeps it open
    Result setFrequencyDirect(uint8_t finger, uint16_t frequencyHz);  // Sets freq on pre-selected channel
    Result activatePreSelected(uint8_t finger, uint8_t amplitude);    // RTP write only
    void closeAllChannels();                                          // Close mux after pre-selected ops
    int8_t getPreSelectedFinger() const;                              // -1 if none

    // Recovery and diagnostics
    uint8_t verifyAndHeal();                         // Reconfigure DRV2605s that reset (brownout)
    uint8_t readVBatBurst(uint8_t* out, uint8_t maxSamples);  // DRV2605 VBAT register samples
    uint8_t probeMotorPresence();                    // Boot / serial MOTOR_PRESENT
    bool isMotorPresent(uint8_t finger) const;
    uint8_t getMotorPresentCount() const;
    bool lastProbeSupplyDipped() const;
    void diagSweep();                                // serial MOTOR_DIAG
    void diagRegs(const char* tag);
    void diagDriveOne(uint8_t finger);               // serial MOTOR_TEST:<n>

private:
    TCA9548A _tca;
    Adafruit_DRV2605 _drv[MAX_ACTUATORS];
    bool _fingerActive[MAX_ACTUATORS];
    bool _fingerEnabled[MAX_ACTUATORS];
    int8_t _preSelectedFinger;    // Currently pre-selected finger (-1 if none)
    SemaphoreHandle_t _i2cMutex;  // Protects I2C operations from concurrent access

    bool selectChannel(uint8_t finger);
    void closeChannels();
    void configureDRV2605(Adafruit_DRV2605& drv);
    uint8_t amplitudeToRTP(uint8_t amplitude) const;  // 0-100% -> 0-127
};
```

**Usage:**
```cpp
#include "hardware.h"

HapticController haptic;

// Initialize hardware
if (!haptic.begin()) {
    Serial.println(F("[ERROR] Haptic controller initialization failed"));
}

// Activate motor (standard path)
haptic.activate(FINGER_INDEX, 80);  // 80% amplitude
delay(100);
haptic.deactivate(FINGER_INDEX);

// Fast path using I2C pre-selection (for FreeRTOS motor task)
haptic.selectChannelPersistent(0);  // Pre-select channel
haptic.setFrequencyDirect(0, 250);  // Set frequency
// ... later, when event is due:
haptic.activatePreSelected(0, 100);  // RTP write only
haptic.closeAllChannels();

// Stop all motors
haptic.stopAll();
```

---

#### BatteryMonitor Class

Battery voltage and charge percentage (defined in `hardware.h`). Two backends, selected by `BATTERY_SENSE_ADC`: the VBAT divider ADC on the Feather nRF52840, and the DRV2605 VBAT register on PentaBuzzer.

```cpp
// include/hardware.h

class BatteryMonitor {
public:
    BatteryMonitor();
    bool begin();
    float readVoltage();
    uint8_t getPercentage(float voltage = -1.0f);  // reads if voltage not given
    BatteryStatus getStatus();
    bool isLow(float voltage = -1.0f);             // < BATTERY_LOW_VOLTAGE
    bool isCritical(float voltage = -1.0f);        // < BATTERY_CRITICAL_VOLTAGE

    // Required before begin() on boards without a battery ADC (PentaBuzzer)
    void attachHaptic(HapticController* haptic);
};
```

**Usage:**
```cpp
#include "hardware.h"

BatteryMonitor battery;
battery.attachHaptic(&haptic);  // PentaBuzzer: VBAT source; no-op on the Feather
battery.begin();

BatteryStatus status = battery.getStatus();
Serial.printf("Battery: %.2fV (%d%%) - %s\n",
    status.voltage, status.percentage, status.statusString());
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
    void update();                                             // animate; call every loop()

    void setPattern(const RGBColor& color, LEDPattern pattern);
    void setColor(uint8_t r, uint8_t g, uint8_t b);           // SOLID
    void setColor(const RGBColor& color);                      // SOLID
    void off();
    void setBrightness(uint8_t brightness);
    RGBColor getColor() const;
    LEDPattern getPattern() const;

private:
    Adafruit_NeoPixel _pixel;
    RGBColor _baseColor;
    RGBColor _displayColor;
    LEDPattern _pattern;
    bool _initialized;
};
```

The controller is intentionally state-agnostic: callers pass a color+pattern.
The device-state → color/pattern mapping lives in `src/main.cpp` (`onStateChange`
and the boot sequence). See [LED Controller](#led-controller) for the full mapping.

**Usage:**
```cpp
#include "hardware.h"  // LEDController is in hardware.h

LEDController led;
led.begin();

led.setPattern(Colors::GREEN, LEDPattern::SOLID);        // READY
led.setPattern(Colors::GREEN, LEDPattern::PULSE_SLOW);   // RUNNING
led.setPattern(Colors::RED, LEDPattern::BLINK_SLOW);     // ERROR / boot failure

// Animate in the main loop (required for breathe/pulse/blink):
led.update();

led.off();
```

---

## BLE Communication

### Header: `ble_manager.h`

#### BLEManager Class

BLE communication over the Nordic UART Service. One API, two backends: Bluefruit/SoftDevice on the Feather nRF52840 (`src/ble_manager_nrf52.cpp`) and NimBLE on PentaBuzzer (`src/ble_manager_esp32.cpp`). The PRIMARY is a peripheral that accepts the phone and the SECONDARY; the SECONDARY is a central that connects to the PRIMARY. Incoming connections are classified by an `IDENTIFY` handshake.

```cpp
// include/ble_manager.h

#if defined(BOARD_BLUEBUZZAH_NRF52)
#include <bluefruit.h>
#endif

#define CONN_HANDLE_INVALID 0xFFFF
#define MAX_CONNECTIONS 2

enum class ConnectionType : uint8_t {
    NONE = 0,
    UNKNOWN,    // Pending identification (waiting for IDENTIFY message)
    PHONE,      // Phone app connection (PRIMARY only)
    SECONDARY,  // SECONDARY device connection (PRIMARY only)
    PRIMARY     // PRIMARY device connection (SECONDARY only)
};

typedef void (*BLEConnectCallback)(uint16_t connHandle, ConnectionType type);
typedef void (*BLEDisconnectCallback)(uint16_t connHandle, ConnectionType type, uint8_t reason);
typedef void (*BLEMessageCallback)(uint16_t connHandle, const char* message, uint64_t rxTimestamp);
enum class TxStampKind : uint8_t {
    NONE = 0,
    PING_T1,   // PRIMARY -> SECONDARY: stamp T1 at write time
    PONG_T3    // SECONDARY -> PRIMARY: stamp T3 at write time
};
typedef void (*BLETxStampCallback)(TxStampKind kind, uint32_t seqId, uint64_t txTimeUs);

class BLEManager {
public:
    BLEManager();
    bool begin(DeviceRole role, const char* deviceName = BLE_NAME);  // PRIMARY starts advertising here
    void update();                                                   // call every loop()

    // Advertising (PRIMARY)
    bool startAdvertising();
    void stopAdvertising();
    bool isAdvertising() const;

    // Scanning (SECONDARY)
    bool startScanning(const char* targetName = BLE_NAME);
    void stopScanning();
    void setScannerAutoRestart(bool enabled);
    bool isScanning() const;

    // Connection management
    bool isSecondaryConnected() const;   // PRIMARY only
    bool isPhoneConnected() const;       // PRIMARY only
    bool isPrimaryConnected() const;     // SECONDARY only
    ConnectionType getConnectionType(uint16_t connHandle) const;
    uint8_t getConnectionCount() const;
    void disconnect(uint16_t connHandle);
    void disconnectAll();

    // Messaging (queued, non-blocking)
    bool send(uint16_t connHandle, const char* message);
    bool sendToSecondary(const char* message);  // PRIMARY only
    bool sendToPhone(const char* message);      // PRIMARY only
    bool sendToPrimary(const char* message);    // SECONDARY only
    uint8_t broadcast(const char* message);
    bool sendPingStamped(uint32_t seqId);       // PING with T1 stamped at radio handoff
    bool sendPongStamped(uint16_t connHandle, uint32_t seqId, uint64_t t2, uint64_t anchorUs = 0);
    bool hasPendingTx() const;                  // True while either TX lane still holds a queued message
    void processPeerTx();                       // Send queued peer-glove traffic now, without waiting for update()

    // Callback registration
    void setConnectCallback(BLEConnectCallback callback);
    void setDisconnectCallback(BLEDisconnectCallback callback);
    void setMessageCallback(BLEMessageCallback callback);
    void setTxStampCallback(BLETxStampCallback callback);

    // Getters
    DeviceRole getRole() const;
    uint16_t getSecondaryHandle() const;
    uint16_t getPhoneHandle() const;
    uint16_t getPrimaryHandle() const;
    float getConnectionIntervalMs(uint16_t connHandle) const;
    float getSecondaryConnectionIntervalMs() const;
};

extern BLEManager* g_bleManager;  // Bridges the stack's C callbacks to the instance
```

Outgoing messages go into two TX rings: a high-priority lane for PRIMARY↔SECONDARY sync traffic and a normal lane for phone responses. The high-priority lane is always drained first.

**Usage:**
```cpp
#include "ble_manager.h"

BLEManager ble;

// Callbacks
void onBLEConnect(uint16_t connHandle, ConnectionType type) {
    Serial.printf("[BLE] Connected: %d\n", connHandle);
}

void onBLEDisconnect(uint16_t connHandle, ConnectionType type, uint8_t reason) {
    Serial.printf("[BLE] Disconnected: %d, reason: 0x%02X\n", connHandle, reason);
}

void onBLEMessage(uint16_t connHandle, const char* message, uint64_t rxTimestamp) {
    Serial.printf("[BLE] RX from %d: %s\n", connHandle, message);
}

// Setup
ble.setConnectCallback(onBLEConnect);
ble.setDisconnectCallback(onBLEDisconnect);
ble.setMessageCallback(onBLEMessage);
ble.begin(deviceRole, BLE_NAME);  // PRIMARY: advertising starts inside begin()

// SECONDARY: Scan for the PRIMARY
if (deviceRole == DeviceRole::SECONDARY) {
    ble.startScanning(BLE_NAME);
}

// Send data (MenuController builds and EOT-terminates phone responses)
ble.sendToPhone("CMD:PING\nPONG:\n\x04");

// Check connection
if (ble.isSecondaryConnected()) {
    Serial.println(F("SECONDARY connected"));
}

// In loop()
ble.update();
```

---

### BLE Protocol Commands

Phone commands are handled by `MenuController::handleCommand()`. Parameters are colon-separated. The full request/response reference is [BLE_PROTOCOL.md](BLE_PROTOCOL.md).

```cpp
// Command types (string-based protocol)

// Device Information
// INFO - Get device information
// BATTERY - Get battery status
// PING - Connection test

// Profile Management
// PROFILE_LIST - List available profiles
// PROFILE_LOAD:<id> - Load built-in profile by ID (1-6)
// PROFILE_GET - Get current profile
// PROFILE_CUSTOM:<KEY>:<VALUE>[:<KEY>:<VALUE>...] - Edit the Custom profile

// Session Control
// SESSION_START - Start a session with the current profile
// SESSION_PAUSE
// SESSION_RESUME
// SESSION_STOP
// SESSION_STATUS

// Parameter Adjustment
// PARAM_SET:<param>:<value>

// Calibration
// CALIBRATE_START
// CALIBRATE_BUZZ:<finger>:<intensity>:<duration_ms>
// CALIBRATE_STOP

// System
// HELP
// RESTART
// THERAPY_LED_OFF[:<true|false|1|0>]
// DEBUG[:<true|false|1|0>]

// Serial only (handleSerialCommand() in src/main.cpp, never via BLE)
// SET_ROLE:<PRIMARY|SECONDARY>
```

---

## Therapy Engine

### Header: `therapy_engine.h`

#### TherapyEngine Class

Core therapy execution engine using a callback-driven architecture. TherapyEngine is decoupled from hardware: each macrocycle (3 patterns × the active fingers) is generated as a `Macrocycle`, sent to the SECONDARY through `SendMacrocycleCallback`, and scheduled locally through the FreeRTOS scheduling callbacks (`ActivationQueue` + motor task in `src/main.cpp`).

```cpp
// include/therapy_engine.h

enum class PatternType {
    RNDP = 0,
    SEQUENTIAL = 1,
    MIRRORED = 2,
};

typedef void (*ActivateCallback)(uint8_t finger, uint8_t amplitude);
typedef void (*DeactivateCallback)(uint8_t finger);
typedef void (*CycleCompleteCallback)(uint32_t cycleCount);
typedef void (*SetFrequencyCallback)(uint8_t finger, uint16_t frequencyHz);
typedef void (*MacrocycleStartCallback)(uint32_t macrocycleCount);
typedef void (*SendMacrocycleCallback)(const Macrocycle& macrocycle);
typedef void (*ScheduleActivationCallback)(uint64_t activateTimeUs, uint8_t finger,
                                           uint8_t amplitude, uint16_t durationMs, uint16_t frequencyHz);
typedef void (*StartSchedulingCallback)();
typedef bool (*IsSchedulingCompleteCallback)();
typedef uint32_t (*GetLeadTimeCallback)();  // microseconds

class TherapyEngine {
public:
    TherapyEngine();  // Default constructor, no parameters

    // =========================================================================
    // CALLBACKS - Set these before starting a session
    // =========================================================================

    // Motor control callbacks (registered by main.cpp; the macrocycle path does not call them)
    void setActivateCallback(ActivateCallback callback);
    void setDeactivateCallback(DeactivateCallback callback);

    // Event callbacks
    void setCycleCompleteCallback(CycleCompleteCallback callback);      // After each macrocycle
    void setSetFrequencyCallback(SetFrequencyCallback callback);        // For Custom vCR

    // Sync protocol callbacks (PRIMARY)
    void setMacrocycleStartCallback(MacrocycleStartCallback callback);  // Before macrocycle
    void setSendMacrocycleCallback(SendMacrocycleCallback callback);    // To send batch

    // FreeRTOS scheduling callbacks (PRIMARY motor task)
    void setSchedulingCallbacks(ScheduleActivationCallback scheduleCallback,
                                StartSchedulingCallback startCallback,
                                IsSchedulingCompleteCallback isCompleteCallback);

    // Adaptive lead time callback (defaults to 50 ms when unset)
    void setGetLeadTimeCallback(GetLeadTimeCallback callback);

    // Physical fingers patterns are generated over (shrunk when motor ports are empty)
    void setActiveFingers(const uint8_t* fingers, uint8_t count);
    uint8_t getActiveFingerCount() const;

    void setFrequencyRandomization(bool enabled, uint16_t minHz = 210, uint16_t maxHz = 255);

    // =========================================================================
    // SESSION CONTROL
    // =========================================================================

    // No default arguments: every parameter must be passed
    void startSession(
        uint32_t durationSec,
        PatternType patternType,
        float timeOnMs,
        float timeOffMs,
        float jitterPercent,
        uint8_t numFingers,
        bool mirrorPattern,
        uint8_t amplitudeMin,
        uint8_t amplitudeMax,
        bool isTestMode
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
    uint32_t getTotalActivations() const;
    uint32_t getElapsedSeconds() const;
    uint32_t getRemainingSeconds() const;
    uint32_t getDurationSeconds() const;
    uint16_t getFrequency(uint8_t finger) const;
    uint8_t getAmplitudeMin() const;
    uint8_t getAmplitudeMax() const;
    PatternType getPatternType() const;
};
```

**Important:** The `update()` method MUST be called frequently in the main loop for therapy to progress. Motors are driven only through the scheduling callbacks; without `setSchedulingCallbacks()` the engine generates macrocycles but schedules no local activations.

**Usage** (PRIMARY wiring, from `initializeTherapy()` in `src/main.cpp`):
```cpp
#include "therapy_engine.h"
#include "activation_queue.h"

TherapyEngine therapy;

void onScheduleActivation(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
                          uint16_t durationMs, uint16_t frequencyHz) {
    activationQueue.enqueue(activateTimeUs, finger, amplitude, durationMs, frequencyHz);
}
bool onIsSchedulingComplete() { return activationQueue.isEmpty(); }
void onStartScheduling() { /* motor task wakes on enqueue */ }

// Set up callbacks before starting
therapy.setActivateCallback(onActivate);
therapy.setDeactivateCallback(onDeactivate);
therapy.setCycleCompleteCallback(onCycleComplete);
therapy.setSendMacrocycleCallback(onSendMacrocycle);      // -> SECONDARY
therapy.setSchedulingCallbacks(onScheduleActivation, onStartScheduling, onIsSchedulingComplete);
therapy.setGetLeadTimeCallback(onGetLeadTime);            // syncProtocol.calculateAdaptiveLeadTime()

// Start session (noisy vCR: mirrored pattern)
therapy.startSession(
    7200,                      // 2 hours
    PatternType::RNDP,         // Random permutation
    100.0f,                    // 100ms burst duration
    67.0f,                     // 67ms inter-burst interval
    23.5f,                     // 23.5% jitter
    MAX_ACTUATORS,             // all fingers
    true,                      // Mirror pattern (noisy vCR)
    100,                       // amplitudeMin (%)
    100,                       // amplitudeMax (%)
    false                      // not a test session
);

// Main loop - update() is REQUIRED
void loop() {
    therapy.update();
}
```

---

### Pattern Generation

Pattern generation for bilateral therapy. Patterns are produced by free functions in `therapy_engine.h` and returned as a `Pattern`.

```cpp
// include/therapy_engine.h

struct Pattern {
    std::vector<uint8_t> primarySequence;
    std::vector<uint8_t> secondarySequence;
    std::vector<float> timeOffMs;   // TIME_OFF + jitter for each finger
    uint8_t numFingers;
    float burstDurationMs;          // TIME_ON
    float interBurstIntervalMs;     // TIME_RELAX after the pattern cycle

    float getTotalDurationMs() const;
    void getFingerPair(uint8_t index, uint8_t& primaryFinger, uint8_t& secondaryFinger) const;
};

Pattern generateRandomPermutation(uint8_t numFingers = MAX_ACTUATORS, float timeOnMs = 100.0f,
                                  float timeOffMs = 67.0f, float jitterPercent = 0.0f,
                                  bool mirrorPattern = false);
Pattern generateSequentialPattern(uint8_t numFingers = MAX_ACTUATORS, float timeOnMs = 100.0f,
                                  float timeOffMs = 67.0f, float jitterPercent = 0.0f,
                                  bool mirrorPattern = false, bool reverse = false);
Pattern generateMirroredPattern(uint8_t numFingers = MAX_ACTUATORS, float timeOnMs = 100.0f,
                                float timeOffMs = 67.0f, float jitterPercent = 0.0f,
                                bool randomize = true);
float calculateRelaxIntervalMs(uint8_t numFingers, float timeOnMs, float timeOffMs);
```

**Bilateral Mirroring:**

| vCR Type        | mirrorPattern | Behavior                              |
|-----------------|---------------|---------------------------------------|
| **Noisy vCR**   | `true`        | Same finger activated on both hands   |
| **Regular vCR** | `false`       | Independent random sequences per hand |

See [THERAPY_ENGINE.md](THERAPY_ENGINE.md) for pattern and timing details.

---

## Synchronization Protocol

### Header: `sync_protocol.h`

#### SyncCommand Class

Builds, serializes and parses PRIMARY↔SECONDARY messages. Messages are sent with `BLEManager::sendToSecondary()` / `sendToPrimary()`; PING and PONG go through `BLEManager::sendPingStamped()` / `sendPongStamped()` so T1/T3 are stamped at radio handoff.

```cpp
// include/sync_protocol.h

class SyncCommand {
public:
    SyncCommand();
    explicit SyncCommand(SyncCommandType type, uint32_t sequenceId = 0);  // timestamp = now

    bool serialize(char* buffer, size_t bufferSize) const;
    bool deserialize(const char* message);

    SyncCommandType getType() const;
    const char* getTypeString() const;
    uint32_t getSequenceId() const;
    uint64_t getTimestamp() const;

    // Positional data values (keys "0", "1", ... are not sent on the wire)
    bool setData(const char* key, const char* value);
    bool setData(const char* key, int32_t value);
    bool setDataUnsigned(const char* key, uint32_t value);
    const char* getData(const char* key) const;
    int32_t getDataInt(const char* key, int32_t defaultValue = 0) const;
    uint32_t getDataUnsigned(const char* key, uint32_t defaultValue = 0) const;
    bool hasData(const char* key) const;

    // Factory methods
    static SyncCommand createStartSession(uint32_t sequenceId = 0);
    static SyncCommand createPauseSession(uint32_t sequenceId = 0);
    static SyncCommand createResumeSession(uint32_t sequenceId = 0);
    static SyncCommand createStopSession(uint32_t sequenceId = 0);
    static SyncCommand createDeactivate(uint32_t sequenceId = 0);
    static SyncCommand createPing(uint32_t sequenceId);
    static SyncCommand createPingWithT1(uint32_t sequenceId, uint64_t t1);
    static SyncCommand createPong(uint32_t sequenceId);
    static SyncCommand createPongWithTimestamps(uint32_t sequenceId, uint64_t t2, uint64_t t3);
    static SyncCommand createPongWithAnchor(uint32_t sequenceId, uint64_t t2, uint64_t t3, uint64_t anchorUs);
    static SyncCommand createDebugFlash(uint32_t sequenceId);
    static SyncCommand createDebugFlashWithTime(uint32_t sequenceId, uint64_t flashTime);
    static SyncCommand createMacrocycleAck(uint32_t sequenceId);

    // Macrocycle batch (compact MC: format)
    static bool serializeMacrocycle(char* buffer, size_t bufferSize, const Macrocycle& macrocycle);
    static size_t getMacrocycleSerializedSize(const Macrocycle& macrocycle);
    static bool deserializeMacrocycle(const char* message, size_t messageLen, Macrocycle& macrocycle);
};

uint64_t getMicros();                       // 64-bit microsecond clock used for sync timestamps
extern SequenceGenerator g_sequenceGenerator;  // g_sequenceGenerator.next() -> next sequence ID
```

#### SimpleSyncProtocol Class

PTP-style clock offset estimation (PRIMARY) and lead-time calculation. `src/main.cpp` owns one instance, `syncProtocol`.

```cpp
// include/sync_protocol.h

class SimpleSyncProtocol {
public:
    SimpleSyncProtocol();

    // PTP offset from one PING/PONG exchange: ((T2 - T1) + (T3 - T4)) / 2
    int64_t calculatePTPOffset(uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4);
    [[nodiscard]] bool updateOffsetEMAWithQuality(int64_t offset, uint32_t rttUs);
    bool addOffsetSampleWithQuality(int64_t offset, uint32_t rttUs);
    void updateLatency(uint32_t rttUs);

    bool isClockSyncValid() const;          // SYNC_MIN_VALID_SAMPLES accepted, or warm start confirmed
    int64_t getMedianOffset() const;
    int64_t getCorrectedOffset() const;     // median offset + drift correction
    float getDriftRate() const;
    uint32_t getAverageRTT() const;
    uint32_t calculateAdaptiveLeadTime() const;  // RTT + 3σ + overheads, clamped
    uint64_t primaryToLocalTime(uint64_t primaryTime) const;
    uint64_t localToPrimaryTime(uint64_t localTime) const;

    // Warm start after a brief disconnect
    bool tryWarmStart();
    void invalidateWarmStartCache();
    bool isWarmStartMode() const;

    void resetClockSync();
    void reset();
};
```

**Message Format:**
```
<COMMAND>:<sequenceId>|<timestamp_us>[|<value0>|<value1>|...]
```

Data values are positional (no keys). Macrocycles use their own compact format:
```
MC:<seq>|<baseHigh>|<baseLow>|<offHigh>|<offLow>|<durationMs>|<count>|<delta>,<finger>,<amp>[,<freqOffset>]|...
```

**SYNC Commands:**

| Command        | Direction | Description               |
|----------------|-----------|---------------------------|
| START_SESSION  | P->S      | Begin therapy             |
| PAUSE_SESSION  | P->S      | Pause current session     |
| RESUME_SESSION | P->S      | Resume paused session     |
| STOP_SESSION   | P->S      | Stop session              |
| MC             | P->S      | Macrocycle: batch of 3 × active-finger motor events (12 on v2, 15 on v3 with all fingers) |
| MC_ACK         | S->P      | Macrocycle acknowledgment |
| PING           | P->S      | Keepalive + clock sync (timestamp = T1) |
| PONG           | S->P      | Keepalive + clock sync (values = T2, T3) |
| DEBUG_FLASH    | P->S      | Synchronized LED flash (debug mode) |
| DEACTIVATE     | P->S      | Defined in `SyncCommandType`; not sent or handled |
| BUZZ           | P->S      | Deprecated; SECONDARY logs a warning and ignores it |

**Examples:**
```
START_SESSION:1|1234567890
MC:42|0|5000000|0|1250|100|12|0,0,100|167,1,100|...
PING:7|1234567890
PONG:7|1234567990|1234567925|1234567950
MC_ACK:42|5001200
```

See [SYNCHRONIZATION_PROTOCOL.md](SYNCHRONIZATION_PROTOCOL.md) for timing, lead time and the full message reference.

**Usage:**
```cpp
#include "sync_protocol.h"

// PRIMARY: forward a session command to the SECONDARY (MenuController::handleSessionStart)
SyncCommand cmd = SyncCommand::createStartSession(0);
char buffer[64];
if (cmd.serialize(buffer, sizeof(buffer))) {
    ble.sendToSecondary(buffer);
}

// PRIMARY: PING for keepalive + clock sync (70ms until clock sync is valid,
// 250ms during therapy, 1s idle) - sendPing() in src/main.cpp
ble.sendPingStamped(g_sequenceGenerator.next());

// PRIMARY: on PONG, compute the offset from T1..T4
int64_t offset = syncProtocol.calculatePTPOffset(t1, t2, t3, t4);
bool accepted = syncProtocol.updateOffsetEMAWithQuality(offset, rtt);

// SECONDARY: parse an incoming message
SyncCommand rx;
if (rx.deserialize(message) && rx.getType() == SyncCommandType::STOP_SESSION) {
    stateMachine.transition(StateTrigger::STOP_SESSION);
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
    uint8_t  finger;        // Motor index (0 to MAX_ACTUATORS-1)
    uint8_t  amplitude;     // Intensity (0-100), only used for ACTIVATE
    uint16_t frequencyHz;   // Motor frequency, only used for ACTIVATE
    MotorEventType type;    // ACTIVATE or DEACTIVATE
    volatile bool active;   // Slot in use
};

class ActivationQueue {
public:
    static constexpr uint8_t MAX_EVENTS = 64;  // MACROCYCLE_MAX_EVENTS activations + deactivations (30 at 5 motors) + margin

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

Lock-free ring buffer for staging motor events from BLE callbacks (SoftDevice/ISR context on nRF, NimBLE host task on ESP32) to main loop. Uses SPSC (single-producer, single-consumer) model with memory barriers (`platformMemoryBarrier()`) for thread safety.

```cpp
// include/motor_event_buffer.h

struct StagedMotorEvent {
    uint64_t activateTimeUs;   // Absolute activation time
    uint8_t finger;            // Finger index (0 to MAX_ACTUATORS-1)
    uint8_t amplitude;         // Amplitude percentage (0-100)
    uint16_t durationMs;       // Duration in milliseconds
    uint16_t frequencyHz;      // Frequency in Hz
    bool isMacrocycleLast;     // True if last event in macrocycle batch
    bool isMacrocycleFirst;    // True if first event after beginMacrocycle()
    volatile bool valid;       // Marks slot as ready
};

class MotorEventBuffer {
public:
    static constexpr uint8_t MAX_STAGED = 32;

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
// In BLE callback:
motorEventBuffer.beginMacrocycle();
motorEventBuffer.stage(timeUs, finger, amp, dur, freq, isLast);

// In main loop:
StagedMotorEvent event;
while (motorEventBuffer.unstage(event)) {
    if (event.isMacrocycleFirst) {
        activationQueue.clear();  // New macrocycle replaces old events
    }
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
    HAPTIC_HEAL,         // no params - round-robin DRV2605 reset check/heal
    SCANNER_RESTART,     // 0, 0, delay_ms
    LED_FLASH            // r, g, b (packed in param1/2/3)
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
// In BLE callback - can't do I2C here:
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
    const char* getSyncConfidence() const;  // "HIGH", "MEDIUM", "LOW", or "UNKNOWN"

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

Command processing and routing for phone (BLE) and Updater (USB serial) commands. Coordinates between TherapyEngine, BatteryMonitor, HapticController, TherapyStateMachine, ProfileManager, and BLEManager. Replies go out through the send callback.

```cpp
// include/menu_controller.h

#define RESPONSE_BUFFER_SIZE 512

typedef void (*SendResponseCallback)(const char* response);
typedef void (*RestartCallback)();
typedef void (*SendToSecondaryCallback)(const char* message);
typedef bool (*SyncReadyCallback)();

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

    void setDeviceInfo(DeviceRole role, const char* firmwareVersion, const char* deviceName);
    void setSendCallback(SendResponseCallback callback);
    void setRestartCallback(RestartCallback callback);

    // Process an incoming command; the reply is sent through the send callback.
    // Returns false for internal (device-to-device) messages unless allowInternal is set,
    // and for invalid or unknown commands.
    bool handleCommand(const char* message, bool allowInternal = false);
    bool isInternalMessage(const char* message);  // prefix match on INTERNAL_MESSAGES[]

    void setSendToSecondaryCallback(SendToSecondaryCallback callback);
    // Set callback that gates SESSION_START on bilateral clock sync
    void setSyncReadyCallback(SyncReadyCallback callback);
    // Set callback that gates SESSION_START on both gloves' battery levels
    void setBatteryGateCallback(BatteryGateCallback callback);
    // Set callback returning the SESSION_START error when either glove is in ERROR, else nullptr
    void setGloveErrorCallback(GloveErrorCallback callback);
    // Set callback reporting each glove's level for INFO/BATTERY BATLP/BATLS
    void setBatteryLevelCallback(BatteryLevelCallback callback);

    // SECONDARY battery reply (deferred INFO/BATTERY responses)
    void setSecondaryBatteryVoltage(float voltage);
    void checkSecondaryBatteryResponse();
    void checkSecondaryBatteryTimeout();

    // Calibration
    bool isCalibrating() const;
    bool calibrationBuzz(uint8_t finger, uint8_t intensity, uint16_t durationMs);
    void cancelCalibrationBuzz();
    void updateCalibrationBuzz();  // call from loop()

private:
    // Response formatting
    void beginResponse();          // starts with CMD:<command>
    void addResponseLine(const char* key, const char* value);
    void sendResponse();           // appends EOT, calls send callback, logs [MENU-TX]
    void sendError(const char* message);

    // Command handlers (one per command, e.g.)
    void handleInfo();
    void handleBattery();
    void handleSessionStart();
    void handleProfileLoad(const char params[][PARAM_BUFFER_SIZE], uint8_t paramCount);
    void handleCalibrateBuzz(const char params[][PARAM_BUFFER_SIZE], uint8_t paramCount);
    void handleHelp();
    // ...
};
```

**Usage** (`setup()` in `src/main.cpp`):
```cpp
#include "menu_controller.h"

MenuController menu;

// In setup(), after initializing other components:
menu.begin(&therapy, &battery, &haptic, &stateMachine, &profiles, &ble);
menu.setDeviceInfo(deviceRole, FIRMWARE_VERSION, BLE_NAME);
menu.setSendCallback(onMenuSendResponse);
menu.setSendToSecondaryCallback(onMenuSendToSecondary);
menu.setRestartCallback(restartAfterTxDrain);
menu.setSyncReadyCallback(phoneStartSyncReady);

// Process command string (reply arrives via onMenuSendResponse)
menu.handleCommand("SESSION_START");
```

---

## LED Controller

`LEDController` (`include/hardware.h`, implemented in `src/hardware.cpp`) drives the
single onboard status NeoPixel. It exposes a generic color+pattern API; the mapping
from device state to color/pattern lives inline in `src/main.cpp` (`onStateChange()`
and the boot sequence), not in the controller.

```cpp
class LEDController {
    bool begin();                                             // init NeoPixel
    void update();                                            // MUST call each loop() to animate
    void setPattern(const RGBColor& color, LEDPattern pattern);
    void setColor(uint8_t r, uint8_t g, uint8_t b);          // SOLID
    void setColor(const RGBColor& color);                     // SOLID
    void off();
    void setBrightness(uint8_t brightness);                   // capped at LED_BRIGHTNESS
    RGBColor getColor() const;
    LEDPattern getPattern() const;
};
```

### Patterns (`LEDPattern`, `include/hardware.h`)

| Pattern         | Timing (`include/config.h`)     | Meaning                          |
|-----------------|---------------------------------|----------------------------------|
| `SOLID`         | constant on                     | READY, PAUSED                    |
| `BREATHE_SLOW`  | 2000 ms fade cycle              | IDLE                             |
| `PULSE_SLOW`    | 1500 ms fade cycle              | RUNNING, LOW_BATTERY             |
| `BLINK_FAST`    | 200 ms on / 200 ms off          | STOPPING                         |
| `BLINK_SLOW`    | 1000 ms on / 1000 ms off        | ERROR, no role configured        |
| `BLINK_URGENT`  | 150 ms on / 150 ms off          | CRITICAL_BATTERY, critical level |
| `BLINK_CONNECT` | 250 ms on / 250 ms off          | CONNECTING, CONNECTION_LOST      |
| `DOUBLE_BLINK`  | two quick blinks, then pause    | missing/failed motor(s) at boot  |
| `OFF`           | off                             | RUNNING with "LED off" enabled   |

### Boot Sequence Patterns (`src/main.cpp`)

| Boot phase                              | Color  | Pattern         |
|-----------------------------------------|--------|-----------------|
| No role configured                      | Orange | `BLINK_SLOW`    |
| LED initialized                         | Blue   | `BLINK_CONNECT` |
| Hardware init success                   | Cyan   | `BLINK_CONNECT` |
| Hardware init failed                    | Red    | `BLINK_SLOW`    |
| Fewer than 4 motors detected (5 s, PentaBuzzer only, then `ERROR`) | Red | `DOUBLE_BLINK` |
| BLE ready (enters IDLE)                 | Blue   | `BREATHE_SLOW`  |
| BLE init failed                         | Red    | `BLINK_SLOW`    |
| Glove pair connected (enters READY)     | Green  | `SOLID`         |
| Boot hardware fault (enters `ERROR` after BLE init, until reboot) | Red | `BLINK_SLOW` |

### Therapy / Device State Patterns (`src/main.cpp` `applyLed`)

| State                | Color  | Pattern                                        |
|----------------------|--------|------------------------------------------------|
| `IDLE`               | Blue   | `BREATHE_SLOW`                                 |
| `CONNECTING`         | Blue   | `BLINK_CONNECT`                                |
| `READY`              | Green  | `SOLID`                                        |
| `RUNNING`            | Green  | `PULSE_SLOW` (or `OFF` if "LED off during therapy") |
| `PAUSED`             | Yellow | `SOLID`                                        |
| `STOPPING`           | Yellow | `BLINK_FAST`                                   |
| `LOW_BATTERY`        | Orange | `PULSE_SLOW`                                   |
| `CRITICAL_BATTERY`   | Red    | `BLINK_URGENT`                                 |
| `ERROR`              | Red    | `BLINK_SLOW`                                   |
| `CONNECTION_LOST`    | Purple | `BLINK_CONNECT` (30 s window, then reverts to IDLE) |
| `PHONE_DISCONNECTED` | —      | the prior state's LED                          |

`CONNECTING` is mapped but never entered by current firmware.

Each glove's LED also shows its own battery level. A low level turns `IDLE` orange `BREATHE_SLOW`, `READY` orange `SOLID`, and `RUNNING` orange `PULSE_SLOW` (even with "LED off during therapy"); the other states keep their colors. A critical level is red `BLINK_URGENT` in every state. Orange `BLINK_SLOW` stays exclusive to "no role configured".

A brief **white `SOLID`** flash marks each macrocycle start on both gloves while debug
mode (`DEBUG:1`) is on, and overrides "LED off during therapy" for its duration.

---

## Usage Examples

### Complete System Initialization

Trimmed from `src/main.cpp` (logging, board-specific blocks and the motor task setup omitted).

```cpp
// main.cpp

#include <Arduino.h>
#include "power_controller.h"
#include "config.h"
#include "types.h"
#include "hardware.h"
#include "ble_manager.h"
#include "therapy_engine.h"
#include "state_machine.h"
#include "menu_controller.h"
#include "profile_manager.h"
#include "sync_protocol.h"
#include "deferred_queue.h"

// Global instances
HapticController haptic;
BatteryMonitor battery;
LEDController led;
PowerController power;
BLEManager ble;
TherapyEngine therapy;
TherapyStateMachine stateMachine;
MenuController menu;
ProfileManager profiles;
SimpleSyncProtocol syncProtocol;

void setup() {
    Serial.begin(115200);

    power.begin();  // PentaBuzzer power path; no-op on nRF

    // 1. LED first (configuration feedback)
    if (led.begin()) {
        led.setPattern(Colors::BLUE, LEDPattern::BLINK_CONNECT);
    }

    // 2. Profiles and stored role
    profiles.begin();
    if (!profiles.hasStoredRole()) {
        waitForRoleConfiguration();  // orange blink, waits for SET_ROLE, reboots
    }
    deviceRole = determineRole();    // stored role (or USER button override)

    // 3. Hardware (haptic.begin(), battery.attachHaptic() + battery.begin(), motor task)
    hardwareReady = initializeHardware();
    led.setPattern(hardwareReady ? Colors::CYAN : Colors::RED,
                   hardwareReady ? LEDPattern::BLINK_CONNECT : LEDPattern::BLINK_SLOW);

    // 4. BLE: callbacks, ble.begin(deviceRole, BLE_NAME), SECONDARY startScanning()
    bleReady = initializeBLE();
    if (bleReady) {
        led.setPattern(Colors::BLUE, LEDPattern::BREATHE_SLOW);  // IDLE
    } else {
        led.setPattern(Colors::RED, LEDPattern::BLINK_SLOW);
    }

    // 5. Therapy engine callbacks (see Therapy Engine)
    initializeTherapy();

    // 6. State machine
    stateMachine.begin(TherapyState::IDLE);
    stateMachine.onStateChange(onStateChange);  // LED mapping - see LED Controller
    if (g_hardwareFault) {
        stateMachine.transition(StateTrigger::ERROR_OCCURRED);  // red slow blink until reboot
    }

    // 7. Menu controller
    menu.begin(&therapy, &battery, &haptic, &stateMachine, &profiles, &ble);
    menu.setDeviceInfo(deviceRole, FIRMWARE_VERSION, BLE_NAME);
    menu.setSendCallback(onMenuSendResponse);
    menu.setSendToSecondaryCallback(onMenuSendToSecondary);
    menu.setRestartCallback(restartAfterTxDrain);
    menu.setSyncReadyCallback(phoneStartSyncReady);

    // 8. Deferred work executor (BLE callback -> loop handoff)
    deferredQueue.setExecutor(executeDeferredWork);
}
```

Inside `loop()`, the PRIMARY sends the unified keepalive + clock sync PING and the SECONDARY watches for its timeout:

```cpp
// PRIMARY: 70ms until clock sync is valid, 250ms during therapy, 1s idle
uint32_t pingIntervalMs = therapy.isRunning() ? SYNC_ACTIVE_INTERVAL_MS : KEEPALIVE_INTERVAL_MS;
if (!syncProtocol.isClockSyncValid() || syncProtocol.isWarmStartMode()) {
    pingIntervalMs = SYNC_ACQUIRE_INTERVAL_MS;
}
if (deviceRole == DeviceRole::PRIMARY && isConnected &&
    (now - lastKeepalive >= pingIntervalMs)) {
    lastKeepalive = now;
    sendPing();  // ble.sendPingStamped(g_sequenceGenerator.next())
}

// SECONDARY: 6s without a PING = connection lost
if (deviceRole == DeviceRole::SECONDARY && ble.isPrimaryConnected()) {
    uint32_t lastKA = lastKeepaliveReceived;
    if (lastKA > 0 && (millis() - lastKA > KEEPALIVE_TIMEOUT_MS)) {
        handleKeepaliveTimeout();
    }
}

led.update();  // animate LED (call every loop)
```

---

### Simple Therapy Execution

Standalone session on one device, scheduling motor events through the `ActivationQueue` (which needs the FreeRTOS motor task from `src/main.cpp` to execute them).

```cpp
#include "hardware.h"
#include "therapy_engine.h"
#include "activation_queue.h"
#include "types.h"

HapticController haptic;
TherapyEngine engine;

void scheduleActivation(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
                        uint16_t durationMs, uint16_t frequencyHz) {
    activationQueue.enqueue(activateTimeUs, finger, amplitude, durationMs, frequencyHz);
}
void startScheduling() {}
bool isSchedulingComplete() { return activationQueue.isEmpty(); }

void setup() {
    Serial.begin(115200);

    // Initialize hardware
    if (!haptic.begin()) {
        Serial.println(F("[ERROR] Hardware init failed"));
        while (true) { delay(1000); }
    }
    // ... create the motor task and call activationQueue.begin(&haptic, motorTaskHandle)

    engine.setSchedulingCallbacks(scheduleActivation, startScheduling, isSchedulingComplete);

    // Start session (noisy vCR parameters)
    engine.startSession(
        60,                        // 60 seconds
        PatternType::RNDP,         // Random permutation
        100.0f,                    // 100ms burst duration
        67.0f,                     // 67ms inter-burst interval
        23.5f,                     // 23.5% jitter
        MAX_ACTUATORS,             // all fingers
        true,                      // Mirror pattern (noisy vCR)
        100,                       // amplitudeMin (%)
        100,                       // amplitudeMax (%)
        false                      // not a test session
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

TherapyStateMachine stateMachine;
LEDController led;

void onStateChange(const StateTransition& t) {
    Serial.printf("State: %s -> %s\n",
        therapyStateToString(t.fromState),
        therapyStateToString(t.toState));
    // LED reflects the new state via onStateChange() — see LED Controller
}

void setup() {
    Serial.begin(115200);
    led.begin();

    // Register callback
    stateMachine.begin(TherapyState::IDLE);
    stateMachine.onStateChange(onStateChange);

    // Perform transitions
    stateMachine.transition(StateTrigger::CONNECTED);       // IDLE -> READY
    stateMachine.transition(StateTrigger::START_SESSION);   // -> RUNNING
    delay(1000);
    stateMachine.transition(StateTrigger::PAUSE_SESSION);   // -> PAUSED
    delay(1000);
    stateMachine.transition(StateTrigger::RESUME_SESSION);  // -> RUNNING
    delay(1000);
    stateMachine.transition(StateTrigger::STOP_SESSION);    // -> STOPPING
    stateMachine.transition(StateTrigger::STOPPED);         // -> IDLE
}

void loop() {
    led.update();  // animate LED (call every loop)
    delay(50);
}
```

---

## Common Include Patterns

```cpp
// Core types
#include "types.h"        // DeviceRole, TherapyState, StateTrigger, Macrocycle, etc.
#include "config.h"       // Constants, pin definitions (includes board_config.h)

// Hardware
#include "hardware.h"     // HapticController, BatteryMonitor, LEDController

// Communication
#include "ble_manager.h"  // BLE radio and connection management
#include "sync_protocol.h" // PRIMARY-SECONDARY messaging

// Application
#include "state_machine.h"
#include "therapy_engine.h"  // TherapyEngine, PatternType, pattern generators
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
// include/types.h
enum class Result : uint8_t {
    OK = 0,
    ERROR_TIMEOUT,
    ERROR_INVALID_PARAM,
    ERROR_NOT_CONNECTED,
    ERROR_HARDWARE,
    ERROR_NOT_INITIALIZED,
    ERROR_BUSY,
    ERROR_DISABLED
};

// Example usage
Result result = haptic.activate(finger, amplitude);
if (result != Result::OK) {
    Serial.printf("[ERROR] activate failed: %d\n", (int)result);
}

// Menu error response (src/menu_controller.cpp)
void MenuController::sendError(const char* message) {
    beginResponse();                    // CMD:<command>
    addResponseLine("ERROR", message);  // ERROR:<message>
    sendResponse();                     // appends EOT (0x04), sends via callback
}
```

On the wire an error reads `CMD:<command>\nERROR:<message>\n\x04`.

Menu replies, errors included, begin with a `CMD:<command>` line naming the command they answer (`MenuController::beginResponse()`). Only `ERROR:Invalid command format` has no `CMD:` line, since the command was never parsed.

---

## Testing

The firmware uses **PlatformIO test framework** for validation. Test suites target the native envs and run on the host, without hardware.

**Test Commands:**
```bash
pio test -e native                        # Native tests, 4-actuator build
pio test -e native_penta                  # Native tests, 5-actuator build
pio test -e native -f test_sync_protocol  # One suite
pio test -e native_coverage               # Coverage (macOS); native_coverage_gcc on Linux
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

**Firmware Version**: `FIRMWARE_VERSION` (`include/config.h` default; release builds use the release tag)
**Platform**: Arduino C++ with PlatformIO

---

## Additional Resources

- [Architecture Guide](ARCHITECTURE.md) - System design and patterns
- [Boot Sequence](BOOT_SEQUENCE.md) - Boot process and LED indicators
- [Therapy Engine](THERAPY_ENGINE.md) - Pattern generation and timing
- [Testing Guide](TESTING.md) - Test framework and procedures
- [Synchronization Protocol](SYNCHRONIZATION_PROTOCOL.md) - Device sync details
- [BLE Protocol](BLE_PROTOCOL.md) - Command reference for mobile apps
