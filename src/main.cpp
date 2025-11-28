/**
 * @file main.cpp
 * @brief BlueBuzzah Firmware - Main Application
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * Therapy engine with pattern generation and execution:
 * - PRIMARY mode: Generates patterns and sends to SECONDARY
 * - SECONDARY mode: Receives and executes buzz commands
 * - Pattern types: RNDP, Sequential, Mirrored
 * - BLE synchronization between devices
 *
 * Configuration:
 * - Define DEVICE_ROLE_PRIMARY or DEVICE_ROLE_SECONDARY before building
 * - Or hold USER button during boot for SECONDARY mode
 * - Send "START" command via BLE to begin therapy test
 */

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "hardware.h"
#include "ble_manager.h"
#include "sync_protocol.h"
#include "therapy_engine.h"
#include "state_machine.h"
#include "menu_controller.h"
#include "profile_manager.h"

// =============================================================================
// CONFIGURATION
// =============================================================================

// USER button pin (active LOW on Feather nRF52840)
#define USER_BUTTON_PIN 7

// =============================================================================
// GLOBAL INSTANCES
// =============================================================================

HapticController haptic;
BatteryMonitor battery;
LEDController led;
BLEManager ble;
TherapyEngine therapy;
TherapyStateMachine stateMachine;
MenuController menu;
ProfileManager profiles;

// =============================================================================
// STATE VARIABLES
// =============================================================================

DeviceRole deviceRole = DeviceRole::PRIMARY;
bool hardwareReady = false;
bool bleReady = false;

// Timing
uint32_t lastBatteryCheck = 0;
uint32_t lastHeartbeat = 0;
uint32_t lastStatusPrint = 0;
uint32_t heartbeatSequence = 0;

// Connection state
bool wasConnected = false;

// Therapy state tracking (for detecting session end)
bool wasTherapyRunning = false;

// SECONDARY heartbeat monitoring
uint32_t lastHeartbeatReceived = 0;  // Tracks last heartbeat from PRIMARY

// Finger names for display
const char* FINGER_NAMES[] = { "Thumb", "Index", "Middle", "Ring", "Pinky" };

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

void printBanner();
DeviceRole determineRole();
bool initializeHardware();
bool initializeBLE();
bool initializeTherapy();
void sendHeartbeat();
void printStatus();
void startTherapyTest();
void stopTherapyTest();

// BLE Callbacks
void onBLEConnect(uint16_t connHandle, ConnectionType type);
void onBLEDisconnect(uint16_t connHandle, ConnectionType type, uint8_t reason);
void onBLEMessage(uint16_t connHandle, const char* message);

// Therapy Callbacks
void onSendCommand(const char* commandType, uint8_t leftFinger, uint8_t rightFinger, uint8_t amplitude, uint32_t seq);
void onActivate(uint8_t finger, uint8_t amplitude);
void onDeactivate(uint8_t finger);
void onCycleComplete(uint32_t cycleCount);

// State Machine Callback
void onStateChange(const StateTransition& transition);

// Menu Controller Callback
void onMenuSendResponse(const char* response);

// SECONDARY Heartbeat Timeout
void handleHeartbeatTimeout();

// Serial-Only Commands (not available via BLE)
void handleSerialCommand(const char* command);

// Role Configuration Wait (blocks until role is set)
void waitForRoleConfiguration();

// =============================================================================
// ROLE CONFIGURATION WAIT
// =============================================================================

/**
 * @brief Block boot and wait for role configuration via Serial
 *
 * Called when device boots without a stored role. Blinks LED orange
 * and waits for SET_ROLE:PRIMARY or SET_ROLE:SECONDARY command.
 * Device auto-reboots after role is saved.
 */
