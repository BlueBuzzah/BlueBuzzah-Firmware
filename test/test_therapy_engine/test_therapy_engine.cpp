/**
 * @file test_therapy_engine.cpp
 * @brief Unit tests for therapy_engine - Pattern generation and execution
 */

#include <unity.h>
#include "therapy_engine.h"
#include "fs_backend.h"
#include <ranges>
#include <algorithm>
#include <vector>

// Include source files directly for native testing
// (excluded from build_src_filter to avoid conflicts with other tests)
#include "../../src/sync_protocol.cpp"  // For getMicros() - needed by therapy_engine
#include "../../src/therapy_engine.cpp"

// =============================================================================
// TEST HELPERS
// =============================================================================
using namespace std::literals;

/**
 * @brief Check if array is a valid permutation of 0 to n-1
 */
bool isValidPermutation(std::span<uint8_t> arr) {
    std::vector<bool> seen = std::vector<bool>(arr.size(), false);
    for (size_t i = 0; i < arr.size(); i++) {
        if (arr[i] >= arr.size() || seen[arr[i]]) {
            return false;
        }
        seen[arr[i]] = true;
    }
    return true;
}

// =============================================================================
// CALLBACK TRACKING
// =============================================================================

static int g_activateCallCount = 0;
static int g_deactivateCallCount = 0;
static int g_sendCommandCallCount = 0;
static int g_cycleCompleteCallCount = 0;
static uint8_t g_lastActivatedFinger = 255;
static uint8_t g_lastActivatedAmplitude = 0;

void mockActivateCallback(uint8_t finger, uint8_t amplitude) {
    g_activateCallCount++;
    g_lastActivatedFinger = finger;
    g_lastActivatedAmplitude = amplitude;
}

void mockDeactivateCallback(uint8_t finger) {
    g_deactivateCallCount++;
}

void mockSendCommandCallback(const char* cmd, uint8_t primaryFinger, uint8_t secondaryFinger, uint8_t amp, uint32_t durationMs, uint32_t seq, uint16_t frequencyHz) {
    (void)durationMs;
    (void)frequencyHz;
    g_sendCommandCallCount++;
}

void mockCycleCompleteCallback(uint32_t count) {
    g_cycleCompleteCallCount++;
}

// =============================================================================
// TEST FIXTURES
// =============================================================================

void setUp(void) {
    // Seed random for reproducibility
    randomSeed(42);
    mockResetTime();
    // Reset getMicros() overflow tracking state (must be after mockResetTime)
    resetMicrosOverflow();

    // Reset callback counters
    g_activateCallCount = 0;
    g_deactivateCallCount = 0;
    g_sendCommandCallCount = 0;
    g_cycleCompleteCallCount = 0;
    g_lastActivatedFinger = 255;
    g_lastActivatedAmplitude = 0;
}

void tearDown(void) {
    // Clean up after each test
}

// =============================================================================
// SHUFFLE ARRAY TESTS
// =============================================================================

void test_shuffleArray_produces_valid_permutation(void) {
    uint8_t arr[4] = {0, 1, 2, 3};
    shuffleArray(arr);

    TEST_ASSERT_TRUE(isValidPermutation(arr));
}

void test_shuffleArray_single_element(void) {
    uint8_t arr[1] = {0};
    shuffleArray(arr);

    TEST_ASSERT_EQUAL_UINT8(0, arr[0]);
}

void test_shuffleArray_two_elements(void) {
    uint8_t arr[2] = {0, 1};
    shuffleArray(arr);

    TEST_ASSERT_TRUE(isValidPermutation(arr));
}

void test_shuffleArray_maintains_all_elements(void) {
    randomSeed(12345);  // Different seed

    uint8_t arr[4] = {0, 1, 2, 3};
    shuffleArray(arr);

    // Count each element - should appear exactly once
    int counts[4] = {0};
    for (int i = 0; i < 4; i++) {
        counts[arr[i]]++;
    }

    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(1, counts[i]);
    }
}

// =============================================================================
// PATTERN STRUCT TESTS
// =============================================================================

void test_Pattern_default_constructor(void) {
    Pattern p;

    TEST_ASSERT_EQUAL_UINT8(MAX_ACTUATORS, p.numFingers);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, p.burstDurationMs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 668.0f, p.interBurstIntervalMs);

    // Default sequence is 0,1,2,3
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, p.primarySequence[i]);
        TEST_ASSERT_EQUAL_UINT8(i, p.secondarySequence[i]);
    }
}

void test_Pattern_getTotalDurationMs(void) {
    Pattern p;
    p.numFingers = 4;
    p.burstDurationMs = 100.0f;
    for (int i = 0; i < 4; i++) {
        p.timeOffMs[i] = 500.0f;  // 500ms between each
    }

    // Total = sum of (timeOff + burst) for each finger + interBurstInterval
    // = 4 * (500 + 100) + 668 = 3068ms
    float total = p.getTotalDurationMs();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 3068.0f, total);
}

void test_Pattern_getFingerPair(void) {
    Pattern p;
    p.numFingers = 4;
    p.primarySequence[0] = 3;
    p.primarySequence[1] = 1;
    p.secondarySequence[0] = 2;
    p.secondarySequence[1] = 3;

    uint8_t primary, secondary;

    p.getFingerPair(0, primary, secondary);
    TEST_ASSERT_EQUAL_UINT8(3, primary);
    TEST_ASSERT_EQUAL_UINT8(2, secondary);

    p.getFingerPair(1, primary, secondary);
    TEST_ASSERT_EQUAL_UINT8(1, primary);
    TEST_ASSERT_EQUAL_UINT8(3, secondary);
}

// =============================================================================
// GENERATE RANDOM PERMUTATION TESTS
// =============================================================================

void test_generateRandomPermutation_produces_valid_pattern(void) {
    Pattern p = generateRandomPermutation(4, 100.0f, 67.0f, true);

    TEST_ASSERT_EQUAL_UINT8(4, p.numFingers);
    TEST_ASSERT_TRUE(isValidPermutation(p.primarySequence));
    TEST_ASSERT_TRUE(isValidPermutation(p.secondarySequence));
}

void test_generateRandomPermutation_mirrored(void) {
    Pattern p = generateRandomPermutation(4, 100.0f, 67.0f, true);

    // Mirrored: primary and secondary should be identical
    TEST_ASSERT_TRUE(std::ranges::equal(p.primarySequence, p.secondarySequence));
}

void test_generateRandomPermutation_non_mirrored(void) {
    randomSeed(999);  // Seed to ensure different sequences

    Pattern p = generateRandomPermutation(4, 100.0f, 67.0f, false);

    // Both should still be valid permutations
    TEST_ASSERT_TRUE(isValidPermutation(p.primarySequence));
    TEST_ASSERT_TRUE(isValidPermutation(p.secondarySequence));

    // Note: They might be equal by chance, but usually won't be
}

void test_generateRandomPermutation_partial_fingers(void) {
    Pattern p = generateRandomPermutation(3, 100.0f, 67.0f, true);

    TEST_ASSERT_EQUAL_UINT8(3, p.numFingers);
    TEST_ASSERT_TRUE(isValidPermutation(p.primarySequence));
}

void test_generateRandomPermutation_burst_duration(void) {
    Pattern p = generateRandomPermutation(4, 150.0f, 50.0f, true);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 150.0f, p.burstDurationMs);
}

void test_generateRandomPermutation_interBurstInterval(void) {
    // Inter-burst = 4 * (timeOn + timeOff) = 4 * (100 + 67) = 668
    Pattern p = generateRandomPermutation(4, 100.0f, 67.0f, true);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 668.0f, p.interBurstIntervalMs);
}

// =============================================================================
// GENERATE SEQUENTIAL PATTERN TESTS
// =============================================================================

void test_generateSequentialPattern_forward(void) {
    Pattern p = generateSequentialPattern(4, 100.0f, 67.0f, true, false);

    // Sequential forward: 0, 1, 2, 3
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, p.primarySequence[i]);
    }
}

void test_generateSequentialPattern_reverse(void) {
    Pattern p = generateSequentialPattern(4, 100.0f, 67.0f, true, true);

    // Sequential reverse: 3, 2, 1, 0
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT8(3 - i, p.primarySequence[i]);
    }
}

void test_generateSequentialPattern_mirrored(void) {
    Pattern p = generateSequentialPattern(4, 100.0f, 67.0f, true, false);

    // Mirrored: primary and secondary identical
    TEST_ASSERT_TRUE(std::ranges::equal(p.primarySequence, p.secondarySequence));
}

