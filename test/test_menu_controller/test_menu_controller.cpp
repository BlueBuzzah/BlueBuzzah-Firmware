/**
 * @file test_menu_controller.cpp
 * @brief Unit tests for MenuController class
 *
 * Tests:
 * - isInternalMessage() prefix matching
 * - Command parsing and dispatch
 * - Response formatting and callbacks
 * - Calibration state management
 */

#include <unity.h>
#include <Arduino.h>
#include <cstring>
#include <cstdlib>

// =============================================================================
// MOCK DEFINITIONS FOR DEPENDENCIES
// =============================================================================

// Forward declarations
class TherapyEngine;
class BatteryMonitor;
class HapticController;
class TherapyStateMachine;
class ProfileManager;

// Mock BatteryStatus
struct BatteryStatus {
    float voltage;
    uint8_t percentage;
    bool isCharging;
    bool isLow;
    bool isCritical;

    BatteryStatus() : voltage(3.7f), percentage(75), isCharging(false), isLow(false), isCritical(false) {}
};

// Mock TherapyProfile
enum class ActuatorType { LRA, ERM };

struct TherapyProfile {
    char name[32];
    ActuatorType actuatorType;
    uint8_t frequencyHz;
    float timeOnMs;
    float timeOffMs;
    uint16_t sessionDurationMin;
    uint8_t amplitudeMin;
    uint8_t amplitudeMax;
    char patternType[16];
    bool mirrorPattern;
    float jitterPercent;
};

// Mock TherapyState enum (from state_machine.h)
enum class TherapyState {
    UNINITIALIZED,
    IDLE,
    READY,
    RUNNING,
    PAUSED,
    STOPPING,
    BATTERY_CRITICAL,
    ERROR_RECOVERABLE,
    ERROR_FATAL,
    SECONDARY_CONNECTING,
    SECONDARY_CONNECTED
};

// Mock StateTrigger enum
enum class StateTrigger {
    INITIALIZE_COMPLETE,
    PHONE_CONNECTED,
    PHONE_DISCONNECTED,
    SECONDARY_CONNECTED,
    SECONDARY_DISCONNECTED,
    START_SESSION,
    PAUSE_SESSION,
    RESUME_SESSION,
    STOP_SESSION,
    SESSION_COMPLETE,
    BATTERY_CRITICAL,
    BATTERY_RECOVERED,
    ERROR_OCCURRED,
    ERROR_CLEARED,
    RESET
};

// Mock TherapyEngine
class TherapyEngine {
public:
    bool _running = false;
    bool _paused = false;
    uint32_t _elapsedSec = 0;
    uint32_t _durationSec = 3600;

    bool isRunning() const { return _running; }
    bool isPaused() const { return _paused; }
    void startSession(uint32_t, uint8_t, float, float, float, uint8_t, bool) { _running = true; }
    void pause() { _paused = true; _running = false; }
    void resume() { _paused = false; _running = true; }
    void stop() { _running = false; _paused = false; }
    uint32_t getElapsedSeconds() const { return _elapsedSec; }
    uint32_t getDurationSeconds() const { return _durationSec; }
};

// Mock BatteryMonitor
class BatteryMonitor {
public:
    BatteryStatus _status;

    BatteryStatus getStatus() const { return _status; }
};

// Mock HapticController
class HapticController {
public:
    bool _enabled[8] = {true, true, true, true, true, false, false, false};
    int _lastActivatedFinger = -1;
    int _lastIntensity = 0;

    bool isEnabled(int finger) const { return finger < 8 ? _enabled[finger] : false; }
    void activate(int finger, int intensity) { _lastActivatedFinger = finger; _lastIntensity = intensity; }
    void deactivate(int) {}
    void emergencyStop() {}
};

// Mock TherapyStateMachine
class TherapyStateMachine {
public:
    TherapyState _state = TherapyState::IDLE;

    TherapyState getCurrentState() const { return _state; }
    void transition(StateTrigger) {}
    bool isRunning() const { return _state == TherapyState::RUNNING; }
    bool isPaused() const { return _state == TherapyState::PAUSED; }
    bool isReady() const { return _state == TherapyState::READY; }
};

// Mock ProfileManager
class ProfileManager {
public:
    TherapyProfile _profile;
    const char* _profileNames[3] = {"Default", "Gentle", "Intense"};
    uint8_t _profileCount = 3;
    uint8_t _currentProfileId = 1;

    ProfileManager() {
        strcpy(_profile.name, "Default");
        _profile.actuatorType = ActuatorType::LRA;
        _profile.frequencyHz = 235;
        _profile.timeOnMs = 100.0f;
        _profile.timeOffMs = 67.0f;
        _profile.sessionDurationMin = 120;
        _profile.amplitudeMin = 50;
        _profile.amplitudeMax = 100;
        strcpy(_profile.patternType, "rndp");
        _profile.mirrorPattern = true;
        _profile.jitterPercent = 23.5f;
    }

    const char** getProfileNames(uint8_t* count) { *count = _profileCount; return _profileNames; }
    bool loadProfile(int) { return true; }
    const char* getCurrentProfileName() const { return _profile.name; }
    uint8_t getCurrentProfileId() const { return _currentProfileId; }
    const TherapyProfile* getCurrentProfile() const { return &_profile; }
    bool setParameter(const char*, const char*) { return true; }
};

// Mock isActiveState (mirrors include/types.h; mock enum has no LOW_BATTERY)
bool isActiveState(TherapyState state) {
    return state == TherapyState::RUNNING || state == TherapyState::PAUSED;
}

// Mock helper function
const char* therapyStateToString(TherapyState state) {
    switch (state) {
        case TherapyState::IDLE: return "IDLE";
        case TherapyState::READY: return "READY";
        case TherapyState::RUNNING: return "RUNNING";
        case TherapyState::PAUSED: return "PAUSED";
        default: return "UNKNOWN";
    }
}

// Mock NVIC_SystemReset
void NVIC_SystemReset() {}

// Mock BLEManager
class BLEManager {
public:
    bool _secondaryConnected = true;

    bool isSecondaryConnected() const { return _secondaryConnected; }
};

// =============================================================================
// INCLUDE MENU CONTROLLER (after mocks)
// =============================================================================

