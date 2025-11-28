/**
 * @file types.h
 * @brief BlueBuzzah firmware type definitions - Enums, structs, and constants
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

// =============================================================================
// RESULT CODES
// =============================================================================

/**
 * @brief Standard result codes for function returns
 */
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

// =============================================================================
// DEVICE ROLE
// =============================================================================

/**
 * @brief Device role in the bilateral therapy system
 */
enum class DeviceRole : uint8_t {
    PRIMARY = 0,    // Left glove - orchestrates therapy, connects to phone
    SECONDARY = 1   // Right glove - follows PRIMARY commands
};

/**
 * @brief Get string representation of device role
 */
inline const char* deviceRoleToString(DeviceRole role) {
    switch (role) {
        case DeviceRole::PRIMARY: return "PRIMARY";
        case DeviceRole::SECONDARY: return "SECONDARY";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Get device tag for logging
 */
inline const char* deviceRoleToTag(DeviceRole role) {
    switch (role) {
        case DeviceRole::PRIMARY: return "[PRIMARY]";
        case DeviceRole::SECONDARY: return "[SECONDARY]";
        default: return "[UNKNOWN]";
    }
}

// =============================================================================
// THERAPY STATE
// =============================================================================

/**
 * @brief Therapy session state machine states
 */
enum class TherapyState : uint8_t {
    IDLE = 0,           // No active session, system ready
    CONNECTING,         // Establishing BLE connection during boot
    READY,              // Connected, ready for therapy
    RUNNING,            // Active therapy session
    PAUSED,             // Session paused, can resume
    STOPPING,           // Session ending, cleanup in progress
    ERROR,              // Error condition, motors stopped
    LOW_BATTERY,        // Battery < 20%, session can continue
    CRITICAL_BATTERY,   // Battery < 5%, forced shutdown
    CONNECTION_LOST,    // PRIMARY-SECONDARY BLE lost
    PHONE_DISCONNECTED  // Phone BLE lost (PRIMARY only, informational)
};

/**
 * @brief Get string representation of therapy state
 */
inline const char* therapyStateToString(TherapyState state) {
    switch (state) {
        case TherapyState::IDLE: return "IDLE";
        case TherapyState::CONNECTING: return "CONNECTING";
        case TherapyState::READY: return "READY";
        case TherapyState::RUNNING: return "RUNNING";
        case TherapyState::PAUSED: return "PAUSED";
        case TherapyState::STOPPING: return "STOPPING";
        case TherapyState::ERROR: return "ERROR";
        case TherapyState::LOW_BATTERY: return "LOW_BATTERY";
        case TherapyState::CRITICAL_BATTERY: return "CRITICAL_BATTERY";
        case TherapyState::CONNECTION_LOST: return "CONNECTION_LOST";
        case TherapyState::PHONE_DISCONNECTED: return "PHONE_DISCONNECTED";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Check if state represents an active therapy session
 */
inline bool isActiveState(TherapyState state) {
    return state == TherapyState::RUNNING ||
           state == TherapyState::PAUSED ||
           state == TherapyState::LOW_BATTERY;
}

/**
 * @brief Check if state represents an error condition
 */
inline bool isErrorState(TherapyState state) {
    return state == TherapyState::ERROR ||
           state == TherapyState::CRITICAL_BATTERY ||
           state == TherapyState::CONNECTION_LOST;
}

// =============================================================================
// STATE TRIGGERS
// =============================================================================

/**
 * @brief Events that trigger state machine transitions
 */
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

/**
 * @brief Get string representation of state trigger
 */
inline const char* stateTriggerToString(StateTrigger trigger) {
    switch (trigger) {
        case StateTrigger::CONNECTED: return "CONNECTED";
        case StateTrigger::DISCONNECTED: return "DISCONNECTED";
        case StateTrigger::START_SESSION: return "START_SESSION";
        case StateTrigger::PAUSE_SESSION: return "PAUSE_SESSION";
        case StateTrigger::RESUME_SESSION: return "RESUME_SESSION";
        case StateTrigger::STOP_SESSION: return "STOP_SESSION";
        case StateTrigger::SESSION_COMPLETE: return "SESSION_COMPLETE";
        case StateTrigger::BATTERY_WARNING: return "BATTERY_WARNING";
        case StateTrigger::BATTERY_CRITICAL: return "BATTERY_CRITICAL";
        case StateTrigger::BATTERY_OK: return "BATTERY_OK";
        case StateTrigger::RECONNECTED: return "RECONNECTED";
        case StateTrigger::RECONNECT_FAILED: return "RECONNECT_FAILED";
        case StateTrigger::PHONE_LOST: return "PHONE_LOST";
        case StateTrigger::PHONE_RECONNECTED: return "PHONE_RECONNECTED";
        case StateTrigger::PHONE_TIMEOUT: return "PHONE_TIMEOUT";
        case StateTrigger::ERROR_OCCURRED: return "ERROR_OCCURRED";
        case StateTrigger::EMERGENCY_STOP: return "EMERGENCY_STOP";
        case StateTrigger::RESET: return "RESET";
        case StateTrigger::STOPPED: return "STOPPED";
        case StateTrigger::FORCED_SHUTDOWN: return "FORCED_SHUTDOWN";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// BOOT RESULT
// =============================================================================

/**
 * @brief Result of boot sequence execution
 */
enum class BootResult : uint8_t {
    FAILED = 0,         // Boot sequence failed
    SUCCESS_NO_PHONE,   // PRIMARY-SECONDARY connected, no phone
    SUCCESS_WITH_PHONE, // All connections established
    SUCCESS             // Generic success (SECONDARY only)
};

/**
 * @brief Check if boot was successful
 */
inline bool isBootSuccess(BootResult result) {
    return result != BootResult::FAILED;
}

// =============================================================================
// ACTUATOR TYPE
// =============================================================================

/**
 * @brief Type of haptic actuator
 */
enum class ActuatorType : uint8_t {
    LRA = 0,  // Linear Resonant Actuator (preferred for vCR)
    ERM = 1   // Eccentric Rotating Mass motor
};

// =============================================================================
// SYNC COMMAND TYPE
// =============================================================================

/**
 * @brief Types of synchronization commands between PRIMARY and SECONDARY
 */
enum class SyncCommandType : uint8_t {
    START_SESSION = 0,
    PAUSE_SESSION,
    RESUME_SESSION,
    STOP_SESSION,
    EXECUTE_BUZZ,
    DEACTIVATE,
    HEARTBEAT,
    SYNC_ADJ,
    SYNC_ADJ_START,
    BUZZ_COMPLETE,
    FIRST_SYNC,
    ACK_SYNC_ADJ
};

/**
 * @brief Get string representation of sync command type
 */
inline const char* syncCommandTypeToString(SyncCommandType type) {
    switch (type) {
        case SyncCommandType::START_SESSION: return "START_SESSION";
        case SyncCommandType::PAUSE_SESSION: return "PAUSE_SESSION";
        case SyncCommandType::RESUME_SESSION: return "RESUME_SESSION";
        case SyncCommandType::STOP_SESSION: return "STOP_SESSION";
        case SyncCommandType::EXECUTE_BUZZ: return "EXECUTE_BUZZ";
        case SyncCommandType::DEACTIVATE: return "DEACTIVATE";
        case SyncCommandType::HEARTBEAT: return "HEARTBEAT";
        case SyncCommandType::SYNC_ADJ: return "SYNC_ADJ";
        case SyncCommandType::SYNC_ADJ_START: return "SYNC_ADJ_START";
        case SyncCommandType::BUZZ_COMPLETE: return "BUZZ_COMPLETE";
        case SyncCommandType::FIRST_SYNC: return "FIRST_SYNC";
        case SyncCommandType::ACK_SYNC_ADJ: return "ACK_SYNC_ADJ";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// STRUCTS
// =============================================================================

/**
 * @brief RGB color structure for LED control
 */
struct RGBColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    RGBColor() : r(0), g(0), b(0) {}
    RGBColor(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}

    bool operator==(const RGBColor& other) const {
        return r == other.r && g == other.g && b == other.b;
    }

    bool operator!=(const RGBColor& other) const {
        return !(*this == other);
    }
};

// Predefined colors
namespace Colors {
    const RGBColor OFF(0, 0, 0);
    const RGBColor RED(255, 0, 0);
    const RGBColor GREEN(0, 255, 0);
    const RGBColor BLUE(0, 0, 255);
    const RGBColor WHITE(255, 255, 255);
    const RGBColor YELLOW(255, 255, 0);
    const RGBColor ORANGE(255, 128, 0);
    const RGBColor PURPLE(128, 0, 255);
    const RGBColor CYAN(0, 255, 255);
}

/**
 * @brief Battery status information
 */
struct BatteryStatus {
    float voltage;          // Current voltage in volts
    uint8_t percentage;     // Estimated percentage (0-100)
    bool isLow;             // True if below LOW_VOLTAGE threshold
    bool isCritical;        // True if below CRITICAL_VOLTAGE threshold

    BatteryStatus() : voltage(0.0f), percentage(0), isLow(false), isCritical(false) {}

    /**
     * @brief Get status as string
     */
    const char* statusString() const {
        if (isCritical) return "CRITICAL";
        if (isLow) return "LOW";
        return "OK";
    }

    /**
     * @brief Check if battery status is OK
     */
    bool isOk() const {
        return !isLow && !isCritical;
    }

    /**
     * @brief Check if action is required (low or critical)
     */
    bool requiresAction() const {
        return isLow || isCritical;
    }
};

/**
 * @brief Device configuration loaded from settings.json
 */
struct DeviceConfig {
    DeviceRole role;
    char bleName[32];
    char deviceTag[16];

    DeviceConfig() : role(DeviceRole::PRIMARY) {
        strcpy(bleName, "BlueBuzzah");
        strcpy(deviceTag, "[PRIMARY]");
    }

    /**
     * @brief Check if device is PRIMARY
     */
    bool isPrimary() const {
        return role == DeviceRole::PRIMARY;
    }

    /**
     * @brief Check if device is SECONDARY
     */
    bool isSecondary() const {
        return role == DeviceRole::SECONDARY;
    }
};

/**
 * @brief Therapy session configuration
 */
struct TherapyConfig {
    uint32_t durationSec;       // Session duration in seconds
    uint8_t amplitude;          // Motor amplitude (0-100)
    uint16_t frequencyHz;       // LRA frequency in Hz
    uint16_t timeOnMs;          // Vibration on time in ms
    uint16_t timeOffMs;         // Vibration off time in ms
    uint8_t jitterPercent;      // Timing jitter (0-100)
    uint8_t numFingers;         // Number of fingers to use (1-5)
    bool mirrorPattern;         // True for noisy vCR, false for regular vCR
    ActuatorType actuatorType;  // LRA or ERM

    TherapyConfig() :
        durationSec(7200),      // 2 hours default
        amplitude(80),
        frequencyHz(175),
        timeOnMs(100),
        timeOffMs(100),
        jitterPercent(0),
        numFingers(5),
        mirrorPattern(true),    // Noisy vCR by default
        actuatorType(ActuatorType::LRA) {}
};

/**
 * @brief Connection state tracking
 */
struct ConnectionState {
    uint16_t phoneConnHandle;       // Phone connection (PRIMARY only)
    uint16_t secondaryConnHandle;   // SECONDARY connection (PRIMARY only)
    uint16_t primaryConnHandle;     // PRIMARY connection (SECONDARY only)

    ConnectionState() :
        phoneConnHandle(0xFFFF),
        secondaryConnHandle(0xFFFF),
        primaryConnHandle(0xFFFF) {}

    bool isPhoneConnected() const {
        return phoneConnHandle != 0xFFFF;
    }

    bool isSecondaryConnected() const {
        return secondaryConnHandle != 0xFFFF;
    }

    bool isPrimaryConnected() const {
        return primaryConnHandle != 0xFFFF;
    }

    void clearPhone() { phoneConnHandle = 0xFFFF; }
    void clearSecondary() { secondaryConnHandle = 0xFFFF; }
    void clearPrimary() { primaryConnHandle = 0xFFFF; }
    void clearAll() {
        phoneConnHandle = 0xFFFF;
        secondaryConnHandle = 0xFFFF;
        primaryConnHandle = 0xFFFF;
    }
};

#endif // TYPES_H