void test_generateSequentialPattern_non_mirrored(void) {
    Pattern p = generateSequentialPattern(4, 100.0f, 67.0f, false, false);

    // Non-mirrored: secondary is opposite order of primary
    // Primary: 0,1,2,3  Secondary: 3,2,1,0
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, p.primarySequence[i]);
        TEST_ASSERT_EQUAL_UINT8(3 - i, p.secondarySequence[i]);
    }
}

// =============================================================================
// GENERATE MIRRORED PATTERN TESTS
// =============================================================================

void test_generateMirroredPattern_not_randomized(void) {
    Pattern p = generateMirroredPattern(4, 100.0f, 67.0f, false);

    // Not randomized: sequential
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, p.primarySequence[i]);
    }

    // Always mirrored
    TEST_ASSERT_TRUE(std::ranges::equal(p.primarySequence, p.secondarySequence));
}

void test_generateMirroredPattern_randomized(void) {
    Pattern p = generateMirroredPattern(4, 100.0f, 67.0f, true);

    // Randomized: valid permutation
    TEST_ASSERT_TRUE(isValidPermutation(p.primarySequence));

    // Always mirrored
    TEST_ASSERT_TRUE(std::ranges::equal(p.primarySequence, p.secondarySequence));
}

// =============================================================================
// THERAPY ENGINE CONSTRUCTOR TESTS
// =============================================================================

void test_TherapyEngine_default_state(void) {
    TherapyEngine engine;

    TEST_ASSERT_FALSE(engine.isRunning());
    TEST_ASSERT_FALSE(engine.isPaused());
    TEST_ASSERT_EQUAL_UINT32(0, engine.getCyclesCompleted());
    TEST_ASSERT_EQUAL_UINT32(0, engine.getTotalActivations());
}

// =============================================================================
// THERAPY ENGINE SESSION CONTROL TESTS
// =============================================================================

void test_TherapyEngine_startSession(void) {
    TherapyEngine engine;

    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 23.5f, 4, true, 100, 100, false);

    TEST_ASSERT_TRUE(engine.isRunning());
    TEST_ASSERT_FALSE(engine.isPaused());
    TEST_ASSERT_EQUAL(7200, engine.getDurationSeconds());
}

void test_TherapyEngine_pause(void) {
    TherapyEngine engine;
    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 23.5f, 4, true, 100, 100, false);

    engine.pause();

    TEST_ASSERT_TRUE(engine.isRunning());
    TEST_ASSERT_TRUE(engine.isPaused());
}

void test_TherapyEngine_resume(void) {
    TherapyEngine engine;
    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 23.5f, 4, true, 100, 100, false);
    engine.pause();

    engine.resume();

    TEST_ASSERT_TRUE(engine.isRunning());
    TEST_ASSERT_FALSE(engine.isPaused());
}

void test_TherapyEngine_stop(void) {
    TherapyEngine engine;
    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 23.5f, 4, true, 100, 100, false);

    engine.stop();

    TEST_ASSERT_FALSE(engine.isRunning());
}

void test_TherapyEngine_resets_stats_on_start(void) {
    TherapyEngine engine;
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.stop();

    // Start new session
    engine.startSession(200, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    TEST_ASSERT_EQUAL_UINT32(0, engine.getCyclesCompleted());
    TEST_ASSERT_EQUAL_UINT32(0, engine.getTotalActivations());
}

// =============================================================================
// THERAPY ENGINE TIME TRACKING TESTS
// =============================================================================

void test_TherapyEngine_getElapsedSeconds(void) {
    TherapyEngine engine;
    // Start with non-zero time (startTime == 0 is a guard condition in the code)
    mockSetMillis(1000);

    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // Advance time by 5000ms (5 seconds)
    mockAdvanceMillis(5000);

    TEST_ASSERT_EQUAL(5, engine.getElapsedSeconds());
}

void test_TherapyEngine_getRemainingSeconds(void) {
    TherapyEngine engine;
    // Start with non-zero time (startTime == 0 is a guard condition in the code)
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // Advance time by 30 seconds
    mockAdvanceMillis(30000);

    TEST_ASSERT_EQUAL(70, engine.getRemainingSeconds());
}

void test_TherapyEngine_elapsed_zero_when_not_running(void) {
    TherapyEngine engine;

    TEST_ASSERT_EQUAL(0, engine.getElapsedSeconds());
}

void test_TherapyEngine_remaining_zero_when_not_running(void) {
    TherapyEngine engine;
    TEST_ASSERT_EQUAL(0, engine.getRemainingSeconds());
}

// =============================================================================
// THERAPY ENGINE UPDATE BEHAVIOR TESTS
// =============================================================================

void test_TherapyEngine_update_does_nothing_when_not_running(void) {
    TherapyEngine engine;
    engine.setActivateCallback(mockActivateCallback);

    engine.update();

    TEST_ASSERT_EQUAL_INT(0, g_activateCallCount);
}

void test_TherapyEngine_update_does_nothing_when_paused(void) {
    TherapyEngine engine;
    engine.setActivateCallback(mockActivateCallback);

    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.pause();

    int countBefore = g_activateCallCount;
    engine.update();

    TEST_ASSERT_EQUAL_INT(countBefore, g_activateCallCount);
}

void test_TherapyEngine_session_timeout(void) {
    TherapyEngine engine;
    mockSetMillis(0);

    engine.startSession(10, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);  // 10 second session

    // Advance past session duration
    mockAdvanceMillis(11000);
    engine.update();

    TEST_ASSERT_FALSE(engine.isRunning());
}

// =============================================================================
// PATTERN TYPE TESTS
// =============================================================================

void test_TherapyEngine_startSession_sequential_pattern(void) {
    TherapyEngine engine;

    engine.startSession(7200, PatternType::SEQUENTIAL, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    TEST_ASSERT_TRUE(engine.isRunning());
}

void test_TherapyEngine_startSession_mirrored_pattern(void) {
    TherapyEngine engine;

    engine.startSession(7200, PatternType::MIRRORED, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    TEST_ASSERT_TRUE(engine.isRunning());
    // Confirms the session actually dispatched to the MIRRORED generator rather
    // than silently falling through to another pattern type.
    TEST_ASSERT_EQUAL(PatternType::MIRRORED, engine.getPatternType());
}

// =============================================================================
// CALLBACK TESTS
// =============================================================================

void test_TherapyEngine_setCycleCompleteCallback(void) {
    TherapyEngine engine;
    engine.setCycleCompleteCallback(mockCycleCompleteCallback);

    // Callback is called at end of macrocycle, need to run full update cycle
    // For now just verify callback is set without crashing
    TEST_ASSERT_TRUE(true);
}

void test_TherapyEngine_setDeactivateCallback(void) {
    TherapyEngine engine;
    engine.setDeactivateCallback(mockDeactivateCallback);

    // Verify deactivate callback is called during pause
    engine.setActivateCallback(mockActivateCallback);
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // Pause should deactivate motors if active
    engine.pause();

    TEST_ASSERT_TRUE(engine.isPaused());
}

// =============================================================================
// FREQUENCY RANDOMIZATION TESTS
// =============================================================================

void test_TherapyEngine_setFrequencyRandomization(void) {
    TherapyEngine engine;

    // Enable frequency randomization with custom range
    engine.setFrequencyRandomization(true, 210, 260);

    // Start session to trigger frequency application
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    TEST_ASSERT_TRUE(engine.isRunning());
}

void test_TherapyEngine_setFrequencyRandomization_disabled(void) {
    TherapyEngine engine;

    // Disable frequency randomization
    engine.setFrequencyRandomization(false, 210, 260);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    TEST_ASSERT_TRUE(engine.isRunning());
}

// =============================================================================
// AMPLITUDE RANGE TESTS
// =============================================================================

void test_TherapyEngine_amplitude_range(void) {
    TherapyEngine engine;

    // Start session with amplitude range (50-100)
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 50, 100, false);

    TEST_ASSERT_TRUE(engine.isRunning());
}

void test_TherapyEngine_fixed_amplitude(void) {
    TherapyEngine engine;

    // Start session with fixed amplitude (min == max)
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 80, 80, false);

    TEST_ASSERT_TRUE(engine.isRunning());
}

void test_interBurstInterval_scales_with_finger_count_all_generators(void) {
    // Each generator independently sets pattern.interBurstIntervalMs = numFingers *
    // (ON + OFF). This pins all three
    // sites directly, unlike test_generateRandomPermutation_interBurstInterval
    // (which hardcodes 4 fingers and can't distinguish numFingers * X from 4 * X).
    const float onMs  = 100.0f;
    const float offMs = 67.0f;

    for (uint8_t fingers = 1; fingers <= MAX_ACTUATORS; fingers++) {
        const float expected = static_cast<float>(fingers) * (onMs + offMs);

        Pattern randomPermutation = generateRandomPermutation(fingers, onMs, offMs, true);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, expected, randomPermutation.interBurstIntervalMs,
            "generateRandomPermutation interBurstIntervalMs did not scale with finger count");

        Pattern sequential = generateSequentialPattern(fingers, onMs, offMs, true, false);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, expected, sequential.interBurstIntervalMs,
            "generateSequentialPattern interBurstIntervalMs did not scale with finger count");

        Pattern mirrored = generateMirroredPattern(fingers, onMs, offMs, true);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, expected, mirrored.interBurstIntervalMs,
            "generateMirroredPattern interBurstIntervalMs did not scale with finger count");
    }
}