// Define constants from config.h
#define FIRMWARE_VERSION "2.0.0-test"
#define BLE_NAME "BlueBuzzah-Test"
#define MAX_ACTUATORS 5
#define PATTERN_TYPE_RNDP 0
#define PATTERN_TYPE_SEQUENTIAL 1

// Include DeviceRole enum
enum class DeviceRole {
    PRIMARY,
    SECONDARY,
    STANDALONE
};

const char* deviceRoleToString(DeviceRole role) {
    switch (role) {
        case DeviceRole::PRIMARY: return "PRIMARY";
        case DeviceRole::SECONDARY: return "SECONDARY";
        case DeviceRole::STANDALONE: return "STANDALONE";
        default: return "UNKNOWN";
    }
}

// Now we manually implement the parts of MenuController we want to test
// since including the actual header would bring in hardware dependencies

// =============================================================================
// INTERNAL MESSAGE PREFIXES (from menu_controller.cpp)
// =============================================================================

const char* INTERNAL_MESSAGES[] = {
    "BUZZ",
    "PING",
    "PARAM_UPDATE",
    "SEED",
    "SEED_ACK",
    "GET_BATTERY",
    "BATRESPONSE",
    "ACK_PARAM_UPDATE",
    "SYNC:",
    "IDENTIFY:"
};

const uint8_t INTERNAL_MESSAGE_COUNT = sizeof(INTERNAL_MESSAGES) / sizeof(INTERNAL_MESSAGES[0]);

// =============================================================================
// SIMPLE MENU CONTROLLER FOR TESTING
// =============================================================================

#define EOT_CHAR '\x04'
#define RESPONSE_BUFFER_SIZE 512
#define PARAM_BUFFER_SIZE 64
#define MAX_COMMAND_PARAMS 16

typedef void (*SendResponseCallback)(const char* response);
typedef void (*RestartCallback)();

/**
 * @brief Test version of MenuController focusing on testable logic
 */
class TestMenuController {
public:
    // Public for test inspection
    TherapyEngine* _therapy;
    BatteryMonitor* _battery;
    HapticController* _haptic;
    TherapyStateMachine* _stateMachine;
    ProfileManager* _profiles;
    BLEManager* _ble;

    DeviceRole _role;
    char _firmwareVersion[16];
    char _deviceName[32];

    SendResponseCallback _sendCallback;
    RestartCallback _restartCallback;

    bool _isCalibrating;
    uint32_t _calibrationStartTime;
    int8_t _calibBuzzFinger;
    uint32_t _calibBuzzOffTime;
    char _responseBuffer[RESPONSE_BUFFER_SIZE];

    // Deferred command tracking for SECONDARY battery query
    enum class DeferredCommand : uint8_t {
        NONE = 0,
        BATTERY,
        INFO
    };

    typedef void (*SendToSecondaryCallback)(const char* message);
    SendToSecondaryCallback _sendToSecondaryCallback;
    DeferredCommand _deferredCommand;
    volatile float _secondaryBatteryVoltage;
    bool _waitingForSecondaryBattery;
    uint32_t _secondaryBatteryRequestTime;

    // Track last processed command for testing
    char _lastCommand[32];
    uint8_t _lastParamCount;

    TestMenuController() :
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
        _secondaryBatteryRequestTime(0),
        _isCalibrating(false),
        _calibrationStartTime(0),
        _calibBuzzFinger(-1),
        _calibBuzzOffTime(0),
        _lastParamCount(0)
    {
        strcpy(_firmwareVersion, FIRMWARE_VERSION);
        strcpy(_deviceName, BLE_NAME);
        memset(_responseBuffer, 0, sizeof(_responseBuffer));
        memset(_lastCommand, 0, sizeof(_lastCommand));
    }

    void begin(
        TherapyEngine* therapyEngine,
        BatteryMonitor* batteryMonitor,
        HapticController* hapticController,
        TherapyStateMachine* stateMachine,
        ProfileManager* profileManager = nullptr
    ) {
        _therapy = therapyEngine;
        _battery = batteryMonitor;
        _haptic = hapticController;
        _stateMachine = stateMachine;
        _profiles = profileManager;
    }