void waitForRoleConfiguration() {
    Serial.println(F("\n========================================"));
    Serial.println(F(" DEVICE NOT CONFIGURED"));
    Serial.println(F("========================================"));
    Serial.println(F("Role not set. Send one of:"));
    Serial.println(F("  SET_ROLE:PRIMARY"));
    Serial.println(F("  SET_ROLE:SECONDARY"));
    Serial.println(F("\nDevice will reboot after configuration."));
    Serial.println(F("========================================\n"));

    bool ledOn = false;
    uint32_t lastBlink = 0;
    const uint32_t BLINK_INTERVAL = 500;

    while (true) {
        // Blink LED orange
        if (millis() - lastBlink >= BLINK_INTERVAL) {
            lastBlink = millis();
            ledOn = !ledOn;
            if (ledOn) {
                led.setColor(LED_COLOR_ORANGE);
            } else {
                led.off();
            }
        }

        // Check for serial input
        if (Serial.available()) {
            String input = Serial.readStringUntil('\n');
            input.trim();

            // Only process SET_ROLE commands
            if (input.startsWith("SET_ROLE:")) {
                handleSerialCommand(input.c_str());
                // handleSerialCommand will reboot after saving
            } else if (input.length() > 0) {
                Serial.println(F("[CONFIG] Only SET_ROLE command accepted."));
                Serial.println(F("  Use: SET_ROLE:PRIMARY or SET_ROLE:SECONDARY"));
            }
        }
    }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    // Initialize serial
    Serial.begin(115200);

    // Configure USER button
    pinMode(USER_BUTTON_PIN, INPUT_PULLUP);

    // Wait for serial with timeout
    uint32_t serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart < 3000)) {
        delay(10);
    }

    printBanner();

    // Initialize LED FIRST (needed for configuration feedback)
    Serial.println(F("\n--- LED Initialization ---"));
    if (led.begin()) {
        led.setColor(Colors::BLUE);
        Serial.println(F("LED: OK"));
    }

    // Initialize Profile Manager (needed for role determination)
    Serial.println(F("\n--- Profile Manager Initialization ---"));
    profiles.begin();
    Serial.printf("[PROFILE] Initialized with %d profiles\n", profiles.getProfileCount());

    // Check if device has a configured role
    if (!profiles.hasStoredRole()) {
        // Block and wait for role configuration via Serial
        waitForRoleConfiguration();
        // Note: waitForRoleConfiguration() never returns - it reboots after role is set
    }

    // Determine device role (from settings or button override)
    deviceRole = determineRole();
    Serial.printf("\n[ROLE] Device configured as: %s\n", deviceRoleToString(deviceRole));

    delay(500);

    // Initialize hardware
    Serial.println(F("\n--- Hardware Initialization ---"));
    hardwareReady = initializeHardware();

    if (hardwareReady) {
        led.setColor(Colors::CYAN);
        Serial.println(F("[SUCCESS] Hardware initialization complete"));
    } else {
        led.setColor(Colors::RED);
        Serial.println(F("[WARNING] Some hardware initialization failed"));
    }

    // Initialize BLE
    Serial.println(F("\n--- BLE Initialization ---"));
    bleReady = initializeBLE();

    if (bleReady) {
        led.setColor(Colors::GREEN);
        Serial.println(F("[SUCCESS] BLE initialization complete"));
    } else {
        led.setColor(Colors::RED);
        Serial.println(F("[FAILURE] BLE initialization failed"));
    }

    // Initialize Therapy Engine
    Serial.println(F("\n--- Therapy Engine Initialization ---"));
    initializeTherapy();
    Serial.println(F("[SUCCESS] Therapy engine initialized"));

    // Initialize State Machine
    Serial.println(F("\n--- State Machine Initialization ---"));
    stateMachine.begin(TherapyState::IDLE);
    stateMachine.onStateChange(onStateChange);
    Serial.println(F("[SUCCESS] State machine initialized"));

    // Initialize Menu Controller
    Serial.println(F("\n--- Menu Controller Initialization ---"));
    menu.begin(&therapy, &battery, &haptic, &stateMachine, &profiles);
    menu.setDeviceInfo(deviceRole, FIRMWARE_VERSION, BLE_NAME);
    menu.setSendCallback(onMenuSendResponse);
    Serial.println(F("[SUCCESS] Menu controller initialized"));

    // Initial battery reading
    Serial.println(F("\n--- Battery Status ---"));
    BatteryStatus battStatus = battery.getStatus();
    Serial.printf("[BATTERY] %.2fV | %d%% | Status: %s\n",
                  battStatus.voltage, battStatus.percentage, battStatus.statusString());

    // Instructions
    Serial.println(F("\n+============================================================+"));
    if (deviceRole == DeviceRole::PRIMARY) {
        Serial.println(F("|  PRIMARY MODE - Advertising as 'BlueBuzzah'              |"));
        Serial.println(F("|  Send 'TEST' via BLE to start 30-second therapy test     |"));
        Serial.println(F("|  Send 'STOP' via BLE to stop therapy                     |"));
    } else {
        Serial.println(F("|  SECONDARY MODE - Scanning for 'BlueBuzzah'              |"));
        Serial.println(F("|  Will execute BUZZ commands from PRIMARY                 |"));
    }
    Serial.println(F("+============================================================+"));
    Serial.println(F("|  Heartbeat sent every 2 seconds when connected            |"));
    Serial.println(F("|  Status printed every 5 seconds                           |"));
    Serial.println(F("+============================================================+\n"));
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
    uint32_t now = millis();

    // Process BLE events
    ble.update();

    // Process Serial commands (uses serial-only handler for SET_ROLE, GET_ROLE)
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            Serial.printf("[SERIAL] Command: %s\n", input.c_str());
            handleSerialCommand(input.c_str());
        }
    }

    // Update therapy engine (both roles - PRIMARY generates patterns for sync,
    // SECONDARY needs this for standalone hardware tests)
    therapy.update();

    // Detect when therapy session ends (for resuming scanning on SECONDARY)
    bool isTherapyRunning = therapy.isRunning();
    if (wasTherapyRunning && !isTherapyRunning) {
        // Therapy just stopped
        Serial.println(F("\n+============================================================+"));
        Serial.println(F("|  THERAPY TEST COMPLETE                                     |"));
        Serial.println(F("+============================================================+\n"));

        haptic.emergencyStop();
        stateMachine.transition(StateTrigger::STOP_SESSION);
        stateMachine.transition(StateTrigger::STOPPED);

        // Resume scanning on SECONDARY after standalone test
        if (deviceRole == DeviceRole::SECONDARY && !ble.isPrimaryConnected()) {
            Serial.println(F("[TEST] Resuming scanning..."));
            ble.setScannerAutoRestart(true);  // Re-enable health check
            ble.startScanning(BLE_NAME);
        }
    }
    wasTherapyRunning = isTherapyRunning;

    // SECONDARY: Check for heartbeat timeout during active connection
    if (deviceRole == DeviceRole::SECONDARY && ble.isPrimaryConnected()) {
        if (lastHeartbeatReceived > 0 &&
            (millis() - lastHeartbeatReceived > HEARTBEAT_TIMEOUT_MS)) {
            handleHeartbeatTimeout();
        }
    }

    // Check connection state changes
    bool isConnected = (deviceRole == DeviceRole::PRIMARY) ?
                       ble.isSecondaryConnected() :
                       ble.isPrimaryConnected();

    if (isConnected != wasConnected) {
        wasConnected = isConnected;
        if (isConnected) {
            led.setColor(Colors::GREEN);
            Serial.println(F("[STATE] Connected!"));
        } else {
            led.setColor(Colors::BLUE);
            Serial.println(F("[STATE] Disconnected"));
        }
    }

    // Send heartbeat every 2 seconds when connected
    if (isConnected && (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS)) {
        lastHeartbeat = now;
        sendHeartbeat();
    }

    // Print status every 5 seconds
    if (now - lastStatusPrint >= 5000) {
        lastStatusPrint = now;
        printStatus();
    }

    // Check battery every 60 seconds
    if (now - lastBatteryCheck >= BATTERY_CHECK_INTERVAL_MS) {
        lastBatteryCheck = now;
        BatteryStatus status = battery.getStatus();
        Serial.printf("[BATTERY] %.2fV | %d%% | Status: %s\n",
                      status.voltage, status.percentage, status.statusString());
    }

    // Small delay to prevent busy-looping
    delay(10);
}