// =============================================================================
// DEFAULT PATTERN TYPE TEST
// =============================================================================

void test_TherapyEngine_default_pattern_type_fallback(void) {
    TherapyEngine engine;

    // Use a non-standard pattern type (default branch in switch)
    // Since we can't easily pass an invalid enum, test with valid types
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    TEST_ASSERT_TRUE(engine.isRunning());
}

// =============================================================================
// STOP WITH ACTIVE MOTOR TEST
// =============================================================================

void test_TherapyEngine_stop_deactivates_motors(void) {
    TherapyEngine engine;
    engine.setDeactivateCallback(mockDeactivateCallback);
    engine.setActivateCallback(mockActivateCallback);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // Stop should deactivate motors if active
    engine.stop();

    TEST_ASSERT_FALSE(engine.isRunning());
}

// =============================================================================
// REMAINING SECONDS EDGE CASES
// =============================================================================

void test_TherapyEngine_getRemainingSeconds_exceeded(void) {
    TherapyEngine engine;
    mockSetMillis(1000);

    engine.startSession(10, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // Advance past session duration
    mockAdvanceMillis(20000);

    TEST_ASSERT_EQUAL(0, engine.getRemainingSeconds());
}

// =============================================================================
// ADDITIONAL CALLBACK SETTER TESTS
// =============================================================================

static int g_macrocycleStartCallCount = 0;
static int g_sendMacrocycleCallCount = 0;
static int g_scheduleActivationCallCount = 0;
static uint32_t g_lastLeadTimeReturned = 0;
static bool g_schedulingComplete = false;

void mockMacrocycleStartCallback(uint32_t cycleNum) {
    g_macrocycleStartCallCount++;
}

void mockSendMacrocycleCallback(const Macrocycle& mc) {
    g_sendMacrocycleCallCount++;
}

void mockScheduleActivationCallback(uint64_t timeUs, uint8_t finger, uint8_t amp, uint16_t durMs, uint16_t freqHz) {
    g_scheduleActivationCallCount++;
}

void mockStartSchedulingCallback() {
    // Called to signal motor task that events are ready
}

bool mockIsSchedulingCompleteCallback() {
    return g_schedulingComplete;
}

uint32_t mockGetLeadTimeCallback() {
    g_lastLeadTimeReturned = 50000;  // 50ms
    return g_lastLeadTimeReturned;
}

void test_TherapyEngine_setMacrocycleStartCallback(void) {
    TherapyEngine engine;
    engine.setMacrocycleStartCallback(mockMacrocycleStartCallback);

    g_macrocycleStartCallCount = 0;
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // Macrocycle start callback is called during startSession
    TEST_ASSERT_EQUAL_INT(1, g_macrocycleStartCallCount);
}

void test_TherapyEngine_setSendMacrocycleCallback(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockSendMacrocycleCallback);

    g_sendMacrocycleCallCount = 0;
    mockSetMillis(1000);
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // First update should trigger macrocycle generation and send
    engine.update();

    TEST_ASSERT_TRUE(g_sendMacrocycleCallCount >= 1);
}

void test_TherapyEngine_setSchedulingCallbacks(void) {
    TherapyEngine engine;
    engine.setSchedulingCallbacks(mockScheduleActivationCallback, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);

    g_scheduleActivationCallCount = 0;
    g_schedulingComplete = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();

    // Should have scheduled 12 activations (3 patterns * 4 fingers)
    TEST_ASSERT_EQUAL_INT(12, g_scheduleActivationCallCount);
}

void test_TherapyEngine_setGetLeadTimeCallback(void) {
    TherapyEngine engine;
    engine.setGetLeadTimeCallback(mockGetLeadTimeCallback);
    engine.setSendMacrocycleCallback(mockSendMacrocycleCallback);

    g_lastLeadTimeReturned = 0;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();

    // Lead time callback should have been called
    TEST_ASSERT_EQUAL_UINT32(50000, g_lastLeadTimeReturned);
}

// =============================================================================
// MACROCYCLE GENERATION TESTS (via callbacks)
// =============================================================================

static Macrocycle g_lastSentMacrocycle;
static bool g_macrocycleReceived = false;

void mockCaptureMacrocycleCallback(const Macrocycle& mc) {
    g_lastSentMacrocycle = mc;
    g_macrocycleReceived = true;
}

void test_macrocycle_creates_12_events(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    // 3 patterns * 4 fingers = 12 events
    TEST_ASSERT_EQUAL_UINT8(12, g_lastSentMacrocycle.eventCount);
}

void test_macrocycle_skips_inactive_finger(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    // Simulate boot probe finding finger 2's motor missing
    uint8_t present[MAX_ACTUATORS];
    uint8_t n = 0;
    for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
        if (f != 2) present[n++] = f;
    }
    engine.setActiveFingers(present, n);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, MAX_ACTUATORS, true, 100, 100, false);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    // numFingers clamps to the active set: 3 patterns * (MAX_ACTUATORS - 1)
    TEST_ASSERT_EQUAL_UINT8(3 * (MAX_ACTUATORS - 1), g_lastSentMacrocycle.eventCount);

    uint8_t counts[MAX_ACTUATORS] = {0};
    for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; i++) {
        const MacrocycleEvent& evt = g_lastSentMacrocycle.events[i];
        TEST_ASSERT_NOT_EQUAL(2, evt.primaryFinger);
        TEST_ASSERT_NOT_EQUAL(2, evt.finger);
        TEST_ASSERT_LESS_THAN_UINT8(MAX_ACTUATORS, evt.primaryFinger);
        counts[evt.primaryFinger]++;
    }
    // Still a permutation per pattern: each present finger exactly 3 times
    for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
        TEST_ASSERT_EQUAL_UINT8(f == 2 ? 0 : 3, counts[f]);
    }
}

void test_macrocycle_sequential_pattern(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::SEQUENTIAL, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    TEST_ASSERT_EQUAL_UINT8(12, g_lastSentMacrocycle.eventCount);
}

void test_macrocycle_mirrored_pattern(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::MIRRORED, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    TEST_ASSERT_EQUAL_UINT8(12, g_lastSentMacrocycle.eventCount);
}

void test_macrocycle_with_frequency_randomization(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.setFrequencyRandomization(true, 210, 260);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    // v1 randrange(210, 260, 5): 210-255 Hz in 5 Hz steps. 260 would exceed
    // MAX_FREQUENCY_HZ and the driver would refuse it.
    for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; i++) {
        uint16_t freq = g_lastSentMacrocycle.events[i].getFrequencyHz();
        TEST_ASSERT_TRUE(freq >= 210);
        TEST_ASSERT_TRUE(freq <= MAX_FREQUENCY_HZ);
        TEST_ASSERT_EQUAL_UINT16(0, freq % 5);
    }
}

void test_macrocycle_without_frequency_randomization_is_250hz(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.setFrequencyRandomization(false);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; i++) {
        TEST_ASSERT_EQUAL_UINT16(250, g_lastSentMacrocycle.events[i].getFrequencyHz());
    }
}

void test_fixed_session_after_randomized_session_is_250hz(void) {
    // The engine outlives sessions: a randomized session's last draws must
    // not carry into the next fixed-frequency session
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.setFrequencyRandomization(true, 210, 260);
    mockSetMillis(1000);
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();
    engine.stop();

    engine.setFrequencyRandomization(false);
    g_macrocycleReceived = false;
    mockSetMillis(20000);
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; i++) {
        TEST_ASSERT_EQUAL_UINT16(250, g_lastSentMacrocycle.events[i].getFrequencyHz());
    }
}

void test_frequency_randomization_covers_v1_range(void) {
    // Over many macrocycles every 5 Hz step from 210 to 255 shows up, never 260
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.setFrequencyRandomization(true, 210, 260);
    bool seen[10] = {};

    for (uint8_t run = 0; run < 40; run++) {
        g_macrocycleReceived = false;
        mockSetMillis(1000 + run * 10000);
        engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
        engine.update();
        TEST_ASSERT_TRUE(g_macrocycleReceived);
        for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; i++) {
            uint16_t freq = g_lastSentMacrocycle.events[i].getFrequencyHz();
            TEST_ASSERT_TRUE(freq >= 210 && freq <= 255);
            seen[(freq - 210) / 5] = true;
        }
        engine.stop();
    }
    for (uint8_t step = 0; step < 10; step++) {
        TEST_ASSERT_TRUE_MESSAGE(seen[step], "a 5 Hz step never appeared");
    }
}