    void setDeviceInfo(DeviceRole role, const char* firmwareVersion, const char* deviceName) {
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

    void setSendCallback(SendResponseCallback callback) {
        _sendCallback = callback;
    }

    void setRestartCallback(RestartCallback callback) {
        _restartCallback = callback;
    }

    bool isCalibrating() const { return _isCalibrating; }

    /**
     * @brief Check if message is an internal sync message
     */
    bool isInternalMessage(const char* message) {
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

    /**
     * @brief Parse command and extract parameters
     */
    bool parseCommand(const char* message, char* command, char params[][PARAM_BUFFER_SIZE], uint8_t& paramCount) {
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

        // Store for test verification
        strcpy(_lastCommand, command);
        _lastParamCount = paramCount;

        return true;
    }

    // Response helpers
    void beginResponse() {
        _responseBuffer[0] = '\0';
    }

    void addResponseLine(const char* key, const char* value) {
        char line[128];
        snprintf(line, sizeof(line), "%s:%s\n", key, value ? value : "");

        size_t currentLen = strlen(_responseBuffer);
        size_t lineLen = strlen(line);

        if (currentLen + lineLen < RESPONSE_BUFFER_SIZE - 2) {
            strcat(_responseBuffer, line);
        }
    }

    void addResponseLine(const char* key, int32_t value) {
        char valueStr[16];
        snprintf(valueStr, sizeof(valueStr), "%ld", (long)value);
        addResponseLine(key, valueStr);
    }

    void addResponseLine(const char* key, float value, uint8_t decimals = 2) {
        char valueStr[16];
        char format[8];
        snprintf(format, sizeof(format), "%%.%df", decimals);
        snprintf(valueStr, sizeof(valueStr), format, value);
        addResponseLine(key, valueStr);
    }

    void sendResponse() {
        size_t len = strlen(_responseBuffer);
        if (len < RESPONSE_BUFFER_SIZE - 1) {
            _responseBuffer[len] = EOT_CHAR;
            _responseBuffer[len + 1] = '\0';
        }

        if (_sendCallback) {
            _sendCallback(_responseBuffer);
        }
    }

    void sendError(const char* message) {
        beginResponse();
        addResponseLine("ERROR", message);
        sendResponse();
    }

    // Calibration methods
    void startCalibration() {
        _isCalibrating = true;
        _calibrationStartTime = millis();
    }

    void stopCalibration() {
        _isCalibrating = false;
    }

    void setSendToSecondaryCallback(SendToSecondaryCallback callback) {
        _sendToSecondaryCallback = callback;
    }

    void setBLE(BLEManager* ble) {
        _ble = ble;
    }

    // Calibration buzz methods (mirror MenuController::calibrationBuzz /
    // MenuController::updateCalibrationBuzz)
    void calibrationBuzz(uint8_t finger, uint8_t intensity, uint16_t durationMs) {
        if (!_haptic || finger >= MAX_ACTUATORS || !_haptic->isEnabled(finger)) {
            return;
        }
        if (_calibBuzzFinger >= 0) {
            _haptic->deactivate(static_cast<uint8_t>(_calibBuzzFinger));
        }
        _haptic->activate(finger, intensity);
        _calibBuzzFinger = static_cast<int8_t>(finger);
        _calibBuzzOffTime = millis() + durationMs;
    }

    void updateCalibrationBuzz() {
        if (_calibBuzzFinger >= 0 &&
            static_cast<int32_t>(millis() - _calibBuzzOffTime) >= 0) {
            if (_haptic) {
                _haptic->deactivate(static_cast<uint8_t>(_calibBuzzFinger));
            }
            _calibBuzzFinger = -1;
        }
    }

    void handleCalibrateBuzz(const char params[][PARAM_BUFFER_SIZE], uint8_t paramCount) {
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

    void handleCalibrateStop() {
        _isCalibrating = false;
        _calibBuzzFinger = -1;

        if (_haptic) {
            _haptic->emergencyStop();
        }

        beginResponse();
        addResponseLine("MODE", "NORMAL");
        sendResponse();
    }

    // Simulate handleBattery with deferred SECONDARY battery query
    void handleBattery() {
        beginResponse();

        if (_battery) {
            BatteryStatus status = _battery->getStatus();
            addResponseLine("BATP", status.voltage, 2);
        } else {
            addResponseLine("BATP", "0.00");
        }

        // Request SECONDARY battery if callback is available
        if (_sendToSecondaryCallback) {
            _deferredCommand = DeferredCommand::BATTERY;
            _waitingForSecondaryBattery = true;
            _secondaryBatteryRequestTime = millis();
            _sendToSecondaryCallback("GET_BATTERY");
            return;
        }

        // No SECONDARY connection - respond immediately with 0.00
        addResponseLine("BATS", "0.00");
        sendResponse();
    }

    // Simulate handleInfo with deferred SECONDARY battery query
    void handleInfo() {
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

        if (_battery) {
            BatteryStatus status = _battery->getStatus();
            addResponseLine("BATP", status.voltage, 2);
        } else {
            addResponseLine("BATP", "0.00");
        }

        // Request SECONDARY battery if callback is available
        if (_sendToSecondaryCallback) {
            _deferredCommand = DeferredCommand::INFO;
            _waitingForSecondaryBattery = true;
            _secondaryBatteryRequestTime = millis();
            _sendToSecondaryCallback("GET_BATTERY");
            return;
        }

        // No SECONDARY connection - respond immediately with 0.00
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
    }

    void handleSecondaryBatteryResponse(float voltage) {
        if (!_waitingForSecondaryBattery) {
            return;
        }

        _waitingForSecondaryBattery = false;
        _secondaryBatteryVoltage = voltage;

        addResponseLine("BATS", voltage, 2);

        if (_deferredCommand == DeferredCommand::INFO) {
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

    void checkSecondaryBatteryTimeout() {
        if (!_waitingForSecondaryBattery) {
            return;
        }

        if (millis() - _secondaryBatteryRequestTime >= 1000) {
            handleSecondaryBatteryResponse(0.0f);
        }
    }

    void handlePing() {
        beginResponse();
        addResponseLine("PONG", "");
        sendResponse();
    }

    void handleProfileLoad(int profileId) {
        if (!_profiles) {
            sendError("Profile manager not available");
            return;
        }

        if (_stateMachine && isActiveState(_stateMachine->getCurrentState())) {
            sendError("Session must be stopped before loading a profile");
            return;
        }

        if (!_profiles->loadProfile(profileId)) {
            sendError("Invalid profile ID");
            return;
        }

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
        addResponseLine("PROFILE", _profiles->getCurrentProfileName());
        sendResponse();

        if (_restartCallback) {
            _restartCallback();
        }
    }

    /**
     * @brief Handle incoming command and send response via callback
     * @param message Raw command message
     * @param allowInternal When true, bypass the internal-message prefix filter
     *        (messages arriving on an identified PHONE connection are always commands)
     * @return true if command was processed
     */
    bool handleCommand(const char* message, bool allowInternal = false) {
        if (!message || strlen(message) == 0) {
            return false;
        }

        // Skip internal messages unless caller vouches for the source
        if (!allowInternal && isInternalMessage(message)) {
            return false;
        }

        char command[32];
        char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
        uint8_t paramCount = 0;

        if (!parseCommand(message, command, params, paramCount)) {
            sendError("Invalid command format");
            return false;
        }

        if (strcmp(command, "PING") == 0) {
            handlePing();
            return true;
        }

        if (strcmp(command, "PROFILE_LOAD") == 0) {
            if (paramCount < 1) {
                sendError("Profile ID required");
                return false;
            }
            handleProfileLoad(atoi(params[0]));
            return true;
        }

        if (strcmp(command, "CALIBRATE_START") == 0) {
            startCalibration();
            beginResponse();
            addResponseLine("MODE", "CALIBRATION");
            sendResponse();
            return true;
        }

        if (strcmp(command, "CALIBRATE_BUZZ") == 0) {
            handleCalibrateBuzz(params, paramCount);
            return true;
        }

        if (strcmp(command, "CALIBRATE_STOP") == 0) {
            handleCalibrateStop();
            return true;
        }

        return false;
    }
};

// =============================================================================
// TEST FIXTURES
// =============================================================================

static TestMenuController* g_menu = nullptr;
static TherapyEngine* g_therapy = nullptr;
static BatteryMonitor* g_battery = nullptr;
static HapticController* g_haptic = nullptr;
static TherapyStateMachine* g_stateMachine = nullptr;
static ProfileManager* g_profiles = nullptr;
static BLEManager* g_ble = nullptr;

// Track callback invocations
static char g_lastResponse[RESPONSE_BUFFER_SIZE];
static int g_responseCount = 0;
static int g_restartCount = 0;
static char g_lastSecondaryMessage[64];
static int g_secondaryMessageCount = 0;

// Aliases matching the brief's naming (same backing storage as above)
#define capturedResponse g_lastResponse
#define capturedSecondaryMessage g_lastSecondaryMessage

void testSendCallback(const char* response) {
    strncpy(g_lastResponse, response, sizeof(g_lastResponse) - 1);
    g_lastResponse[sizeof(g_lastResponse) - 1] = '\0';
    g_responseCount++;
}

void testRestartCallback() {
    g_restartCount++;
}

void testSendToSecondaryCallback(const char* message) {
    strncpy(g_lastSecondaryMessage, message, sizeof(g_lastSecondaryMessage) - 1);
    g_lastSecondaryMessage[sizeof(g_lastSecondaryMessage) - 1] = '\0';
    g_secondaryMessageCount++;
}

static void resetCapturedResponse(void) {
    memset(g_lastResponse, 0, sizeof(g_lastResponse));
    g_responseCount = 0;
}

static void resetSecondaryCapture(void) {
    memset(g_lastSecondaryMessage, 0, sizeof(g_lastSecondaryMessage));
    g_secondaryMessageCount = 0;
}

void setUp(void) {
    g_menu = new TestMenuController();
    g_therapy = new TherapyEngine();
    g_battery = new BatteryMonitor();
    g_haptic = new HapticController();
    g_stateMachine = new TherapyStateMachine();
    g_profiles = new ProfileManager();
    g_ble = new BLEManager();

    g_menu->begin(g_therapy, g_battery, g_haptic, g_stateMachine, g_profiles);
    g_menu->setSendCallback(testSendCallback);
    g_menu->setRestartCallback(testRestartCallback);
    g_menu->setBLE(g_ble);

    memset(g_lastResponse, 0, sizeof(g_lastResponse));
    g_responseCount = 0;
    g_restartCount = 0;
    memset(g_lastSecondaryMessage, 0, sizeof(g_lastSecondaryMessage));
    g_secondaryMessageCount = 0;

    mockResetTime();
}

void tearDown(void) {
    delete g_menu;
    delete g_therapy;
    delete g_battery;
    delete g_haptic;
    delete g_stateMachine;
    delete g_profiles;
    delete g_ble;

    g_menu = nullptr;
    g_therapy = nullptr;
    g_battery = nullptr;
    g_haptic = nullptr;
    g_stateMachine = nullptr;
    g_profiles = nullptr;
}

// =============================================================================
// INTERNAL MESSAGE TESTS
// =============================================================================

void test_isInternalMessage_null_returns_false() {
    TEST_ASSERT_FALSE(g_menu->isInternalMessage(nullptr));
}

void test_isInternalMessage_empty_returns_false() {
    TEST_ASSERT_FALSE(g_menu->isInternalMessage(""));
}

void test_isInternalMessage_PARAM_UPDATE_returns_true() {
    TEST_ASSERT_TRUE(g_menu->isInternalMessage("PARAM_UPDATE:INTENSITY:75"));
}

void test_isInternalMessage_SEED_returns_true() {
    TEST_ASSERT_TRUE(g_menu->isInternalMessage("SEED:12345"));
}

void test_isInternalMessage_SEED_ACK_returns_true() {
    TEST_ASSERT_TRUE(g_menu->isInternalMessage("SEED_ACK:12345"));
}

void test_isInternalMessage_GET_BATTERY_returns_true() {
    TEST_ASSERT_TRUE(g_menu->isInternalMessage("GET_BATTERY"));
}

void test_isInternalMessage_BATRESPONSE_returns_true() {
    TEST_ASSERT_TRUE(g_menu->isInternalMessage("BATRESPONSE:3.72:85"));
}

void test_isInternalMessage_ACK_PARAM_UPDATE_returns_true() {
    TEST_ASSERT_TRUE(g_menu->isInternalMessage("ACK_PARAM_UPDATE:INTENSITY"));
}

void test_isInternalMessage_SYNC_prefix_returns_true() {
    TEST_ASSERT_TRUE(g_menu->isInternalMessage("SYNC:12345:67890"));
}

void test_isInternalMessage_IDENTIFY_prefix_returns_true() {
    TEST_ASSERT_TRUE(g_menu->isInternalMessage("IDENTIFY:PRIMARY"));
}

void test_isInternalMessage_user_command_INFO_returns_false() {
    TEST_ASSERT_FALSE(g_menu->isInternalMessage("INFO"));
}

void test_isInternalMessage_user_command_BATTERY_returns_false() {
    TEST_ASSERT_FALSE(g_menu->isInternalMessage("BATTERY"));
}

void test_isInternalMessage_user_command_SESSION_START_returns_false() {
    TEST_ASSERT_FALSE(g_menu->isInternalMessage("SESSION_START"));
}

void test_isInternalMessage_PING_returns_true() {
    TEST_ASSERT_TRUE(g_menu->isInternalMessage("PING"));
}

void test_isInternalMessage_partial_match_not_prefix_returns_false() {
    // "BUZ" doesn't match "BUZZ" prefix
    TEST_ASSERT_FALSE(g_menu->isInternalMessage("BUZ"));
}

void test_isInternalMessage_case_sensitive() {
    // Internal messages are uppercase, lowercase should not match
    TEST_ASSERT_FALSE(g_menu->isInternalMessage("buzz"));
    TEST_ASSERT_FALSE(g_menu->isInternalMessage("Buzz"));
}

// =============================================================================
// COMMAND PARSING TESTS
// =============================================================================

void test_parseCommand_null_message_returns_false() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_FALSE(g_menu->parseCommand(nullptr, command, params, paramCount));
}

void test_parseCommand_empty_message_returns_false() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_FALSE(g_menu->parseCommand("", command, params, paramCount));
}

void test_parseCommand_whitespace_only_returns_false() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_FALSE(g_menu->parseCommand("   ", command, params, paramCount));
}

void test_parseCommand_simple_command_no_params() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_TRUE(g_menu->parseCommand("INFO", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("INFO", command);
    TEST_ASSERT_EQUAL_UINT8(0, paramCount);
}

void test_parseCommand_converts_to_uppercase() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_TRUE(g_menu->parseCommand("info", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("INFO", command);
}

void test_parseCommand_mixed_case_to_uppercase() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_TRUE(g_menu->parseCommand("Session_Start", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("SESSION_START", command);
}

void test_parseCommand_single_param() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_TRUE(g_menu->parseCommand("PROFILE_LOAD:1", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("PROFILE_LOAD", command);
    TEST_ASSERT_EQUAL_UINT8(1, paramCount);
    TEST_ASSERT_EQUAL_STRING("1", params[0]);
}

void test_parseCommand_multiple_params() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_TRUE(g_menu->parseCommand("CALIBRATE_BUZZ:2:75:200", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("CALIBRATE_BUZZ", command);
    TEST_ASSERT_EQUAL_UINT8(3, paramCount);
    TEST_ASSERT_EQUAL_STRING("2", params[0]);
    TEST_ASSERT_EQUAL_STRING("75", params[1]);
    TEST_ASSERT_EQUAL_STRING("200", params[2]);
}

void test_parseCommand_strips_newline() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_TRUE(g_menu->parseCommand("PING\n", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("PING", command);
}

void test_parseCommand_strips_carriage_return() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_TRUE(g_menu->parseCommand("PING\r", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("PING", command);
}

void test_parseCommand_strips_crlf() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_TRUE(g_menu->parseCommand("PING\r\n", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("PING", command);
}

void test_parseCommand_strips_EOT() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    char msg[32] = "PING";
    msg[4] = EOT_CHAR;
    msg[5] = '\0';

    TEST_ASSERT_TRUE(g_menu->parseCommand(msg, command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("PING", command);
}

void test_parseCommand_trims_leading_whitespace() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_TRUE(g_menu->parseCommand("   INFO", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("INFO", command);
}

void test_parseCommand_max_params() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    // Create command with many params
    TEST_ASSERT_TRUE(g_menu->parseCommand("CMD:1:2:3:4:5:6:7:8:9:10:11:12:13:14:15:16", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("CMD", command);
    TEST_ASSERT_EQUAL_UINT8(MAX_COMMAND_PARAMS, paramCount);  // Should cap at max
}

void test_parseCommand_key_value_pairs() {
    char command[32];
    char params[MAX_COMMAND_PARAMS][PARAM_BUFFER_SIZE];
    uint8_t paramCount = 0;

    TEST_ASSERT_TRUE(g_menu->parseCommand("PROFILE_CUSTOM:FREQ:200:ON:100:OFF:67", command, params, paramCount));
    TEST_ASSERT_EQUAL_STRING("PROFILE_CUSTOM", command);
    TEST_ASSERT_EQUAL_UINT8(6, paramCount);
    TEST_ASSERT_EQUAL_STRING("FREQ", params[0]);
    TEST_ASSERT_EQUAL_STRING("200", params[1]);
    TEST_ASSERT_EQUAL_STRING("ON", params[2]);
    TEST_ASSERT_EQUAL_STRING("100", params[3]);
}

// =============================================================================
// RESPONSE FORMATTING TESTS
// =============================================================================

void test_beginResponse_clears_buffer() {
    strcpy(g_menu->_responseBuffer, "previous content");
    g_menu->beginResponse();
    TEST_ASSERT_EQUAL_STRING("", g_menu->_responseBuffer);
}

void test_addResponseLine_string_value() {
    g_menu->beginResponse();
    g_menu->addResponseLine("KEY", "VALUE");
    TEST_ASSERT_EQUAL_STRING("KEY:VALUE\n", g_menu->_responseBuffer);
}

void test_addResponseLine_null_value() {
    g_menu->beginResponse();
    g_menu->addResponseLine("KEY", (const char*)nullptr);
    TEST_ASSERT_EQUAL_STRING("KEY:\n", g_menu->_responseBuffer);
}

void test_addResponseLine_integer_value() {
    g_menu->beginResponse();
    g_menu->addResponseLine("COUNT", (int32_t)42);
    TEST_ASSERT_EQUAL_STRING("COUNT:42\n", g_menu->_responseBuffer);
}

void test_addResponseLine_negative_integer() {
    g_menu->beginResponse();
    g_menu->addResponseLine("TEMP", (int32_t)-10);
    TEST_ASSERT_EQUAL_STRING("TEMP:-10\n", g_menu->_responseBuffer);
}

void test_addResponseLine_float_default_decimals() {
    g_menu->beginResponse();
    g_menu->addResponseLine("VOLTAGE", 3.72f);
    TEST_ASSERT_EQUAL_STRING("VOLTAGE:3.72\n", g_menu->_responseBuffer);
}

void test_addResponseLine_float_custom_decimals() {
    g_menu->beginResponse();
    g_menu->addResponseLine("PRECISE", 1.23456f, 4);
    // Note: float precision may vary slightly
    TEST_ASSERT_TRUE(strstr(g_menu->_responseBuffer, "PRECISE:1.234") != nullptr);
}

void test_addResponseLine_multiple_lines() {
    g_menu->beginResponse();
    g_menu->addResponseLine("A", "1");
    g_menu->addResponseLine("B", "2");
    g_menu->addResponseLine("C", "3");
    TEST_ASSERT_EQUAL_STRING("A:1\nB:2\nC:3\n", g_menu->_responseBuffer);
}

void test_sendResponse_adds_EOT() {
    g_menu->beginResponse();
    g_menu->addResponseLine("KEY", "VALUE");
    g_menu->sendResponse();

    // Check that EOT was added
    size_t len = strlen(g_menu->_responseBuffer);
    TEST_ASSERT_EQUAL_CHAR(EOT_CHAR, g_menu->_responseBuffer[len - 1]);
}

void test_sendResponse_invokes_callback() {
    g_menu->beginResponse();
    g_menu->addResponseLine("PONG", "");
    g_menu->sendResponse();

    TEST_ASSERT_EQUAL_INT(1, g_responseCount);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "PONG:") != nullptr);
}

void test_sendError_formats_correctly() {
    g_menu->sendError("Test error message");

    TEST_ASSERT_EQUAL_INT(1, g_responseCount);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "ERROR:Test error message") != nullptr);

    // Should end with EOT
    size_t len = strlen(g_lastResponse);
    TEST_ASSERT_EQUAL_CHAR(EOT_CHAR, g_lastResponse[len - 1]);
}

// =============================================================================
// COMMAND DISPATCH TESTS
// =============================================================================

void test_handleCommand_null_returns_false() {
    TEST_ASSERT_FALSE(g_menu->handleCommand(nullptr));
}

void test_handleCommand_empty_returns_false() {
    TEST_ASSERT_FALSE(g_menu->handleCommand(""));
}

void test_handleCommand_PING_without_allowInternal_is_swallowed() {
    // PING matches the internal-message prefix filter, so a caller that
    // doesn't vouch for the source (e.g. a SECONDARY glove connection)
    // must not have it dispatched.
    TEST_ASSERT_FALSE(g_menu->handleCommand("PING"));
    TEST_ASSERT_EQUAL_INT(0, g_responseCount);
}

void test_handleCommand_PING_with_allowInternal_dispatches_PONG() {
    // A command arriving on an identified PHONE connection bypasses the
    // internal-message filter and is dispatched normally.
    TEST_ASSERT_TRUE(g_menu->handleCommand("PING", true));
    TEST_ASSERT_EQUAL_INT(1, g_responseCount);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "PONG:") != nullptr);
}

void test_handleCommand_allowInternal_defaults_to_false() {
    // Default-argument behavior must match explicit false.
    TEST_ASSERT_FALSE(g_menu->handleCommand("PING"));
    TEST_ASSERT_EQUAL_INT(0, g_responseCount);
}

// =============================================================================
// DEVICE INFO TESTS
// =============================================================================

void test_setDeviceInfo_updates_role() {
    g_menu->setDeviceInfo(DeviceRole::SECONDARY, nullptr, nullptr);
    TEST_ASSERT_EQUAL(DeviceRole::SECONDARY, g_menu->_role);
}

void test_setDeviceInfo_updates_firmware_version() {
    g_menu->setDeviceInfo(DeviceRole::PRIMARY, "1.0.0", nullptr);
    TEST_ASSERT_EQUAL_STRING("1.0.0", g_menu->_firmwareVersion);
}

void test_setDeviceInfo_updates_device_name() {
    g_menu->setDeviceInfo(DeviceRole::PRIMARY, nullptr, "CustomName");
    TEST_ASSERT_EQUAL_STRING("CustomName", g_menu->_deviceName);
}

void test_setDeviceInfo_truncates_long_version() {
    g_menu->setDeviceInfo(DeviceRole::PRIMARY, "1234567890123456789", nullptr);
    // Should be truncated to fit in buffer (16 chars including null)
    TEST_ASSERT_EQUAL(15, strlen(g_menu->_firmwareVersion));
}

void test_setDeviceInfo_truncates_long_name() {
    g_menu->setDeviceInfo(DeviceRole::PRIMARY, nullptr, "ThisIsAVeryLongDeviceNameThatShouldBeTruncated");
    // Should be truncated to fit in buffer (32 chars including null)
    TEST_ASSERT_EQUAL(31, strlen(g_menu->_deviceName));
}

// =============================================================================
// CALIBRATION STATE TESTS
// =============================================================================

void test_isCalibrating_initially_false() {
    TEST_ASSERT_FALSE(g_menu->isCalibrating());
}

void test_startCalibration_sets_calibrating_true() {
    g_menu->startCalibration();
    TEST_ASSERT_TRUE(g_menu->isCalibrating());
}

void test_startCalibration_records_start_time() {
    mockAdvanceMillis(1000);
    g_menu->startCalibration();
    TEST_ASSERT_EQUAL_UINT32(1000, g_menu->_calibrationStartTime);
}

void test_stopCalibration_sets_calibrating_false() {
    g_menu->startCalibration();
    TEST_ASSERT_TRUE(g_menu->isCalibrating());

    g_menu->stopCalibration();
    TEST_ASSERT_FALSE(g_menu->isCalibrating());
}

void test_calibrateBuzz_does_not_block(void) {
    g_menu->handleCommand("CALIBRATE_START");
    resetCapturedResponse();
    uint32_t before = millis();
    g_menu->handleCommand("CALIBRATE_BUZZ:1:80:2000");
    uint32_t elapsed = millis() - before;
    TEST_ASSERT_TRUE(elapsed < 100);   // must not delay(duration)
    TEST_ASSERT_NOT_NULL(strstr(capturedResponse, "FINGER:1"));
    g_menu->handleCommand("CALIBRATE_STOP");
}

void test_calibrateBuzz_secondary_finger_relayed(void) {
    g_menu->handleCommand("CALIBRATE_START");
    resetSecondaryCapture();
    resetCapturedResponse();
    g_menu->setSendToSecondaryCallback(testSendToSecondaryCallback);
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "CALIBRATE_BUZZ:%d:80:500", MAX_ACTUATORS);  // first secondary finger
    g_menu->handleCommand(cmd);
    TEST_ASSERT_NOT_NULL(strstr(capturedSecondaryMessage, "CALIB_BUZZ:0:80:500"));
    g_menu->handleCommand("CALIBRATE_STOP");
}

// =============================================================================
// CALLBACK TESTS
// =============================================================================

void test_sendCallback_not_invoked_when_null() {
    g_menu->setSendCallback(nullptr);
    g_menu->beginResponse();
    g_menu->addResponseLine("TEST", "VALUE");
    g_menu->sendResponse();  // Should not crash

    // Response count should not change (callback was null)
    TEST_ASSERT_EQUAL_INT(0, g_responseCount);
}

void test_restartCallback_stored() {
    TEST_ASSERT_NOT_NULL(g_menu->_restartCallback);
}

// =============================================================================
// INITIALIZATION TESTS
// =============================================================================

void test_begin_stores_component_references() {
    TEST_ASSERT_EQUAL_PTR(g_therapy, g_menu->_therapy);
    TEST_ASSERT_EQUAL_PTR(g_battery, g_menu->_battery);
    TEST_ASSERT_EQUAL_PTR(g_haptic, g_menu->_haptic);
    TEST_ASSERT_EQUAL_PTR(g_stateMachine, g_menu->_stateMachine);
    TEST_ASSERT_EQUAL_PTR(g_profiles, g_menu->_profiles);
}

void test_begin_allows_null_profile_manager() {
    TestMenuController* menu = new TestMenuController();
    menu->begin(g_therapy, g_battery, g_haptic, g_stateMachine, nullptr);

    TEST_ASSERT_NULL(menu->_profiles);
    delete menu;
}

void test_default_firmware_version() {
    TestMenuController* menu = new TestMenuController();
    TEST_ASSERT_EQUAL_STRING(FIRMWARE_VERSION, menu->_firmwareVersion);
    delete menu;
}

void test_default_device_name() {
    TestMenuController* menu = new TestMenuController();
    TEST_ASSERT_EQUAL_STRING(BLE_NAME, menu->_deviceName);
    delete menu;
}

void test_default_role_is_primary() {
    TestMenuController* menu = new TestMenuController();
    TEST_ASSERT_EQUAL(DeviceRole::PRIMARY, menu->_role);
    delete menu;
}

// =============================================================================
// SECONDARY BATTERY QUERY TESTS
// =============================================================================

void test_handleBattery_returns_secondary_voltage() {
    // Set up the send-to-secondary callback to simulate SECONDARY connection
    g_menu->setSendToSecondaryCallback(testSendToSecondaryCallback);

    // Set PRIMARY battery
    g_battery->_status.voltage = 3.85f;

    // Call handleBattery - should defer and send GET_BATTERY to SECONDARY
    g_menu->handleBattery();

    // Verify GET_BATTERY was sent to SECONDARY
    TEST_ASSERT_EQUAL_STRING("GET_BATTERY", g_lastSecondaryMessage);
    TEST_ASSERT_EQUAL_INT(1, g_secondaryMessageCount);

    // No response sent yet (deferred)
    TEST_ASSERT_EQUAL_INT(0, g_responseCount);

    // Simulate SECONDARY responding with battery voltage
    g_menu->handleSecondaryBatteryResponse(3.72f);

    // Now the response should be sent
    TEST_ASSERT_EQUAL_INT(1, g_responseCount);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "BATP:3.85") != nullptr);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "BATS:3.72") != nullptr);
}

void test_handleBattery_returns_zero_when_secondary_unavailable() {
    // No send-to-secondary callback set - simulates no SECONDARY connection

    // Set PRIMARY battery
    g_battery->_status.voltage = 3.85f;

    // Call handleBattery - should respond immediately with BATS:0.00
    g_menu->handleBattery();

    TEST_ASSERT_EQUAL_INT(1, g_responseCount);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "BATP:3.85") != nullptr);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "BATS:0.00") != nullptr);
}