// =============================================================================
// INITIALIZATION FUNCTIONS
// =============================================================================

void printBanner() {
    Serial.println(F("\n"));
    Serial.println(F("+============================================================+"));
    Serial.println(F("|                  BlueBuzzah Firmware                       |"));
    Serial.println(F("+============================================================+"));
    Serial.printf( "|  Firmware: %-47s |\n", FIRMWARE_VERSION);
    Serial.println(F("|  Platform: Adafruit Feather nRF52840 Express              |"));
    Serial.println(F("+============================================================+"));
}

DeviceRole determineRole() {
    // Check if USER button is held (active LOW)
    // Button held = SECONDARY mode (emergency override)
    if (digitalRead(USER_BUTTON_PIN) == LOW) {
        Serial.println(F("[INFO] USER button held - forcing SECONDARY mode"));
        delay(500);  // Debounce
        return DeviceRole::SECONDARY;
    }

    // Check if role was loaded from settings.json
    if (profiles.hasStoredRole()) {
        Serial.println(F("[INFO] Using role from settings.json"));
        return profiles.getDeviceRole();
    }

    // Default to PRIMARY if no settings found
    Serial.println(F("[INFO] No role in settings - defaulting to PRIMARY"));
    return DeviceRole::PRIMARY;
}

bool initializeHardware() {
    bool success = true;

    // Initialize haptic controller
    Serial.println(F("\nInitializing Haptic Controller..."));
    if (!haptic.begin()) {
        Serial.println(F("[ERROR] Haptic controller initialization failed"));
        success = false;
    } else {
        Serial.printf("Haptic Controller: %d/%d fingers enabled\n",
                      haptic.getEnabledCount(), MAX_ACTUATORS);
    }

    // Initialize battery monitor
    Serial.println(F("\nInitializing Battery Monitor..."));
    if (!battery.begin()) {
        Serial.println(F("[ERROR] Battery monitor initialization failed"));
        success = false;
    } else {
        Serial.println(F("Battery Monitor: OK"));
    }

    return success;
}