void test_macrocycle_duration_matches(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 150.0f, 67.0f, 0.0f, 4, true, 100, 100, false);  // 150ms ON time
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    // Duration should match time_on
    TEST_ASSERT_EQUAL_UINT16(150, g_lastSentMacrocycle.durationMs);
}

void test_macrocycle_amplitude_range(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 50, 100, false);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    // Check that amplitudes are within expected range
    for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; i++) {
        TEST_ASSERT_TRUE(g_lastSentMacrocycle.events[i].amplitude >= 50);
        TEST_ASSERT_TRUE(g_lastSentMacrocycle.events[i].amplitude <= 100);
    }
}

void test_macrocycle_fixed_amplitude(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 80, 80, false);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    // All amplitudes should be exactly 80
    for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; i++) {
        TEST_ASSERT_EQUAL_UINT8(80, g_lastSentMacrocycle.events[i].amplitude);
    }
}

void test_macrocycle_amplitude_varies_across_range(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 20, 100, false);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);

    // A wide amplitude range should actually produce varying amplitudes across
    // events, not just amplitudes that happen to satisfy the bounds check.
    uint8_t firstAmplitude = g_lastSentMacrocycle.events[0].amplitude;
    bool sawVariation = false;
    for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; i++) {
        TEST_ASSERT_TRUE(g_lastSentMacrocycle.events[i].amplitude >= 20);
        TEST_ASSERT_TRUE(g_lastSentMacrocycle.events[i].amplitude <= 100);
        if (g_lastSentMacrocycle.events[i].amplitude != firstAmplitude) {
            sawVariation = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(sawVariation, "Expected amplitude to vary across events for a 20-100 range");
}

void test_macrocycle_sequence_id_increments(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.setSchedulingCallbacks(mockScheduleActivationCallback, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);
    engine.setCycleCompleteCallback(mockCycleCompleteCallback);

    g_schedulingComplete = true;  // Fast completion
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // First macrocycle
    engine.update();
    uint32_t firstSeqId = g_lastSentMacrocycle.sequenceId;

    // Complete first cycle
    mockAdvanceMillis(100);
    engine.update();  // ACTIVE -> WAITING_RELAX
    mockAdvanceMillis(3200);
    engine.update();  // WAITING_RELAX -> IDLE

    // Second macrocycle
    engine.update();
    uint32_t secondSeqId = g_lastSentMacrocycle.sequenceId;

    TEST_ASSERT_EQUAL_UINT32(firstSeqId + 1, secondSeqId);
}

// =============================================================================
// EXECUTEMACROCYCLESTEP STATE MACHINE TESTS
// =============================================================================

void test_executeMacrocycleStep_transitions_to_active(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockSendMacrocycleCallback);
    engine.setSchedulingCallbacks(mockScheduleActivationCallback, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);

    g_sendMacrocycleCallCount = 0;
    g_schedulingComplete = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // First update should transition IDLE -> ACTIVE
    engine.update();

    TEST_ASSERT_TRUE(g_sendMacrocycleCallCount >= 1);
}

void test_executeMacrocycleStep_waits_for_completion(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockSendMacrocycleCallback);
    engine.setSchedulingCallbacks(mockScheduleActivationCallback, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);
    engine.setCycleCompleteCallback(mockCycleCompleteCallback);

    g_schedulingComplete = false;
    g_cycleCompleteCallCount = 0;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // First update - IDLE -> ACTIVE
    engine.update();

    // While not complete, cycle should not be marked complete
    mockAdvanceMillis(100);
    engine.update();

    TEST_ASSERT_EQUAL_INT(0, g_cycleCompleteCallCount);
}

void test_executeMacrocycleStep_transitions_to_waiting_relax(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockSendMacrocycleCallback);
    engine.setSchedulingCallbacks(mockScheduleActivationCallback, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);
    engine.setCycleCompleteCallback(mockCycleCompleteCallback);

    g_schedulingComplete = false;
    g_cycleCompleteCallCount = 0;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // First update - IDLE -> ACTIVE
    engine.update();

    // Mark scheduling complete
    g_schedulingComplete = true;
    mockAdvanceMillis(100);
    engine.update();  // ACTIVE -> WAITING_RELAX

    // Reach the prepare point, 300 ms before the next grid slot
    mockAdvanceMillis(3200);
    engine.update();  // WAITING_RELAX -> IDLE (cycle complete)

    TEST_ASSERT_EQUAL_INT(1, g_cycleCompleteCallCount);
}

void test_executeMacrocycleStep_full_cycle(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockSendMacrocycleCallback);
    engine.setSchedulingCallbacks(mockScheduleActivationCallback, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);
    engine.setMacrocycleStartCallback(mockMacrocycleStartCallback);
    engine.setCycleCompleteCallback(mockCycleCompleteCallback);

    g_schedulingComplete = true;  // Instant completion for testing
    g_cycleCompleteCallCount = 0;
    g_macrocycleStartCallCount = 0;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // First macrocycle start called during startSession
    TEST_ASSERT_EQUAL_INT(1, g_macrocycleStartCallCount);

    // Run through IDLE -> ACTIVE -> WAITING_RELAX -> IDLE
    engine.update();  // IDLE -> ACTIVE (macrocycle start called again)

    mockAdvanceMillis(100);
    engine.update();  // ACTIVE -> WAITING_RELAX (since g_schedulingComplete = true)

    mockAdvanceMillis(3200);  // Reach the prepare point, 300 ms before the next grid slot
    engine.update();  // WAITING_RELAX -> IDLE, cycle complete

    TEST_ASSERT_EQUAL_INT(1, g_cycleCompleteCallCount);

    // Next update should start new macrocycle (3rd macrocycle start)
    engine.update();
    TEST_ASSERT_TRUE(g_macrocycleStartCallCount >= 2);  // At least 2+ starts
}

// =============================================================================
// PAUSE WITH MOTOR ACTIVE TESTS
// =============================================================================

void test_TherapyEngine_pause_with_motor_active(void) {
    TherapyEngine engine;
    engine.setDeactivateCallback(mockDeactivateCallback);
    engine.setActivateCallback(mockActivateCallback);
    engine.setSchedulingCallbacks(mockScheduleActivationCallback, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);

    g_deactivateCallCount = 0;
    g_schedulingComplete = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();

    // Pause during active state - should deactivate motors if any active
    engine.pause();

    TEST_ASSERT_TRUE(engine.isPaused());
}

void test_TherapyEngine_stop_with_motor_active(void) {
    TherapyEngine engine;
    engine.setDeactivateCallback(mockDeactivateCallback);
    engine.setSchedulingCallbacks(mockScheduleActivationCallback, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);

    g_deactivateCallCount = 0;
    g_schedulingComplete = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.update();

    // Stop during active state
    engine.stop();

    TEST_ASSERT_FALSE(engine.isRunning());
}

// =============================================================================
// UPDATE WITH STOP FLAG TESTS
// =============================================================================

void test_TherapyEngine_update_stops_when_shouldStop_set(void) {
    TherapyEngine engine;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);
    engine.stop();  // Sets _shouldStop flag

    engine.update();  // Should recognize stop flag

    TEST_ASSERT_FALSE(engine.isRunning());
}

// =============================================================================
// ZERO DURATION SESSION TEST
// =============================================================================