void test_handleInfo_includes_secondary_voltage() {
    // Set up the send-to-secondary callback
    g_menu->setSendToSecondaryCallback(testSendToSecondaryCallback);

    // Set PRIMARY battery
    g_battery->_status.voltage = 4.10f;

    // Call handleInfo - should defer
    g_menu->handleInfo();

    // Verify GET_BATTERY was sent
    TEST_ASSERT_EQUAL_STRING("GET_BATTERY", g_lastSecondaryMessage);

    // No response yet
    TEST_ASSERT_EQUAL_INT(0, g_responseCount);

    // Simulate SECONDARY battery response
    g_menu->handleSecondaryBatteryResponse(3.92f);

    // Response should include all INFO fields
    TEST_ASSERT_EQUAL_INT(1, g_responseCount);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "ROLE:PRIMARY") != nullptr);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "BATP:4.10") != nullptr);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "BATS:3.92") != nullptr);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "STATUS:IDLE") != nullptr);
}

void test_handleBattery_timeout_returns_zero() {
    // When no BATRESPONSE arrives within 1000ms, checkSecondaryBatteryTimeout
    // should complete the deferred response with BATS:0.00

    // Set up SECONDARY connection callback (triggers deferred path)
    g_menu->setSendToSecondaryCallback(testSendToSecondaryCallback);

    // Set PRIMARY battery voltage
    g_battery->_status.voltage = 3.85f;

    // Call handleBattery - defers waiting for SECONDARY response
    g_menu->handleBattery();

    // Verify GET_BATTERY was sent to SECONDARY
    TEST_ASSERT_EQUAL_STRING("GET_BATTERY", g_lastSecondaryMessage);

    // No response sent yet (deferred, waiting for BATRESPONSE)
    TEST_ASSERT_EQUAL_INT(0, g_responseCount);

    // Check timeout before 1000ms - should NOT trigger
    mockAdvanceMillis(999);
    g_menu->checkSecondaryBatteryTimeout();
    TEST_ASSERT_EQUAL_INT(0, g_responseCount);

    // Advance past the 1000ms timeout threshold
    mockAdvanceMillis(2);  // Now at 1001ms total
    g_menu->checkSecondaryBatteryTimeout();

    // Response should now be sent with BATS:0.00 fallback
    TEST_ASSERT_EQUAL_INT(1, g_responseCount);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "BATP:3.85") != nullptr);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "BATS:0.00") != nullptr);
}