bool initializeBLE() {
    // Set up BLE callbacks
    ble.setConnectCallback(onBLEConnect);
    ble.setDisconnectCallback(onBLEDisconnect);
    ble.setMessageCallback(onBLEMessage);

    // Initialize BLE with appropriate role
    if (!ble.begin(deviceRole, BLE_NAME)) {
        Serial.println(F("[ERROR] BLE begin() failed"));
        return false;
    }

    // Start advertising or scanning based on role
    if (deviceRole == DeviceRole::PRIMARY) {
        if (!ble.startAdvertising()) {
            Serial.println(F("[ERROR] Failed to start advertising"));
            return false;
        }
        Serial.println(F("[BLE] Advertising started"));
    } else {
        if (!ble.startScanning(BLE_NAME)) {
            Serial.println(F("[ERROR] Failed to start scanning"));
            return false;
        }
        Serial.println(F("[BLE] Scanning started"));
    }

    return true;
}

bool initializeTherapy() {
    // Set local motor callbacks (both roles need these for standalone tests)
    therapy.setActivateCallback(onActivate);
    therapy.setDeactivateCallback(onDeactivate);
    therapy.setCycleCompleteCallback(onCycleComplete);

    // Set BLE sync callback (PRIMARY only - sends commands to SECONDARY)
    if (deviceRole == DeviceRole::PRIMARY) {
        therapy.setSendCommandCallback(onSendCommand);
    }
    return true;
}

// =============================================================================
// BLE EVENT HANDLERS
// =============================================================================

void sendHeartbeat() {
    heartbeatSequence++;

    // Create heartbeat command
    SyncCommand cmd = SyncCommand::createHeartbeat(heartbeatSequence);

    // Serialize
    char buffer[128];
    if (cmd.serialize(buffer, sizeof(buffer))) {
        // Send based on role
        bool sent = false;
        if (deviceRole == DeviceRole::PRIMARY) {
            sent = ble.sendToSecondary(buffer);
        } else {
            sent = ble.sendToPrimary(buffer);
        }

        if (sent) {
            Serial.printf("[TX] %s\n", buffer);
        }
    }
}