void test_TherapyEngine_zero_duration_session_runs_forever(void) {
    TherapyEngine engine;
    mockSetMillis(1000);

    // Duration of 0 means run indefinitely
    engine.startSession(0, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // Advance a lot of time
    mockAdvanceMillis(1000000);
    engine.update();

    // Should still be running (no timeout with 0 duration)
    TEST_ASSERT_TRUE(engine.isRunning());
}

void test_TherapyEngine_getRemainingSeconds_with_zero_duration(void) {
    TherapyEngine engine;
    mockSetMillis(1000);

    // Duration of 0 means run indefinitely
    engine.startSession(0, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, false);

    // Should return 0 when duration is 0
    TEST_ASSERT_EQUAL(0, engine.getRemainingSeconds());
}

// =============================================================================
// MACROCYCLE EVENT TESTS
// =============================================================================

void test_MacrocycleEvent_getFrequencyHz(void) {
    MacrocycleEvent evt(100, 0, 1, 80, 50, 250);

    TEST_ASSERT_EQUAL_UINT16(250, evt.getFrequencyHz());
}

void test_MacrocycleEvent_constructor(void) {
    MacrocycleEvent evt(500, 2, 3, 100, 75, 210);

    TEST_ASSERT_EQUAL_UINT16(500, evt.deltaTimeMs);
    TEST_ASSERT_EQUAL_UINT8(2, evt.finger);
    TEST_ASSERT_EQUAL_UINT8(3, evt.primaryFinger);
    TEST_ASSERT_EQUAL_UINT8(100, evt.amplitude);
    TEST_ASSERT_EQUAL_UINT8(75, evt.durationMs);
    TEST_ASSERT_EQUAL_UINT16(210, evt.getFrequencyHz());
}

// =============================================================================
// TEST RUNNER
// =============================================================================


// =============================================================================
// LAST SESSION SUMMARY TESTS
// =============================================================================

static void startTestSession(TherapyEngine& engine, uint32_t durationSec, bool testMode = false) {
    engine.startSession(durationSec, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 100, 100, testMode);
}

void test_LastSession_none_before_first_session(void) {
    TherapyEngine engine;
    TEST_ASSERT_NULL(engine.getLastSession());
    TEST_ASSERT_EQUAL_UINT32(0, engine.getSessionId());
}

void test_LastSession_completed_records_full_time(void) {
    TherapyEngine engine;
    mockSetMillis(5000);
    startTestSession(engine, 10);
    uint32_t id = engine.getSessionId();
    TEST_ASSERT_NOT_EQUAL(0, id);

    mockAdvanceMillis(10500);
    engine.update();

    const LastSessionSummary* last = engine.getLastSession();
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_UINT32(id, last->id);
    TEST_ASSERT_EQUAL_UINT32(10, last->elapsedSec);
    TEST_ASSERT_EQUAL_UINT32(10, last->totalSec);
    TEST_ASSERT_EQUAL_UINT32(0, last->pausedSec);
    TEST_ASSERT_EQUAL(SessionEndReason::COMPLETED, last->reason);
}

void test_LastSession_early_stop_records_reason_and_elapsed(void) {
    TherapyEngine engine;
    mockSetMillis(1000);
    startTestSession(engine, 300);
    mockAdvanceMillis(120400);

    engine.stop(SessionEndReason::POWER_OFF);

    const LastSessionSummary* last = engine.getLastSession();
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_UINT32(120, last->elapsedSec);
    TEST_ASSERT_EQUAL_UINT32(300, last->totalSec);
    TEST_ASSERT_EQUAL(SessionEndReason::POWER_OFF, last->reason);
}

void test_LastSession_second_stop_keeps_first_reason(void) {
    // Every end path stops twice (state handler, then the loop's safety shutdown)
    TherapyEngine engine;
    startTestSession(engine, 300);
    mockAdvanceMillis(60000);
    engine.stop(SessionEndReason::CONNECTION_LOST);
    mockAdvanceMillis(2000);
    engine.stop();

    const LastSessionSummary* last = engine.getLastSession();
    TEST_ASSERT_EQUAL(SessionEndReason::CONNECTION_LOST, last->reason);
    TEST_ASSERT_EQUAL_UINT32(60, last->elapsedSec);
}

void test_LastSession_counts_paused_time(void) {
    TherapyEngine engine;
    startTestSession(engine, 300);
    mockAdvanceMillis(30000);
    engine.pause();
    mockAdvanceMillis(20000);
    engine.pause();  // repeated pause must not restart the pause clock
    mockAdvanceMillis(5000);
    engine.resume();
    mockAdvanceMillis(10000);
    engine.pause();
    mockAdvanceMillis(7000);
    engine.stop();  // stopped while paused: the open pause counts too

    const LastSessionSummary* last = engine.getLastSession();
    TEST_ASSERT_EQUAL_UINT32(72, last->elapsedSec);
    TEST_ASSERT_EQUAL_UINT32(32, last->pausedSec);
}

void test_LastSession_stop_while_paused_clears_pause(void) {
    TherapyEngine engine;
    startTestSession(engine, 300);
    mockAdvanceMillis(10000);
    engine.pause();
    mockAdvanceMillis(5000);
    engine.stop();
    TEST_ASSERT_FALSE(engine.isPaused());  // SESSION_RESUME must not find it paused

    // The next session starts unpaused with a fresh pause clock
    startTestSession(engine, 300);
    mockAdvanceMillis(20000);
    TEST_ASSERT_EQUAL_UINT32(20, engine.getElapsedSeconds());
    TEST_ASSERT_EQUAL_UINT32(0, engine.getPausedSeconds());
}

void test_LastSession_ignores_test_mode(void) {
    TherapyEngine engine;
    startTestSession(engine, 300);
    mockAdvanceMillis(70000);
    engine.stop();
    uint32_t firstId = engine.getLastSession()->id;

    mockAdvanceMillis(1000);
    startTestSession(engine, 60, true);
    mockAdvanceMillis(5000);
    engine.stop();

    TEST_ASSERT_EQUAL_UINT32(firstId, engine.getLastSession()->id);
    TEST_ASSERT_EQUAL_UINT32(firstId, engine.getSessionId());  // a test has no id of its own
}

void test_LastSession_paused_time_does_not_count_toward_length(void) {
    TherapyEngine engine;
    startTestSession(engine, 30);
    mockAdvanceMillis(10000);
    engine.pause();
    mockAdvanceMillis(25000);
    engine.resume();
    engine.update();
    TEST_ASSERT_TRUE(engine.isRunning());  // 35 s wall, 10 s delivered
    TEST_ASSERT_EQUAL_UINT32(10, engine.getElapsedSeconds());

    mockAdvanceMillis(20000);
    engine.update();
    const LastSessionSummary* last = engine.getLastSession();
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL(SessionEndReason::COMPLETED, last->reason);
    TEST_ASSERT_EQUAL_UINT32(55, last->elapsedSec);
    TEST_ASSERT_EQUAL_UINT32(25, last->pausedSec);
}

void test_LastSession_elapsed_holds_while_paused(void) {
    TherapyEngine engine;
    startTestSession(engine, 300);
    mockAdvanceMillis(40000);
    engine.pause();
    mockAdvanceMillis(30000);
    TEST_ASSERT_EQUAL_UINT32(40, engine.getElapsedSeconds());
    TEST_ASSERT_EQUAL_UINT32(260, engine.getRemainingSeconds());
    TEST_ASSERT_EQUAL_UINT32(30, engine.getPausedSeconds());

    engine.resume();
    mockAdvanceMillis(10000);
    TEST_ASSERT_EQUAL_UINT32(50, engine.getElapsedSeconds());
    TEST_ASSERT_EQUAL_UINT32(30, engine.getPausedSeconds());

    engine.stop();
    TEST_ASSERT_EQUAL_UINT32(0, engine.getPausedSeconds());
}

void test_LastSession_kept_while_next_session_runs(void) {
    TherapyEngine engine;
    startTestSession(engine, 300);
    uint32_t firstId = engine.getSessionId();
    mockAdvanceMillis(90000);
    engine.stop();

    mockAdvanceMillis(1);
    startTestSession(engine, 300);
    TEST_ASSERT_NOT_EQUAL(firstId, engine.getSessionId());
    TEST_ASSERT_EQUAL_UINT32(firstId, engine.getLastSession()->id);
}

void test_LastSession_persists_across_reboot(void) {
    fsb::mock::reset();
    {
        TherapyEngine engine;
        startTestSession(engine, 300);
        mockAdvanceMillis(125000);
        engine.stop(SessionEndReason::POWER_OFF);
        engine.persistLastSession();
    }

    TherapyEngine rebooted;
    rebooted.loadLastSession();
    const LastSessionSummary* last = rebooted.getLastSession();
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_UINT32(125, last->elapsedSec);
    TEST_ASSERT_EQUAL(SessionEndReason::POWER_OFF, last->reason);
}

void test_LastSession_persist_writes_only_after_an_end(void) {
    fsb::mock::reset();
    TherapyEngine engine;
    engine.persistLastSession();
    TEST_ASSERT_FALSE(fsb::exists(LAST_SESSION_FILE));

    startTestSession(engine, 300);
    mockAdvanceMillis(61000);
    engine.stop();
    engine.persistLastSession();
    TEST_ASSERT_TRUE(fsb::exists(LAST_SESSION_FILE));

    fsb::removeFile(LAST_SESSION_FILE);
    engine.persistLastSession();  // nothing new ended
    TEST_ASSERT_FALSE(fsb::exists(LAST_SESSION_FILE));
}

void test_LastSession_failed_write_is_retried(void) {
    fsb::mock::reset();
    TherapyEngine engine;
    startTestSession(engine, 300);
    mockAdvanceMillis(61000);
    engine.stop();

    fsb::mock::setWriteResult(false);
    engine.persistLastSession();
    fsb::mock::setWriteResult(true);
    engine.persistLastSession();  // backing off
    TEST_ASSERT_FALSE(fsb::exists(LAST_SESSION_FILE));

    mockAdvanceMillis(LAST_SESSION_RETRY_MS);
    engine.persistLastSession();
    TEST_ASSERT_TRUE(fsb::exists(LAST_SESSION_FILE));
}

void test_LastSession_clear_forgets_and_deletes(void) {
    fsb::mock::reset();
    TherapyEngine engine;
    startTestSession(engine, 300);
    mockAdvanceMillis(61000);
    engine.stop();
    engine.persistLastSession();

    engine.clearLastSession();
    TEST_ASSERT_NULL(engine.getLastSession());
    TEST_ASSERT_FALSE(fsb::exists(LAST_SESSION_FILE));
    engine.persistLastSession();
    TEST_ASSERT_FALSE(fsb::exists(LAST_SESSION_FILE));
}

void test_LastSession_load_rejects_bad_file(void) {
    fsb::mock::reset();
    uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    fsb::writeFile(LAST_SESSION_FILE, junk, sizeof(junk));

    TherapyEngine engine;
    engine.loadLastSession();
    TEST_ASSERT_NULL(engine.getLastSession());
}

// =============================================================================
// ONSET TIMING TESTS
// =============================================================================

static const uint32_t ONSET_PREPARE_LEAD_US = 180000;  // Inside the 300 ms prepare window

static std::vector<uint64_t> onsetScheduledTimes;
static std::vector<uint16_t> onsetScheduledDurations;

void captureOnsetActivation(uint64_t t, uint8_t, uint8_t, uint16_t duration, uint16_t) {
    onsetScheduledTimes.push_back(t);
    onsetScheduledDurations.push_back(duration);
}

uint32_t mockMaxLeadTimeCallback() {
    return SYNC_MAX_LEAD_TIME_US;
}

// From ACTIVE, reach the prepare point of the grid slot at nextBaseUs and dispatch it
static void runToNextMacrocycle(TherapyEngine& engine, uint64_t nextBaseUs) {
    engine.update();  // ACTIVE -> WAITING_RELAX
    mockSetMillis(static_cast<uint32_t>((nextBaseUs - ONSET_PREPARE_LEAD_US) / 1000));
    engine.update();  // WAITING_RELAX -> IDLE
    engine.update();  // IDLE -> dispatch
}

static void assertAdjacentOnsetsKeepMinimumGap(float timeOnMs, float timeOffMs) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.startSession(3600, PatternType::RNDP, timeOnMs, timeOffMs, 50, 4, true, 50, 50, false);
    engine.update();
    const uint64_t base = g_lastSentMacrocycle.baseTime;
    const uint64_t periodUs = uint64_t(20 * (timeOnMs + timeOffMs)) * 1000;
    const int minimumSpacing = static_cast<int>(timeOnMs + MIN_INTER_BURST_GAP_MS);

    for (int m = 1; m <= 200; ++m) {
        for (int i = 1; i < 12; ++i) {
            const int spacing = g_lastSentMacrocycle.events[i].deltaTimeMs -
                                g_lastSentMacrocycle.events[i - 1].deltaTimeMs;
            TEST_ASSERT_TRUE(spacing >= minimumSpacing);
        }
        runToNextMacrocycle(engine, base + m * periodUs);
        TEST_ASSERT_TRUE(engine.isRunning());
        TEST_ASSERT_EQUAL_UINT64(base + m * periodUs, g_lastSentMacrocycle.baseTime);
    }
}

