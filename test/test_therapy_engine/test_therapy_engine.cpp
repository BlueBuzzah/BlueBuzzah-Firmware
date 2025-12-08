/**
 * @file test_therapy_engine.cpp
 * @brief Unit tests for therapy_engine - Pattern generation and execution
 */

#include <unity.h>
#include "therapy_engine.h"
#include <ranges>
#include <algorithm>
#include <vector>

// Include source file directly for native testing
// (excluded from build_src_filter to avoid conflicts with other tests)
#include "../../src/therapy_engine.cpp"

// =============================================================================
// TEST HELPERS
// =============================================================================

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

    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 23.5f, 4, true);

    TEST_ASSERT_TRUE(engine.isRunning());
    TEST_ASSERT_FALSE(engine.isPaused());
    TEST_ASSERT_EQUAL_UINT32(7200, engine.getDurationSeconds());
}

void test_TherapyEngine_pause(void) {
    TherapyEngine engine;
    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 23.5f, 4, true);

    engine.pause();

    TEST_ASSERT_TRUE(engine.isRunning());
    TEST_ASSERT_TRUE(engine.isPaused());
}

void test_TherapyEngine_resume(void) {
    TherapyEngine engine;
    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 23.5f, 4, true);
    engine.pause();

    engine.resume();

    TEST_ASSERT_TRUE(engine.isRunning());
    TEST_ASSERT_FALSE(engine.isPaused());
}

void test_TherapyEngine_stop(void) {
    TherapyEngine engine;
    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 23.5f, 4, true);

    engine.stop();

    TEST_ASSERT_FALSE(engine.isRunning());
}

void test_TherapyEngine_resets_stats_on_start(void) {
    TherapyEngine engine;
    engine.startSession(100, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.stop();

    // Start new session
    engine.startSession(200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);

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

    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    // Advance time by 5000ms (5 seconds)
    mockAdvanceMillis(5000);

    TEST_ASSERT_EQUAL_UINT32(5, engine.getElapsedSeconds());
}

void test_TherapyEngine_getRemainingSeconds(void) {
    TherapyEngine engine;
    // Start with non-zero time (startTime == 0 is a guard condition in the code)
    mockSetMillis(1000);

    engine.startSession(100, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);

    // Advance time by 30 seconds
    mockAdvanceMillis(30000);

    TEST_ASSERT_EQUAL_UINT32(70, engine.getRemainingSeconds());
}

void test_TherapyEngine_elapsed_zero_when_not_running(void) {
    TherapyEngine engine;

    TEST_ASSERT_EQUAL_UINT32(0, engine.getElapsedSeconds());
}

void test_TherapyEngine_remaining_zero_when_not_running(void) {
    TherapyEngine engine;

    TEST_ASSERT_EQUAL_UINT32(0, engine.getRemainingSeconds());
}

// =============================================================================
// THERAPY ENGINE CALLBACK TESTS
// =============================================================================

void test_TherapyEngine_setActivateCallback(void) {
    TherapyEngine engine;
    engine.setActivateCallback(mockActivateCallback);

    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();  // Should trigger activation

    TEST_ASSERT_TRUE(g_activateCallCount > 0);
}

void test_TherapyEngine_setDeactivateCallback(void) {
    TherapyEngine engine;
    engine.setActivateCallback(mockActivateCallback);
    engine.setDeactivateCallback(mockDeactivateCallback);

    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();  // Activate

    // Advance past burst duration to trigger deactivation
    mockAdvanceMillis(150);
    engine.update();

    TEST_ASSERT_TRUE(g_deactivateCallCount > 0);
}

void test_TherapyEngine_pause_deactivates_motors(void) {
    TherapyEngine engine;
    engine.setActivateCallback(mockActivateCallback);
    engine.setDeactivateCallback(mockDeactivateCallback);

    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();  // Activate motors

    int deactivateCountBefore = g_deactivateCallCount;
    engine.pause();  // Should deactivate

    TEST_ASSERT_TRUE(g_deactivateCallCount > deactivateCountBefore);
}

void test_TherapyEngine_stop_deactivates_motors(void) {
    TherapyEngine engine;
    engine.setActivateCallback(mockActivateCallback);
    engine.setDeactivateCallback(mockDeactivateCallback);

    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();  // Activate motors

    int deactivateCountBefore = g_deactivateCallCount;
    engine.stop();  // Should deactivate

    TEST_ASSERT_TRUE(g_deactivateCallCount > deactivateCountBefore);
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

    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.pause();

    int countBefore = g_activateCallCount;
    engine.update();

    TEST_ASSERT_EQUAL_INT(countBefore, g_activateCallCount);
}

void test_TherapyEngine_session_timeout(void) {
    TherapyEngine engine;
    mockSetMillis(0);

    engine.startSession(10, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);  // 10 second session

    // Advance past session duration
    mockAdvanceMillis(11000);
    engine.update();

    TEST_ASSERT_FALSE(engine.isRunning());
}

// =============================================================================
// PATTERN TYPE TESTS
// =============================================================================

void test_TherapyEngine_pattern_type_rndp(void) {
    TherapyEngine engine;
    engine.setActivateCallback(mockActivateCallback);

    engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();

    // Should have activated (RNDP pattern generated)
    TEST_ASSERT_TRUE(g_activateCallCount > 0);
}

void test_TherapyEngine_pattern_type_sequential(void) {
    TherapyEngine engine;
    engine.setActivateCallback(mockActivateCallback);

    engine.startSession(7200, PATTERN_TYPE_SEQUENTIAL, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();

    TEST_ASSERT_TRUE(g_activateCallCount > 0);
}

void test_TherapyEngine_pattern_type_mirrored(void) {
    TherapyEngine engine;
    engine.setActivateCallback(mockActivateCallback);

    engine.startSession(7200, PATTERN_TYPE_MIRRORED, 100.0f, 67.0f, 0.0f, 4, true);
    engine.update();

    TEST_ASSERT_TRUE(g_activateCallCount > 0);
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

    // Therapy Engine Callback Tests
    RUN_TEST(test_TherapyEngine_setActivateCallback);
    RUN_TEST(test_TherapyEngine_setDeactivateCallback);
    RUN_TEST(test_TherapyEngine_pause_deactivates_motors);
    RUN_TEST(test_TherapyEngine_stop_deactivates_motors);

    // Therapy Engine Update Behavior Tests
    RUN_TEST(test_TherapyEngine_update_does_nothing_when_not_running);
    RUN_TEST(test_TherapyEngine_update_does_nothing_when_paused);
    RUN_TEST(test_TherapyEngine_session_timeout);

    // Pattern Type Tests
    RUN_TEST(test_TherapyEngine_pattern_type_rndp);
    RUN_TEST(test_TherapyEngine_pattern_type_sequential);
    RUN_TEST(test_TherapyEngine_pattern_type_mirrored);

    return UNITY_END();
}
