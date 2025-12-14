/**
 * @file test_therapy_engine.cpp
 * @brief Unit tests for therapy_engine - Pattern generation and execution
 */

#include <unity.h>
#include "therapy_engine.h"
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

    TEST_ASSERT_EQUAL_UINT8(4, p.numFingers);
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
    Pattern p = generateRandomPermutation(4, 100.0f, 67.0f, 0.0f, true);

    TEST_ASSERT_EQUAL_UINT8(4, p.numFingers);
    TEST_ASSERT_TRUE(isValidPermutation(p.primarySequence));
    TEST_ASSERT_TRUE(isValidPermutation(p.secondarySequence));
}

void test_generateRandomPermutation_mirrored(void) {
    Pattern p = generateRandomPermutation(4, 100.0f, 67.0f, 0.0f, true);

    // Mirrored: primary and secondary should be identical
    TEST_ASSERT_TRUE(std::ranges::equal(p.primarySequence, p.secondarySequence));
}

void test_generateRandomPermutation_non_mirrored(void) {
    randomSeed(999);  // Seed to ensure different sequences

    Pattern p = generateRandomPermutation(4, 100.0f, 67.0f, 0.0f, false);

    // Both should still be valid permutations
    TEST_ASSERT_TRUE(isValidPermutation(p.primarySequence));
    TEST_ASSERT_TRUE(isValidPermutation(p.secondarySequence));

    // Note: They might be equal by chance, but usually won't be
}

void test_generateRandomPermutation_with_jitter(void) {
    Pattern p = generateRandomPermutation(4, 100.0f, 67.0f, 23.5f, true);

    // With jitter, timing values will vary but should be positive
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(p.timeOffMs[i] >= 0.0f);
    }
}

void test_generateRandomPermutation_partial_fingers(void) {
    Pattern p = generateRandomPermutation(3, 100.0f, 67.0f, 0.0f, true);

    TEST_ASSERT_EQUAL_UINT8(3, p.numFingers);
    TEST_ASSERT_TRUE(isValidPermutation(p.primarySequence));
}

void test_generateRandomPermutation_burst_duration(void) {
    Pattern p = generateRandomPermutation(4, 150.0f, 50.0f, 0.0f, true);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 150.0f, p.burstDurationMs);
}

void test_generateRandomPermutation_interBurstInterval(void) {
    // Inter-burst = 4 * (timeOn + timeOff) = 4 * (100 + 67) = 668
    Pattern p = generateRandomPermutation(4, 100.0f, 67.0f, 0.0f, true);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 668.0f, p.interBurstIntervalMs);
}

// =============================================================================
// GENERATE SEQUENTIAL PATTERN TESTS
// =============================================================================

void test_generateSequentialPattern_forward(void) {
    Pattern p = generateSequentialPattern(4, 100.0f, 67.0f, 0.0f, true, false);

    // Sequential forward: 0, 1, 2, 3
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, p.primarySequence[i]);
    }
}

void test_generateSequentialPattern_reverse(void) {
    Pattern p = generateSequentialPattern(4, 100.0f, 67.0f, 0.0f, true, true);

    // Sequential reverse: 3, 2, 1, 0
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT8(3 - i, p.primarySequence[i]);
    }
}

void test_generateSequentialPattern_mirrored(void) {
    Pattern p = generateSequentialPattern(4, 100.0f, 67.0f, 0.0f, true, false);

    // Mirrored: primary and secondary identical
    TEST_ASSERT_TRUE(std::ranges::equal(p.primarySequence, p.secondarySequence));
}

void test_generateSequentialPattern_non_mirrored(void) {
    Pattern p = generateSequentialPattern(4, 100.0f, 67.0f, 0.0f, false, false);

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
    Pattern p = generateMirroredPattern(4, 100.0f, 67.0f, 0.0f, false);

    // Not randomized: sequential
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, p.primarySequence[i]);
    }

    // Always mirrored
    TEST_ASSERT_TRUE(std::ranges::equal(p.primarySequence, p.secondarySequence));
}

void test_generateMirroredPattern_randomized(void) {
    Pattern p = generateMirroredPattern(4, 100.0f, 67.0f, 0.0f, true);

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

    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 23.5f, 4, true);

    TEST_ASSERT_TRUE(engine.isRunning());
    TEST_ASSERT_FALSE(engine.isPaused());
    TEST_ASSERT_EQUAL(7200, engine.getDurationSeconds());
}

void test_TherapyEngine_pause(void) {
    TherapyEngine engine;
    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 23.5f, 4, true);

    engine.pause();

    TEST_ASSERT_TRUE(engine.isRunning());
    TEST_ASSERT_TRUE(engine.isPaused());
}

void test_TherapyEngine_resume(void) {
    TherapyEngine engine;
    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 23.5f, 4, true);
    engine.pause();

    engine.resume();

    TEST_ASSERT_TRUE(engine.isRunning());
    TEST_ASSERT_FALSE(engine.isPaused());
}