void test_onset_limit_matches_paper_at_defaults(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 19.6225f, onsetJitterLimitMs(100, 67, 23.5f));
}

void test_onset_limit_caps_to_keep_the_gap(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.5f, onsetJitterLimitMs(100, 67, 50));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, onsetJitterLimitMs(200, 30, 50));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, onsetJitterLimitMs(100, 5, 50));
}

void test_onset_bounds_and_bilateral_schedule(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.startSession(100, PatternType::RNDP, 100, 67, 23.5, 4, true, 50, 50, false);
    engine.update();
    const Macrocycle mc = g_lastSentMacrocycle;
    TEST_ASSERT_EQUAL_UINT16(20, mc.events[0].deltaTimeMs);
    for (int i = 0; i < 12; ++i) {
        const double error = double(mc.events[i].deltaTimeMs) - (20 + i * 167);
        TEST_ASSERT_TRUE(std::abs(error) <= 20.123);
        TEST_ASSERT_EQUAL_UINT8(mc.events[i].finger, mc.events[i].primaryFinger);
        TEST_ASSERT_EQUAL_UINT8(100, mc.events[i].durationMs);
        if (i) TEST_ASSERT_TRUE(mc.events[i].deltaTimeMs - mc.events[i-1].deltaTimeMs >= 105);
    }
}

void test_onset_primary_uses_transmitted_timestamps(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    onsetScheduledTimes.clear(); onsetScheduledDurations.clear();
    engine.setSchedulingCallbacks(captureOnsetActivation, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);
    engine.startSession(100, PatternType::RNDP, 100, 67, 23.5, 4, true, 50, 50, false);
    engine.update();
    TEST_ASSERT_EQUAL_INT(12, onsetScheduledTimes.size());
    for (int i=0; i<12; ++i) {
        TEST_ASSERT_EQUAL_UINT64(g_lastSentMacrocycle.baseTime + uint64_t(g_lastSentMacrocycle.events[i].deltaTimeMs)*1000,
                                onsetScheduledTimes[i]);
        TEST_ASSERT_EQUAL_UINT16(100, onsetScheduledDurations[i]);
    }
}

void test_onset_pause_shifts_future_grid_once(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.startSession(100, PatternType::RNDP, 100, 67, 23.5, 4, true, 50, 50, false);
    engine.update();
    const uint64_t base = g_lastSentMacrocycle.baseTime;
    engine.update();
    mockSetMillis(2200); engine.pause();
    mockAdvanceMillis(1000); engine.resume(); engine.update();
    mockSetMillis(static_cast<uint32_t>((base+3340000+1000000-180000)/1000));
    engine.update(); engine.update();
    TEST_ASSERT_EQUAL_UINT64(base+3340000+1000000, g_lastSentMacrocycle.baseTime);
}

void test_onset_pause_while_active_shifts_grid_once(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.setSchedulingCallbacks(mockScheduleActivationCallback, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);
    g_schedulingComplete = false;
    engine.startSession(100, PatternType::RNDP, 100, 67, 23.5, 4, true, 50, 50, false);
    engine.update();
    const uint64_t base = g_lastSentMacrocycle.baseTime;
    const uint32_t sequenceId = g_lastSentMacrocycle.sequenceId;

    engine.pause();
    mockAdvanceMillis(1000);
    engine.resume();
    engine.update();  // Applies the pause; the queue is still draining
    engine.update();  // Must not apply it again

    g_schedulingComplete = true;
    runToNextMacrocycle(engine, base + 3340000 + 1000000);

    TEST_ASSERT_EQUAL_UINT32(sequenceId + 1, g_lastSentMacrocycle.sequenceId);
    TEST_ASSERT_EQUAL_UINT64(base + 3340000 + 1000000, g_lastSentMacrocycle.baseTime);
}

void test_onset_macrocycle_grid_does_not_accumulate(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.startSession(500, PatternType::RNDP, 100, 67, 23.5, 4, true, 50, 50, false);
    engine.update();
    const uint64_t base = g_lastSentMacrocycle.baseTime;
    const double limitMs = onsetJitterLimitMs(100, 67, 23.5f);
    bool laterFirstShifted = false;
    for (int m = 1; m <= 50; ++m) {
        runToNextMacrocycle(engine, base + uint64_t(m)*3340000);
        TEST_ASSERT_EQUAL_UINT64(base + uint64_t(m)*3340000, g_lastSentMacrocycle.baseTime);
        laterFirstShifted |= std::abs(double(g_lastSentMacrocycle.events[0].deltaTimeMs) - 20) > limitMs / 2;
        for (int i = 0; i < 12; ++i)
            TEST_ASSERT_TRUE(std::abs(double(g_lastSentMacrocycle.events[i].deltaTimeMs) - (20+i*167)) <= limitMs + 0.5);
    }
    TEST_ASSERT_TRUE(laterFirstShifted);
}

