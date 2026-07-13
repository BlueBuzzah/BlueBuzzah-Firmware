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
#include "ble_manager.h"
#include "sync_protocol.h"
#include "platform.h"

// =============================================================================
// INTERNAL MESSAGE PREFIXES
// =============================================================================

const char* INTERNAL_MESSAGES[] = {
    "BUZZ",
    "PING",
    "PONG",
    "PARAM_UPDATE",
    "SEED",
    "SEED_ACK",
    "GET_BATTERY",
    "BATRESPONSE",
    "ACK_PARAM_UPDATE",
    "SYNC_",           // Covers SYNC_ADJ, SYNC_PROBE, SYNC_PROBE_ACK
    "FIRST_SYNC",
    "ACK_SYNC",        // Covers ACK_SYNC_ADJ
    "START_SESSION",
    "PAUSE_SESSION",
    "RESUME_SESSION",
    "STOP_SESSION",
    "IDENTIFY:",
    "LED_OFF_SYNC",
    "DEBUG_FLASH",
    "DEBUG_SYNC",
    "MC:",             // Macrocycle batch message
    "MC_ACK:",         // Macrocycle acknowledgment
    "CALIB_BUZZ"
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
    _ble(nullptr),
    _role(DeviceRole::PRIMARY),
    _sendCallback(nullptr),
    _restartCallback(nullptr),
    _sendToSecondaryCallback(nullptr),
    _deferredCommand(DeferredCommand::NONE),
    _secondaryBatteryVoltage(0.0f),
    _waitingForSecondaryBattery(false),
    _secondaryBatteryReceived(false),
    _secondaryBatteryRequestTime(0),
    _isCalibrating(false),
    _calibrationStartTime(0),
    _calibBuzzFinger(-1),
    _calibBuzzOffTime(0),
    _calibBuzzPendingFinger(-1),
    _calibBuzzPendingIntensity(0),
    _calibBuzzPendingDuration(0),
    _calibBuzzCancelPending(false),
    _calibBuzzRequestPending(false)
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
    ProfileManager* profileManager,
    BLEManager* bleManager
) {
    _therapy = therapyEngine;
    _battery = batteryMonitor;
    _haptic = hapticController;
    _stateMachine = stateMachine;
    _profiles = profileManager;
    _ble = bleManager;

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

void MenuController::setSendToSecondaryCallback(SendToSecondaryCallback callback) {
    _sendToSecondaryCallback = callback;
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

bool MenuController::handleCommand(const char* message, bool allowInternal) {
    if (!message || strlen(message) == 0) {
        return false;
    }

    // Skip internal messages unless caller vouches for the source
    // (messages from an identified PHONE connection are always commands)
    if (!allowInternal && isInternalMessage(message)) {
        return false;
    }

    // Late IDENTIFY handshake (connection already classified) — consume
    // silently, it is never a command and must not generate an error reply
    if (strncmp(message, "IDENTIFY:", 9) == 0) {
        return true;
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

    // A new command is about to be dispatched: cancel any stale deferred
    // INFO/BATTERY response still waiting on the SECONDARY glove's battery
    // reply. Without this, a late reply (or its timeout) would complete
    // against a response buffer this command is about to overwrite, sending
    // a corrupted or foreign response to the phone.
    if (_waitingForSecondaryBattery) {
        _waitingForSecondaryBattery = false;
        _deferredCommand = DeferredCommand::NONE;
    }

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
    } else if (strcmp(command, "THERAPY_LED_OFF") == 0) {
        handleTherapyLedOff(params, paramCount);
    } else if (strcmp(command, "DEBUG") == 0) {
        handleDebug(params, paramCount);
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
        *c = static_cast<char>(toupper(*c));
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
// SECONDARY BATTERY RESPONSE HANDLING
// =============================================================================

void MenuController::setSecondaryBatteryVoltage(float voltage) {
    _secondaryBatteryVoltage = voltage;   // volatile write
    _secondaryBatteryReceived = true;     // volatile write — signals main loop
}

void MenuController::checkSecondaryBatteryResponse() {
    if (!_waitingForSecondaryBattery || !_secondaryBatteryReceived) {
        return;
    }
    _secondaryBatteryReceived = false;
    // Now safe to call handleSecondaryBatteryResponse from main loop context
    handleSecondaryBatteryResponse(_secondaryBatteryVoltage);
}

void MenuController::handleSecondaryBatteryResponse(float voltage) {
    if (!_waitingForSecondaryBattery) {
        return;  // No pending request
    }

    _waitingForSecondaryBattery = false;
    _secondaryBatteryVoltage = voltage;

    // Complete the deferred response
    addResponseLine("BATS", voltage, 2);

    if (_deferredCommand == DeferredCommand::INFO) {
        // INFO response needs STATUS after BATS
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
    }

    _deferredCommand = DeferredCommand::NONE;
    sendResponse();
}

void MenuController::checkSecondaryBatteryTimeout() {
    if (!_waitingForSecondaryBattery) {
        return;
    }

    if (millis() - _secondaryBatteryRequestTime >= SECONDARY_BATTERY_TIMEOUT_MS) {
        Serial.println(F("[MENU] SECONDARY battery response timeout"));
        handleSecondaryBatteryResponse(0.0f);
    }
}

// =============================================================================
// DEVICE INFO COMMANDS
// =============================================================================

void MenuController::handleInfo() {
    beginResponse();

    addResponseLine("ROLE", deviceRoleToString(_role));
    addResponseLine("NAME", _deviceName);
    addResponseLine("FW", _firmwareVersion);
    addResponseLine("MOTORS", static_cast<int32_t>(MAX_ACTUATORS));
    if (_profiles) {
        char profLine[48];
        snprintf(profLine, sizeof(profLine), "%d:%s",
                 _profiles->getCurrentProfileId(), _profiles->getCurrentProfileName());
        addResponseLine("PROFILE", profLine);
    }

    // Get battery status
    if (_battery) {
        BatteryStatus status = _battery->getStatus();
        addResponseLine("BATP", status.voltage, 2);
    } else {
        addResponseLine("BATP", "0.00");
    }

    // Guard: already waiting for SECONDARY — return 0.00 immediately
    if (_waitingForSecondaryBattery) {
        addResponseLine("BATS", "0.00");
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
        return;
    }

    // Request SECONDARY battery if callback is available and SECONDARY is connected
    if (_sendToSecondaryCallback && _ble && _ble->isSecondaryConnected()) {
        _deferredCommand = DeferredCommand::INFO;
        _waitingForSecondaryBattery = true;
        _secondaryBatteryRequestTime = millis();
        _sendToSecondaryCallback("GET_BATTERY");
        return;  // Response completed in handleSecondaryBatteryResponse()
    }

    // No SECONDARY connection - respond immediately with 0.00
    addResponseLine("BATS", "0.00");

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

    // Guard: already waiting for SECONDARY — return 0.00 immediately
    if (_waitingForSecondaryBattery) {
        addResponseLine("BATS", "0.00");
        sendResponse();
        return;
    }

    // Request SECONDARY battery if callback is available and SECONDARY is connected
    if (_sendToSecondaryCallback && _ble && _ble->isSecondaryConnected()) {
        _deferredCommand = DeferredCommand::BATTERY;
        _waitingForSecondaryBattery = true;
        _secondaryBatteryRequestTime = millis();
        _sendToSecondaryCallback("GET_BATTERY");
        return;  // Response completed in handleSecondaryBatteryResponse()
    }

    // No SECONDARY connection - respond immediately with 0.00
    addResponseLine("BATS", "0.00");

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

void MenuController::handleProfileLoad(const char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
    if (paramCount < 1) {
        sendError("Profile ID required");
        return;
    }

    if (!_profiles) {
        sendError("Profile manager not available");
        return;
    }

    if (_stateMachine && isActiveState(_stateMachine->getCurrentState())) {
        sendError("Session must be stopped before loading a profile");
        return;
    }

    int profileId = atoi(params[0]);
    if (!_profiles->loadProfile(static_cast<uint8_t>(profileId))) {
        sendError("Invalid profile ID");
        return;
    }

    // Save settings to persist profile change
    _profiles->saveSettings();

    // Stop any active therapy session before rebooting
    if (_therapy) {
        _therapy->stop();
    }
    if (_haptic) {
        _haptic->emergencyStop();
    }
    if (_stateMachine) {
        _stateMachine->transition(StateTrigger::STOP_SESSION);
    }

    // Send response before reboot
    beginResponse();
    addResponseLine("STATUS", "REBOOTING");
    addResponseLine("PROFILE", _profiles->getCurrentProfileName());
    sendResponse();

    // Give time for response to be sent
    delay(100);

    // Reboot
    if (_restartCallback) {
        _restartCallback();
    } else {
        platformSystemReset();
    }
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

void MenuController::handleProfileCustom(const char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
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
    PatternType patternType = PatternType::RNDP;
    float timeOnMs = 100.0f;
    float timeOffMs = 67.0f;
    float jitterPercent = 23.5f;
    uint8_t numFingers = MAX_ACTUATORS;  // Fallback when no profile is active
    bool mirror = true;

    if (_profiles) {
        const TherapyProfile* profile = _profiles->getCurrentProfile();
        if (profile) {
            durationSec = profile->sessionDurationMin * 60;
            timeOnMs = profile->timeOnMs;
            timeOffMs = profile->timeOffMs;
            jitterPercent = profile->jitterPercent;
            numFingers = profile->numFingers;
            mirror = profile->mirrorPattern;

            if (strcmp(profile->patternType, "rndp") == 0) {
                patternType = PatternType::RNDP;
            } else if (strcmp(profile->patternType, "sequential") == 0) {
                patternType = PatternType::SEQUENTIAL;
            }
        }
    }

    // Start session
    _therapy->startSession(durationSec, patternType, timeOnMs, timeOffMs, jitterPercent, numFingers, mirror);

    // Update state machine
    if (_stateMachine) {
        _stateMachine->transition(StateTrigger::START_SESSION);
    }

    // Notify SECONDARY of session start
    if (_ble && _ble->isSecondaryConnected()) {
        SyncCommand cmd = SyncCommand::createStartSession(0);
        char buffer[64];
        if (cmd.serialize(buffer, sizeof(buffer))) {
            _ble->sendToSecondary(buffer);
        }
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

    // Notify SECONDARY of session pause
    if (_ble && _ble->isSecondaryConnected()) {
        SyncCommand cmd = SyncCommand::createPauseSession(0);
        char buffer[64];
        if (cmd.serialize(buffer, sizeof(buffer))) {
            _ble->sendToSecondary(buffer);
        }
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

    // Notify SECONDARY of session resume
    if (_ble && _ble->isSecondaryConnected()) {
        SyncCommand cmd = SyncCommand::createResumeSession(0);
        char buffer[64];
        if (cmd.serialize(buffer, sizeof(buffer))) {
            _ble->sendToSecondary(buffer);
        }
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

    // Notify SECONDARY of session stop
    if (_ble && _ble->isSecondaryConnected()) {
        SyncCommand cmd = SyncCommand::createStopSession(0);
        char buffer[64];
        if (cmd.serialize(buffer, sizeof(buffer))) {
            _ble->sendToSecondary(buffer);
        }
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
            progress = static_cast<uint8_t>((elapsed * 100) / total);
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

void MenuController::handleParamSet(const char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
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

    // Create local copy and convert param name to uppercase
    char paramName[PARAM_BUFFER_SIZE];
    strncpy(paramName, params[0], PARAM_BUFFER_SIZE - 1);
    paramName[PARAM_BUFFER_SIZE - 1] = '\0';
    for (char* c = paramName; *c; c++) {
        *c = static_cast<char>(toupper(*c));
    }

    if (!_profiles->setParameter(paramName, params[1])) {
        sendError("Invalid parameter name or value out of range");
        return;
    }

    beginResponse();
    addResponseLine("PARAM", paramName);
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

void MenuController::handleCalibrateBuzz(const char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
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

    // Validate ranges. Indices 0..MAX_ACTUATORS-1 are local; the rest map to
    // the SECONDARY glove (4-finger boards: 0-7, 5-finger boards: 0-9).
    constexpr int maxFingerIndex = 2 * MAX_ACTUATORS - 1;
    if (finger < 0 || finger > maxFingerIndex) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Invalid finger index (0-%d)", maxFingerIndex);
        sendError(msg);
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

    if (finger < MAX_ACTUATORS) {
        calibrationBuzz(static_cast<uint8_t>(finger),
                        static_cast<uint8_t>(intensity),
                        static_cast<uint16_t>(duration));
    } else {
        // Relay to the SECONDARY glove (its local index = finger - MAX_ACTUATORS)
        if (!_sendToSecondaryCallback || !_ble || !_ble->isSecondaryConnected()) {
            sendError("Secondary glove not connected");
            return;
        }
        char relay[48];
        snprintf(relay, sizeof(relay), "CALIB_BUZZ:%d:%d:%d",
                 finger - MAX_ACTUATORS, intensity, duration);
        _sendToSecondaryCallback(relay);
    }

    beginResponse();
    addResponseLine("FINGER", (int32_t)finger);
    addResponseLine("INTENSITY", (int32_t)intensity);
    addResponseLine("DURATION", (int32_t)duration);
    sendResponse();
}

void MenuController::handleCalibrateStop() {
    _isCalibrating = false;

    // Cancel any pending/in-flight one-shot so it cannot reactivate a motor
    // after emergencyStop() below. Actual hardware deactivation and clearing
    // of _calibBuzzFinger happens in updateCalibrationBuzz() from loop().
    {
        PLATFORM_CRITICAL_ENTER();
        _calibBuzzRequestPending = false;
        _calibBuzzCancelPending = true;
        PLATFORM_CRITICAL_EXIT();
    }

    if (_haptic) {
        _haptic->emergencyStop();
    }

    beginResponse();
    addResponseLine("MODE", "NORMAL");
    sendResponse();
}

void MenuController::calibrationBuzz(uint8_t finger, uint8_t intensity, uint16_t durationMs) {
    if (!_haptic || finger >= MAX_ACTUATORS || !_haptic->isEnabled(finger)) {
        return;
    }

    // Called from BLE-callback context: only record the pending request.
    // The haptic hardware is driven exclusively from loop() via
    // updateCalibrationBuzz(), which avoids interleaving I2C mux
    // transactions from two contexts.
    {
        PLATFORM_CRITICAL_ENTER();
        _calibBuzzPendingFinger = static_cast<int8_t>(finger);
        _calibBuzzPendingIntensity = intensity;
        _calibBuzzPendingDuration = durationMs;
        _calibBuzzCancelPending = false;
        _calibBuzzRequestPending = true;  // publish flag: set last
        PLATFORM_CRITICAL_EXIT();
    }
}

void MenuController::updateCalibrationBuzz() {
    bool requestPending;
    bool cancelPending;
    int8_t pendingFinger = -1;
    uint8_t pendingIntensity = 0;
    uint16_t pendingDuration = 0;

    {
        PLATFORM_CRITICAL_ENTER();
        requestPending = _calibBuzzRequestPending;
        cancelPending = _calibBuzzCancelPending;
        if (requestPending) {
            pendingFinger = _calibBuzzPendingFinger;
            pendingIntensity = _calibBuzzPendingIntensity;
            pendingDuration = _calibBuzzPendingDuration;
        }
        _calibBuzzRequestPending = false;
        _calibBuzzCancelPending = false;
        PLATFORM_CRITICAL_EXIT();
    }

    if (cancelPending && !requestPending && _calibBuzzFinger >= 0) {
        if (_haptic) {
            _haptic->deactivate(static_cast<uint8_t>(_calibBuzzFinger));
        }
        _calibBuzzFinger = -1;
    }

    if (requestPending) {
        if (_calibBuzzFinger >= 0 && _haptic) {
            _haptic->deactivate(static_cast<uint8_t>(_calibBuzzFinger));
        }
        if (_haptic) {
            _haptic->activate(pendingFinger, pendingIntensity);
        }
        _calibBuzzFinger = pendingFinger;
        _calibBuzzOffTime = millis() + pendingDuration;
    }

    if (_calibBuzzFinger >= 0 &&
        static_cast<int32_t>(millis() - _calibBuzzOffTime) >= 0) {
        if (_haptic) {
            _haptic->deactivate(static_cast<uint8_t>(_calibBuzzFinger));
        }
        _calibBuzzFinger = -1;
    }
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
    addResponseLine("COMMAND", "THERAPY_LED_OFF");
    addResponseLine("COMMAND", "DEBUG");
    sendResponse();
}

void MenuController::handleRestart() {
    // Stop any active therapy session before rebooting
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
    addResponseLine("STATUS", "REBOOTING");
    sendResponse();

    // Give time for response to be sent
    delay(100);

    // Call restart callback if set
    if (_restartCallback) {
        _restartCallback();
    } else {
        // Use NVIC system reset
        platformSystemReset();
    }
}

// =============================================================================
// LED CONTROL COMMAND
// =============================================================================

void MenuController::handleTherapyLedOff(const char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
    if (!_profiles) {
        sendError("Profile manager not available");
        return;
    }

    // Query mode: no parameter - return current value
    if (paramCount == 0) {
        beginResponse();
        addResponseLine("THERAPY_LED_OFF", _profiles->getTherapyLedOff() ? "true" : "false");
        sendResponse();
        return;
    }

    // Set mode: parse boolean value
    bool newValue = false;
    const char* value = params[0];

    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        newValue = true;
    } else if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        newValue = false;
    } else {
        sendError("Invalid value. Use: true/false or 1/0");
        return;
    }

    // Update setting
    _profiles->setTherapyLedOff(newValue);

    // Persist to flash
    if (!_profiles->saveSettings()) {
        Serial.println(F("[MENU] Warning: Failed to save settings"));
    }

    // Sync to SECONDARY if connected
    if (_ble && _ble->isSecondaryConnected()) {
        char syncBuffer[32];
        snprintf(syncBuffer, sizeof(syncBuffer), "LED_OFF_SYNC:%d", newValue ? 1 : 0);
        _ble->sendToSecondary(syncBuffer);
        Serial.printf("[SYNC] Sent LED_OFF_SYNC:%d to SECONDARY\n", newValue ? 1 : 0);
    }

    beginResponse();
    addResponseLine("THERAPY_LED_OFF", newValue ? "true" : "false");
    sendResponse();
}

// =============================================================================
// DEBUG MODE COMMAND
// =============================================================================

void MenuController::handleDebug(const char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
    if (!_profiles) {
        sendError("Profile manager not available");
        return;
    }

    // Query mode: no parameter - return current value
    if (paramCount == 0) {
        beginResponse();
        addResponseLine("DEBUG", _profiles->getDebugMode() ? "true" : "false");
        sendResponse();
        return;
    }

    // Set mode: parse boolean value
    bool newValue = false;
    const char* value = params[0];

    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        newValue = true;
    } else if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        newValue = false;
    } else {
        sendError("Invalid value. Use: true/false or 1/0");
        return;
    }

    // Update setting
    _profiles->setDebugMode(newValue);

    // Persist to flash
    if (!_profiles->saveSettings()) {
        Serial.println(F("[MENU] Warning: Failed to save settings"));
    }

    // Sync to SECONDARY if connected
    if (_ble && _ble->isSecondaryConnected()) {
        char syncBuffer[32];
        snprintf(syncBuffer, sizeof(syncBuffer), "DEBUG_SYNC:%d", newValue ? 1 : 0);
        _ble->sendToSecondary(syncBuffer);
        Serial.printf("[SYNC] Sent DEBUG_SYNC:%d to SECONDARY\n", newValue ? 1 : 0);
    }

    beginResponse();
    addResponseLine("DEBUG", newValue ? "true" : "false");
    sendResponse();
}