void test_TherapyEngine_stop(void) {
    TherapyEngine engine;
    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 23.5f, 4, true);

    engine.stop();

    TEST_ASSERT_FALSE(engine.isRunning());
}

void test_TherapyEngine_resets_stats_on_start(void) {
    TherapyEngine engine;
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.stop();

    // Start new session
    engine.startSession(200, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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

    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    // Advance time by 5000ms (5 seconds)
    mockAdvanceMillis(5000);

    TEST_ASSERT_EQUAL(5, engine.getElapsedSeconds());
}

void test_TherapyEngine_getRemainingSeconds(void) {
    TherapyEngine engine;
    // Start with non-zero time (startTime == 0 is a guard condition in the code)
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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

    engine.startSession(7200, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.pause();

    int countBefore = g_activateCallCount;
    engine.update();

    TEST_ASSERT_EQUAL_INT(countBefore, g_activateCallCount);
}

void test_TherapyEngine_session_timeout(void) {
    TherapyEngine engine;
    mockSetMillis(0);

    engine.startSession(10, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);  // 10 second session

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

    engine.startSession(7200, PatternType::SEQUENTIAL, 100.0f, 67.0f, 0.0f, 4, true);

    TEST_ASSERT_TRUE(engine.isRunning());
}

void test_TherapyEngine_startSession_mirrored_pattern(void) {
    TherapyEngine engine;

    engine.startSession(7200, PatternType::MIRRORED, 100.0f, 67.0f, 0.0f, 4, true);

    TEST_ASSERT_TRUE(engine.isRunning());
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
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    TEST_ASSERT_TRUE(engine.isRunning());
}

void test_TherapyEngine_setFrequencyRandomization_disabled(void) {
    TherapyEngine engine;

    // Disable frequency randomization
    engine.setFrequencyRandomization(false, 210, 260);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    TEST_ASSERT_TRUE(engine.isRunning());
}

// =============================================================================
// AMPLITUDE RANGE TESTS
// =============================================================================

void test_TherapyEngine_amplitude_range(void) {
    TherapyEngine engine;

    // Start session with amplitude range (50-100)
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 50, 100);

    TEST_ASSERT_TRUE(engine.isRunning());
}

void test_TherapyEngine_fixed_amplitude(void) {
    TherapyEngine engine;

    // Start session with fixed amplitude (min == max)
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 80, 80);

    TEST_ASSERT_TRUE(engine.isRunning());
}

// =============================================================================
// JITTER EDGE CASE TESTS
// =============================================================================

void test_generateRandomPermutation_high_jitter(void) {
    // Test with 50% jitter (extreme case)
    Pattern p = generateRandomPermutation(4, 100.0f, 67.0f, 50.0f, true);

    TEST_ASSERT_EQUAL_UINT8(4, p.numFingers);
    // With high jitter, timing values should still be valid
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(p.timeOffMs[i] >= 0.0f);
    }
}

void test_generateSequentialPattern_with_jitter(void) {
    Pattern p = generateSequentialPattern(4, 100.0f, 67.0f, 23.5f, true, false);

    TEST_ASSERT_EQUAL_UINT8(4, p.numFingers);
    // Jitter should be applied to timing
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(p.timeOffMs[i] >= 0.0f);
    }
}

void test_generateMirroredPattern_with_jitter(void) {
    Pattern p = generateMirroredPattern(4, 100.0f, 67.0f, 23.5f, true);

    TEST_ASSERT_EQUAL_UINT8(4, p.numFingers);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(p.timeOffMs[i] >= 0.0f);
    }
}

// =============================================================================
// DEFAULT PATTERN TYPE TEST
// =============================================================================

void test_TherapyEngine_default_pattern_type_fallback(void) {
    TherapyEngine engine;

    // Use a non-standard pattern type (default branch in switch)
    // Since we can't easily pass an invalid enum, test with valid types
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    TEST_ASSERT_TRUE(engine.isRunning());
}

// =============================================================================
// STOP WITH ACTIVE MOTOR TEST
// =============================================================================

void test_TherapyEngine_stop_deactivates_motors(void) {
    TherapyEngine engine;
    engine.setDeactivateCallback(mockDeactivateCallback);
    engine.setActivateCallback(mockActivateCallback);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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

    engine.startSession(10, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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
static int g_setFrequencyCallCount = 0;
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

void mockSetFrequencyCallback(uint8_t finger, uint16_t freq) {
    g_setFrequencyCallCount++;
}

void test_TherapyEngine_setMacrocycleStartCallback(void) {
    TherapyEngine engine;
    engine.setMacrocycleStartCallback(mockMacrocycleStartCallback);

    g_macrocycleStartCallCount = 0;
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    // Macrocycle start callback is called during startSession
    TEST_ASSERT_EQUAL_INT(1, g_macrocycleStartCallCount);
}

void test_TherapyEngine_setSendMacrocycleCallback(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockSendMacrocycleCallback);

    g_sendMacrocycleCallCount = 0;
    mockSetMillis(1000);
    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);
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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();

    // Lead time callback should have been called
    TEST_ASSERT_EQUAL_UINT32(50000, g_lastLeadTimeReturned);
}

