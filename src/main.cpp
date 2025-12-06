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
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include "config.h"
#include "types.h"
#include "hardware.h"
#include "ble_manager.h"
#include "sync_protocol.h"
#include "therapy_engine.h"
#include "state_machine.h"
#include "menu_controller.h"
#include "profile_manager.h"
#include "latency_metrics.h"
#include "sync_timer.h"
#include "timer_scheduler.h"
#include "deferred_queue.h"

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
SimpleSyncProtocol syncProtocol;

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

// Boot window auto-start tracking (PRIMARY only)
// When SECONDARY connects but phone doesn't within 30s, auto-start therapy
uint32_t bootWindowStart = 0;    // When SECONDARY connected (starts countdown)
bool bootWindowActive = false;   // Whether we're waiting for phone
bool autoStartTriggered = false; // Prevent repeated auto-starts

// Heartbeat monitoring (bidirectional)
uint32_t lastHeartbeatReceived = 0;      // SECONDARY: Tracks last heartbeat from PRIMARY
uint32_t lastSecondaryHeartbeat = 0;     // PRIMARY: Tracks last heartbeat from SECONDARY

// PRIMARY-side heartbeat timeout (must be < BLE supervision timeout ~4s)
static constexpr uint32_t PRIMARY_HEARTBEAT_TIMEOUT_MS = 2500;  // 2.5 seconds

// PING/PONG latency measurement (PRIMARY only)
uint64_t pingStartTime = 0;               // Timestamp when PING was sent (micros)

// Background latency probing (non-blocking, timer-based)
static uint32_t lastProbeTime = 0;
static constexpr uint32_t PROBE_INTERVAL_MS = 500;  // Probe every 500ms during therapy

// SECONDARY non-blocking motor deactivation
int8_t activeMotorFinger = -1;            // Currently active motor (-1 = none)
uint32_t motorDeactivateTime = 0;         // Time to deactivate motor (millis)

// Safety shutdown flag - set by BLE callback (ISR context), processed by loop
volatile bool safetyShutdownPending = false;

// Finger names for display (4 fingers per hand - index through pinky, no thumb per v1)
const char *FINGER_NAMES[] = {"Index", "Middle", "Ring", "Pinky"};

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
void autoStartTherapy();

// PING/PONG latency measurement (PRIMARY only)
void sendPing();

// BLE Callbacks
void onBLEConnect(uint16_t connHandle, ConnectionType type);
void onBLEDisconnect(uint16_t connHandle, ConnectionType type, uint8_t reason);
void onBLEMessage(uint16_t connHandle, const char *message);

// Therapy Callbacks
void onSendCommand(const char *commandType, uint8_t primaryFinger, uint8_t secondaryFinger, uint8_t amplitude, uint32_t durationMs, uint32_t seq, uint16_t frequencyHz);
void onSetFrequency(uint8_t finger, uint16_t frequencyHz);
void onActivate(uint8_t finger, uint8_t amplitude);
void onDeactivate(uint8_t finger);
void onCycleComplete(uint32_t cycleCount);
void onMacrocycleStart(uint32_t macrocycleCount);

// State Machine Callback
void onStateChange(const StateTransition &transition);

// Menu Controller Callback
void onMenuSendResponse(const char *response);

// SECONDARY Heartbeat Timeout
void handleHeartbeatTimeout();

// Deferred Work Executor
void executeDeferredWork(DeferredWorkType type, uint8_t p1, uint8_t p2, uint32_t p3);

// Scheduler Callbacks (for deferred haptic sequences)
void hapticDeactivateCallback(void* ctx);
void hapticSecondPulseCallback(void* ctx);

// Serial-Only Commands (not available via BLE)
void handleSerialCommand(const char *command);

// Role Configuration Wait (blocks until role is set)
void waitForRoleConfiguration();

// Safety shutdown helper (centralized motor stop sequence)
void safeMotorShutdown();

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
void waitForRoleConfiguration()
{
    Serial.println(F("\n========================================"));
    Serial.println(F(" DEVICE NOT CONFIGURED"));
    Serial.println(F("========================================"));
    Serial.println(F("Role not set. Send one of:"));
    Serial.println(F("  SET_ROLE:PRIMARY"));
    Serial.println(F("  SET_ROLE:SECONDARY"));
    Serial.println(F("\nDevice will reboot after configuration."));
    Serial.println(F("========================================\n"));

    // Use slow blink orange pattern for unconfigured state
    led.setPattern(Colors::ORANGE, LEDPattern::BLINK_SLOW);

    while (true)
    {
        // Update LED pattern animation
        led.update();

        // Check for serial input
        if (Serial.available())
        {
            String input = Serial.readStringUntil('\n');
            input.trim();

            // Only process SET_ROLE commands
            if (input.startsWith("SET_ROLE:"))
            {
                handleSerialCommand(input.c_str());
                // handleSerialCommand will reboot after saving
            }
            else if (input.length() > 0)
            {
                Serial.println(F("[CONFIG] Only SET_ROLE command accepted."));
                Serial.println(F("  Use: SET_ROLE:PRIMARY or SET_ROLE:SECONDARY"));
            }
        }

        delay(10); // Small delay to prevent busy-looping
    }
}

// =============================================================================
// SAFE MOTOR SHUTDOWN
// =============================================================================

/**
 * @brief Centralized safe motor shutdown sequence
 *
 * Called from safety shutdown handler and other stop paths.
 * Order of operations is critical for safety:
 * 1. Stop therapy engine (prevents new motor activations from being generated)
 * 2. Cancel scheduler callbacks (prevents pending motor ops)
 * 3. Clear deferred queue (prevents queued activations)
 * 4. Cancel sync timer (PRIMARY only)
 * 5. Deactivate active motor (SECONDARY - before clearing state)
 * 6. Emergency stop all motors (final safety net)
 */
void safeMotorShutdown()
{
    // 1. Stop therapy engine FIRST - prevents new motor activations
    therapy.stop();

    // 2. Cancel all pending scheduler callbacks
    scheduler.cancelAll();

    // 3. Clear deferred work queue
    deferredQueue.clear();

    // 4. Cancel PRIMARY sync timer
    if (deviceRole == DeviceRole::PRIMARY) {
        syncTimer.cancel();
    }

    // 5. SECONDARY: Deactivate active motor before clearing state
    if (deviceRole == DeviceRole::SECONDARY && activeMotorFinger >= 0) {
        haptic.deactivate(activeMotorFinger);
        activeMotorFinger = -1;
        motorDeactivateTime = 0;
    }

    // 6. Emergency stop all motors
    haptic.emergencyStop();
}

// =============================================================================
// SETUP
// =============================================================================