void printStatus() {
    Serial.println(F("------------------------------------------------------------"));

    // Line 1: Role and State
    Serial.printf("[STATUS] Role: %s | State: %s\n",
                  deviceRoleToString(deviceRole),
                  therapyStateToString(stateMachine.getCurrentState()));

    // Line 2: BLE activity and connections
    if (deviceRole == DeviceRole::PRIMARY) {
        Serial.printf("[BLE] Advertising: %s | Connections: %d\n",
                      ble.isAdvertising() ? "YES" : "NO",
                      ble.getConnectionCount());
        Serial.printf("[CONN] SECONDARY: %s | Phone: %s\n",
                      ble.isSecondaryConnected() ? "Connected" : "Waiting...",
                      ble.isPhoneConnected() ? "Connected" : "Waiting...");
    } else {
        // SECONDARY mode
        Serial.printf("[BLE] Scanning: %s | Connections: %d\n",
                      ble.isScanning() ? "YES" : "NO",
                      ble.getConnectionCount());

        if (ble.isPrimaryConnected()) {
            uint32_t timeSinceHB = millis() - lastHeartbeatReceived;
            Serial.printf("[CONN] PRIMARY: Connected | Last HB: %lums ago\n", timeSinceHB);
        } else {
            Serial.println(F("[CONN] PRIMARY: Searching..."));
        }
    }

    Serial.println(F("------------------------------------------------------------"));
}

// =============================================================================
// BLE CALLBACKS
// =============================================================================

void onBLEConnect(uint16_t connHandle, ConnectionType type) {
    const char* typeStr = "UNKNOWN";
    switch (type) {
        case ConnectionType::UNKNOWN: typeStr = "UNKNOWN"; break;
        case ConnectionType::PHONE: typeStr = "PHONE"; break;
        case ConnectionType::SECONDARY: typeStr = "SECONDARY"; break;
        case ConnectionType::PRIMARY: typeStr = "PRIMARY"; break;
        default: break;
    }

    Serial.printf("[CONNECT] Handle: %d, Type: %s\n", connHandle, typeStr);

    // If SECONDARY device connected to PRIMARY, send identification
    if (deviceRole == DeviceRole::SECONDARY && type == ConnectionType::PRIMARY) {
        Serial.println(F("[SECONDARY] Sending IDENTIFY:SECONDARY to PRIMARY"));
        ble.sendToPrimary("IDENTIFY:SECONDARY");
        // Start heartbeat timeout tracking
        lastHeartbeatReceived = millis();
    }

    // Update state machine on relevant connections
    if ((deviceRole == DeviceRole::PRIMARY && type == ConnectionType::SECONDARY) ||
        (deviceRole == DeviceRole::SECONDARY && type == ConnectionType::PRIMARY)) {
        stateMachine.transition(StateTrigger::CONNECTED);
    }

    // Quick haptic feedback on thumb
    if (haptic.isEnabled(FINGER_THUMB)) {
        haptic.activate(FINGER_THUMB, 30);
        delay(50);
        haptic.deactivate(FINGER_THUMB);
    }
}

void onBLEDisconnect(uint16_t connHandle, ConnectionType type, uint8_t reason) {
    const char* typeStr = "UNKNOWN";
    switch (type) {
        case ConnectionType::PHONE: typeStr = "PHONE"; break;
        case ConnectionType::SECONDARY: typeStr = "SECONDARY"; break;
        case ConnectionType::PRIMARY: typeStr = "PRIMARY"; break;
        default: break;
    }

    Serial.printf("[DISCONNECT] Handle: %d, Type: %s, Reason: 0x%02X\n",
                  connHandle, typeStr, reason);

    // Update state machine on relevant disconnections
    if ((deviceRole == DeviceRole::PRIMARY && type == ConnectionType::SECONDARY) ||
        (deviceRole == DeviceRole::SECONDARY && type == ConnectionType::PRIMARY)) {
        stateMachine.transition(StateTrigger::DISCONNECTED);
    } else if (deviceRole == DeviceRole::PRIMARY && type == ConnectionType::PHONE) {
        stateMachine.transition(StateTrigger::PHONE_LOST);
    }

    // Double haptic pulse on thumb
    if (haptic.isEnabled(FINGER_THUMB)) {
        haptic.activate(FINGER_THUMB, 50);
        delay(50);
        haptic.deactivate(FINGER_THUMB);
        delay(100);
        haptic.activate(FINGER_THUMB, 50);
        delay(50);
        haptic.deactivate(FINGER_THUMB);
    }
}