void test_handleInfo_includes_motors_and_profile() {
    // INFO response must expose motor count and the active profile so the
    // app can detect board type and stay in sync with the current profile.
    char expected[32];
    snprintf(expected, sizeof(expected), "MOTORS:%d", MAX_ACTUATORS);

    g_menu->handleInfo();

    TEST_ASSERT_EQUAL_INT(1, g_responseCount);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, expected) != nullptr);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "PROFILE:1:Default") != nullptr);
}

void test_handleInfo_timeout_returns_zero_with_status() {
    // When no BATRESPONSE arrives within 1000ms during INFO command,
    // checkSecondaryBatteryTimeout should complete with BATS:0.00 and include STATUS

    g_menu->setSendToSecondaryCallback(testSendToSecondaryCallback);
    g_battery->_status.voltage = 4.05f;

    // Call handleInfo - defers waiting for SECONDARY response
    g_menu->handleInfo();

    // No response yet
    TEST_ASSERT_EQUAL_INT(0, g_responseCount);

    // Advance past timeout
    mockAdvanceMillis(1001);
    g_menu->checkSecondaryBatteryTimeout();

    // Response should include all INFO fields with BATS:0.00 fallback
    TEST_ASSERT_EQUAL_INT(1, g_responseCount);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "ROLE:PRIMARY") != nullptr);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "BATP:4.05") != nullptr);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "BATS:0.00") != nullptr);
    TEST_ASSERT_TRUE(strstr(g_lastResponse, "STATUS:IDLE") != nullptr);
}