void setup()
{
    // Configure USB device descriptors (must be before Serial.begin)
    TinyUSBDevice.setManufacturerDescriptor("BlueBuzzah Partners");
    TinyUSBDevice.setProductDescriptor("BlueBuzzah");

    // Initialize serial
    Serial.begin(115200);

    // Configure USER button
    pinMode(USER_BUTTON_PIN, INPUT_PULLUP);

    // Wait for serial with timeout
    uint32_t serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart < 3000))
    {
        delay(10);
    }

    // Early debug - print immediately after serial ready
    Serial.printf("\n[BOOT] Serial ready at millis=%lu\n", (unsigned long)millis());
    Serial.flush();

    printBanner();

    // Initialize LED FIRST (needed for configuration feedback)
    Serial.println(F("\n--- LED Initialization ---"));
    if (led.begin())
    {
        led.setPattern(Colors::BLUE, LEDPattern::BLINK_CONNECT);
        Serial.println(F("LED: OK"));
    }

    // Initialize Profile Manager (needed for role determination)
    Serial.println(F("\n--- Profile Manager Initialization ---"));
    profiles.begin();
    Serial.printf("[PROFILE] Initialized with %d profiles\n", profiles.getProfileCount());

    // Check if device has a configured role
    if (!profiles.hasStoredRole())
    {
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

    if (hardwareReady)
    {
        led.setPattern(Colors::CYAN, LEDPattern::BLINK_CONNECT);
        Serial.println(F("[SUCCESS] Hardware initialization complete"));
    }
    else
    {
        led.setPattern(Colors::RED, LEDPattern::BLINK_SLOW);
        Serial.println(F("[WARNING] Some hardware initialization failed"));
    }

    // Initialize BLE
    Serial.println(F("\n--- BLE Initialization ---"));
    Serial.printf("[DEBUG] About to init BLE as %s\n", deviceRoleToString(deviceRole));
    Serial.flush();
    bleReady = initializeBLE();
    Serial.println(F("[DEBUG] BLE init returned"));
    Serial.flush();

    if (bleReady)
    {
        // Start in IDLE state with breathing blue LED
        led.setPattern(Colors::BLUE, LEDPattern::BREATHE_SLOW);
        Serial.println(F("[SUCCESS] BLE initialization complete"));
    }
    else
    {
        led.setPattern(Colors::RED, LEDPattern::BLINK_SLOW);
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
    menu.begin(&therapy, &battery, &haptic, &stateMachine, &profiles, &ble);
    menu.setDeviceInfo(deviceRole, FIRMWARE_VERSION, BLE_NAME);
    menu.setSendCallback(onMenuSendResponse);
    Serial.println(F("[SUCCESS] Menu controller initialized"));

    // Initialize Deferred Queue (for ISR-safe callback operations)
    deferredQueue.setExecutor(executeDeferredWork);
    Serial.println(F("[SUCCESS] Deferred queue initialized"));

    // Initial battery reading
    Serial.println(F("\n--- Battery Status ---"));
    BatteryStatus battStatus = battery.getStatus();
    Serial.printf("[BATTERY] %.2fV | %d%% | Status: %s\n",
                  battStatus.voltage, battStatus.percentage, battStatus.statusString());

    // Instructions
    Serial.println(F("\n+============================================================+"));
    if (deviceRole == DeviceRole::PRIMARY)
    {
        Serial.println(F("|  PRIMARY MODE - Advertising as 'BlueBuzzah'              |"));
        Serial.println(F("|  Send 'TEST' via BLE to start 30-second therapy test     |"));
        Serial.println(F("|  Send 'STOP' via BLE to stop therapy                     |"));
    }
    else
    {
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

void loop()
{
    // SAFETY FIRST: Check for pending shutdown from BLE disconnect callback
    // Must be at VERY TOP before any motor operations to prevent post-disconnect buzz
    if (safetyShutdownPending) {
        safetyShutdownPending = false;
        safeMotorShutdown();
        Serial.println(F("[SAFETY] Emergency motor shutdown complete"));
    }

    // Process hardware timer activation (microsecond precision sync)
    syncTimer.processPendingActivation();

    // Process millisecond timer callbacks (motor deactivation, deferred sequences)
    scheduler.update();

    // Process deferred work queue (haptic operations from BLE callbacks)
    deferredQueue.processOne();

    uint32_t now = millis();

    // Update LED pattern animation
    led.update();

    // Process BLE events (includes non-blocking TX queue)
    ble.update();

    // SECONDARY: Non-blocking motor deactivation check
    if (deviceRole == DeviceRole::SECONDARY && activeMotorFinger >= 0)
    {
        if (now >= motorDeactivateTime)
        {
            Serial.printf("[DEACTIVATE] F%d (timer)\n", activeMotorFinger);
            haptic.deactivate(activeMotorFinger);
            activeMotorFinger = -1;
        }
    }

    // Process Serial commands (uses serial-only handler for SET_ROLE, GET_ROLE)
    if (Serial.available())
    {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0)
        {
            Serial.printf("[SERIAL] Command: %s\n", input.c_str());
            handleSerialCommand(input.c_str());
        }
    }

    // Update therapy engine (both roles - PRIMARY generates patterns for sync,
    // SECONDARY needs this for standalone hardware tests)
    therapy.update();

    // Detect when therapy session ends (for resuming scanning on SECONDARY)
    bool isTherapyRunning = therapy.isRunning();
    if (wasTherapyRunning && !isTherapyRunning)
    {
        // Therapy just stopped
        Serial.println(F("\n+============================================================+"));
        Serial.println(F("|  THERAPY TEST COMPLETE                                     |"));
        Serial.println(F("+============================================================+\n"));

        haptic.emergencyStop();
        stateMachine.transition(StateTrigger::STOP_SESSION);
        stateMachine.transition(StateTrigger::STOPPED);

        // Resume scanning on SECONDARY after standalone test
        if (deviceRole == DeviceRole::SECONDARY && !ble.isPrimaryConnected())
        {
            Serial.println(F("[TEST] Resuming scanning..."));
            ble.setScannerAutoRestart(true); // Re-enable health check
            ble.startScanning(BLE_NAME);
        }
    }
    wasTherapyRunning = isTherapyRunning;

    // SECONDARY: Check for heartbeat timeout during active connection
    if (deviceRole == DeviceRole::SECONDARY && ble.isPrimaryConnected())
    {
        if (lastHeartbeatReceived > 0 &&
            (millis() - lastHeartbeatReceived > HEARTBEAT_TIMEOUT_MS))
        {
            handleHeartbeatTimeout();
        }
    }

    // PRIMARY: Check for SECONDARY heartbeat timeout during therapy
    // This detects SECONDARY power-off faster than BLE supervision timeout (~4s)
    if (deviceRole == DeviceRole::PRIMARY && ble.isSecondaryConnected() && therapy.isRunning())
    {
        if (lastSecondaryHeartbeat > 0 &&
            (millis() - lastSecondaryHeartbeat > PRIMARY_HEARTBEAT_TIMEOUT_MS))
        {
            Serial.println(F("[WARN] SECONDARY heartbeat timeout - stopping therapy"));
            safeMotorShutdown();
            lastSecondaryHeartbeat = 0;  // Reset to prevent repeated triggers
        }
    }

    // PRIMARY: Check boot window for auto-start therapy
    // NOTE: Must use fresh millis() here, not cached 'now' from start of loop().
    // BLE callbacks can fire during loop() and set bootWindowStart to a newer
    // timestamp than 'now', causing unsigned underflow (now - start = negative = huge).
    if (deviceRole == DeviceRole::PRIMARY && bootWindowActive && !autoStartTriggered)
    {
        uint32_t currentTime = millis();
        uint32_t elapsed = currentTime - bootWindowStart;

        if (elapsed >= STARTUP_WINDOW_MS)
        {
            // Boot window expired without phone connecting
            if (ble.isSecondaryConnected() && !ble.isPhoneConnected())
            {
                Serial.printf("[BOOT] 30s window expired (now=%lu, start=%lu, elapsed=%lu) - auto-starting therapy\n",
                              (unsigned long)currentTime, (unsigned long)bootWindowStart, (unsigned long)elapsed);
                bootWindowActive = false;
                autoStartTriggered = true;
                autoStartTherapy();
            }
            else
            {
                // SECONDARY disconnected during window, cancel
                Serial.printf("[BOOT] Window expired but SECONDARY not connected (now=%lu, start=%lu)\n",
                              (unsigned long)currentTime, (unsigned long)bootWindowStart);
                bootWindowActive = false;
            }
        }
    }

    // Periodic latency metrics reporting (when enabled and therapy running)
    static uint32_t lastLatencyReport = 0;
    if (latencyMetrics.enabled && therapy.isRunning())
    {
        if (now - lastLatencyReport >= LATENCY_REPORT_INTERVAL_MS)
        {
            lastLatencyReport = now;
            latencyMetrics.printReport();
        }
    }

    // Check connection state changes
    bool isConnected = (deviceRole == DeviceRole::PRIMARY) ? ble.isSecondaryConnected() : ble.isPrimaryConnected();

    if (isConnected != wasConnected)
    {
        wasConnected = isConnected;
        // LED is handled by state machine - just log the change
        Serial.println(isConnected ? F("[STATE] Connected!") : F("[STATE] Disconnected"));
    }

    // Send heartbeat every 2 seconds when connected
    if (isConnected && (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS))
    {
        lastHeartbeat = now;
        sendHeartbeat();
    }

    // Print status every 5 seconds
    if (now - lastStatusPrint >= 5000)
    {
        lastStatusPrint = now;
        printStatus();
    }

    // Check battery every 60 seconds
    if (now - lastBatteryCheck >= BATTERY_CHECK_INTERVAL_MS)
    {
        lastBatteryCheck = now;
        BatteryStatus status = battery.getStatus();
        Serial.printf("[BATTERY] %.2fV | %d%% | Status: %s\n",
                      status.voltage, status.percentage, status.statusString());
    }

    // LOW PRIORITY: Background latency probing (non-blocking)
    // Runs AFTER all critical work, only during active therapy
    if (deviceRole == DeviceRole::PRIMARY &&
        stateMachine.getCurrentState() == TherapyState::RUNNING &&
        ble.isSecondaryConnected() &&
        (now - lastProbeTime) >= PROBE_INTERVAL_MS)
    {
        sendPing();
        lastProbeTime = now;
    }

    // Yield to BLE stack (non-blocking - allows SoftDevice processing)
    yield();
}

// =============================================================================
// INITIALIZATION FUNCTIONS
// =============================================================================

void printBanner()
{
    Serial.println(F("\n"));
    Serial.println(F("+============================================================+"));
    Serial.println(F("|                  BlueBuzzah Firmware                       |"));
    Serial.println(F("+============================================================+"));
    Serial.printf("|  Firmware: %-47s |\n", FIRMWARE_VERSION);
    Serial.println(F("|  Platform: Adafruit Feather nRF52840 Express              |"));
    Serial.println(F("+============================================================+"));
}

DeviceRole determineRole()
{
    // Check if USER button is held (active LOW)
    // Button held = SECONDARY mode (emergency override)
    if (digitalRead(USER_BUTTON_PIN) == LOW)
    {
        Serial.println(F("[INFO] USER button held - forcing SECONDARY mode"));
        delay(500); // Debounce
        return DeviceRole::SECONDARY;
    }

    // Check if role was loaded from settings.json
    if (profiles.hasStoredRole())
    {
        Serial.println(F("[INFO] Using role from settings.json"));
        return profiles.getDeviceRole();
    }

    // Default to PRIMARY if no settings found
    Serial.println(F("[INFO] No role in settings - defaulting to PRIMARY"));
    return DeviceRole::PRIMARY;
}

bool initializeHardware()
{
    bool success = true;

    // Initialize haptic controller
    Serial.println(F("\nInitializing Haptic Controller..."));
    if (!haptic.begin())
    {
        Serial.println(F("[ERROR] Haptic controller initialization failed"));
        success = false;
    }
    else
    {
        // Safety: Immediately stop all motors in case they were left on from previous session
        haptic.emergencyStop();

        Serial.printf("Haptic Controller: %d/%d fingers enabled\n",
                      haptic.getEnabledCount(), MAX_ACTUATORS);

        // Initialize hardware timer for sync compensation (PRIMARY only)
        if (!syncTimer.begin(&haptic))
        {
            Serial.println(F("[WARN] SyncTimer initialization failed"));
        }
    }

    // Initialize battery monitor
    Serial.println(F("\nInitializing Battery Monitor..."));
    if (!battery.begin())
    {
        Serial.println(F("[ERROR] Battery monitor initialization failed"));
        success = false;
    }
    else
    {
        Serial.println(F("Battery Monitor: OK"));
    }

    return success;
}

bool initializeBLE()
{
    // Set up BLE callbacks
    ble.setConnectCallback(onBLEConnect);
    ble.setDisconnectCallback(onBLEDisconnect);
    ble.setMessageCallback(onBLEMessage);

    // Initialize BLE with appropriate role
    if (!ble.begin(deviceRole, BLE_NAME))
    {
        Serial.println(F("[ERROR] BLE begin() failed"));
        return false;
    }

    // Start advertising or scanning based on role
    if (deviceRole == DeviceRole::PRIMARY)
    {
        if (!ble.startAdvertising())
        {
            Serial.println(F("[ERROR] Failed to start advertising"));
            return false;
        }
        Serial.println(F("[BLE] Advertising started"));
    }
    else
    {
        if (!ble.startScanning(BLE_NAME))
        {
            Serial.println(F("[ERROR] Failed to start scanning"));
            return false;
        }
        Serial.println(F("[BLE] Scanning started"));
    }

    return true;
}

bool initializeTherapy()
{
    // Set local motor callbacks (both roles need these for standalone tests)
    therapy.setActivateCallback(onActivate);
    therapy.setDeactivateCallback(onDeactivate);
    therapy.setCycleCompleteCallback(onCycleComplete);

    // Set BLE sync callback (PRIMARY only - sends commands to SECONDARY)
    if (deviceRole == DeviceRole::PRIMARY)
    {
        therapy.setSendCommandCallback(onSendCommand);
        // Set macrocycle start callback for PING/PONG latency measurement
        therapy.setMacrocycleStartCallback(onMacrocycleStart);
        // Set frequency callback for Custom vCR frequency randomization
        therapy.setSetFrequencyCallback(onSetFrequency);
    }
    return true;
}

// =============================================================================
// BLE EVENT HANDLERS
// =============================================================================

void sendHeartbeat()
{
    heartbeatSequence++;

    // Create heartbeat command
    SyncCommand cmd = SyncCommand::createHeartbeat(heartbeatSequence);

    // Serialize
    char buffer[128];
    if (cmd.serialize(buffer, sizeof(buffer)))
    {
        // Send based on role
        bool sent = false;
        if (deviceRole == DeviceRole::PRIMARY)
        {
            sent = ble.sendToSecondary(buffer);
        }
        else
        {
            sent = ble.sendToPrimary(buffer);
        }

        if (sent)
        {
            Serial.printf("[TX] %s\n", buffer);
        }
    }
}

void printStatus()
{
    Serial.println(F("------------------------------------------------------------"));

    // Line 1: Role and State
    Serial.printf("[STATUS] Role: %s | State: %s\n",
                  deviceRoleToString(deviceRole),
                  therapyStateToString(stateMachine.getCurrentState()));

    // Line 2: BLE activity and connections
    if (deviceRole == DeviceRole::PRIMARY)
    {
        Serial.printf("[BLE] Advertising: %s | Connections: %d\n",
                      ble.isAdvertising() ? "YES" : "NO",
                      ble.getConnectionCount());
        Serial.printf("[CONN] SECONDARY: %s | Phone: %s\n",
                      ble.isSecondaryConnected() ? "Connected" : "Waiting...",
                      ble.isPhoneConnected() ? "Connected" : "Waiting...");
    }
    else
    {
        // SECONDARY mode
        Serial.printf("[BLE] Scanning: %s | Connections: %d\n",
                      ble.isScanning() ? "YES" : "NO",
                      ble.getConnectionCount());

        if (ble.isPrimaryConnected())
        {
            uint32_t timeSinceHB = millis() - lastHeartbeatReceived;
            Serial.printf("[CONN] PRIMARY: Connected | Last HB: %lums ago\n", timeSinceHB);
        }
        else
        {
            Serial.println(F("[CONN] PRIMARY: Searching..."));
        }
    }

    Serial.println(F("------------------------------------------------------------"));
}

// =============================================================================
// BLE CALLBACKS
// =============================================================================

void onBLEConnect(uint16_t connHandle, ConnectionType type)
{
    const char *typeStr = "UNKNOWN";
    switch (type)
    {
    case ConnectionType::UNKNOWN:
        typeStr = "UNKNOWN";
        break;
    case ConnectionType::PHONE:
        typeStr = "PHONE";
        break;
    case ConnectionType::SECONDARY:
        typeStr = "SECONDARY";
        break;
    case ConnectionType::PRIMARY:
        typeStr = "PRIMARY";
        break;
    default:
        break;
    }

    Serial.printf("[CONNECT] Handle: %d, Type: %s\n", connHandle, typeStr);

    // If SECONDARY device connected to PRIMARY, send identification
    if (deviceRole == DeviceRole::SECONDARY && type == ConnectionType::PRIMARY)
    {
        Serial.println(F("[SECONDARY] Sending IDENTIFY:SECONDARY to PRIMARY"));
        ble.sendToPrimary("IDENTIFY:SECONDARY");
        // Start heartbeat timeout tracking
        lastHeartbeatReceived = millis();
    }

    // Update state machine on relevant connections
    if ((deviceRole == DeviceRole::PRIMARY && type == ConnectionType::SECONDARY) ||
        (deviceRole == DeviceRole::SECONDARY && type == ConnectionType::PRIMARY))
    {
        stateMachine.transition(StateTrigger::CONNECTED);
    }

    // PRIMARY: Boot window logic for auto-start
    if (deviceRole == DeviceRole::PRIMARY)
    {
        if (type == ConnectionType::SECONDARY && !autoStartTriggered)
        {
            // SECONDARY connected - start 30-second boot window for phone
            bootWindowStart = millis();
            bootWindowActive = true;
            // Initialize heartbeat tracking (timeout detection starts when first HB received)
            lastSecondaryHeartbeat = millis();
            Serial.printf("[BOOT] SECONDARY connected at %lu - starting 30s boot window for phone\n",
                          (unsigned long)bootWindowStart);
        }
        else if (type == ConnectionType::PHONE && bootWindowActive)
        {
            // Phone connected within boot window - cancel auto-start
            bootWindowActive = false;
            Serial.println(F("[BOOT] Phone connected - boot window cancelled"));
        }
    }

    // Quick haptic feedback on index finger (deferred - not safe in BLE callback)
    if (haptic.isEnabled(FINGER_INDEX))
    {
        deferredQueue.enqueue(DeferredWorkType::HAPTIC_PULSE, FINGER_INDEX, 30, 50);
    }
}

void onBLEDisconnect(uint16_t connHandle, ConnectionType type, uint8_t reason)
{
    const char *typeStr = "UNKNOWN";
    switch (type)
    {
    case ConnectionType::PHONE:
        typeStr = "PHONE";
        break;
    case ConnectionType::SECONDARY:
        typeStr = "SECONDARY";
        break;
    case ConnectionType::PRIMARY:
        typeStr = "PRIMARY";
        break;
    default:
        break;
    }

    Serial.printf("[DISCONNECT] Handle: %d, Type: %s, Reason: 0x%02X\n",
                  connHandle, typeStr, reason);

    // Update state machine on relevant disconnections
    if ((deviceRole == DeviceRole::PRIMARY && type == ConnectionType::SECONDARY) ||
        (deviceRole == DeviceRole::SECONDARY && type == ConnectionType::PRIMARY))
    {
        stateMachine.transition(StateTrigger::DISCONNECTED);

        // SAFETY: Set flag for main loop to execute motor shutdown
        // Cannot call safeMotorShutdown() directly here (BLE callback = ISR context, no I2C)
        safetyShutdownPending = true;

        // PRIMARY: Cancel boot window when SECONDARY disconnects (prevents race condition
        // where stale bootWindowStart causes immediate auto-start on reconnection)
        if (deviceRole == DeviceRole::PRIMARY && bootWindowActive)
        {
            bootWindowActive = false;
            Serial.println(F("[BOOT] SECONDARY disconnected - boot window cancelled"));
        }
    }
    else if (deviceRole == DeviceRole::PRIMARY && type == ConnectionType::PHONE)
    {
        stateMachine.transition(StateTrigger::PHONE_LOST);
    }

    // Double haptic pulse on index finger (deferred - not safe in BLE callback)
    if (haptic.isEnabled(FINGER_INDEX))
    {
        deferredQueue.enqueue(DeferredWorkType::HAPTIC_DOUBLE_PULSE, FINGER_INDEX, 50, 50);
    }
}

// =============================================================================
// DEFERRED WORK EXECUTOR
// =============================================================================

/**
 * @brief Execute deferred work from DeferredQueue
 *
 * Called from main loop when work is dequeued. Handles haptic operations
 * that aren't safe in BLE callback context (I2C operations).
 */
void executeDeferredWork(DeferredWorkType type, uint8_t p1, uint8_t p2, uint32_t p3)
{
    // SAFETY: Skip haptic operations in critical error states
    // Note: CONNECTION_LOST is intentionally NOT blocked - disconnect feedback pulses
    // (queued AFTER safetyShutdownPending is set) should still execute for user feedback
    if (type == DeferredWorkType::HAPTIC_PULSE ||
        type == DeferredWorkType::HAPTIC_DOUBLE_PULSE)
    {
        TherapyState currentState = stateMachine.getCurrentState();
        if (currentState == TherapyState::ERROR ||
            currentState == TherapyState::CRITICAL_BATTERY)
        {
            Serial.println(F("[DEFERRED] Skipping haptic - safety state active"));
            return;
        }
    }

    switch (type)
    {
    case DeferredWorkType::HAPTIC_PULSE:
    {
        // p1=finger, p2=amplitude, p3=duration_ms
        uint8_t finger = p1;
        uint8_t amplitude = p2;
        uint32_t duration = p3;

        if (haptic.isEnabled(finger))
        {
            haptic.activate(finger, amplitude);
            // Schedule deactivation (non-blocking)
            scheduler.schedule(duration, hapticDeactivateCallback, (void*)(uintptr_t)finger);
        }
        break;
    }

    case DeferredWorkType::HAPTIC_DOUBLE_PULSE:
    {
        // p1=finger, p2=amplitude, p3=duration_ms (100ms gap between pulses)
        uint8_t finger = p1;
        uint8_t amplitude = p2;
        uint32_t duration = p3;

        if (haptic.isEnabled(finger))
        {
            // First pulse
            haptic.activate(finger, amplitude);
            // Schedule deactivation after duration
            scheduler.schedule(duration, hapticDeactivateCallback, (void*)(uintptr_t)finger);
            // Schedule second pulse after duration + 100ms gap
            // Pack finger and amplitude into context (finger in low byte, amplitude in high byte)
            uint32_t ctx = (uint32_t)finger | ((uint32_t)amplitude << 8) | ((uint32_t)duration << 16);
            scheduler.schedule(duration + 100, hapticSecondPulseCallback, (void*)(uintptr_t)ctx);
        }
        break;
    }

    case DeferredWorkType::HAPTIC_DEACTIVATE:
    {
        uint8_t finger = p1;
        haptic.deactivate(finger);
        break;
    }

    case DeferredWorkType::SCANNER_RESTART:
    {
        // Restart BLE scanner after delay (handled by scheduler)
        if (deviceRole == DeviceRole::SECONDARY)
        {
            ble.startScanning();
        }
        break;
    }

    default:
        break;
    }
}

/**
 * @brief Timer callback to deactivate haptic motor
 */
void hapticDeactivateCallback(void* ctx)
{
    uint8_t finger = (uint8_t)(uintptr_t)ctx;
    haptic.deactivate(finger);
}

/**
 * @brief Timer callback for second pulse in double-pulse sequence
 */
void hapticSecondPulseCallback(void* ctx)
{
    uint32_t packed = (uint32_t)(uintptr_t)ctx;
    uint8_t finger = packed & 0xFF;
    uint8_t amplitude = (packed >> 8) & 0xFF;
    uint32_t duration = (packed >> 16) & 0xFFFF;

    if (haptic.isEnabled(finger))
    {
        haptic.activate(finger, amplitude);
        // Schedule final deactivation
        scheduler.schedule(duration, hapticDeactivateCallback, (void*)(uintptr_t)finger);
    }
}

void onBLEMessage(uint16_t connHandle, const char *message)
{
    // Check for simple text commands first (for testing)
    // Both PRIMARY and SECONDARY can run standalone tests for hardware verification
    if (strcmp(message, "TEST") == 0 || strcmp(message, "test") == 0)
    {
        startTherapyTest();
        return;
    }

    if (strcmp(message, "STOP") == 0 || strcmp(message, "stop") == 0)
    {
        stopTherapyTest();
        return;
    }

    // Try menu controller first for phone/BLE commands (PRIMARY only)
    if (deviceRole == DeviceRole::PRIMARY && !menu.isInternalMessage(message))
    {
        if (menu.handleCommand(message))
        {
            return; // Command handled by menu controller
        }
    }

    // Parse sync/internal commands
    SyncCommand cmd;
    if (cmd.deserialize(message))
    {
        // Handle specific command types
        switch (cmd.getType())
        {
        case SyncCommandType::HEARTBEAT:
            // Track heartbeat for timeout detection (bidirectional)
            if (deviceRole == DeviceRole::SECONDARY)
            {
                lastHeartbeatReceived = millis();
            }
            else if (deviceRole == DeviceRole::PRIMARY)
            {
                lastSecondaryHeartbeat = millis();
            }
            break;

        case SyncCommandType::PING:
            // SECONDARY: Immediately reply with PONG
            if (deviceRole == DeviceRole::SECONDARY)
            {
                SyncCommand pong = SyncCommand::createPong(cmd.getSequenceId());
                char buffer[64];
                if (pong.serialize(buffer, sizeof(buffer)))
                {
                    ble.sendToPrimary(buffer);
                }
            }
            break;

        case SyncCommandType::PONG:
            // PRIMARY: Calculate RTT and update measured latency (with EMA smoothing)
            if (deviceRole == DeviceRole::PRIMARY && pingStartTime > 0)
            {
                uint64_t now = getMicros();
                uint32_t rtt = (uint32_t)(now - pingStartTime);

                // Update latency with EMA smoothing and outlier rejection
                syncProtocol.updateLatency(rtt);

                // Enhanced logging: show raw, smoothed, and sample count
                Serial.printf("[SYNC] RTT=%lu raw=%lu smooth=%lu n=%u\n",
                              (unsigned long)rtt,
                              (unsigned long)syncProtocol.getRawLatency(),
                              (unsigned long)syncProtocol.getMeasuredLatency(),
                              syncProtocol.getSampleCount());

                pingStartTime = 0;  // Clear for next PING
            }
            break;

        case SyncCommandType::BUZZ:
        {
            // SECONDARY: Activate motor immediately (no scheduled execution)
            // PRIMARY has already compensated for BLE latency before sending
            int32_t finger = cmd.getDataInt("0", -1);
            int32_t amplitude = cmd.getDataInt("1", 50);
            int32_t durationMs = cmd.getDataInt("2", 100);  // Default 100ms if not specified
            int32_t freqHz = cmd.getDataInt("3", 235);      // Default 235Hz (middle of range)

            if (finger >= 0 && finger < MAX_ACTUATORS && haptic.isEnabled(finger))
            {
                // Deactivate any previously active motor first
                if (activeMotorFinger >= 0)
                {
                    Serial.printf("[DEACTIVATE] F%d (prev)\n", activeMotorFinger);
                    haptic.deactivate(activeMotorFinger);
                }

                // Apply frequency before activation
                haptic.setFrequency(finger, (uint16_t)freqHz);

                // Activate immediately
                Serial.printf("[ACTIVATE] F%d A%d dur=%ldms freq=%ldHz\n", finger, amplitude, durationMs, freqHz);
                haptic.activate(finger, amplitude);

                // Schedule non-blocking deactivation after duration from profile
                activeMotorFinger = finger;
                motorDeactivateTime = millis() + durationMs;
            }
        }
        break;

        case SyncCommandType::START_SESSION:
            Serial.println(F("[SESSION] Start requested"));
            stateMachine.transition(StateTrigger::START_SESSION);
            break;

        case SyncCommandType::PAUSE_SESSION:
            Serial.println(F("[SESSION] Pause requested"));
            stateMachine.transition(StateTrigger::PAUSE_SESSION);
            break;

        case SyncCommandType::RESUME_SESSION:
            Serial.println(F("[SESSION] Resume requested"));
            stateMachine.transition(StateTrigger::RESUME_SESSION);
            break;

        case SyncCommandType::STOP_SESSION:
            Serial.println(F("[SESSION] Stop requested"));
            haptic.emergencyStop();
            stateMachine.transition(StateTrigger::STOP_SESSION);
            break;

        default:
            break;
        }
    }
}

// =============================================================================
// THERAPY CALLBACKS
// =============================================================================

void onSendCommand(const char *commandType, uint8_t primaryFinger, uint8_t secondaryFinger, uint8_t amplitude, uint32_t durationMs, uint32_t seq, uint16_t frequencyHz)
{
    // Create BUZZ command for SECONDARY device - send secondaryFinger (the finger SECONDARY should activate)
    SyncCommand cmd = SyncCommand::createBuzz(seq, secondaryFinger, amplitude, durationMs, frequencyHz);

    char buffer[128];
    if (cmd.serialize(buffer, sizeof(buffer)))
    {
        ble.sendToSecondary(buffer);
    }

    // Schedule local activation after measured BLE latency (non-blocking)
    // SECONDARY activates immediately on receive, so PRIMARY delays by one-way latency
    // to achieve synchronized motor activation between devices
    uint32_t latencyUs = syncProtocol.getMeasuredLatency();
    if (latencyUs > 0 && haptic.isEnabled(primaryFinger))
    {
        // Hardware timer schedules activation with microsecond precision
        Serial.printf("[ACTIVATE] Scheduled F%d A%d delay=%luus\n", primaryFinger, amplitude, latencyUs);
        syncTimer.scheduleActivation(latencyUs, primaryFinger, amplitude);
    }
    else if (haptic.isEnabled(primaryFinger))
    {
        // No latency measurement yet - activate immediately
        Serial.printf("[ACTIVATE] Immediate F%d A%d (no latency)\n", primaryFinger, amplitude);
        haptic.activate(primaryFinger, amplitude);
    }
}

void onActivate(uint8_t finger, uint8_t amplitude)
{
    // When SECONDARY is connected, onSendCommand handles local activation
    // to achieve synchronized execution. Skip here to avoid duplicate activation.
    if (deviceRole == DeviceRole::PRIMARY && ble.isSecondaryConnected())
    {
        return;
    }

    // Standalone mode: activate local motor directly
    if (haptic.isEnabled(finger))
    {
        haptic.activate(finger, amplitude);
    }
}

void onDeactivate(uint8_t finger)
{
    // Deactivate local motor
    if (haptic.isEnabled(finger))
    {
        Serial.printf("[DEACTIVATE] Finger %d\n", finger);
        haptic.deactivate(finger);
    }
}

void onSetFrequency(uint8_t finger, uint16_t frequencyHz)
{
    // Set frequency for local motor (PRIMARY only - SECONDARY gets frequency in BUZZ command)
    if (haptic.isEnabled(finger))
    {
        haptic.setFrequency(finger, frequencyHz);
    }
}

void onCycleComplete(uint32_t cycleCount)
{
    Serial.printf("[THERAPY] Cycle %lu complete\n", cycleCount);
}

void onMacrocycleStart(uint32_t macrocycleCount)
{
    // Latency probing now handled by background timer in loop()
    // This callback can be used for other macrocycle logic if needed
    (void)macrocycleCount;  // Suppress unused parameter warning
}

// =============================================================================
// THERAPY TEST FUNCTIONS
// =============================================================================

void startTherapyTest()
{
    if (therapy.isRunning())
    {
        Serial.println(F("[TEST] Therapy already running"));
        return;
    }

    // Get current profile
    const TherapyProfile *profile = profiles.getCurrentProfile();
    if (!profile)
    {
        Serial.println(F("[TEST] No profile loaded!"));
        return;
    }

    // Convert pattern type string to constant
    uint8_t patternType = PATTERN_TYPE_RNDP;
    if (strcmp(profile->patternType, "sequential") == 0)
    {
        patternType = PATTERN_TYPE_SEQUENTIAL;
    }
    else if (strcmp(profile->patternType, "mirrored") == 0)
    {
        patternType = PATTERN_TYPE_MIRRORED;
    }

    // Stop scanning during standalone test (SECONDARY only)
    if (deviceRole == DeviceRole::SECONDARY)
    {
        ble.setScannerAutoRestart(false); // Prevent health check from restarting
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

    // Notify SECONDARY of session start (enables pulsing LED on SECONDARY)
    if (deviceRole == DeviceRole::PRIMARY && ble.isSecondaryConnected())
    {
        SyncCommand cmd = SyncCommand::createStartSession(g_sequenceGenerator.next());
        char buffer[64];
        if (cmd.serialize(buffer, sizeof(buffer)))
        {
            ble.sendToSecondary(buffer);
        }
    }

    // Reset latency probing for fresh measurements
    if (deviceRole == DeviceRole::PRIMARY)
    {
        syncProtocol.resetLatency();  // Clear EMA state for fresh warmup
        lastProbeTime = millis() - PROBE_INTERVAL_MS + 100;  // First probe in ~100ms
    }

    // Start 5-minute test using profile settings (send STOP to end early)
    therapy.startSession(
        300, // 5 minutes (300 seconds)
        patternType,
        profile->timeOnMs,
        profile->timeOffMs,
        profile->jitterPercent,
        profile->numFingers,
        profile->mirrorPattern,
        profile->amplitudeMin,
        profile->amplitudeMax);
}

void stopTherapyTest()
{
    if (!therapy.isRunning())
    {
        Serial.println(F("[TEST] Therapy not running"));
        return;
    }

    Serial.println(F("\n+============================================================+"));
    Serial.println(F("|  STOPPING THERAPY TEST                                     |"));
    Serial.println(F("+============================================================+\n"));

    therapy.stop();
    safeMotorShutdown();

    // Update state machine
    stateMachine.transition(StateTrigger::STOP_SESSION);
    stateMachine.transition(StateTrigger::STOPPED);

    // Resume scanning after standalone test (SECONDARY only)
    if (deviceRole == DeviceRole::SECONDARY)
    {
        Serial.println(F("[TEST] Resuming scanning..."));
        ble.setScannerAutoRestart(true); // Re-enable health check
        ble.startScanning(BLE_NAME);
    }
}

/**
 * @brief Auto-start therapy after boot window expires without phone connection
 *
 * Called when PRIMARY+SECONDARY are connected but phone doesn't connect
 * within 30 seconds. Starts therapy with current profile settings.
 */
void autoStartTherapy()
{
    if (deviceRole != DeviceRole::PRIMARY)
    {
        Serial.println(F("[AUTO] Auto-start only available on PRIMARY"));
        return;
    }

    if (therapy.isRunning())
    {
        Serial.println(F("[AUTO] Therapy already running"));
        return;
    }

    // Get current profile
    const TherapyProfile *profile = profiles.getCurrentProfile();
    if (!profile)
    {
        // No profile loaded - fall back to built-in "noisy_vcr" (v1 defaults)
        Serial.println(F("[AUTO] No profile loaded - loading noisy_vcr defaults"));
        profiles.loadProfileByName("noisy_vcr");
        profile = profiles.getCurrentProfile();

        if (!profile)
        {
            Serial.println(F("[AUTO] ERROR: Failed to load fallback profile"));
            return;
        }
    }

    // Convert pattern type string to constant
    uint8_t patternType = PATTERN_TYPE_RNDP;
    if (strcmp(profile->patternType, "sequential") == 0)
    {
        patternType = PATTERN_TYPE_SEQUENTIAL;
    }
    else if (strcmp(profile->patternType, "mirrored") == 0)
    {
        patternType = PATTERN_TYPE_MIRRORED;
    }

    Serial.println(F("\n+============================================================+"));
    Serial.println(F("|  AUTO-STARTING THERAPY (no phone connected)                |"));
    Serial.printf("|  Profile: %-46s |\n", profile->name);
    Serial.printf("|  Duration: 30 min | Pattern: %-4s | Jitter: %5.1f%%         |\n",
                  profile->patternType, profile->jitterPercent);
    Serial.println(F("+============================================================+\n"));

    // Update state machine
    stateMachine.transition(StateTrigger::START_SESSION);

    // Notify SECONDARY of session start (enables pulsing LED on SECONDARY)
    if (ble.isSecondaryConnected())
    {
        SyncCommand cmd = SyncCommand::createStartSession(g_sequenceGenerator.next());
        char buffer[64];
        if (cmd.serialize(buffer, sizeof(buffer)))
        {
            ble.sendToSecondary(buffer);
        }
    }

    // Reset latency probing for fresh measurements
    syncProtocol.resetLatency();  // Clear EMA state for fresh warmup
    lastProbeTime = millis() - PROBE_INTERVAL_MS + 100;  // First probe in ~100ms

    // Start 30-minute session using profile settings
    therapy.startSession(
        1800, // 30 minutes (1800 seconds)
        patternType,
        profile->timeOnMs,
        profile->timeOffMs,
        profile->jitterPercent,
        profile->numFingers,
        profile->mirrorPattern,
        profile->amplitudeMin,
        profile->amplitudeMax);
}

// =============================================================================
// PING/PONG LATENCY MEASUREMENT (PRIMARY only)
// =============================================================================

/**
 * @brief Send PING to SECONDARY to measure BLE latency
 *
 * Called at the start of each macrocycle. SECONDARY immediately replies
 * with PONG, allowing PRIMARY to calculate RTT and one-way latency.
 */
void sendPing()
{
    if (deviceRole != DeviceRole::PRIMARY || !ble.isSecondaryConnected())
    {
        return;
    }

    pingStartTime = getMicros();
    SyncCommand cmd = SyncCommand::createPing(g_sequenceGenerator.next());

    char buffer[64];
    if (cmd.serialize(buffer, sizeof(buffer)))
    {
        ble.sendToSecondary(buffer);
    }
}

// =============================================================================
// STATE MACHINE CALLBACK
// =============================================================================

/**
 * @brief Update LED pattern based on therapy state
 *
 * LED Pattern Mapping:
 * | State              | Color  | Pattern       | Description                    |
 * |--------------------|--------|---------------|--------------------------------|
 * | IDLE               | Blue   | Breathe slow  | Calm, system ready             |
 * | CONNECTING         | Blue   | Fast blink    | Actively connecting            |
 * | READY              | Green  | Solid         | Connected, stable              |
 * | RUNNING            | Green  | Pulse slow    | Active therapy                 |
 * | PAUSED             | Yellow | Solid         | Session paused                 |
 * | STOPPING           | Yellow | Fast blink    | Winding down                   |
 * | ERROR              | Red    | Slow blink    | Error condition                |
 * | LOW_BATTERY        | Orange | Slow blink    | Battery warning                |
 * | CRITICAL_BATTERY   | Red    | Urgent blink  | Critical - shutdown imminent   |
 * | CONNECTION_LOST    | Purple | Fast blink    | BLE connection lost            |
 * | PHONE_DISCONNECTED | —      | No change     | Informational only             |
 */
void onStateChange(const StateTransition &transition)
{
    // Update LED pattern based on new state
    switch (transition.toState)
    {
    case TherapyState::IDLE:
        led.setPattern(Colors::BLUE, LEDPattern::BREATHE_SLOW);
        break;

    case TherapyState::CONNECTING:
        led.setPattern(Colors::BLUE, LEDPattern::BLINK_CONNECT);
        break;

    case TherapyState::READY:
        led.setPattern(Colors::GREEN, LEDPattern::SOLID);
        break;

    case TherapyState::RUNNING:
        led.setPattern(Colors::GREEN, LEDPattern::PULSE_SLOW);
        break;

    case TherapyState::PAUSED:
        led.setPattern(Colors::YELLOW, LEDPattern::SOLID);
        break;

    case TherapyState::STOPPING:
        led.setPattern(Colors::YELLOW, LEDPattern::BLINK_FAST);
        break;

    case TherapyState::ERROR:
        led.setPattern(Colors::RED, LEDPattern::BLINK_SLOW);
        // Emergency stop on error
        haptic.emergencyStop();
        therapy.stop();
        break;

    case TherapyState::CRITICAL_BATTERY:
        led.setPattern(Colors::RED, LEDPattern::BLINK_URGENT);
        // Emergency stop on critical battery
        haptic.emergencyStop();
        therapy.stop();
        break;

    case TherapyState::LOW_BATTERY:
        led.setPattern(Colors::ORANGE, LEDPattern::BLINK_SLOW);
        break;

    case TherapyState::CONNECTION_LOST:
        led.setPattern(Colors::PURPLE, LEDPattern::BLINK_CONNECT);
        // Stop therapy on connection loss
        if (therapy.isRunning())
        {
            therapy.stop();
        }
        // Always call emergencyStop - motors can be active without therapy running
        // (e.g., from deferred queue connect/disconnect pulses)
        // Note: safeMotorShutdown() will also run from loop() via safetyShutdownPending
        // but this provides immediate belt-and-suspenders safety
        haptic.emergencyStop();
        break;

    case TherapyState::PHONE_DISCONNECTED:
        // Informational only - keep current LED pattern
        break;

    default:
        break;
    }
}

// =============================================================================
// MENU CONTROLLER CALLBACK
// =============================================================================

void onMenuSendResponse(const char *response)
{
    // Send response to phone (or whoever sent the command)
    if (ble.isPhoneConnected())
    {
        ble.sendToPhone(response);
    }
}

// =============================================================================
// SECONDARY HEARTBEAT TIMEOUT HANDLER
// =============================================================================

void handleHeartbeatTimeout()
{
    Serial.println(F("[WARN] Heartbeat timeout - PRIMARY connection lost"));

    // 1. Safety first - stop therapy and all motors immediately
    therapy.stop();
    safeMotorShutdown();

    // 2. Update state machine (LED handled by onStateChange callback)
    stateMachine.transition(StateTrigger::DISCONNECTED);

    // 3. Attempt reconnection (3 attempts, 2s apart)
    for (uint8_t attempt = 1; attempt <= 3; attempt++)
    {
        Serial.printf("[RECOVERY] Attempt %d/3...\n", attempt);
        delay(2000);

        if (ble.isPrimaryConnected())
        {
            Serial.println(F("[RECOVERY] PRIMARY reconnected"));
            stateMachine.transition(StateTrigger::RECONNECTED);
            lastHeartbeatReceived = millis(); // Reset timeout
            return;
        }
    }

    // 4. Recovery failed - return to IDLE
    Serial.println(F("[RECOVERY] Failed - returning to IDLE"));
    stateMachine.transition(StateTrigger::RECONNECT_FAILED);
    lastHeartbeatReceived = 0; // Reset for next session

    // 5. Restart scanning for PRIMARY
    ble.startScanning(BLE_NAME);
}

// =============================================================================
// SERIAL-ONLY COMMANDS
// =============================================================================

void handleSerialCommand(const char *command)
{
    // SET_ROLE - one-time device configuration (serial only for security)
    if (strncmp(command, "SET_ROLE:", 9) == 0)
    {
        const char *roleStr = command + 9;
        if (strcasecmp(roleStr, "PRIMARY") == 0)
        {
            profiles.setDeviceRole(DeviceRole::PRIMARY);
            profiles.saveSettings();
            safeMotorShutdown();  // Ensure motors off before reset
            Serial.println(F("[CONFIG] Role set to PRIMARY - restarting..."));
            Serial.flush();
            delay(100);
            NVIC_SystemReset();
        }
        else if (strcasecmp(roleStr, "SECONDARY") == 0)
        {
            profiles.setDeviceRole(DeviceRole::SECONDARY);
            profiles.saveSettings();
            safeMotorShutdown();  // Ensure motors off before reset
            Serial.println(F("[CONFIG] Role set to SECONDARY - restarting..."));
            Serial.flush();
            delay(100);
            NVIC_SystemReset();
        }
        else
        {
            Serial.println(F("[ERROR] Invalid role. Use: SET_ROLE:PRIMARY or SET_ROLE:SECONDARY"));
        }
        return;
    }

    // GET_ROLE - query current device role
    if (strcmp(command, "GET_ROLE") == 0)
    {
        Serial.printf("[CONFIG] Current role: %s\n", deviceRoleToString(deviceRole));
        return;
    }

    // GET_VER - query firmware version
    if (strcmp(command, "GET_VER") == 0)
    {
        Serial.printf("VER:%s\n", FIRMWARE_VERSION);
        return;
    }

    // SET_PROFILE - change default therapy profile (persisted)
    if (strncmp(command, "SET_PROFILE:", 12) == 0)
    {
        const char *profileStr = command + 12;
        const char *internalName = nullptr;

        // Map user-friendly names to internal profile names
        if (strcasecmp(profileStr, "REGULAR") == 0)
        {
            internalName = "regular_vcr";
        }
        else if (strcasecmp(profileStr, "NOISY") == 0)
        {
            internalName = "noisy_vcr";
        }
        else if (strcasecmp(profileStr, "HYBRID") == 0)
        {
            internalName = "hybrid_vcr";
        }
        else if (strcasecmp(profileStr, "GENTLE") == 0)
        {
            internalName = "gentle";
        }

        if (internalName && profiles.loadProfileByName(internalName))
        {
            profiles.saveSettings();

            // Stop any active therapy session before rebooting
            therapy.stop();
            safeMotorShutdown();
            stateMachine.transition(StateTrigger::STOP_SESSION);

            Serial.printf("[CONFIG] Profile set to %s - restarting...\n", profileStr);
            Serial.flush();
            delay(100);
            NVIC_SystemReset();
        }
        else
        {
            Serial.println(F("[ERROR] Invalid profile. Use: SET_PROFILE:REGULAR, NOISY, HYBRID, or GENTLE"));
        }
        return;
    }

    // GET_PROFILE - query current profile
    if (strcmp(command, "GET_PROFILE") == 0)
    {
        const char *name = profiles.getCurrentProfileName();
        // Map internal name back to user-friendly name for output
        const char *displayName = name;
        if (strcasecmp(name, "regular_vcr") == 0)
            displayName = "REGULAR";
        else if (strcasecmp(name, "noisy_vcr") == 0)
            displayName = "NOISY";
        else if (strcasecmp(name, "hybrid_vcr") == 0)
            displayName = "HYBRID";
        else if (strcasecmp(name, "gentle") == 0)
            displayName = "GENTLE";

        Serial.printf("PROFILE:%s\n", displayName);
        return;
    }

    // =========================================================================
    // LATENCY METRICS COMMANDS
    // =========================================================================

    // LATENCY_ON - Enable latency metrics (aggregated mode)
    if (strcmp(command, "LATENCY_ON") == 0)
    {
        latencyMetrics.enable(false);
        return;
    }

    // LATENCY_ON_VERBOSE - Enable latency metrics with per-buzz logging
    if (strcmp(command, "LATENCY_ON_VERBOSE") == 0)
    {
        latencyMetrics.enable(true);
        return;
    }

    // LATENCY_OFF - Disable latency metrics
    if (strcmp(command, "LATENCY_OFF") == 0)
    {
        latencyMetrics.disable();
        return;
    }

    // GET_LATENCY - Print current latency metrics report
    if (strcmp(command, "GET_LATENCY") == 0)
    {
        latencyMetrics.printReport();
        return;
    }

    // RESET_LATENCY - Reset all latency metrics
    if (strcmp(command, "RESET_LATENCY") == 0)
    {
        latencyMetrics.reset();
        Serial.println(F("[LATENCY] Metrics reset"));
        return;
    }

    // =========================================================================

    // FACTORY_RESET - delete settings file and reboot
    if (strcmp(command, "FACTORY_RESET") == 0)
    {
        Serial.println(F("[CONFIG] Factory reset - deleting settings..."));
        if (InternalFS.remove(SETTINGS_FILE))
        {
            Serial.println(F("[CONFIG] Settings deleted successfully"));
        }
        else
        {
            Serial.println(F("[CONFIG] No settings file to delete"));
        }
        safeMotorShutdown();  // Ensure motors off before reset
        Serial.println(F("[CONFIG] Rebooting..."));
        Serial.flush();
        delay(100);
        NVIC_SystemReset();
        return;
    }

    // REBOOT - restart the device
    if (strcmp(command, "REBOOT") == 0)
    {
        safeMotorShutdown();  // Ensure motors off before reset
        Serial.println(F("[CONFIG] Rebooting..."));
        Serial.flush();
        delay(100);
        NVIC_SystemReset();
        return;
    }

    // Not a serial-only command, pass to regular BLE message handler
    onBLEMessage(0, command);
}
