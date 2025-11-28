/**
 * @file menu_controller.cpp
 * @brief BlueBuzzah menu/command controller - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "menu_controller.h"
#include "therapy_engine.h"
#include "hardware.h"
#include "state_machine.h"
#include "profile_manager.h"

// =============================================================================
// INTERNAL MESSAGE PREFIXES
// =============================================================================

const char* INTERNAL_MESSAGES[] = {
    "EXECUTE_BUZZ",
    "BUZZ_COMPLETE",
    "PARAM_UPDATE",
    "SEED",
    "SEED_ACK",
    "GET_BATTERY",
    "BATRESPONSE",
    "ACK_PARAM_UPDATE",
    "HEARTBEAT",
    "SYNC:",
    "IDENTIFY:"
};

const uint8_t INTERNAL_MESSAGE_COUNT = sizeof(INTERNAL_MESSAGES) / sizeof(INTERNAL_MESSAGES[0]);

// =============================================================================
// CONSTRUCTOR
// =============================================================================

MenuController::MenuController() :
    _therapy(nullptr),
    _battery(nullptr),
    _haptic(nullptr),
    _stateMachine(nullptr),
    _profiles(nullptr),
    _role(DeviceRole::PRIMARY),
    _sendCallback(nullptr),
    _restartCallback(nullptr),
    _isCalibrating(false),
    _calibrationStartTime(0)
{
    strcpy(_firmwareVersion, FIRMWARE_VERSION);
    strcpy(_deviceName, BLE_NAME);
    memset(_responseBuffer, 0, sizeof(_responseBuffer));
}

// =============================================================================
// INITIALIZATION
// =============================================================================

void MenuController::begin(
    TherapyEngine* therapyEngine,
    BatteryMonitor* batteryMonitor,
    HapticController* hapticController,
    TherapyStateMachine* stateMachine,
    ProfileManager* profileManager
) {
    _therapy = therapyEngine;
    _battery = batteryMonitor;
    _haptic = hapticController;
    _stateMachine = stateMachine;
    _profiles = profileManager;

    Serial.println(F("[MENU] Controller initialized"));
}

void MenuController::setDeviceInfo(DeviceRole role, const char* firmwareVersion, const char* deviceName) {
    _role = role;

    if (firmwareVersion) {
        strncpy(_firmwareVersion, firmwareVersion, sizeof(_firmwareVersion) - 1);
        _firmwareVersion[sizeof(_firmwareVersion) - 1] = '\0';
    }

    if (deviceName) {
        strncpy(_deviceName, deviceName, sizeof(_deviceName) - 1);
        _deviceName[sizeof(_deviceName) - 1] = '\0';
    }
}

void MenuController::setSendCallback(SendResponseCallback callback) {
    _sendCallback = callback;
}

void MenuController::setRestartCallback(RestartCallback callback) {
    _restartCallback = callback;
}

// =============================================================================
// COMMAND PROCESSING
// =============================================================================

bool MenuController::isInternalMessage(const char* message) {
    if (!message || strlen(message) == 0) {
        return false;
    }

    for (uint8_t i = 0; i < INTERNAL_MESSAGE_COUNT; i++) {
        if (strncmp(message, INTERNAL_MESSAGES[i], strlen(INTERNAL_MESSAGES[i])) == 0) {
            return true;
        }
    }
    return false;
}

bool MenuController::handleCommand(const char* message) {
    if (!message || strlen(message) == 0) {
        return false;
    }

    // Skip internal messages
    if (isInternalMessage(message)) {
        return false;
    }

    // Parse command
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    if (!parseCommand(message, command, params, paramCount)) {
        sendError("Invalid command format");
        return false;
    }

    Serial.printf("[MENU] Command: %s, Params: %d\n", command, paramCount);

    // Dispatch to handler
    if (strcmp(command, "INFO") == 0) {
        handleInfo();
    } else if (strcmp(command, "BATTERY") == 0) {
        handleBattery();
    } else if (strcmp(command, "PING") == 0) {
        handlePing();
    } else if (strcmp(command, "PROFILE_LIST") == 0) {
        handleProfileList();
    } else if (strcmp(command, "PROFILE_LOAD") == 0) {
        handleProfileLoad(params, paramCount);
    } else if (strcmp(command, "PROFILE_GET") == 0) {
        handleProfileGet();
    } else if (strcmp(command, "PROFILE_CUSTOM") == 0) {
        handleProfileCustom(params, paramCount);
    } else if (strcmp(command, "SESSION_START") == 0) {
        handleSessionStart();
    } else if (strcmp(command, "SESSION_PAUSE") == 0) {
        handleSessionPause();
    } else if (strcmp(command, "SESSION_RESUME") == 0) {
        handleSessionResume();
    } else if (strcmp(command, "SESSION_STOP") == 0) {
        handleSessionStop();
    } else if (strcmp(command, "SESSION_STATUS") == 0) {
        handleSessionStatus();
    } else if (strcmp(command, "PARAM_SET") == 0) {
        handleParamSet(params, paramCount);
    } else if (strcmp(command, "CALIBRATE_START") == 0) {
        handleCalibrateStart();
    } else if (strcmp(command, "CALIBRATE_BUZZ") == 0) {
        handleCalibrateBuzz(params, paramCount);
    } else if (strcmp(command, "CALIBRATE_STOP") == 0) {
        handleCalibrateStop();
    } else if (strcmp(command, "HELP") == 0) {
        handleHelp();
    } else if (strcmp(command, "RESTART") == 0) {
        handleRestart();
    } else {
        char errorMsg[64];
        snprintf(errorMsg, sizeof(errorMsg), "Unknown command: %s", command);
        sendError(errorMsg);
        return false;
    }

    return true;
}

// =============================================================================
// COMMAND PARSING
// =============================================================================

bool MenuController::parseCommand(const char* message, char* command, char params[][PARAM_BUFFER_SIZE], uint8_t& paramCount) {
    if (!message || !command || !params) {
        return false;
    }

    // Create a working copy
    char buffer[256];
    strncpy(buffer, message, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // Strip newlines and EOT
    char* p = buffer;
    while (*p) {
        if (*p == '\n' || *p == '\r' || *p == EOT_CHAR) {
            *p = '\0';
            break;
        }
        p++;
    }

    // Trim leading whitespace
    p = buffer;
    while (*p == ' ') p++;

    if (strlen(p) == 0) {
        return false;
    }

    // Split on colon
    paramCount = 0;
    char* token = strtok(p, ":");

    if (!token) {
        return false;
    }

    // First token is the command (uppercase it)
    strncpy(command, token, 31);
    command[31] = '\0';

    // Convert command to uppercase
    for (char* c = command; *c; c++) {
        *c = toupper(*c);
    }

    // Remaining tokens are parameters
    while ((token = strtok(nullptr, ":")) != nullptr && paramCount < MAX_COMMAND_PARAMS) {
        strncpy(params[paramCount], token, PARAM_BUFFER_SIZE - 1);
        params[paramCount][PARAM_BUFFER_SIZE - 1] = '\0';
        paramCount++;
    }

    return true;
}

// =============================================================================
// RESPONSE FORMATTING
// =============================================================================

void MenuController::beginResponse() {
    _responseBuffer[0] = '\0';
}

void MenuController::addResponseLine(const char* key, const char* value) {
    char line[128];
    snprintf(line, sizeof(line), "%s:%s\n", key, value ? value : "");

    size_t currentLen = strlen(_responseBuffer);
    size_t lineLen = strlen(line);

    if (currentLen + lineLen < RESPONSE_BUFFER_SIZE - 2) {
        strcat(_responseBuffer, line);
    }
}

void MenuController::addResponseLine(const char* key, int32_t value) {
    char valueStr[16];
    snprintf(valueStr, sizeof(valueStr), "%ld", (long)value);
    addResponseLine(key, valueStr);
}

void MenuController::addResponseLine(const char* key, float value, uint8_t decimals) {
    char valueStr[16];
    char format[8];
    snprintf(format, sizeof(format), "%%.%df", decimals);
    snprintf(valueStr, sizeof(valueStr), format, value);
    addResponseLine(key, valueStr);
}

void MenuController::sendResponse() {
    // Add EOT terminator
    size_t len = strlen(_responseBuffer);
    if (len < RESPONSE_BUFFER_SIZE - 1) {
        _responseBuffer[len] = EOT_CHAR;
        _responseBuffer[len + 1] = '\0';
    }

    // Send via callback
    if (_sendCallback) {
        _sendCallback(_responseBuffer);
    }

    // Also print to serial
    Serial.printf("[MENU-TX] %s\n", _responseBuffer);
}

void MenuController::sendError(const char* message) {
    beginResponse();
    addResponseLine("ERROR", message);
    sendResponse();
}

// =============================================================================
// DEVICE INFO COMMANDS
// =============================================================================

void MenuController::handleInfo() {
    beginResponse();

    addResponseLine("ROLE", deviceRoleToString(_role));
    addResponseLine("NAME", _deviceName);
    addResponseLine("FW", _firmwareVersion);

    // Get battery status
    if (_battery) {
        BatteryStatus status = _battery->getStatus();
        addResponseLine("BATP", status.voltage, 2);
    } else {
        addResponseLine("BATP", "0.00");
    }
    addResponseLine("BATS", "0.00");  // SECONDARY battery (placeholder)

    // Get therapy status
    const char* statusStr = "IDLE";
    if (_stateMachine) {
        if (_stateMachine->isRunning()) {
            statusStr = "RUNNING";
        } else if (_stateMachine->isPaused()) {
            statusStr = "PAUSED";
        } else if (_stateMachine->isReady()) {
            statusStr = "READY";
        }
    }
    addResponseLine("STATUS", statusStr);

    sendResponse();
}

void MenuController::handleBattery() {
    beginResponse();

    if (_battery) {
        BatteryStatus status = _battery->getStatus();
        addResponseLine("BATP", status.voltage, 2);
    } else {
        addResponseLine("BATP", "0.00");
    }
    addResponseLine("BATS", "0.00");  // SECONDARY battery (placeholder)

    sendResponse();
}

void MenuController::handlePing() {
    beginResponse();
    addResponseLine("PONG", "");
    sendResponse();
}

// =============================================================================
// PROFILE COMMANDS
// =============================================================================

void MenuController::handleProfileList() {
    if (!_profiles) {
        sendError("Profile manager not available");
        return;
    }

    beginResponse();

    // Get profile list
    uint8_t count = 0;
    const char** names = _profiles->getProfileNames(&count);

    for (uint8_t i = 0; i < count; i++) {
        char line[64];
        snprintf(line, sizeof(line), "%d:%s", i + 1, names[i]);
        addResponseLine("PROFILE", line);
    }

    sendResponse();
}

void MenuController::handleProfileLoad(char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
    // Check if session is active
    if (_therapy && _therapy->isRunning()) {
        sendError("Cannot modify parameters during active session");
        return;
    }

    if (paramCount < 1) {
        sendError("Profile ID required");
        return;
    }

    if (!_profiles) {
        sendError("Profile manager not available");
        return;
    }

    int profileId = atoi(params[0]);
    if (!_profiles->loadProfile(profileId)) {
        sendError("Invalid profile ID");
        return;
    }

    beginResponse();
    addResponseLine("STATUS", "LOADED");
    addResponseLine("PROFILE", _profiles->getCurrentProfileName());
    sendResponse();
}

void MenuController::handleProfileGet() {
    if (!_profiles) {
        sendError("Profile manager not available");
        return;
    }

    const TherapyProfile* profile = _profiles->getCurrentProfile();
    if (!profile) {
        sendError("No profile loaded");
        return;
    }

    beginResponse();
    addResponseLine("TYPE", profile->actuatorType == ActuatorType::LRA ? "LRA" : "ERM");
    addResponseLine("FREQ", (int32_t)profile->frequencyHz);
    addResponseLine("ON", profile->timeOnMs, 1);
    addResponseLine("OFF", profile->timeOffMs, 1);
    addResponseLine("SESSION", (int32_t)profile->sessionDurationMin);
    addResponseLine("AMPMIN", (int32_t)profile->amplitudeMin);
    addResponseLine("AMPMAX", (int32_t)profile->amplitudeMax);
    addResponseLine("PATTERN", profile->patternType);
    addResponseLine("MIRROR", (int32_t)(profile->mirrorPattern ? 1 : 0));
    addResponseLine("JITTER", profile->jitterPercent, 1);
    sendResponse();
}

void MenuController::handleProfileCustom(char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
    // Check if session is active
    if (_therapy && _therapy->isRunning()) {
        sendError("Cannot modify parameters during active session");
        return;
    }

    if (paramCount < 2 || paramCount % 2 != 0) {
        sendError("Invalid parameter format (KEY:VALUE pairs required)");
        return;
    }

    if (!_profiles) {
        sendError("Profile manager not available");
        return;
    }

    // Apply each key-value pair
    for (uint8_t i = 0; i < paramCount; i += 2) {
        if (!_profiles->setParameter(params[i], params[i + 1])) {
            char errorMsg[64];
            snprintf(errorMsg, sizeof(errorMsg), "Invalid parameter: %s", params[i]);
            sendError(errorMsg);
            return;
        }
    }

    beginResponse();
    addResponseLine("STATUS", "CUSTOM_LOADED");
    sendResponse();
}

// =============================================================================
// SESSION COMMANDS
// =============================================================================

void MenuController::handleSessionStart() {
    if (!_therapy) {
        sendError("Therapy engine not available");
        return;
    }

    if (_therapy->isRunning()) {
        sendError("Session already active");
        return;
    }

    // Check battery
    if (_battery) {
        BatteryStatus status = _battery->getStatus();
        if (status.isCritical) {
            sendError("Battery too low");
            return;
        }
    }

    // Get profile parameters
    uint32_t durationSec = 7200;  // Default 2 hours
    uint8_t patternType = PATTERN_TYPE_RNDP;
    float timeOnMs = 100.0f;
    float timeOffMs = 67.0f;
    float jitterPercent = 23.5f;
    uint8_t numFingers = 5;
    bool mirror = true;

    if (_profiles) {
        const TherapyProfile* profile = _profiles->getCurrentProfile();
        if (profile) {
            durationSec = profile->sessionDurationMin * 60;
            timeOnMs = profile->timeOnMs;
            timeOffMs = profile->timeOffMs;
            jitterPercent = profile->jitterPercent;
            mirror = profile->mirrorPattern;

            if (strcmp(profile->patternType, "rndp") == 0) {
                patternType = PATTERN_TYPE_RNDP;
            } else if (strcmp(profile->patternType, "sequential") == 0) {
                patternType = PATTERN_TYPE_SEQUENTIAL;
            }
        }
    }

    // Start session
    _therapy->startSession(durationSec, patternType, timeOnMs, timeOffMs, jitterPercent, numFingers, mirror);

    // Update state machine
    if (_stateMachine) {
        _stateMachine->transition(StateTrigger::START_SESSION);
    }

    beginResponse();
    addResponseLine("SESSION_STATUS", "RUNNING");
    sendResponse();
}

void MenuController::handleSessionPause() {
    if (!_therapy || !_therapy->isRunning()) {
        sendError("No active session");
        return;
    }

    _therapy->pause();

    if (_stateMachine) {
        _stateMachine->transition(StateTrigger::PAUSE_SESSION);
    }

    beginResponse();
    addResponseLine("SESSION_STATUS", "PAUSED");
    sendResponse();
}

void MenuController::handleSessionResume() {
    if (!_therapy) {
        sendError("No active session");
        return;
    }

    if (!_therapy->isPaused()) {
        sendError("Session is not paused");
        return;
    }

    _therapy->resume();

    if (_stateMachine) {
        _stateMachine->transition(StateTrigger::RESUME_SESSION);
    }

    beginResponse();
    addResponseLine("SESSION_STATUS", "RUNNING");
    sendResponse();
}

void MenuController::handleSessionStop() {
    if (_therapy) {
        _therapy->stop();
    }

    if (_haptic) {
        _haptic->emergencyStop();
    }

    if (_stateMachine) {
        _stateMachine->transition(StateTrigger::STOP_SESSION);
    }

    beginResponse();
    addResponseLine("SESSION_STATUS", "IDLE");
    sendResponse();
}

void MenuController::handleSessionStatus() {
    beginResponse();

    const char* statusStr = "IDLE";
    uint32_t elapsed = 0;
    uint32_t total = 0;
    uint8_t progress = 0;

    if (_stateMachine) {
        statusStr = therapyStateToString(_stateMachine->getCurrentState());
    }

    if (_therapy && _therapy->isRunning()) {
        elapsed = _therapy->getElapsedSeconds();
        total = _therapy->getDurationSeconds();
        if (total > 0) {
            progress = (elapsed * 100) / total;
        }
    }

    addResponseLine("SESSION_STATUS", statusStr);
    addResponseLine("ELAPSED", (int32_t)elapsed);
    addResponseLine("TOTAL", (int32_t)total);
    addResponseLine("PROGRESS", (int32_t)progress);
    sendResponse();
}

// =============================================================================
// PARAMETER COMMANDS
// =============================================================================

void MenuController::handleParamSet(char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
    if (_therapy && _therapy->isRunning()) {
        sendError("Cannot modify parameters during active session");
        return;
    }

    if (paramCount < 2) {
        sendError("Parameter name and value required");
        return;
    }

    if (!_profiles) {
        sendError("Profile manager not available");
        return;
    }

    // Convert param name to uppercase
    for (char* c = params[0]; *c; c++) {
        *c = toupper(*c);
    }

    if (!_profiles->setParameter(params[0], params[1])) {
        sendError("Invalid parameter name or value out of range");
        return;
    }

    beginResponse();
    addResponseLine("PARAM", params[0]);
    addResponseLine("VALUE", params[1]);
    sendResponse();
}

// =============================================================================
// CALIBRATION COMMANDS
// =============================================================================

void MenuController::handleCalibrateStart() {
    if (_therapy && _therapy->isRunning()) {
        sendError("Cannot calibrate during active session");
        return;
    }

    _isCalibrating = true;
    _calibrationStartTime = millis();

    beginResponse();
    addResponseLine("MODE", "CALIBRATION");
    sendResponse();
}

void MenuController::handleCalibrateBuzz(char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
    if (!_isCalibrating) {
        sendError("Not in calibration mode");
        return;
    }

    if (paramCount < 3) {
        sendError("Finger, intensity, and duration required");
        return;
    }

    int finger = atoi(params[0]);
    int intensity = atoi(params[1]);
    int duration = atoi(params[2]);

    // Validate ranges
    if (finger < 0 || finger > 7) {
        sendError("Invalid finger index (0-7)");
        return;
    }
    if (intensity < 0 || intensity > 100) {
        sendError("Intensity out of range (0-100)");
        return;
    }
    if (duration < 50 || duration > 2000) {
        sendError("Duration out of range (50-2000ms)");
        return;
    }

    // Execute buzz on local fingers (0-4)
    if (finger < MAX_ACTUATORS && _haptic) {
        if (_haptic->isEnabled(finger)) {
            _haptic->activate(finger, intensity);
            delay(duration);
            _haptic->deactivate(finger);
        }
    }
    // Fingers 5-7 would be sent to SECONDARY device

    beginResponse();
    addResponseLine("FINGER", (int32_t)finger);
    addResponseLine("INTENSITY", (int32_t)intensity);
    addResponseLine("DURATION", (int32_t)duration);
    sendResponse();
}

void MenuController::handleCalibrateStop() {
    _isCalibrating = false;

    if (_haptic) {
        _haptic->emergencyStop();
    }

    beginResponse();
    addResponseLine("MODE", "NORMAL");
    sendResponse();
}

// =============================================================================
// SYSTEM COMMANDS
// =============================================================================

void MenuController::handleHelp() {
    beginResponse();
    addResponseLine("COMMAND", "INFO");
    addResponseLine("COMMAND", "BATTERY");
    addResponseLine("COMMAND", "PING");
    addResponseLine("COMMAND", "PROFILE_LIST");
    addResponseLine("COMMAND", "PROFILE_LOAD");
    addResponseLine("COMMAND", "PROFILE_GET");
    addResponseLine("COMMAND", "PROFILE_CUSTOM");
    addResponseLine("COMMAND", "SESSION_START");
    addResponseLine("COMMAND", "SESSION_PAUSE");
    addResponseLine("COMMAND", "SESSION_RESUME");
    addResponseLine("COMMAND", "SESSION_STOP");
    addResponseLine("COMMAND", "SESSION_STATUS");
    addResponseLine("COMMAND", "PARAM_SET");
    addResponseLine("COMMAND", "CALIBRATE_START");
    addResponseLine("COMMAND", "CALIBRATE_BUZZ");
    addResponseLine("COMMAND", "CALIBRATE_STOP");
    addResponseLine("COMMAND", "HELP");
    addResponseLine("COMMAND", "RESTART");
    sendResponse();
}

void MenuController::handleRestart() {
    beginResponse();
    addResponseLine("STATUS", "REBOOTING");
    sendResponse();

    // Give time for response to be sent
    delay(100);

    // Call restart callback if set
    if (_restartCallback) {
        _restartCallback();
    } else {
        // Use NVIC system reset
        NVIC_SystemReset();
    }
}
