/**
 * @file menu_controller.h
 * @brief BlueBuzzah menu/command controller - BLE command processing
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * Implements BLE command parsing and handling for all 18 protocol commands:
 * - Device info: INFO, BATTERY, PING
 * - Profiles: PROFILE_LIST, PROFILE_LOAD, PROFILE_GET, PROFILE_CUSTOM
 * - Session: SESSION_START, SESSION_PAUSE, SESSION_RESUME, SESSION_STOP, SESSION_STATUS
 * - Parameters: PARAM_SET
 * - Calibration: CALIBRATE_START, CALIBRATE_BUZZ, CALIBRATE_STOP
 * - System: HELP, RESTART
 */

#ifndef MENU_CONTROLLER_H
#define MENU_CONTROLLER_H

#include <Arduino.h>
#include "types.h"
#include "config.h"

// Forward declarations
class TherapyEngine;
class BatteryMonitor;
class HapticController;
class TherapyStateMachine;
class ProfileManager;

// =============================================================================
// CONSTANTS
// =============================================================================

// Message terminator (EOT character)
#define EOT_CHAR '\x04'

// Response buffer size
#define RESPONSE_BUFFER_SIZE 512

// Parameter buffer size
#define PARAM_BUFFER_SIZE 64

// Maximum parameters per command
#define MAX_COMMAND_PARAMS 16

// =============================================================================
// INTERNAL MESSAGE PREFIXES
// =============================================================================

// Messages that should be passed through without menu processing
extern const char* INTERNAL_MESSAGES[];
extern const uint8_t INTERNAL_MESSAGE_COUNT;

// =============================================================================
// CALLBACK TYPES
// =============================================================================

/**
 * @brief Callback for sending response over BLE
 */
typedef void (*SendResponseCallback)(const char* response);

/**
 * @brief Callback for device restart
 */
typedef void (*RestartCallback)();

// =============================================================================
// MENU CONTROLLER CLASS
// =============================================================================

/**
 * @brief Menu command controller for BlueBuzzah
 *
 * Handles:
 * - Command parsing from BLE strings
 * - All 18 protocol command handlers
 * - Response formatting (KEY:VALUE with EOT)
 * - Error handling
 * - Internal message pass-through
 * - State validation (no profile changes during sessions)
 *
 * Usage:
 *   MenuController menu;
 *   menu.begin(&therapy, &battery, &haptic, &stateMachine, &profiles);
 *   menu.setDeviceInfo(DeviceRole::PRIMARY, "2.0.0", "BlueBuzzah");
 *   menu.setSendCallback(onSendResponse);
 *
 *   // Handle incoming command
 *   menu.handleCommand("BATTERY\n");
 *   // Callback receives: "BATP:3.72\nBATS:0.00\n\x04"
 */
class MenuController {
public:
    MenuController();

    /**
     * @brief Initialize menu controller with component references
     * @param therapyEngine TherapyEngine instance
     * @param batteryMonitor BatteryMonitor instance
     * @param hapticController HapticController instance
     * @param stateMachine TherapyStateMachine instance
     * @param profileManager ProfileManager instance (optional)
     */
    void begin(
        TherapyEngine* therapyEngine,
        BatteryMonitor* batteryMonitor,
        HapticController* hapticController,
        TherapyStateMachine* stateMachine,
        ProfileManager* profileManager = nullptr
    );

    /**
     * @brief Set device information
     * @param role Device role (PRIMARY or SECONDARY)
     * @param firmwareVersion Firmware version string
     * @param deviceName Device name
     */
    void setDeviceInfo(DeviceRole role, const char* firmwareVersion, const char* deviceName);

    /**
     * @brief Set callback for sending responses
     */
    void setSendCallback(SendResponseCallback callback);

    /**
     * @brief Set callback for device restart
     */
    void setRestartCallback(RestartCallback callback);

    // =========================================================================
    // COMMAND PROCESSING
    // =========================================================================

    /**
     * @brief Handle incoming command and send response via callback
     * @param message Raw command message
     * @return true if command was processed
     */
    bool handleCommand(const char* message);

    /**
     * @brief Check if message is an internal sync message
     * @param message Message to check
     * @return true if internal message (should be handled separately)
     */
    bool isInternalMessage(const char* message);

    // =========================================================================
    // CALIBRATION STATE
    // =========================================================================

    /**
     * @brief Check if currently in calibration mode
     */
    bool isCalibrating() const { return _isCalibrating; }

private:
    // Component references
    TherapyEngine* _therapy;
    BatteryMonitor* _battery;
    HapticController* _haptic;
    TherapyStateMachine* _stateMachine;
    ProfileManager* _profiles;

    // Device info
    DeviceRole _role;
    char _firmwareVersion[16];
    char _deviceName[32];

    // Callbacks
    SendResponseCallback _sendCallback;
    RestartCallback _restartCallback;

    // State
    bool _isCalibrating;
    uint32_t _calibrationStartTime;

    // Response buffer
    char _responseBuffer[RESPONSE_BUFFER_SIZE];

    // =========================================================================
    // COMMAND PARSING
    // =========================================================================

    /**
     * @brief Parse command and extract parameters
     * @param message Input message
     * @param command Output command name (uppercase)
     * @param params Output parameter array
     * @param paramCount Output parameter count
     * @return true if parsing successful
     */
    bool parseCommand(const char* message, char* command, char params[][PARAM_BUFFER_SIZE], uint8_t& paramCount);

    // =========================================================================
    // RESPONSE FORMATTING
    // =========================================================================

    /**
     * @brief Start building a response
     */
    void beginResponse();

    /**
     * @brief Add key:value line to response
     */
    void addResponseLine(const char* key, const char* value);
    void addResponseLine(const char* key, int32_t value);
    void addResponseLine(const char* key, float value, uint8_t decimals = 2);

    /**
     * @brief Finalize and send response
     */
    void sendResponse();

    /**
     * @brief Send error response
     */
    void sendError(const char* message);

    // =========================================================================
    // COMMAND HANDLERS
    // =========================================================================

    void handleInfo();
    void handleBattery();
    void handlePing();

    void handleProfileList();
    void handleProfileLoad(char params[][PARAM_BUFFER_SIZE], uint8_t paramCount);
    void handleProfileGet();
    void handleProfileCustom(char params[][PARAM_BUFFER_SIZE], uint8_t paramCount);

    void handleSessionStart();
    void handleSessionPause();
    void handleSessionResume();
    void handleSessionStop();
    void handleSessionStatus();

    void handleParamSet(char params[][PARAM_BUFFER_SIZE], uint8_t paramCount);

    void handleCalibrateStart();
    void handleCalibrateBuzz(char params[][PARAM_BUFFER_SIZE], uint8_t paramCount);
    void handleCalibrateStop();

    void handleHelp();
    void handleRestart();
};

#endif // MENU_CONTROLLER_H