void onBLEMessage(uint16_t connHandle, const char* message) {
    Serial.printf("[RX] Handle: %d, Message: %s\n", connHandle, message);

    // Check for simple text commands first (for testing)
    // Both PRIMARY and SECONDARY can run standalone tests for hardware verification
    if (strcmp(message, "TEST") == 0 || strcmp(message, "test") == 0) {
        startTherapyTest();
        return;
    }

    if (strcmp(message, "STOP") == 0 || strcmp(message, "stop") == 0) {
        stopTherapyTest();
        return;
    }

    // Try menu controller first for phone/BLE commands (PRIMARY only)
    if (deviceRole == DeviceRole::PRIMARY && !menu.isInternalMessage(message)) {
        if (menu.handleCommand(message)) {
            return;  // Command handled by menu controller
        }
    }

    // Parse sync/internal commands
    SyncCommand cmd;
    if (cmd.deserialize(message)) {
        Serial.printf("[CMD] Type: %s, Seq: %lu, Time: %llu\n",
                      cmd.getTypeString(),
                      (unsigned long)cmd.getSequenceId(),
                      (unsigned long long)cmd.getTimestamp());

        // Handle specific command types
        switch (cmd.getType()) {
            case SyncCommandType::HEARTBEAT:
                // Flash LED briefly
                led.setColor(Colors::CYAN);
                delay(50);
                led.setColor(Colors::GREEN);
                // Track heartbeat for SECONDARY timeout detection
                if (deviceRole == DeviceRole::SECONDARY) {
                    lastHeartbeatReceived = millis();
                }
                break;

            case SyncCommandType::EXECUTE_BUZZ:
                {
                    int32_t finger = cmd.getDataInt("finger", -1);
                    int32_t amplitude = cmd.getDataInt("amplitude", 50);

                    if (finger >= 0 && finger < MAX_ACTUATORS) {
                        Serial.printf("[BUZZ] Finger: %s, Amplitude: %d%%\n",
                                      FINGER_NAMES[finger], amplitude);

                        if (haptic.isEnabled(finger)) {
                            haptic.activate(finger, amplitude);
                            delay(100);
                            haptic.deactivate(finger);

                            // Send completion acknowledgment
                            SyncCommand ack = SyncCommand::createBuzzComplete(cmd.getSequenceId());
                            char ackBuffer[64];
                            if (ack.serialize(ackBuffer, sizeof(ackBuffer))) {
                                if (deviceRole == DeviceRole::SECONDARY) {
                                    ble.sendToPrimary(ackBuffer);
                                }
                            }
                        }
                    }
                }
                break;

            case SyncCommandType::START_SESSION:
                Serial.println(F("[SESSION] Start requested"));
                led.setColor(Colors::GREEN);
                break;

            case SyncCommandType::PAUSE_SESSION:
                Serial.println(F("[SESSION] Pause requested"));
                led.setColor(Colors::YELLOW);
                break;

            case SyncCommandType::RESUME_SESSION:
                Serial.println(F("[SESSION] Resume requested"));
                led.setColor(Colors::GREEN);
                break;

            case SyncCommandType::STOP_SESSION:
                Serial.println(F("[SESSION] Stop requested"));
                haptic.emergencyStop();
                led.setColor(Colors::BLUE);
                break;

            default:
                break;
        }
    }
}

// =============================================================================
// THERAPY CALLBACKS
// =============================================================================

void onSendCommand(const char* commandType, uint8_t leftFinger, uint8_t rightFinger, uint8_t amplitude, uint32_t seq) {
    // Create and send EXECUTE_BUZZ command to SECONDARY
    SyncCommand cmd(SyncCommandType::EXECUTE_BUZZ, seq);
    cmd.setData("finger", (int32_t)leftFinger);  // SECONDARY only needs one finger
    cmd.setData("amplitude", (int32_t)amplitude);

    char buffer[128];
    if (cmd.serialize(buffer, sizeof(buffer))) {
        if (ble.sendToSecondary(buffer)) {
            Serial.printf("[TX->SEC] %s (L:%d R:%d)\n", commandType, leftFinger, rightFinger);
        }
    }
}