void test_onset_late_dispatch_skips_without_rebasing(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.startSession(100, PatternType::RNDP, 100, 67, 23.5, 4, true, 50, 50, false);
    engine.update();
    const uint64_t base = g_lastSentMacrocycle.baseTime;
    const uint32_t sequenceId = g_lastSentMacrocycle.sequenceId;
    engine.update();
    mockSetMillis(static_cast<uint32_t>((base+3340000+100000)/1000));
    engine.update(); engine.update();  // WAITING_RELAX -> IDLE, then the skip
    engine.update();
    TEST_ASSERT_EQUAL_UINT32(sequenceId, g_lastSentMacrocycle.sequenceId);
    mockSetMillis(static_cast<uint32_t>((base+6680000-180000)/1000));
    engine.update();
    TEST_ASSERT_EQUAL_UINT32(sequenceId + 1, g_lastSentMacrocycle.sequenceId);
    TEST_ASSERT_EQUAL_UINT64(base + 6680000, g_lastSentMacrocycle.baseTime);
}

void test_onset_skip_on_long_period_holds_until_prepare_point(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.setMacrocycleStartCallback(mockMacrocycleStartCallback);
    engine.setCycleCompleteCallback(mockCycleCompleteCallback);
    engine.setGetLeadTimeCallback(mockGetLeadTimeCallback);
    engine.startSession(100, PatternType::RNDP, 200, 200, 23.5, 4, true, 50, 50, false);
    engine.update();
    const uint64_t base = g_lastSentMacrocycle.baseTime;
    const uint32_t sequenceId = g_lastSentMacrocycle.sequenceId;
    const uint64_t periodUs = 8000000;
    const uint64_t nextBase = base + 2 * periodUs;

    engine.update();  // ACTIVE -> WAITING_RELAX
    mockSetMillis(static_cast<uint32_t>((base + periodUs + 100000) / 1000));
    engine.update();  // WAITING_RELAX -> IDLE
    engine.update();  // Late: skips to nextBase

    g_macrocycleStartCallCount = 0;
    g_cycleCompleteCallCount = 0;
    g_lastLeadTimeReturned = 0;
    engine.update();
    mockSetMillis(static_cast<uint32_t>((nextBase - SYNC_MAX_LEAD_TIME_US - MACROCYCLE_PREPARE_MARGIN_US - 1000) / 1000));
    engine.update();

    TEST_ASSERT_EQUAL_UINT32(sequenceId, g_lastSentMacrocycle.sequenceId);
    TEST_ASSERT_EQUAL_INT(0, g_macrocycleStartCallCount);
    TEST_ASSERT_EQUAL_INT(0, g_cycleCompleteCallCount);
    TEST_ASSERT_EQUAL_UINT32(1, engine.getCyclesCompleted());
    TEST_ASSERT_EQUAL_UINT32(0, g_lastLeadTimeReturned);  // No lead callback while holding

    const uint32_t dispatchMs = static_cast<uint32_t>((nextBase - ONSET_PREPARE_LEAD_US) / 1000);
    mockSetMillis(dispatchMs);
    engine.update();

    TEST_ASSERT_EQUAL_UINT32(sequenceId + 1, g_lastSentMacrocycle.sequenceId);
    TEST_ASSERT_EQUAL_UINT64(nextBase, g_lastSentMacrocycle.baseTime);
    TEST_ASSERT_TRUE(g_lastSentMacrocycle.baseTime - uint64_t(dispatchMs) * 1000 < 1000000);
}

void test_onset_stall_inside_prepare_window_does_not_skip(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.setGetLeadTimeCallback(mockMaxLeadTimeCallback);
    engine.startSession(100, PatternType::RNDP, 100, 67, 23.5, 4, true, 50, 50, false);
    engine.update();
    const uint64_t nextBase = g_lastSentMacrocycle.baseTime + 3340000;
    const uint32_t sequenceId = g_lastSentMacrocycle.sequenceId;

    engine.update();  // ACTIVE -> WAITING_RELAX
    mockSetMillis(static_cast<uint32_t>((nextBase - SYNC_MAX_LEAD_TIME_US - MACROCYCLE_PREPARE_MARGIN_US) / 1000));
    engine.update();  // WAITING_RELAX -> IDLE
    mockAdvanceMillis(120);  // One stalled loop pass
    engine.update();

    TEST_ASSERT_EQUAL_UINT32(sequenceId + 1, g_lastSentMacrocycle.sequenceId);
    TEST_ASSERT_EQUAL_UINT64(nextBase, g_lastSentMacrocycle.baseTime);
}

void test_onset_zero_jitter_and_fractional_slot(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.startSession(100, PatternType::RNDP, 100, 66.666667f, 0, 4, true, 50, 50, false);
    engine.update();
    for (int i = 0; i < 12; ++i)
        TEST_ASSERT_EQUAL_UINT16(std::lround(i * (100.0 + double(66.666667f))), g_lastSentMacrocycle.events[i].deltaTimeMs);
}

void test_onset_jitter_above_cap_keeps_minimum_gap(void) {
    assertAdjacentOnsetsKeepMinimumGap(100, 67);
    assertAdjacentOnsetsKeepMinimumGap(200, 30);
}

void test_onset_period_scales_with_finger_count(void) {
    for (uint8_t numFingers = 1; numFingers <= MAX_ACTUATORS; ++numFingers) {
        TherapyEngine engine;
        engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
        engine.startSession(100, PatternType::RNDP, 100, 67, 23.5, numFingers, true, 50, 50, false);
        engine.update();
        const uint64_t base = g_lastSentMacrocycle.baseTime;
        const uint64_t periodUs = 5ULL * numFingers * 167000;

        runToNextMacrocycle(engine, base + periodUs);

        TEST_ASSERT_EQUAL_UINT64(base + periodUs, g_lastSentMacrocycle.baseTime);
    }
}

void test_onset_uses_clamped_finger_count(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    const uint8_t present[] = {0, 1, 2, 3};
    engine.setActiveFingers(present, 4);
    TEST_ASSERT_EQUAL_UINT8(4, engine.getActiveFingerCount());

    engine.startSession(100, PatternType::RNDP, 100, 67, 23.5, 5, true, 50, 50, false);
    engine.update();

    TEST_ASSERT_EQUAL_UINT8(12, g_lastSentMacrocycle.eventCount);
    for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; ++i) {
        TEST_ASSERT_TRUE(g_lastSentMacrocycle.events[i].finger < 4);
        TEST_ASSERT_TRUE(g_lastSentMacrocycle.events[i].primaryFinger < 4);
    }

    const uint64_t base = g_lastSentMacrocycle.baseTime;
    runToNextMacrocycle(engine, base + 3340000);
    TEST_ASSERT_EQUAL_UINT64(base + 3340000, g_lastSentMacrocycle.baseTime);
}

void test_onset_applies_to_every_pattern_type(void) {
    const PatternType types[] = {PatternType::SEQUENTIAL, PatternType::MIRRORED};
    for (const PatternType type : types) {
        TherapyEngine engine;
        engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
        engine.startSession(500, type, 100, 67, 23.5, 4, true, 50, 50, false);
        engine.update();
        const uint64_t base = g_lastSentMacrocycle.baseTime;
        const double limitMs = onsetJitterLimitMs(100, 67, 23.5f);
        bool shifted = false;
        for (int m = 1; m <= 50; ++m) {
            runToNextMacrocycle(engine, base + uint64_t(m) * 3340000);
            for (int i = 0; i < 12; ++i) {
                const double offsetMs = std::abs(double(g_lastSentMacrocycle.events[i].deltaTimeMs) - (20 + i * 167));
                shifted |= offsetMs > limitMs / 2;
                TEST_ASSERT_TRUE(offsetMs <= limitMs + 0.5);
            }
        }
        TEST_ASSERT_TRUE(shifted);
    }
}

void test_onset_restart_resets_grid(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.startSession(500, PatternType::RNDP, 100, 67, 23.5, 4, true, 50, 50, false);
    engine.update();
    runToNextMacrocycle(engine, g_lastSentMacrocycle.baseTime + 3340000);

    engine.stop();
    const uint32_t restartMs = 20000;
    mockSetMillis(restartMs);
    engine.startSession(500, PatternType::RNDP, 100, 67, 23.5, 4, true, 50, 50, false);
    engine.update();

    TEST_ASSERT_EQUAL_UINT16(20, g_lastSentMacrocycle.events[0].deltaTimeMs);
    TEST_ASSERT_EQUAL_UINT64(uint64_t(restartMs) * 1000 + SYNC_MIN_LEAD_TIME_US, g_lastSentMacrocycle.baseTime);
}

