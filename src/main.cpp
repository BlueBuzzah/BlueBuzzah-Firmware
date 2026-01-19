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
#include "rtos.h"  // FreeRTOS for motor task
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
#include "deferred_queue.h"
#include "activation_queue.h"
#include "motor_event_buffer.h"

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
uint32_t lastKeepalive = 0;        // Time of last keepalive PING sent (PRIMARY)
uint32_t lastStatusPrint = 0;

// Connection state
bool wasConnected = false;

// Therapy state tracking (for detecting session end)
bool wasTherapyRunning = false;

// Boot window auto-start tracking (PRIMARY only)
// When SECONDARY connects but phone doesn't within 30s, auto-start therapy
// SP-H1 fix: Mark as volatile since written in BLE callback, read in loop
volatile uint32_t bootWindowStart = 0;    // When SECONDARY connected (starts countdown)
volatile bool bootWindowActive = false;   // Whether we're waiting for phone
bool autoStartTriggered = false; // Prevent repeated auto-starts (only accessed from main loop)

// Sync validity delayed start (PRIMARY only)
// If sync not valid when auto-start triggers, retry after 1 second
bool autoStartScheduled = false;   // Whether we're waiting to retry auto-start
uint32_t autoStartTime = 0;        // When to retry auto-start (millis)

// Keepalive monitoring (bidirectional via PING/PONG)
// MUST be volatile: updated in BLE callback context, read in main loop
volatile uint32_t lastKeepaliveReceived = 0;  // SECONDARY: Last PING/BUZZ from PRIMARY
volatile uint32_t lastSecondaryKeepalive = 0; // PRIMARY: Last PONG from SECONDARY

// PRIMARY-side keepalive timeout
// Aligned with SECONDARY's KEEPALIVE_TIMEOUT_MS (6000) to prevent race conditions
// where PRIMARY shuts down before SECONDARY has timed out
static constexpr uint32_t PRIMARY_KEEPALIVE_TIMEOUT_MS = 6000; // 6 seconds

// PING/PONG latency measurement (PRIMARY only)
// MUST be volatile: written in main loop, read in BLE callback
volatile uint64_t pingStartTime = 0; // Timestamp when PING was sent (micros)
volatile uint64_t pingT1 = 0;        // T1 for PTP offset calculation

// SP-C5 fix: Use binary semaphore instead of volatile bool to prevent missed signals
// Old pattern had race: callback sets true, loop reads+clears, callback sets again, signal lost
SemaphoreHandle_t safetyShutdownSema = nullptr;

// Debug flash state (synchronized LED flash at macrocycle start)
bool debugFlashActive = false;
uint32_t debugFlashEndTime = 0;
RGBColor savedLedColor;
LEDPattern savedLedPattern = LEDPattern::SOLID;

// Pending PTP-scheduled flash (non-blocking - checked in loop())
// These must be volatile: written in BLE callback, read in main loop
static volatile bool g_pendingFlashActive = false;
static volatile uint64_t g_pendingFlashTime = 0;

// Finger names for display (4 fingers per hand - index through pinky, no thumb per v1)
const char *FINGER_NAMES[] = {"Index", "Middle", "Ring", "Pinky"};

// =============================================================================
// ATOMIC 64-BIT ACCESS HELPERS
// =============================================================================
// On 32-bit ARM, 64-bit reads/writes are NOT atomic. A read can see a
// partially-written value (torn read) when the write is interrupted mid-update.
// These helpers disable interrupts to ensure atomic access.

static inline uint64_t atomicRead64(volatile uint64_t* ptr) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint64_t val = *ptr;
    __set_PRIMASK(primask);
    return val;
}

static inline void atomicWrite64(volatile uint64_t* ptr, uint64_t val) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *ptr = val;
    __set_PRIMASK(primask);
}

// =============================================================================
// FREERTOS MOTOR TASK
// =============================================================================
// High-priority task (Priority 4/HIGHEST) for motor events.
// Uses FreeRTOS timing + busy-wait for sub-millisecond precision.
// Handles BOTH activations AND deactivations from unified event queue.

static TaskHandle_t motorTaskHandle = nullptr;

/**
 * @brief Pre-select the next activation's I2C channel
 *
 * Called after a DEACTIVATE to prepare for the next ACTIVATE.
 * This moves the mux selection and frequency setup OFF the critical path,
 * reducing activation latency from ~500μs to ~100μs.
 */
static void preSelectNextActivation() {
    MotorEvent nextEvent;
    if (activationQueue.peekNextEvent(nextEvent) &&
        nextEvent.type == MotorEventType::ACTIVATE &&
        haptic.isEnabled(nextEvent.finger)) {
        // Pre-select channel and set frequency (non-critical path)
        if (haptic.selectChannelPersistent(nextEvent.finger)) {
            haptic.setFrequencyDirect(nextEvent.finger, nextEvent.frequencyHz);
        }
    }
}

/**
 * @brief Execute a motor event (activation or deactivation)
 * @param event The motor event to execute
 *
 * M1 fix: Captures lateness AFTER motor I2C operations for accurate timing.
 * H6 fix: Handles 64-bit lateness values correctly in printf.
 * Phase 2: Uses I2C pre-selection for faster activation when available.
 */
static void executeMotorEvent(const MotorEvent& event) {
    uint64_t beforeOp = getMicros();

    if (event.type == MotorEventType::ACTIVATE) {
        if (haptic.isEnabled(event.finger)) {
            // Phase 2: Check if this finger is pre-selected for fast-path activation
            bool usedFastPath = false;
            if (haptic.getPreSelectedFinger() == static_cast<int8_t>(event.finger)) {
                // Fast path: Channel already selected, frequency already set
                // Just write RTP value (~100μs vs ~500μs for full path)
                haptic.activatePreSelected(event.finger, event.amplitude);
                haptic.closeAllChannels();  // Close after activation for bus safety
                usedFastPath = true;
            } else {
                // Slow path: Full mux selection + frequency set + activate
                haptic.setFrequency(event.finger, event.frequencyHz);
                haptic.activate(event.finger, event.amplitude);
            }

            // M1 fix: Capture time AFTER I2C ops for true lateness
            uint64_t afterOp = getMicros();
            int64_t drift_us = static_cast<int64_t>(afterOp - event.timeUs);

            // Record latency metrics (if enabled)
            if (latencyMetrics.enabled) {
                latencyMetrics.recordExecution(static_cast<int32_t>(drift_us));
            }

            if (profiles.getDebugMode()) {
                // H6 fix: Handle 64-bit lateness (split into seconds + microseconds if large)
                if (drift_us >= 0 && drift_us < 1000000) {
                    Serial.printf("[MOTOR_TASK] ACTIVATE F%d A%d @%dHz (drift: %ldus)%s\n",
                                  event.finger, event.amplitude, event.frequencyHz,
                                  static_cast<long>(drift_us),
                                  usedFastPath ? " [FAST]" : "");
                } else {
                    // Large or negative drift - print with more precision
                    Serial.printf("[MOTOR_TASK] ACTIVATE F%d A%d @%dHz (drift: %ld.%06ldus)%s\n",
                                  event.finger, event.amplitude, event.frequencyHz,
                                  static_cast<long>(drift_us / 1000000),
                                  static_cast<long>(drift_us % 1000000),
                                  usedFastPath ? " [FAST]" : "");
                }
            }
        }
    } else {
        haptic.deactivate(event.finger);

        // M1 fix: Capture time AFTER I2C ops for true drift measurement
        uint64_t afterOp = getMicros();
        int64_t drift_us = static_cast<int64_t>(afterOp - event.timeUs);

        // Record latency metrics for deactivation (if enabled)
        // Note: Deactivation timing is less critical than activation for bilateral sync,
        // but we record it for completeness and to track overall timing precision
        if (latencyMetrics.enabled) {
            latencyMetrics.recordExecution(static_cast<int32_t>(drift_us));
        }

        if (profiles.getDebugMode()) {
            // H6 fix: Handle 64-bit drift
            if (drift_us >= 0 && drift_us < 1000000) {
                Serial.printf("[MOTOR_TASK] DEACTIVATE F%d (drift: %ldus)\n",
                              event.finger, static_cast<long>(drift_us));
            } else {
                Serial.printf("[MOTOR_TASK] DEACTIVATE F%d (drift: %ld.%06ldus)\n",
                              event.finger,
                              static_cast<long>(drift_us / 1000000),
                              static_cast<long>(drift_us % 1000000));
            }
        }

        // Phase 2: Pre-select next activation's channel while we have time
        // This moves mux selection OFF the critical path for the next ACTIVATE
        preSelectNextActivation();
    }
    (void)beforeOp;  // Suppress unused warning if debug mode off
}