void onActivate(uint8_t finger, uint8_t amplitude) {
    // Activate local motor (PRIMARY side)
    if (haptic.isEnabled(finger)) {
        haptic.activate(finger, amplitude);
    }
}

void onDeactivate(uint8_t finger) {
    // Deactivate local motor
    if (haptic.isEnabled(finger)) {
        haptic.deactivate(finger);
    }
}

void onCycleComplete(uint32_t cycleCount) {
    Serial.printf("[THERAPY] Cycle %lu complete\n", cycleCount);

    // Flash LED purple briefly
    led.setColor(Colors::PURPLE);
    delay(50);
    led.setColor(Colors::GREEN);
}

// =============================================================================
// THERAPY TEST FUNCTIONS
// =============================================================================

void startTherapyTest() {
    if (therapy.isRunning()) {
        Serial.println(F("[TEST] Therapy already running"));
        return;
    }

    // Get current profile
    const TherapyProfile* profile = profiles.getCurrentProfile();
    if (!profile) {
        Serial.println(F("[TEST] No profile loaded!"));
        return;
    }

    // Convert pattern type string to constant
    uint8_t patternType = PATTERN_TYPE_RNDP;
    if (strcmp(profile->patternType, "sequential") == 0) {
        patternType = PATTERN_TYPE_SEQUENTIAL;
    } else if (strcmp(profile->patternType, "mirrored") == 0) {
        patternType = PATTERN_TYPE_MIRRORED;
    }

    // Stop scanning during standalone test (SECONDARY only)
    if (deviceRole == DeviceRole::SECONDARY) {
        ble.setScannerAutoRestart(false);  // Prevent health check from restarting
        ble.stopScanning();
        Serial.println(F("[TEST] Scanning paused for standalone test"));
    }

    Serial.println(F("\n+============================================================+"));
    Serial.println(F("|  STARTING 5-MINUTE THERAPY TEST  (send STOP to end)      |"));
    Serial.printf("|  Profile: %-46s |\n", profile->name);
    Serial.printf("|  Pattern: %-4s | Jitter: %5.1f%% | Mirror: %-3s             |\n",
                  profile->patternType, profile->jitterPercent,
                  profile->mirrorPattern ? "ON" : "OFF");
    Serial.println(F("+============================================================+\n"));

    // Update state machine
    stateMachine.transition(StateTrigger::START_SESSION);

    // Start 5-minute test using profile settings (send STOP to end early)
    therapy.startSession(
        300,                        // 5 minutes (300 seconds)
        patternType,
        profile->timeOnMs,
        profile->timeOffMs,
        profile->jitterPercent,
        profile->numFingers,
        profile->mirrorPattern
    );
}

void stopTherapyTest() {
    if (!therapy.isRunning()) {
        Serial.println(F("[TEST] Therapy not running"));
        return;
    }

    Serial.println(F("\n+============================================================+"));
    Serial.println(F("|  STOPPING THERAPY TEST                                     |"));
    Serial.println(F("+============================================================+\n"));

    therapy.stop();
    haptic.emergencyStop();

    // Update state machine
    stateMachine.transition(StateTrigger::STOP_SESSION);
    stateMachine.transition(StateTrigger::STOPPED);

    // Resume scanning after standalone test (SECONDARY only)
    if (deviceRole == DeviceRole::SECONDARY) {
        Serial.println(F("[TEST] Resuming scanning..."));
        ble.setScannerAutoRestart(true);  // Re-enable health check
        ble.startScanning(BLE_NAME);
    }
}

// =============================================================================
// STATE MACHINE CALLBACK
// =============================================================================