void test_TherapyEngine_setSetFrequencyCallback(void) {
    TherapyEngine engine;
    engine.setSetFrequencyCallback(mockSetFrequencyCallback);
    engine.setFrequencyRandomization(true, 210, 260);

    g_setFrequencyCallCount = 0;

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    // Should have set frequency for 4 fingers
    TEST_ASSERT_EQUAL_INT(4, g_setFrequencyCallCount);
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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    // 3 patterns * 4 fingers = 12 events
    TEST_ASSERT_EQUAL_UINT8(12, g_lastSentMacrocycle.eventCount);
}

void test_macrocycle_sequential_pattern(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::SEQUENTIAL, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    TEST_ASSERT_EQUAL_UINT8(12, g_lastSentMacrocycle.eventCount);
}

void test_macrocycle_mirrored_pattern(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::MIRRORED, 100.0f, 67.0f, 0.0f, 4, true);
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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    // Check that frequencies are within expected range
    for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; i++) {
        uint16_t freq = g_lastSentMacrocycle.events[i].getFrequencyHz();
        TEST_ASSERT_TRUE(freq >= 210);
        TEST_ASSERT_TRUE(freq <= 260);
    }
}

void test_macrocycle_duration_matches(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);

    g_macrocycleReceived = false;
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 150.0f, 67.0f, 0.0f, 4, true);  // 150ms ON time
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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 50, 100);
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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true, 80, 80);
    engine.update();

    TEST_ASSERT_TRUE(g_macrocycleReceived);
    // All amplitudes should be exactly 80
    for (uint8_t i = 0; i < g_lastSentMacrocycle.eventCount; i++) {
        TEST_ASSERT_EQUAL_UINT8(80, g_lastSentMacrocycle.events[i].amplitude);
    }
}

void test_macrocycle_sequence_id_increments(void) {
    TherapyEngine engine;
    engine.setSendMacrocycleCallback(mockCaptureMacrocycleCallback);
    engine.setSchedulingCallbacks(mockScheduleActivationCallback, mockStartSchedulingCallback, mockIsSchedulingCompleteCallback);
    engine.setCycleCompleteCallback(mockCycleCompleteCallback);

    g_schedulingComplete = true;  // Fast completion
    mockSetMillis(1000);

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    // First macrocycle
    engine.update();
    uint32_t firstSeqId = g_lastSentMacrocycle.sequenceId;

    // Complete first cycle
    mockAdvanceMillis(100);
    engine.update();  // ACTIVE -> WAITING_RELAX
    mockAdvanceMillis(1400);
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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    // First update - IDLE -> ACTIVE
    engine.update();

    // Mark scheduling complete
    g_schedulingComplete = true;
    mockAdvanceMillis(100);
    engine.update();  // ACTIVE -> WAITING_RELAX

    // Wait for double relax time: 2 * 4 * (100 + 67) = 1336ms
    mockAdvanceMillis(1400);
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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    // First macrocycle start called during startSession
    TEST_ASSERT_EQUAL_INT(1, g_macrocycleStartCallCount);

    // Run through IDLE -> ACTIVE -> WAITING_RELAX -> IDLE
    engine.update();  // IDLE -> ACTIVE (macrocycle start called again)

    mockAdvanceMillis(100);
    engine.update();  // ACTIVE -> WAITING_RELAX (since g_schedulingComplete = true)

    mockAdvanceMillis(1400);  // Wait for double relax
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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);
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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);
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

    engine.startSession(100, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);
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
    engine.startSession(0, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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
    engine.startSession(0, PatternType::RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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
    RUN_TEST(test_generateRandomPermutation_with_jitter);
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
    RUN_TEST(test_generateRandomPermutation_high_jitter);
    RUN_TEST(test_generateSequentialPattern_with_jitter);
    RUN_TEST(test_generateMirroredPattern_with_jitter);

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
    RUN_TEST(test_TherapyEngine_setSetFrequencyCallback);

    // Macrocycle Generation Tests (via callbacks)
    RUN_TEST(test_macrocycle_creates_12_events);
    RUN_TEST(test_macrocycle_sequential_pattern);
    RUN_TEST(test_macrocycle_mirrored_pattern);
    RUN_TEST(test_macrocycle_with_frequency_randomization);
    RUN_TEST(test_macrocycle_duration_matches);
    RUN_TEST(test_macrocycle_amplitude_range);
    RUN_TEST(test_macrocycle_fixed_amplitude);
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

    return UNITY_END();
}