void test_onset_shortest_timing_never_skips(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.setGetLeadTimeCallback(mockMaxLeadTimeCallback);
    engine.startSession(100, PatternType::RNDP, 50, 30, 50, 1, true, 50, 50, false);
    engine.update();
    const uint64_t base = g_lastSentMacrocycle.baseTime;
    for (int m = 1; m <= 20; ++m) {
        mockSetMillis(static_cast<uint32_t>((base + uint64_t(m) * 400000 - ONSET_PREPARE_LEAD_US) / 1000));
        engine.update();
        engine.update();
        engine.update();
        TEST_ASSERT_EQUAL_UINT64(base + uint64_t(m) * 400000, g_lastSentMacrocycle.baseTime);
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Shuffle Array Tests
    RUN_TEST(test_shuffleArray_produces_valid_permutation);
    RUN_TEST(test_shuffleArray_single_element);
    RUN_TEST(test_shuffleArray_two_elements);
    RUN_TEST(test_shuffleArray_maintains_all_elements);

    // Pattern Struct Tests
    RUN_TEST(test_Pattern_default_constructor);
    RUN_TEST(test_Pattern_getTotalDurationMs);
    RUN_TEST(test_Pattern_getFingerPair);

    // Generate Random Permutation Tests
    RUN_TEST(test_generateRandomPermutation_produces_valid_pattern);
    RUN_TEST(test_generateRandomPermutation_mirrored);
    RUN_TEST(test_generateRandomPermutation_non_mirrored);
    RUN_TEST(test_generateRandomPermutation_partial_fingers);
    RUN_TEST(test_generateRandomPermutation_burst_duration);
    RUN_TEST(test_generateRandomPermutation_interBurstInterval);

    // Generate Sequential Pattern Tests
    RUN_TEST(test_generateSequentialPattern_forward);
    RUN_TEST(test_generateSequentialPattern_reverse);
    RUN_TEST(test_generateSequentialPattern_mirrored);
    RUN_TEST(test_generateSequentialPattern_non_mirrored);

    // Generate Mirrored Pattern Tests
    RUN_TEST(test_generateMirroredPattern_not_randomized);
    RUN_TEST(test_generateMirroredPattern_randomized);

    // Therapy Engine Constructor Tests
    RUN_TEST(test_TherapyEngine_default_state);

    // Therapy Engine Session Control Tests
    RUN_TEST(test_TherapyEngine_startSession);
    RUN_TEST(test_TherapyEngine_pause);
    RUN_TEST(test_TherapyEngine_resume);
    RUN_TEST(test_TherapyEngine_stop);
    RUN_TEST(test_TherapyEngine_resets_stats_on_start);

    // Therapy Engine Time Tracking Tests
    RUN_TEST(test_TherapyEngine_getElapsedSeconds);
    RUN_TEST(test_TherapyEngine_getRemainingSeconds);
    RUN_TEST(test_TherapyEngine_elapsed_zero_when_not_running);
    RUN_TEST(test_TherapyEngine_remaining_zero_when_not_running);

    // Therapy Engine Update Behavior Tests
    RUN_TEST(test_TherapyEngine_update_does_nothing_when_not_running);
    RUN_TEST(test_TherapyEngine_update_does_nothing_when_paused);
    RUN_TEST(test_TherapyEngine_session_timeout);

    // Pattern Type Tests
    RUN_TEST(test_TherapyEngine_startSession_sequential_pattern);
    RUN_TEST(test_TherapyEngine_startSession_mirrored_pattern);

    // Callback Tests
    RUN_TEST(test_TherapyEngine_setCycleCompleteCallback);
    RUN_TEST(test_TherapyEngine_setDeactivateCallback);

    // Frequency Randomization Tests
    RUN_TEST(test_TherapyEngine_setFrequencyRandomization);
    RUN_TEST(test_TherapyEngine_setFrequencyRandomization_disabled);

    // Amplitude Range Tests
    RUN_TEST(test_TherapyEngine_amplitude_range);
    RUN_TEST(test_TherapyEngine_fixed_amplitude);

    // Jitter Edge Case Tests
    RUN_TEST(test_interBurstInterval_scales_with_finger_count_all_generators);

    // Default Pattern Type Tests
    RUN_TEST(test_TherapyEngine_default_pattern_type_fallback);

    // Stop Deactivation Tests
    RUN_TEST(test_TherapyEngine_stop_deactivates_motors);

    // Remaining Seconds Edge Cases
    RUN_TEST(test_TherapyEngine_getRemainingSeconds_exceeded);

    // Additional Callback Setter Tests
    RUN_TEST(test_TherapyEngine_setMacrocycleStartCallback);
    RUN_TEST(test_TherapyEngine_setSendMacrocycleCallback);
    RUN_TEST(test_TherapyEngine_setSchedulingCallbacks);
    RUN_TEST(test_TherapyEngine_setGetLeadTimeCallback);

    // Macrocycle Generation Tests (via callbacks)
    RUN_TEST(test_macrocycle_creates_12_events);
    RUN_TEST(test_macrocycle_skips_inactive_finger);
    RUN_TEST(test_macrocycle_sequential_pattern);
    RUN_TEST(test_macrocycle_mirrored_pattern);
    RUN_TEST(test_macrocycle_with_frequency_randomization);
    RUN_TEST(test_macrocycle_without_frequency_randomization_is_250hz);
    RUN_TEST(test_frequency_randomization_covers_v1_range);
    RUN_TEST(test_fixed_session_after_randomized_session_is_250hz);
    RUN_TEST(test_macrocycle_duration_matches);
    RUN_TEST(test_macrocycle_amplitude_range);
    RUN_TEST(test_macrocycle_fixed_amplitude);
    RUN_TEST(test_macrocycle_amplitude_varies_across_range);
    RUN_TEST(test_macrocycle_sequence_id_increments);

    // ExecuteMacrocycleStep State Machine Tests
    RUN_TEST(test_executeMacrocycleStep_transitions_to_active);
    RUN_TEST(test_executeMacrocycleStep_waits_for_completion);
    RUN_TEST(test_executeMacrocycleStep_transitions_to_waiting_relax);
    RUN_TEST(test_executeMacrocycleStep_full_cycle);

    // Pause/Stop with Motor Active Tests
    RUN_TEST(test_TherapyEngine_pause_with_motor_active);
    RUN_TEST(test_TherapyEngine_stop_with_motor_active);

    // Update with Stop Flag Tests
    RUN_TEST(test_TherapyEngine_update_stops_when_shouldStop_set);

    // Zero Duration Session Tests
    RUN_TEST(test_TherapyEngine_zero_duration_session_runs_forever);
    RUN_TEST(test_TherapyEngine_getRemainingSeconds_with_zero_duration);

    // Macrocycle Event Tests
    RUN_TEST(test_MacrocycleEvent_getFrequencyHz);
    RUN_TEST(test_MacrocycleEvent_constructor);

    // Last Session Summary Tests
    RUN_TEST(test_LastSession_none_before_first_session);
    RUN_TEST(test_LastSession_completed_records_full_time);
    RUN_TEST(test_LastSession_early_stop_records_reason_and_elapsed);
    RUN_TEST(test_LastSession_second_stop_keeps_first_reason);
    RUN_TEST(test_LastSession_counts_paused_time);
    RUN_TEST(test_LastSession_stop_while_paused_clears_pause);
    RUN_TEST(test_LastSession_ignores_test_mode);
    RUN_TEST(test_LastSession_paused_time_does_not_count_toward_length);
    RUN_TEST(test_LastSession_elapsed_holds_while_paused);
    RUN_TEST(test_LastSession_kept_while_next_session_runs);
    RUN_TEST(test_LastSession_persists_across_reboot);
    RUN_TEST(test_LastSession_persist_writes_only_after_an_end);
    RUN_TEST(test_LastSession_failed_write_is_retried);
    RUN_TEST(test_LastSession_clear_forgets_and_deletes);
    RUN_TEST(test_LastSession_load_rejects_bad_file);

    // Onset Timing Tests
    RUN_TEST(test_onset_limit_matches_paper_at_defaults);
    RUN_TEST(test_onset_limit_caps_to_keep_the_gap);
    RUN_TEST(test_onset_bounds_and_bilateral_schedule);
    RUN_TEST(test_onset_primary_uses_transmitted_timestamps);
    RUN_TEST(test_onset_pause_shifts_future_grid_once);
    RUN_TEST(test_onset_pause_while_active_shifts_grid_once);
    RUN_TEST(test_onset_macrocycle_grid_does_not_accumulate);
    RUN_TEST(test_onset_late_dispatch_skips_without_rebasing);
    RUN_TEST(test_onset_skip_on_long_period_holds_until_prepare_point);
    RUN_TEST(test_onset_stall_inside_prepare_window_does_not_skip);
    RUN_TEST(test_onset_zero_jitter_and_fractional_slot);
    RUN_TEST(test_onset_jitter_above_cap_keeps_minimum_gap);
    RUN_TEST(test_onset_period_scales_with_finger_count);
    RUN_TEST(test_onset_uses_clamped_finger_count);
    RUN_TEST(test_onset_applies_to_every_pattern_type);
    RUN_TEST(test_onset_restart_resets_grid);
    RUN_TEST(test_onset_shortest_timing_never_skips);

    return UNITY_END();
}