void onStateChange(const StateTransition& transition) {
    // Update LED based on new state
    switch (transition.toState) {
        case TherapyState::IDLE:
            led.setColor(Colors::BLUE);
            break;

        case TherapyState::CONNECTING:
            led.setColor(Colors::CYAN);
            break;

        case TherapyState::READY:
            led.setColor(Colors::GREEN);
            break;

        case TherapyState::RUNNING:
            led.setColor(Colors::GREEN);
            break;

        case TherapyState::PAUSED:
            led.setColor(Colors::YELLOW);
            break;

        case TherapyState::STOPPING:
            led.setColor(Colors::ORANGE);
            break;

        case TherapyState::ERROR:
        case TherapyState::CRITICAL_BATTERY:
            led.setColor(Colors::RED);
            // Emergency stop on error
            haptic.emergencyStop();
            therapy.stop();
            break;

        case TherapyState::LOW_BATTERY:
            led.setColor(Colors::ORANGE);
            break;

        case TherapyState::CONNECTION_LOST:
            led.setColor(Colors::RED);
            // Stop therapy on connection loss
            if (therapy.isRunning()) {
                therapy.stop();
                haptic.emergencyStop();
            }
            break;

        case TherapyState::PHONE_DISCONNECTED:
            // Informational only - keep current LED
            break;

        default:
            break;
    }
}

// =============================================================================
// MENU CONTROLLER CALLBACK
// =============================================================================

void onMenuSendResponse(const char* response) {
    // Send response to phone (or whoever sent the command)
    if (ble.isPhoneConnected()) {
        ble.sendToPhone(response);
    }
}

// =============================================================================
// SECONDARY HEARTBEAT TIMEOUT HANDLER
// =============================================================================

void handleHeartbeatTimeout() {
    Serial.println(F("[WARN] Heartbeat timeout - PRIMARY connection lost"));

    // 1. Safety first - stop all motors immediately
    haptic.emergencyStop();
    therapy.stop();

    // 2. Update state machine
    stateMachine.transition(StateTrigger::DISCONNECTED);

    // 3. Visual indicator
    led.setColor(Colors::RED);

    // 4. Attempt reconnection (3 attempts, 2s apart)
    for (uint8_t attempt = 1; attempt <= 3; attempt++) {
        Serial.printf("[RECOVERY] Attempt %d/3...\n", attempt);
        delay(2000);

        if (ble.isPrimaryConnected()) {
            Serial.println(F("[RECOVERY] PRIMARY reconnected"));
            stateMachine.transition(StateTrigger::RECONNECTED);
            led.setColor(Colors::GREEN);
            lastHeartbeatReceived = millis();  // Reset timeout
            return;
        }
    }

    // 5. Recovery failed - return to IDLE
    Serial.println(F("[RECOVERY] Failed - returning to IDLE"));
    stateMachine.transition(StateTrigger::RECONNECT_FAILED);
    led.setColor(Colors::BLUE);
    lastHeartbeatReceived = 0;  // Reset for next session

    // 6. Restart scanning for PRIMARY
    ble.startScanning(BLE_NAME);
}

// =============================================================================
// SERIAL-ONLY COMMANDS
// =============================================================================

void handleSerialCommand(const char* command) {
    // SET_ROLE - one-time device configuration (serial only for security)
    if (strncmp(command, "SET_ROLE:", 9) == 0) {
        const char* roleStr = command + 9;
        if (strcasecmp(roleStr, "PRIMARY") == 0) {
            profiles.setDeviceRole(DeviceRole::PRIMARY);
            profiles.saveSettings();
            Serial.println(F("[CONFIG] Role set to PRIMARY - restarting..."));
            delay(500);
            NVIC_SystemReset();
        } else if (strcasecmp(roleStr, "SECONDARY") == 0) {
            profiles.setDeviceRole(DeviceRole::SECONDARY);
            profiles.saveSettings();
            Serial.println(F("[CONFIG] Role set to SECONDARY - restarting..."));
            delay(500);
            NVIC_SystemReset();
        } else {
            Serial.println(F("[ERROR] Invalid role. Use: SET_ROLE:PRIMARY or SET_ROLE:SECONDARY"));
        }
        return;
    }

    // GET_ROLE - query current device role
    if (strcmp(command, "GET_ROLE") == 0) {
        Serial.printf("[CONFIG] Current role: %s\n", deviceRoleToString(deviceRole));
        return;
    }

    // REBOOT - restart the device
    if (strcmp(command, "REBOOT") == 0) {
        Serial.println(F("[CONFIG] Rebooting..."));
        delay(500);
        NVIC_SystemReset();
        return;
    }

    // Not a serial-only command, pass to regular BLE message handler
    onBLEMessage(0, command);
}