/**
 * @brief High-priority motor task for event-driven activations/deactivations
 *
 * Runs at Priority 4 (HIGHEST) to preempt main loop (Priority 1).
 * Uses FreeRTOS timing for coarse delays, busy-wait for final precision.
 * Processes events from unified ActivationQueue.
 *
 * Bug fixes applied:
 * - C5: Fixed integer underflow in time calculation
 * - H1: Re-capture timestamp after FreeRTOS sleep
 * - H2: Re-check queue before busy-wait for earlier events
 */
static void motorTask(void* pvParameters) {
    (void)pvParameters;

    // TP-4: Wait for initialization signal before processing events
    // This ensures activationQueue.begin() has completed before we access the queue
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    Serial.println(F("[MOTOR_TASK] Initialization complete, entering main loop"));

    for (;;) {
        MotorEvent event;

        // Check if there are any events in the queue
        if (!activationQueue.peekNextEvent(event)) {
            // No events - block until notified of new event
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        // Calculate time until event
        // C5 fix: Subtract in uint64_t space first, then cast to int64_t
        // This correctly handles both future (positive) and past (negative) events
        uint64_t now = getMicros();
        int64_t delayUs = static_cast<int64_t>(event.timeUs - now);

        if (delayUs <= 0) {
            // Event time already passed - execute immediately
            if (activationQueue.dequeueNextEvent(event)) {
                executeMotorEvent(event);
            }
            continue;
        }

        if (delayUs > 2000) {
            // Event is far away (>2ms) - use FreeRTOS sleep
            // Sleep until 1ms before event, then busy-wait
            TickType_t ticks = pdMS_TO_TICKS((delayUs - 1000) / 1000);
            if (ticks > 0) {
                // Wake early if new event is enqueued (may be earlier than current)
                ulTaskNotifyTake(pdTRUE, ticks);
                // H1 fix: Re-capture time after sleep - original `now` is stale
                continue;  // Re-check queue in case new event is earlier
            }
        }

        // H2 fix: Re-check queue before busy-wait to ensure this is still earliest event
        // A new, earlier event may have been enqueued while we were checking timing
        MotorEvent recheckEvent;
        if (activationQueue.peekNextEvent(recheckEvent) &&
            recheckEvent.timeUs < event.timeUs) {
            // Earlier event exists - restart loop to process it first
            continue;
        }

        // Event is close (<2ms) - busy-wait for precision
        while (getMicros() < event.timeUs) {
            taskYIELD();  // Allow other tasks to run briefly
        }

        // Execute event - dequeue first to ensure we get the same event we peeked
        if (activationQueue.dequeueNextEvent(event)) {
            executeMotorEvent(event);
        }
    }
}

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

void printBanner();
DeviceRole determineRole();
bool initializeHardware();
bool initializeBLE();
bool initializeTherapy();
void printStatus();
void startTherapyTest();
void stopTherapyTest();
void autoStartTherapy();

// PING/PONG provides keepalive + clock sync (PRIMARY only)
void sendPing();

// BLE Callbacks
void onBLEConnect(uint16_t connHandle, ConnectionType type);
void onBLEDisconnect(uint16_t connHandle, ConnectionType type, uint8_t reason);
void onBLEMessage(uint16_t connHandle, const char *message, uint64_t rxTimestamp);

// Therapy Callbacks
void onSendMacrocycle(const Macrocycle& macrocycle);
void onSetFrequency(uint8_t finger, uint16_t frequencyHz);
void onActivate(uint8_t finger, uint8_t amplitude);
void onDeactivate(uint8_t finger);
void onCycleComplete(uint32_t cycleCount);
void onMacrocycleStart(uint32_t macrocycleCount);

// PRIMARY scheduling callbacks (FreeRTOS motor task)
void onScheduleActivation(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude, uint16_t durationMs, uint16_t frequencyHz);
void onStartScheduling();
bool onIsSchedulingComplete();
uint32_t onGetLeadTime();

// State Machine Callback
void onStateChange(const StateTransition &transition);

// Menu Controller Callback
void onMenuSendResponse(const char *response);

// SECONDARY Keepalive Timeout
void handleKeepaliveTimeout();

// Debug flash helper
void triggerDebugFlash();

// Deferred Work Executor
void executeDeferredWork(DeferredWorkType type, uint8_t p1, uint8_t p2, uint32_t p3);

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
 * 2. Clear deferred queue (prevents queued activations)
 * 3. Clear activation queue (prevents scheduled macrocycle events)
 * 4. Emergency stop all motors (final safety net)
 */
void safeMotorShutdown()
{
    // 1. Stop therapy engine FIRST - prevents new motor activations
    therapy.stop();

    // 2. Clear deferred work queue
    deferredQueue.clear();

    // 3. Clear activation queue (macrocycle scheduled events)
    activationQueue.clear();

    // 4. Emergency stop all motors
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

    // SP-C5 fix: Create safety shutdown semaphore (binary semaphore for ISR signaling)
    safetyShutdownSema = xSemaphoreCreateBinary();
    if (!safetyShutdownSema)
    {
        Serial.println(F("[WARN] Failed to create safety semaphore - operating without ISR protection"));
    }

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

    // NOTE: Activation queue is initialized later in Hardware Init section
    // after motor task is created (needs valid motorTaskHandle for notifications)

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
    Serial.println(F("|  Keepalive PING sent every 2 seconds when connected       |"));
    Serial.println(F("|  Status printed every 5 seconds                           |"));
    Serial.println(F("+============================================================+\n"));

    // DEBUG: Confirm setup() completes
    Serial.println(F("[DEBUG] setup() complete - entering loop()"));
    Serial.flush();
}

// =============================================================================
// LOOP
// =============================================================================

void loop()
{
    // SAFETY FIRST: Check for pending shutdown from BLE disconnect callback
    // Must be at VERY TOP before any motor operations to prevent post-disconnect buzz
    // SP-C5 fix: Use semaphore take (non-blocking) instead of volatile bool
    // Semaphore handles multiple signals correctly - each give results in a take
    if (safetyShutdownSema && xSemaphoreTake(safetyShutdownSema, 0) == pdTRUE)
    {
        safeMotorShutdown();
        Serial.println(F("[SAFETY] Emergency motor shutdown complete"));
    }

    // Motor events (activations AND deactivations) handled by motor task
    // No polling needed - motor task uses FreeRTOS timing

    // TP-1: Process staged motor events from BLE callbacks
    // Forward from lock-free staging buffer to mutex-protected activationQueue
    // Defensive check: only process if motor task is initialized
    if (motorTaskHandle != nullptr && motorEventBuffer.hasPending()) {
        // Check if this is a macrocycle batch (needs queue clear first)
        bool isMacrocycleBatch = motorEventBuffer.isMacrocyclePending();
        if (isMacrocycleBatch) {
            activationQueue.clear();  // Start fresh for macrocycle
        }

        uint8_t eventsForwarded = 0;
        StagedMotorEvent staged;
        while (motorEventBuffer.unstage(staged)) {
            activationQueue.enqueue(staged.activateTimeUs, staged.finger, staged.amplitude,
                                   staged.durationMs, staged.frequencyHz);
            eventsForwarded++;

            // If this was the last event in a macrocycle, start scheduling
            if (staged.isMacrocycleLast) {
                activationQueue.scheduleNext();
                if (profiles.getDebugMode()) {
                    Serial.printf("[MACROCYCLE] Forwarded %u events, scheduling started\n",
                                  eventsForwarded);
                }
            }
        }

        // For single ACTIVATE events (not macrocycle), log if debug enabled
        if (!isMacrocycleBatch && eventsForwarded > 0 && profiles.getDebugMode()) {
            Serial.printf("[ACTIVATE] Forwarded %u event(s) from staging buffer\n",
                          eventsForwarded);
        }
    }

    // Process deferred work queue (haptic operations from BLE callbacks)
    deferredQueue.processOne();

    uint32_t now = millis();

    // Check for pending PTP-scheduled flash (SECONDARY only)
    // This is the non-blocking replacement for the old delayMicroseconds() approach
    if (g_pendingFlashActive)
    {
        uint64_t nowUs = getMicros();
        // Use atomic read to prevent torn reads on 32-bit ARM
        if (nowUs >= atomicRead64(&g_pendingFlashTime))
        {
            g_pendingFlashActive = false;
            triggerDebugFlash();
        }
    }

    // Debug flash restoration check
    if (debugFlashActive && now >= debugFlashEndTime)
    {
        debugFlashActive = false;
        led.setPattern(savedLedColor, savedLedPattern);
    }

    // Update LED pattern animation
    led.update();

    // Process BLE events (includes non-blocking TX queue)
    ble.update();

    // Motor events handled by motor task - no polling needed

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
        // Therapy just stopped - show appropriate message based on session type
        Serial.println(F("\n+============================================================+"));
        if (therapy.isTestMode())
        {
            Serial.println(F("|  TEST COMPLETE                                             |"));
        }
        else
        {
            Serial.println(F("|  THERAPY SESSION COMPLETE                                  |"));
        }
        Serial.println(F("+============================================================+\n"));

        haptic.emergencyStop();
        stateMachine.transition(StateTrigger::STOP_SESSION);
        stateMachine.transition(StateTrigger::STOPPED);

        // Resume scanning on SECONDARY after standalone test
        if (deviceRole == DeviceRole::SECONDARY && !ble.isPrimaryConnected())
        {
            Serial.println(F("[SECONDARY] Resuming scanning..."));
            ble.setScannerAutoRestart(true); // Re-enable health check
            ble.startScanning(BLE_NAME);
        }
    }
    wasTherapyRunning = isTherapyRunning;

    // SECONDARY: Check for keepalive timeout during active connection
    if (deviceRole == DeviceRole::SECONDARY && ble.isPrimaryConnected())
    {
        if (lastKeepaliveReceived > 0 &&
            (millis() - lastKeepaliveReceived > KEEPALIVE_TIMEOUT_MS))
        {
            handleKeepaliveTimeout();
        }
    }

    // PRIMARY: Check for SECONDARY keepalive timeout during therapy
    // This detects SECONDARY power-off faster than BLE supervision timeout (~4s)
    if (deviceRole == DeviceRole::PRIMARY && ble.isSecondaryConnected() && therapy.isRunning())
    {
        uint32_t lastKA = lastSecondaryKeepalive;  // Read volatile once
        uint32_t nowMs = millis();
        uint32_t elapsed = nowMs - lastKA;
        if (lastKA > 0 && elapsed > PRIMARY_KEEPALIVE_TIMEOUT_MS)
        {
            Serial.printf("[WARN] SECONDARY keepalive timeout - stopping therapy (lastKA=%lu, now=%lu, elapsed=%lu)\n",
                          (unsigned long)lastKA, (unsigned long)nowMs, (unsigned long)elapsed);

            // Send STOP_SESSION to SECONDARY before shutting down
            // This gives SECONDARY a chance to stop gracefully if BLE is still connected
            if (ble.isSecondaryConnected())
            {
                char buffer[64];
                SyncCommand cmd = SyncCommand::createStopSession(g_sequenceGenerator.next());
                if (cmd.serialize(buffer, sizeof(buffer)))
                {
                    ble.sendToSecondary(buffer);
                    Serial.println(F("[SYNC] Sent STOP_SESSION due to timeout"));
                }
            }

            safeMotorShutdown();
            lastSecondaryKeepalive = 0; // Reset to prevent repeated triggers
        }
    }

    // PRIMARY: Check boot window for auto-start therapy
    // SP-H1 fix: Capture bootWindowStart once atomically before comparison
    // This prevents race where callback updates it between read and arithmetic
    if (deviceRole == DeviceRole::PRIMARY && bootWindowActive && !autoStartTriggered)
    {
        uint32_t startSnapshot = bootWindowStart;  // SP-H1: Capture volatile once
        uint32_t currentTime = millis();
        uint32_t elapsed = currentTime - startSnapshot;

        if (elapsed >= STARTUP_WINDOW_MS)
        {
            // Boot window expired without phone connecting
            if (ble.isSecondaryConnected() && !ble.isPhoneConnected())
            {
                Serial.printf("[BOOT] 30s window expired (now=%lu, start=%lu, elapsed=%lu) - auto-starting therapy\n",
                              (unsigned long)currentTime, (unsigned long)startSnapshot, (unsigned long)elapsed);
                bootWindowActive = false;
                autoStartTriggered = true;
                autoStartTherapy();
            }
            else
            {
                // SECONDARY disconnected during window, cancel
                Serial.printf("[BOOT] Window expired but SECONDARY not connected (now=%lu, start=%lu)\n",
                              (unsigned long)currentTime, (unsigned long)startSnapshot);
                bootWindowActive = false;
            }
        }
    }

    // Check for scheduled auto-start retry (sync wasn't valid on first attempt)
    if (autoStartScheduled && millis() >= autoStartTime)
    {
        autoStartScheduled = false;
        autoStartTherapy();
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

    // Unified keepalive + clock sync: PING every 1 second when connected (PRIMARY only)
    // PING/PONG provides both connection monitoring and continuous clock synchronization
    // Clock sync becomes valid after 3 samples (~3 seconds from connection)
    if (deviceRole == DeviceRole::PRIMARY &&
        isConnected &&
        (now - lastKeepalive >= KEEPALIVE_INTERVAL_MS))
    {
        lastKeepalive = now;
        sendPing();
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

        // Create high-priority motor task for preemptive activations
        // Priority 4 (HIGHEST) ensures motor timing isn't blocked by Serial/BLE
        // Stack size: 512 words (2KB) - increased from 256 to prevent stack overflow
        // that was causing crashes during BLE initialization
        BaseType_t taskCreated = xTaskCreate(
            motorTask,           // Task function
            "Motor",             // Name (for debugging)
            512,                 // Stack size (words = 2KB) - increased for safety
            nullptr,             // Parameters
            TASK_PRIO_HIGHEST,   // Priority 4 - preempts main loop
            &motorTaskHandle     // Handle
        );

        if (taskCreated == pdPASS && motorTaskHandle != nullptr) {
            // Set motor task handle for queue notifications
            activationQueue.begin(&haptic, motorTaskHandle);
            // TP-4: Release motor task to run now that queue is initialized
            xTaskNotifyGive(motorTaskHandle);
            Serial.println(F("[SUCCESS] Motor task created and released at Priority 4 (FreeRTOS timing)"));
        } else {
            Serial.println(F("[WARN] Motor task creation failed - motors will not function"));
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

    // Start scanning for SECONDARY role
    // Note: PRIMARY advertising is started in setupAdvertising() during ble.begin()
    if (deviceRole == DeviceRole::SECONDARY)
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
        therapy.setSendMacrocycleCallback(onSendMacrocycle);
        // Set macrocycle start callback for PING/PONG latency measurement
        therapy.setMacrocycleStartCallback(onMacrocycleStart);
        // Set frequency callback for Custom vCR frequency randomization
        therapy.setSetFrequencyCallback(onSetFrequency);
        // Set scheduling callbacks for FreeRTOS motor task
        therapy.setSchedulingCallbacks(onScheduleActivation, onStartScheduling, onIsSchedulingComplete);
        // Set adaptive lead time callback for RTT-based scheduling
        therapy.setGetLeadTimeCallback(onGetLeadTime);
    }
    return true;
}

// =============================================================================
// BLE EVENT HANDLERS
// =============================================================================

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
            uint32_t timeSinceHB = millis() - lastKeepaliveReceived;
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
    // Note: SECONDARY has no warm-start logic because it doesn't maintain sync state.
    // SECONDARY receives clock offset from PRIMARY in every MACROCYCLE message, so it
    // simply waits for the next MACROCYCLE after reconnection.
    if (deviceRole == DeviceRole::SECONDARY && type == ConnectionType::PRIMARY)
    {
        Serial.println(F("[SECONDARY] Sending IDENTIFY:SECONDARY to PRIMARY"));
        ble.sendToPrimary("IDENTIFY:SECONDARY");
        // Start keepalive timeout tracking
        lastKeepaliveReceived = millis();
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
            // Initialize keepalive tracking (timeout detection starts when first PONG received)
            lastSecondaryKeepalive = millis();
            Serial.printf("[BOOT] SECONDARY connected at %lu - starting 30s boot window for phone\n",
                          (unsigned long)bootWindowStart);

            // Reset clock sync state
            syncProtocol.resetClockSync();

            // Try warm-start if recent disconnect (cache preserved across resetClockSync)
            if (syncProtocol.tryWarmStart())
            {
                Serial.println(F("[SYNC] Warm-start mode - need 3 confirmatory samples (~3s)"));
            }
            else
            {
                Serial.println(F("[SYNC] Cold start - need 5 samples for sync (~5s)"));
            }
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

    // Log HCI disconnect reason for diagnostics (helps identify interference vs intentional)
    const char *reasonStr = "UNKNOWN";
    switch (reason)
    {
    case 0x08:
        reasonStr = "SUPERVISION_TIMEOUT";
        break;
    case 0x13:
        reasonStr = "REMOTE_TERMINATED";
        break;
    case 0x16:
        reasonStr = "LOCAL_TERMINATED";
        break;
    case 0x22:
        reasonStr = "LMP_TIMEOUT";
        break;
    case 0x3B:
        reasonStr = "CONN_PARAMS_REJECTED";
        break;
    }
    Serial.printf("[DISCONNECT] HCI Reason: %s\n", reasonStr);

    // Update state machine on relevant disconnections
    if ((deviceRole == DeviceRole::PRIMARY && type == ConnectionType::SECONDARY) ||
        (deviceRole == DeviceRole::SECONDARY && type == ConnectionType::PRIMARY))
    {
        stateMachine.transition(StateTrigger::DISCONNECTED);

        // SAFETY: Signal main loop to execute motor shutdown
        // Cannot call safeMotorShutdown() directly here (BLE callback = ISR context, no I2C)
        // SP-C5 fix: Use semaphore instead of volatile bool to prevent missed signals
        if (safetyShutdownSema)
        {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(safetyShutdownSema, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }

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
    // (queued AFTER safety shutdown semaphore is signaled) should still execute for user feedback
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
            // Use ActivationQueue for proper timing - motor task handles deactivation
            uint64_t now = getMicros();
            activationQueue.enqueue(now, finger, amplitude, duration, 250);  // 250Hz default
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
            // Use ActivationQueue for both pulses - motor task handles timing
            uint64_t now = getMicros();
            // First pulse
            activationQueue.enqueue(now, finger, amplitude, duration, 250);
            // Second pulse after duration + 100ms gap
            uint64_t secondPulseTime = now + ((uint64_t)(duration + 100) * 1000ULL);
            activationQueue.enqueue(secondPulseTime, finger, amplitude, duration, 250);
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
        // Restart BLE scanner
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

void onBLEMessage(uint16_t connHandle [[maybe_unused]], const char *message, uint64_t rxTimestamp)
{
    // rxTimestamp is captured at the earliest possible point in the BLE stack
    // (immediately when data is received in _onUartRx/_onClientUartRx)
    // This provides maximum accuracy for PTP clock synchronization

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

    // Handle LED_OFF_SYNC from PRIMARY (SECONDARY only)
    if (deviceRole == DeviceRole::SECONDARY && strncmp(message, "LED_OFF_SYNC:", 13) == 0)
    {
        int value = atoi(message + 13);
        profiles.setTherapyLedOff(value != 0);
        profiles.saveSettings();
        Serial.printf("[SYNC] LED_OFF_SYNC received: %d\n", value);

        // Update LED immediately if currently running therapy
        if (stateMachine.getCurrentState() == TherapyState::RUNNING)
        {
            if (value)
            {
                led.setPattern(Colors::GREEN, LEDPattern::OFF);
            }
            else
            {
                led.setPattern(Colors::GREEN, LEDPattern::PULSE_SLOW);
            }
        }
        return;
    }

    // Handle DEBUG_SYNC from PRIMARY (SECONDARY only)
    if (deviceRole == DeviceRole::SECONDARY && strncmp(message, "DEBUG_SYNC:", 11) == 0)
    {
        int value = atoi(message + 11);
        profiles.setDebugMode(value != 0);
        profiles.saveSettings();
        Serial.printf("[SYNC] DEBUG_SYNC received: %d\n", value);
        return;
    }

    // Handle MACROCYCLE messages (special format - not standard SyncCommand)
    // Format: MC:seq|baseTime|count|d,f,a,dur,fo|...
    if (strncmp(message, "MC:", 3) == 0)
    {
        if (deviceRole == DeviceRole::SECONDARY)
        {
            // Track connectivity - MACROCYCLE proves PRIMARY is alive
            lastKeepaliveReceived = millis();

            // Parse macrocycle from V2 format (includes clock offset)
            Macrocycle mc;
            if (SyncCommand::deserializeMacrocycle(message, strlen(message), mc))
            {
                // Apply clock offset from PRIMARY (V2 format)
                // PRIMARY calculated this offset and sent it in the message
                // CRITICAL: Cast baseTime to signed before adding signed offset,
                // otherwise negative offset becomes large positive when implicitly converted
                int64_t offset = mc.clockOffset;

                // SAFETY: Reject obviously invalid offsets
                // Valid offset should be within ±35 seconds (35,000,000 µs)
                // SECONDARY can connect up to 30s after PRIMARY boot, plus 5s margin
                constexpr int64_t MAX_VALID_OFFSET = 35000000LL;  // 35 seconds in microseconds
                if (offset > MAX_VALID_OFFSET || offset < -MAX_VALID_OFFSET)
                {
                    // SP-C3 fix: Use split print for 64-bit value (ARM long is 32-bit)
                    int64_t offsetSec = offset / 1000000;
                    int64_t offsetUs = offset % 1000000;
                    if (offset < 0 && offsetUs != 0) offsetUs = -offsetUs;  // Handle negative correctly
                    Serial.printf("[ERROR] MACROCYCLE rejected: invalid offset %ld.%06ldus (exceeds ±35s)\n",
                                  (long)offsetSec, (long)offsetUs);
                    // Still send ACK to avoid retry storms, but don't execute
                    SyncCommand ackCmd = SyncCommand::createMacrocycleAck(mc.sequenceId);
                    char ackBuffer[32];
                    if (ackCmd.serialize(ackBuffer, sizeof(ackBuffer)))
                    {
                        ble.sendToPrimary(ackBuffer);
                    }
                    return;
                }

                uint64_t localBaseTime = static_cast<uint64_t>(static_cast<int64_t>(mc.baseTime) + offset);

                // SAFETY: Validate localBaseTime is reasonable (within ±30 seconds of now)
                uint64_t nowUs = getMicros();

                // Debug logging for offset application
                if (profiles.getDebugMode())
                {
                    int64_t timeUntilExec = static_cast<int64_t>(localBaseTime) - static_cast<int64_t>(nowUs);
                    Serial.printf("[MACROCYCLE] Received seq=%lu offset=%ld baseTime=%lu -> localBaseTime=%lu (rxAt=%lu, timeUntilExec=%ld)\n",
                                  (unsigned long)mc.sequenceId,
                                  (long)offset,
                                  (unsigned long)(mc.baseTime / 1000),
                                  (unsigned long)(localBaseTime / 1000),
                                  (unsigned long)(nowUs / 1000),
                                  (long)(timeUntilExec / 1000));
                }
                int64_t timeDiff = static_cast<int64_t>(localBaseTime) - static_cast<int64_t>(nowUs);
                constexpr int64_t MAX_TIME_DIFF = 30000000LL;  // 30 seconds
                if (timeDiff > MAX_TIME_DIFF || timeDiff < -MAX_TIME_DIFF)
                {
                    // SP-C3 fix: Use split print for 64-bit value (ARM long is 32-bit)
                    int64_t diffSec = timeDiff / 1000000;
                    Serial.printf("[ERROR] MACROCYCLE rejected: baseTime %ld seconds from now\n",
                                  (long)diffSec);  // Division reduces to 32-bit safe range
                    // Still send ACK to avoid retry storms
                    SyncCommand ackCmd = SyncCommand::createMacrocycleAck(mc.sequenceId);
                    char ackBuffer[32];
                    if (ackCmd.serialize(ackBuffer, sizeof(ackBuffer)))
                    {
                        ble.sendToPrimary(ackBuffer);
                    }
                    return;
                }

                // TP-1: Stage all events via lock-free buffer (ISR-safe)
                // Main loop will forward to activationQueue and call scheduleNext()
                motorEventBuffer.beginMacrocycle();
                uint8_t validEvents = 0;
                uint8_t lastValidIndex = 0;

                // First pass: count valid events to mark the last one
                for (uint8_t i = 0; i < mc.eventCount; i++)
                {
                    const MacrocycleEvent& evt = mc.events[i];
                    if (evt.amplitude > 0 && evt.finger < MAX_ACTUATORS)
                    {
                        lastValidIndex = i;
                        validEvents++;
                    }
                }

                // Second pass: stage all valid events
                uint8_t stagedCount = 0;
                for (uint8_t i = 0; i < mc.eventCount; i++)
                {
                    const MacrocycleEvent& evt = mc.events[i];

                    // Skip invalid events (garbage from truncated messages)
                    if (evt.amplitude == 0 || evt.finger >= MAX_ACTUATORS)
                    {
                        continue;
                    }

                    uint64_t localActivateTime = localBaseTime + (evt.deltaTimeMs * 1000ULL);
                    uint16_t freqHz = evt.getFrequencyHz();
                    bool isLast = (i == lastValidIndex);

                    motorEventBuffer.stage(localActivateTime, evt.finger, evt.amplitude,
                                           evt.durationMs, freqHz, isLast);
                    stagedCount++;
                }

                // Note: scheduleNext() will be called by main loop after forwarding events
                // Serial.printf moved to main loop to avoid ISR context I/O

                // Send ACK immediately
                SyncCommand ackCmd = SyncCommand::createMacrocycleAck(mc.sequenceId);
                char ackBuffer[32];
                if (ackCmd.serialize(ackBuffer, sizeof(ackBuffer)))
                {
                    ble.sendToPrimary(ackBuffer);
                }
            }
            else
            {
                Serial.println(F("[ERROR] Failed to parse MACROCYCLE"));
            }
        }
        return;
    }

    // Handle MACROCYCLE_ACK messages
    if (strncmp(message, "MC_ACK:", 7) == 0)
    {
        if (deviceRole == DeviceRole::PRIMARY)
        {
            lastSecondaryKeepalive = millis();
            // TODO: TherapyEngine will handle ACK tracking in Phase 4
            if (profiles.getDebugMode())
            {
                // Parse sequence ID from message
                uint32_t seqId = strtoul(message + 7, nullptr, 10);
                Serial.printf("[MACROCYCLE] ACK received seq=%lu\n", (unsigned long)seqId);
            }
        }
        return;
    }

    // Parse sync/internal commands
    SyncCommand cmd;
    if (cmd.deserialize(message))
    {
        // Handle specific command types
        switch (cmd.getType())
        {
        case SyncCommandType::PING:
            // SECONDARY: Reply with PONG including T2 (early capture), T3 (just before send)
            // Also tracks connectivity - PING proves PRIMARY is alive
            if (deviceRole == DeviceRole::SECONDARY)
            {
                // Track connectivity - PING proves PRIMARY is alive
                lastKeepaliveReceived = millis();

                // T2 = rxTimestamp captured at callback entry (before parsing)
                // This gives us the most accurate receive timestamp
                uint64_t t2 = rxTimestamp;

                // Prepare buffer first, then capture T3 right before send
                char buffer[64];
                uint32_t seqId = cmd.getSequenceId();

                // Capture T3 as late as possible before sending
                // Note: T3 must be captured BEFORE send since it goes in the message
                // Best we can do is minimize work between T3 capture and send call
                uint64_t t3 = getMicros();
                SyncCommand pong = SyncCommand::createPongWithTimestamps(seqId, t2, t3);
                if (pong.serialize(buffer, sizeof(buffer)))
                {
                    ble.sendToPrimary(buffer);

                    // Debug logging (matches PRIMARY's PONG handler logging)
                    if (profiles.getDebugMode())
                    {
                        // Arduino printf doesn't support %llu - cast to uint32_t (timestamps fit in 32-bit for ~71 minutes)
                        Serial.printf("[SYNC] PING seq=%lu T2=%lu T3=%lu -> PONG sent\n",
                                      (unsigned long)seqId,
                                      (unsigned long)(t2 / 1000),  // Convert to ms for readability
                                      (unsigned long)(t3 / 1000));
                    }
                }
            }
            break;

        case SyncCommandType::PONG:
        {
            // PRIMARY: Any PONG proves SECONDARY is alive (keepalive)
            // Update keepalive UNCONDITIONALLY - don't require pingT1 > 0
            // (PONG might arrive late after pingT1 cleared, but still proves link alive)
            if (deviceRole == DeviceRole::PRIMARY)
            {
                lastSecondaryKeepalive = millis();
            }

            // PRIMARY: Calculate RTT and PTP clock offset (requires valid pingT1)
            // Use atomic read to prevent torn reads on 32-bit ARM
            uint64_t t1_atomic = atomicRead64(&pingT1);
            if (deviceRole == DeviceRole::PRIMARY && t1_atomic > 0)
            {

                // T4 = rxTimestamp captured at callback entry (before parsing)
                // This gives us the most accurate receive timestamp
                uint64_t t4 = rxTimestamp;
                uint64_t t1 = t1_atomic;

                // Parse T2 and T3 from PONG data
                // Format depends on whether high bits are used (see createPongWithTimestamps)
                // C4 fix: Use getDataUnsigned to avoid sign extension when values > 2^31
                uint64_t t2, t3;
                if (cmd.hasData("2"))
                {
                    // Full 64-bit: T2High|T2Low|T3High|T3Low
                    uint32_t t2High = cmd.getDataUnsigned("0", 0);
                    uint32_t t2Low = cmd.getDataUnsigned("1", 0);
                    uint32_t t3High = cmd.getDataUnsigned("2", 0);
                    uint32_t t3Low = cmd.getDataUnsigned("3", 0);
                    t2 = ((uint64_t)t2High << 32) | t2Low;
                    t3 = ((uint64_t)t3High << 32) | t3Low;
                }
                else
                {
                    // Simple 32-bit: T2|T3
                    t2 = static_cast<uint64_t>(cmd.getDataUnsigned("0", 0));
                    t3 = static_cast<uint64_t>(cmd.getDataUnsigned("1", 0));
                }

                // Calculate RTT using IEEE 1588 PTP formula (excludes SECONDARY processing)
                // RTT = (T4 - T1) - (T3 - T2)
                // This isolates network latency from SECONDARY processing time
                uint32_t processingTime = (uint32_t)(t3 - t2);
                uint32_t totalRoundTrip = (uint32_t)(t4 - t1);

                // Bounds check: processing time should be positive and reasonable
                if (t3 < t2) {
                    Serial.println("[SYNC] WARNING: Negative processing time detected (clock error)");
                    processingTime = 0;  // Fallback to prevent underflow
                } else if (processingTime > 10000) {  // >10ms is excessive
                    Serial.printf("[SYNC] WARNING: Excessive processing time: %lu us\n",
                                  (unsigned long)processingTime);
                }

                uint32_t rtt = totalRoundTrip - processingTime;

#ifdef DEBUG_SYNC_TIMING
                Serial.printf("[RTT] Total: %lu us, Processing: %lu us, Network: %lu us\n",
                              (unsigned long)totalRoundTrip,
                              (unsigned long)processingTime,
                              (unsigned long)rtt);
#endif

                // Calculate PTP clock offset
                int64_t offset = syncProtocol.calculatePTPOffset(t1, t2, t3, t4);

                // Add sample with RTT-based quality filtering
                // High-RTT samples are rejected as they likely have asymmetric delays
                bool sampleAccepted = false;
                if (syncProtocol.isClockSyncValid())
                {
                    // Already synced - use EMA update (no RTT filtering for maintenance)
                    syncProtocol.updateOffsetEMA(offset);
                    sampleAccepted = true;
                }
                else
                {
                    // Building initial sync - use quality filtering
                    sampleAccepted = syncProtocol.addOffsetSampleWithQuality(offset, rtt);
                }

                // Also update RTT-based latency for backward compatibility
                syncProtocol.updateLatency(rtt);

                // Record RTT for latency metrics (if enabled)
                if (latencyMetrics.enabled) {
                    latencyMetrics.recordRtt(rtt);
                }

                // Enhanced logging (DEBUG only)
                if (profiles.getDebugMode())
                {
                    Serial.printf("[SYNC] RTT=%lu offset_raw=%ld offset_median=%ld offset_corrected=%ld valid=%d samples=%u %s\n",
                                  (unsigned long)rtt,
                                  (long)offset,
                                  (long)syncProtocol.getMedianOffset(),
                                  (long)syncProtocol.getCorrectedOffset(),
                                  syncProtocol.isClockSyncValid() ? 1 : 0,
                                  syncProtocol.getOffsetSampleCount(),
                                  sampleAccepted ? "" : "(rejected)");
                }

                // Use atomic writes for consistency with atomic reads
                atomicWrite64(&pingT1, 0);
                atomicWrite64(&pingStartTime, 0);
            }
        }
        break;

        case SyncCommandType::BUZZ:
        {
            // BUZZ command deprecated - MACROCYCLE-only architecture
            Serial.println(F("[WARN] Received deprecated BUZZ command - firmware uses MACROCYCLE only"));
            // Note: BUZZ enum kept for protocol versioning, but command is no longer supported
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

        case SyncCommandType::DEBUG_FLASH:
            // SECONDARY: Flash LED (with PTP scheduling if available)
            if (deviceRole == DeviceRole::SECONDARY && profiles.getDebugMode())
            {
                // Check if this is a PTP sync command with scheduled flash time
                bool hasPTPTime = cmd.hasData("0");
                if (hasPTPTime && syncProtocol.isClockSyncValid())
                {
                    // Parse flash time from command
                    uint64_t flashTime;
                    if (cmd.hasData("1"))
                    {
                        // Full 64-bit: timeHigh|timeLow
                        // SP-C1 fix: Use getDataUnsigned() to avoid sign extension for values > 2^31
                        uint32_t timeHigh = cmd.getDataUnsigned("0", 0);
                        uint32_t timeLow = cmd.getDataUnsigned("1", 0);
                        flashTime = ((uint64_t)timeHigh << 32) | timeLow;
                    }
                    else
                    {
                        // Simple 32-bit
                        // SP-C1 fix: Use getDataUnsigned() to avoid sign extension
                        flashTime = static_cast<uint64_t>(cmd.getDataUnsigned("0", 0));
                    }

                    // Convert PRIMARY clock time to local (SECONDARY) clock time
                    // Use drift-corrected offset for better accuracy between sync events
                    int64_t offset = syncProtocol.getCorrectedOffset();
                    uint64_t localFlashTime = flashTime + offset;

                    // NON-BLOCKING FIX: Schedule flash for later instead of busy-waiting
                    // The old code used delayMicroseconds() which blocked BLE callback
                    // processing for ~300ms, causing MACROCYCLE messages to queue up
                    // and arrive after their scheduled execution time.
                    // Now we store the flash time and check it in the main loop.
                    // Use atomic write since this runs in BLE callback (ISR) context
                    atomicWrite64(&g_pendingFlashTime, localFlashTime);
                    g_pendingFlashActive = true;
                }
                else
                {
                    // Legacy mode: flash immediately
                    triggerDebugFlash();
                }
            }
            break;

        // Note: MACROCYCLE and MACROCYCLE_ACK are handled above (special format)
        // These cases won't be reached since MC: messages are intercepted earlier

        default:
            break;
        }
    }
}

// =============================================================================
// THERAPY CALLBACKS
// =============================================================================

void onSendMacrocycle(const Macrocycle& macrocycle)
{
    // PRIMARY: Send entire macrocycle (12 events) to SECONDARY in a single message
    // SECONDARY will apply clock offset once and schedule all activations

    if (deviceRole != DeviceRole::PRIMARY || !ble.isSecondaryConnected())
    {
        return;
    }

    // Clear activation queue for new macrocycle (PRIMARY will enqueue via callbacks)
    activationQueue.clear();

    // Make a local copy to set clock offset (callback receives const reference)
    Macrocycle mcCopy = macrocycle;

    // Set clock offset for SECONDARY (V2 format)
    // This allows SECONDARY to convert PRIMARY's baseTime to its local clock
    mcCopy.clockOffset = syncProtocol.getCorrectedOffset();

    // Serialize macrocycle to buffer
    char buffer[MESSAGE_BUFFER_SIZE];
    if (SyncCommand::serializeMacrocycle(buffer, sizeof(buffer), mcCopy))
    {
        ble.sendToSecondary(buffer);

        if (profiles.getDebugMode())
        {
            Serial.printf("[MACROCYCLE] Sent seq=%lu events=%u baseTime=%lu offset=%ld\n",
                          (unsigned long)macrocycle.sequenceId,
                          macrocycle.eventCount,
                          (unsigned long)(macrocycle.baseTime / 1000),
                          (long)mcCopy.clockOffset);
        }
    }
    else
    {
        Serial.println(F("[ERROR] Failed to serialize MACROCYCLE"));
    }
}

void onActivate(uint8_t finger, uint8_t amplitude)
{
    // When SECONDARY is connected, MACROCYCLE batching handles PRIMARY activation
    // scheduling via FreeRTOS motor task. Skip here to avoid duplicate activation.
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

// =============================================================================
// PRIMARY SCHEDULING CALLBACKS (FreeRTOS Motor Task)
// =============================================================================

void onScheduleActivation(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
                          uint16_t durationMs, uint16_t frequencyHz)
{
    // Enqueue activation to ActivationQueue for FreeRTOS motor task
    // This is called by TherapyEngine for each event in a macrocycle
    activationQueue.enqueue(activateTimeUs, finger, amplitude, durationMs, frequencyHz);
}

void onStartScheduling()
{
    // Signal motor task that events are ready
    // Motor task will wake and process events
    activationQueue.scheduleNext();
}

bool onIsSchedulingComplete()
{
    // Check if all scheduled activations have completed (activated and deactivated)
    return activationQueue.isComplete();
}

uint32_t onGetLeadTime()
{
    // Return adaptive lead time based on measured RTT + 3σ margin
    return syncProtocol.calculateAdaptiveLeadTime();
}

void onCycleComplete(uint32_t cycleCount)
{
    Serial.printf("[THERAPY] Cycle %lu complete\n", cycleCount);
}

void onMacrocycleStart(uint32_t macrocycleCount)
{
    // Clock sync handled by main loop 1-second PING interval
    // No additional PING needed at macrocycle boundary

    // DEBUG flash: Trigger synchronized LED flash on both devices at macrocycle start
    if (profiles.getDebugMode())
    {
        if (deviceRole == DeviceRole::PRIMARY && ble.isSecondaryConnected())
        {
            char buffer[64];

            if (syncProtocol.isClockSyncValid())
            {
                // PTP SYNC MODE: Schedule flash at absolute time
                // Use adaptive lead time based on current RTT statistics
                uint32_t leadTimeUs = syncProtocol.calculateAdaptiveLeadTime();
                uint64_t flashTime = getMicros() + leadTimeUs;

                SyncCommand cmd = SyncCommand::createDebugFlashWithTime(g_sequenceGenerator.next(), flashTime);
                if (cmd.serialize(buffer, sizeof(buffer)))
                {
                    ble.sendToSecondary(buffer);
                }

                // NON-BLOCKING: Schedule local flash for later (checked in main loop)
                // Critical: We must NOT block here - the MACROCYCLE is sent after this
                // callback returns. Blocking delays MACROCYCLE transmission!
                // Use atomic write to prevent torn reads on 32-bit ARM
                atomicWrite64(&g_pendingFlashTime, flashTime);
                g_pendingFlashActive = true;
            }
            else
            {
                // LEGACY MODE: Use RTT/2 latency estimation
                SyncCommand cmd = SyncCommand::createDebugFlash(g_sequenceGenerator.next());
                if (cmd.serialize(buffer, sizeof(buffer)))
                {
                    ble.sendToSecondary(buffer);
                }

                // NON-BLOCKING: Schedule local flash for later (checked in main loop)
                uint32_t latencyUs = syncProtocol.getMeasuredLatency();
                if (latencyUs > 0)
                {
                    // Use atomic write to prevent torn reads on 32-bit ARM
                    atomicWrite64(&g_pendingFlashTime, getMicros() + latencyUs);
                    g_pendingFlashActive = true;
                }
                else
                {
                    triggerDebugFlash();  // No latency data, flash immediately
                }
            }
        }
        else
        {
            // Standalone or SECONDARY (for fallback debug testing)
            triggerDebugFlash();
        }
    }

    (void)macrocycleCount; // Suppress unused parameter warning
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

    // Warn if sync not valid when SECONDARY is connected (timing may be off)
    if (deviceRole == DeviceRole::PRIMARY && ble.isSecondaryConnected() &&
        !syncProtocol.isClockSyncValid())
    {
        Serial.println(F("[WARN] Starting test with invalid sync - timing may be misaligned"));
    }

    // Get current profile
    const TherapyProfile *profile = profiles.getCurrentProfile();
    if (!profile)
    {
        Serial.println(F("[TEST] No profile loaded!"));
        return;
    }

    // Convert pattern type string to constant
    PatternType patternType = PatternType::RNDP;
    if (strcmp(profile->patternType, "sequential") == 0)
    {
        patternType = PatternType::SEQUENTIAL;
    }
    else if (strcmp(profile->patternType, "mirrored") == 0)
    {
        patternType = PatternType::MIRRORED;
    }

    // Stop scanning during standalone test (SECONDARY only)
    if (deviceRole == DeviceRole::SECONDARY)
    {
        ble.setScannerAutoRestart(false); // Prevent health check from restarting
        ble.stopScanning();
        Serial.println(F("[TEST] Scanning paused for standalone test"));
    }

    // Test sessions use fixed duration, not profile duration
    uint32_t durationSec = TEST_DURATION_SEC;

    Serial.println(F("\n+============================================================+"));
    Serial.printf("|  STARTING %lu-SECOND TEST SESSION  (send STOP to end)      |\n", durationSec);
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

    // Reset latency metrics for fresh measurements
    if (deviceRole == DeviceRole::PRIMARY)
    {
        syncProtocol.resetLatency();  // Clear EMA state for fresh warmup
    }

    // Start test session using profile settings (send STOP to end early)
    therapy.startSession(
        durationSec,
        patternType,
        profile->timeOnMs,
        profile->timeOffMs,
        profile->jitterPercent,
        profile->numFingers,
        profile->mirrorPattern,
        profile->amplitudeMin,
        profile->amplitudeMax,
        true);  // isTestMode = true
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

    // Check sync validity before starting therapy (SECONDARY must be synced)
    // After reconnection, sync requires 5 samples (~5 seconds) to become valid
    static uint8_t autoStartRetryCount = 0;
    if (ble.isSecondaryConnected() && !syncProtocol.isClockSyncValid())
    {
        if (++autoStartRetryCount > 10)
        {
            // Give up waiting for sync after 10 seconds - start anyway (degraded mode)
            Serial.println(F("[AUTO] Sync not valid after 10s - starting therapy (timing may be degraded)"));
            autoStartRetryCount = 0;
            // Fall through to start therapy
        }
        else
        {
            Serial.printf("[AUTO] Sync not valid (attempt %u/10) - retrying in 1 second\n", autoStartRetryCount);
            // Schedule retry in 1 second
            autoStartScheduled = true;
            autoStartTime = millis() + 1000;
            return;
        }
    }
    else
    {
        // Reset retry counter on successful sync or no SECONDARY connected
        autoStartRetryCount = 0;
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
    PatternType patternType = PatternType::RNDP;
    if (strcmp(profile->patternType, "sequential") == 0)
    {
        patternType = PatternType::SEQUENTIAL;
    }
    else if (strcmp(profile->patternType, "mirrored") == 0)
    {
        patternType = PatternType::MIRRORED;
    }

    uint32_t durationSec = profile->sessionDurationMin * 60;

    Serial.println(F("\n+============================================================+"));
    Serial.println(F("|  AUTO-STARTING THERAPY (no phone connected)                |"));
    Serial.printf("|  Profile: %-46s |\n", profile->name);
    Serial.printf("|  Duration: %d min | Pattern: %-4s | Jitter: %5.1f%%\n",
                  profile->sessionDurationMin, profile->patternType, profile->jitterPercent);
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

    // Reset latency metrics for fresh measurements
    syncProtocol.resetLatency();  // Clear EMA state for fresh warmup

    // Start therapy session using profile settings
    therapy.startSession(
        durationSec,
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
// DEBUG FLASH (synchronized LED indicator at macrocycle start)
// =============================================================================

/**
 * @brief Trigger 200ms white LED flash for debug visualization
 *
 * Saves current LED state and flashes white for 200ms.
 * LED state is restored in loop() after flash duration.
 * Overrides THERAPY_LED_OFF setting for visibility.
 */
void triggerDebugFlash()
{
    // CRITICAL: Only save state if no flash is currently active
    // Otherwise, we'd save the WHITE flash state instead of the real LED state
    // (fixes double-trigger corruption when two flashes fire in quick succession)
    if (!debugFlashActive) {
        savedLedColor = led.getColor();
        savedLedPattern = led.getPattern();
    }

    // Flash WHITE (overrides THERAPY_LED_OFF)
    led.setPattern(Colors::WHITE, LEDPattern::SOLID);

    // Schedule restoration after 50ms (handled in loop())
    debugFlashEndTime = millis() + 50;
    debugFlashActive = true;

    if (profiles.getDebugMode())
    {
        Serial.println(F("[DEBUG] Flash triggered"));
    }
}

// =============================================================================
// PING/PONG LATENCY MEASUREMENT (PRIMARY only)
// =============================================================================

/**
 * @brief Send PING to SECONDARY to measure BLE latency and clock offset
 *
 * Uses PTP-style 4-timestamp protocol:
 * - T1: PRIMARY send time (stored in pingT1, included in PING)
 * - T2: SECONDARY receive time (returned in PONG)
 * - T3: SECONDARY send time (returned in PONG)
 * - T4: PRIMARY receive time (recorded on PONG receipt)
 */
void sendPing()
{
    if (deviceRole != DeviceRole::PRIMARY || !ble.isSecondaryConnected())
    {
        return;
    }

    // Record T1 for PTP offset calculation
    // Use atomic writes to prevent torn reads on 32-bit ARM
    uint64_t t1 = getMicros();
    atomicWrite64(&pingT1, t1);
    atomicWrite64(&pingStartTime, t1); // Keep for backward compat

    SyncCommand cmd = SyncCommand::createPingWithT1(g_sequenceGenerator.next(), t1);

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
        // Check if LED should be off during therapy
        if (profiles.getTherapyLedOff())
        {
            led.setPattern(Colors::GREEN, LEDPattern::OFF);
        }
        else
        {
            led.setPattern(Colors::GREEN, LEDPattern::PULSE_SLOW);
        }
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
        // Note: safeMotorShutdown() will also run from loop() via safety shutdown semaphore
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
// SECONDARY KEEPALIVE TIMEOUT HANDLER
// =============================================================================

void handleKeepaliveTimeout()
{
    Serial.println(F("[WARN] Keepalive timeout - PRIMARY connection lost"));

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
            lastKeepaliveReceived = millis(); // Reset timeout
            return;
        }
    }

    // 4. Recovery failed - return to IDLE
    Serial.println(F("[RECOVERY] Failed - returning to IDLE"));
    stateMachine.transition(StateTrigger::RECONNECT_FAILED);
    lastKeepaliveReceived = 0; // Reset for next session

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
            safeMotorShutdown(); // Ensure motors off before reset
            Serial.println(F("[CONFIG] Role set to PRIMARY - restarting..."));
            Serial.flush();
            delay(100);
            NVIC_SystemReset();
        }
        else if (strcasecmp(roleStr, "SECONDARY") == 0)
        {
            profiles.setDeviceRole(DeviceRole::SECONDARY);
            profiles.saveSettings();
            safeMotorShutdown(); // Ensure motors off before reset
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

    // GET_CLOCK_SYNC - Print PTP clock synchronization status
    if (strcmp(command, "GET_CLOCK_SYNC") == 0)
    {
        Serial.println(F("=== PTP Clock Synchronization Status ==="));
        Serial.printf("Valid: %s\n", syncProtocol.isClockSyncValid() ? "YES" : "NO");
        Serial.printf("Offset samples: %u\n", syncProtocol.getOffsetSampleCount());
        Serial.printf("Median offset: %ld us\n", (long)syncProtocol.getMedianOffset());
        Serial.printf("Corrected offset: %ld us\n", (long)syncProtocol.getCorrectedOffset());
        Serial.printf("Drift rate: %.3f us/ms\n", syncProtocol.getDriftRate());
        Serial.printf("RTT samples: %u\n", syncProtocol.getSampleCount());
        Serial.printf("RTT smoothed: %lu us (avg RTT %lu us)\n",
                      (unsigned long)syncProtocol.getMeasuredLatency(),
                      (unsigned long)syncProtocol.getAverageRTT());
        Serial.printf("RTT variance: %lu us\n", (unsigned long)syncProtocol.getRTTVariance());
        Serial.printf("RTT raw: %lu us\n", (unsigned long)syncProtocol.getRawLatency());
        Serial.printf("Adaptive lead time: %lu us\n", (unsigned long)syncProtocol.calculateAdaptiveLeadTime());
        Serial.printf("Time since sync: %lu ms\n", (unsigned long)syncProtocol.getTimeSinceSync());
        Serial.println(F("========================================="));
        return;
    }

    // GET_SYNC_STATS - Enhanced sync statistics (alias for GET_CLOCK_SYNC with better formatting)
    if (strcmp(command, "GET_SYNC_STATS") == 0)
    {
        Serial.println(F("\n========== SYNC STATISTICS =========="));
        Serial.printf("Device Role: %s\n", deviceRole == DeviceRole::PRIMARY ? "PRIMARY" : "SECONDARY");
        Serial.println(F("-------------------------------------"));

        // Clock Sync Status
        Serial.printf("Clock Sync Valid: %s\n", syncProtocol.isClockSyncValid() ? "YES" : "NO");
        Serial.printf("Offset (corrected): %+ld μs\n", (long)syncProtocol.getCorrectedOffset());
        Serial.printf("Offset (median):    %+ld μs\n", (long)syncProtocol.getMedianOffset());
        Serial.printf("Drift Rate:         %.4f μs/ms\n", syncProtocol.getDriftRate());
        Serial.printf("Offset Samples:     %u/%u\n", syncProtocol.getOffsetSampleCount(), 10);
        Serial.println(F("-------------------------------------"));

        // RTT Statistics
        Serial.printf("RTT (smoothed):     %lu μs\n", (unsigned long)syncProtocol.getAverageRTT());
        Serial.printf("RTT (raw/last):     %lu μs\n", (unsigned long)(syncProtocol.getRawLatency() * 2));
        Serial.printf("RTT Variance:       %lu μs\n", (unsigned long)syncProtocol.getRTTVariance());
        Serial.printf("RTT Samples:        %u\n", syncProtocol.getSampleCount());
        Serial.printf("One-way Latency:    %lu μs\n", (unsigned long)syncProtocol.getMeasuredLatency());
        Serial.println(F("-------------------------------------"));

        // Adaptive Lead Time
        Serial.printf("Adaptive Lead Time: %lu μs (%.2f ms)\n",
                      (unsigned long)syncProtocol.calculateAdaptiveLeadTime(),
                      syncProtocol.calculateAdaptiveLeadTime() / 1000.0f);
        Serial.printf("Time Since Sync:    %lu ms\n", (unsigned long)syncProtocol.getTimeSinceSync());
        Serial.println(F("=====================================\n"));
        return;
    }

    // RESET_CLOCK_SYNC - Reset PTP clock synchronization (idle keepalive will re-establish sync)
    if (strcmp(command, "RESET_CLOCK_SYNC") == 0)
    {
        syncProtocol.resetClockSync();
        syncProtocol.resetLatency();
        Serial.println(F("[SYNC] Reset complete - idle keepalive will re-establish sync"));
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
        safeMotorShutdown(); // Ensure motors off before reset
        Serial.println(F("[CONFIG] Rebooting..."));
        Serial.flush();
        delay(100);
        NVIC_SystemReset();
        return;
    }

    // REBOOT - restart the device
    if (strcmp(command, "REBOOT") == 0)
    {
        safeMotorShutdown(); // Ensure motors off before reset
        Serial.println(F("[CONFIG] Rebooting..."));
        Serial.flush();
        delay(100);
        NVIC_SystemReset();
        return;
    }

    // Not a serial-only command, pass to regular BLE message handler
    // Use current time as timestamp for serial commands (less critical for serial)
    onBLEMessage(0, command, getMicros());
}