void test_profileLoad_rejected_while_running(void) {
    g_stateMachine->_state = TherapyState::RUNNING;

    g_menu->handleCommand("PROFILE_LOAD:2");

    TEST_ASSERT_TRUE(strstr(g_lastResponse, "ERROR:Session must be stopped") != nullptr);
    TEST_ASSERT_EQUAL_INT(0, g_restartCount);

    g_stateMachine->_state = TherapyState::IDLE;
}

void test_profileLoad_rejected_while_paused(void) {
    g_stateMachine->_state = TherapyState::PAUSED;

    g_menu->handleCommand("PROFILE_LOAD:2");

    TEST_ASSERT_TRUE(strstr(g_lastResponse, "ERROR:Session must be stopped") != nullptr);
    TEST_ASSERT_EQUAL_INT(0, g_restartCount);

    g_stateMachine->_state = TherapyState::IDLE;
}

void test_profileLoad_allowed_while_idle(void) {
    g_stateMachine->_state = TherapyState::IDLE;

    g_menu->handleCommand("PROFILE_LOAD:1");

    TEST_ASSERT_TRUE(strstr(g_lastResponse, "STATUS:REBOOTING") != nullptr);
    TEST_ASSERT_EQUAL_INT(1, g_restartCount);
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Internal message tests
    RUN_TEST(test_isInternalMessage_null_returns_false);
    RUN_TEST(test_isInternalMessage_empty_returns_false);
    RUN_TEST(test_isInternalMessage_PARAM_UPDATE_returns_true);
    RUN_TEST(test_isInternalMessage_SEED_returns_true);
    RUN_TEST(test_isInternalMessage_SEED_ACK_returns_true);
    RUN_TEST(test_isInternalMessage_GET_BATTERY_returns_true);
    RUN_TEST(test_isInternalMessage_BATRESPONSE_returns_true);
    RUN_TEST(test_isInternalMessage_ACK_PARAM_UPDATE_returns_true);
    RUN_TEST(test_isInternalMessage_SYNC_prefix_returns_true);
    RUN_TEST(test_isInternalMessage_IDENTIFY_prefix_returns_true);
    RUN_TEST(test_isInternalMessage_user_command_INFO_returns_false);
    RUN_TEST(test_isInternalMessage_user_command_BATTERY_returns_false);
    RUN_TEST(test_isInternalMessage_user_command_SESSION_START_returns_false);
    RUN_TEST(test_isInternalMessage_PING_returns_true);
    RUN_TEST(test_isInternalMessage_partial_match_not_prefix_returns_false);
    RUN_TEST(test_isInternalMessage_case_sensitive);

    // Command parsing tests
    RUN_TEST(test_parseCommand_null_message_returns_false);
    RUN_TEST(test_parseCommand_empty_message_returns_false);
    RUN_TEST(test_parseCommand_whitespace_only_returns_false);
    RUN_TEST(test_parseCommand_simple_command_no_params);
    RUN_TEST(test_parseCommand_converts_to_uppercase);
    RUN_TEST(test_parseCommand_mixed_case_to_uppercase);
    RUN_TEST(test_parseCommand_single_param);
    RUN_TEST(test_parseCommand_multiple_params);
    RUN_TEST(test_parseCommand_strips_newline);
    RUN_TEST(test_parseCommand_strips_carriage_return);
    RUN_TEST(test_parseCommand_strips_crlf);
    RUN_TEST(test_parseCommand_strips_EOT);
    RUN_TEST(test_parseCommand_trims_leading_whitespace);
    RUN_TEST(test_parseCommand_max_params);
    RUN_TEST(test_parseCommand_key_value_pairs);

    // Response formatting tests
    RUN_TEST(test_beginResponse_clears_buffer);
    RUN_TEST(test_addResponseLine_string_value);
    RUN_TEST(test_addResponseLine_null_value);
    RUN_TEST(test_addResponseLine_integer_value);
    RUN_TEST(test_addResponseLine_negative_integer);
    RUN_TEST(test_addResponseLine_float_default_decimals);
    RUN_TEST(test_addResponseLine_float_custom_decimals);
    RUN_TEST(test_addResponseLine_multiple_lines);
    RUN_TEST(test_sendResponse_adds_EOT);
    RUN_TEST(test_sendResponse_invokes_callback);
    RUN_TEST(test_sendError_formats_correctly);

    RUN_TEST(test_handleCommand_null_returns_false);
    RUN_TEST(test_handleCommand_empty_returns_false);
    RUN_TEST(test_handleCommand_PING_without_allowInternal_is_swallowed);
    RUN_TEST(test_handleCommand_PING_with_allowInternal_dispatches_PONG);
    RUN_TEST(test_handleCommand_allowInternal_defaults_to_false);

    // Device info tests
    RUN_TEST(test_setDeviceInfo_updates_role);
    RUN_TEST(test_setDeviceInfo_updates_firmware_version);
    RUN_TEST(test_setDeviceInfo_updates_device_name);
    RUN_TEST(test_setDeviceInfo_truncates_long_version);
    RUN_TEST(test_setDeviceInfo_truncates_long_name);

    // Calibration state tests
    RUN_TEST(test_isCalibrating_initially_false);
    RUN_TEST(test_startCalibration_sets_calibrating_true);
    RUN_TEST(test_startCalibration_records_start_time);
    RUN_TEST(test_stopCalibration_sets_calibrating_false);
    RUN_TEST(test_calibrateBuzz_does_not_block);
    RUN_TEST(test_calibrateBuzz_secondary_finger_relayed);

    // Callback tests
    RUN_TEST(test_sendCallback_not_invoked_when_null);
    RUN_TEST(test_restartCallback_stored);

    // Initialization tests
    RUN_TEST(test_begin_stores_component_references);
    RUN_TEST(test_begin_allows_null_profile_manager);
    RUN_TEST(test_default_firmware_version);
    RUN_TEST(test_default_device_name);
    RUN_TEST(test_default_role_is_primary);

    // Secondary battery query tests
    RUN_TEST(test_handleBattery_returns_secondary_voltage);
    RUN_TEST(test_handleBattery_returns_zero_when_secondary_unavailable);
    RUN_TEST(test_handleInfo_includes_secondary_voltage);
    RUN_TEST(test_handleInfo_includes_motors_and_profile);
    RUN_TEST(test_handleBattery_timeout_returns_zero);
    RUN_TEST(test_handleInfo_timeout_returns_zero_with_status);
    RUN_TEST(test_profileLoad_rejected_while_running);
    RUN_TEST(test_profileLoad_rejected_while_paused);
    RUN_TEST(test_profileLoad_allowed_while_idle);

    return UNITY_END();
}
