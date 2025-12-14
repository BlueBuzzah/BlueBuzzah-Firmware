/**
 * @file test_motor_event_buffer.cpp
 * @brief Unit tests for MotorEventBuffer lock-free ring buffer
 */

#include <unity.h>
#include "motor_event_buffer.h"

// =============================================================================
// TEST FIXTURES
// =============================================================================

// Use a local instance for testing to avoid global state issues
static MotorEventBuffer buffer;

void setUp(void) {
    buffer.clear();
}

void tearDown(void) {
    buffer.clear();
}

// =============================================================================
// STAGED MOTOR EVENT TESTS
// =============================================================================

void test_StagedMotorEvent_default_constructor(void) {
    StagedMotorEvent event;
    TEST_ASSERT_EQUAL_UINT64(0, event.activateTimeUs);
    TEST_ASSERT_EQUAL_UINT8(0, event.finger);
    TEST_ASSERT_EQUAL_UINT8(0, event.amplitude);
    TEST_ASSERT_EQUAL_UINT16(0, event.durationMs);
    TEST_ASSERT_EQUAL_UINT16(0, event.frequencyHz);
    TEST_ASSERT_FALSE(event.isMacrocycleLast);
    TEST_ASSERT_FALSE(event.valid);
}

void test_StagedMotorEvent_clear(void) {
    StagedMotorEvent event;
    event.activateTimeUs = 123456;
    event.finger = 2;
    event.amplitude = 80;
    event.durationMs = 100;
    event.frequencyHz = 250;
    event.isMacrocycleLast = true;
    event.valid = true;

    event.clear();

    TEST_ASSERT_EQUAL_UINT64(0, event.activateTimeUs);
    TEST_ASSERT_EQUAL_UINT8(0, event.finger);
    TEST_ASSERT_EQUAL_UINT8(0, event.amplitude);
    TEST_ASSERT_EQUAL_UINT16(0, event.durationMs);
    TEST_ASSERT_EQUAL_UINT16(0, event.frequencyHz);
    TEST_ASSERT_FALSE(event.isMacrocycleLast);
    TEST_ASSERT_FALSE(event.valid);
}

// =============================================================================
// MOTOR EVENT BUFFER CONSTRUCTOR TESTS
// =============================================================================

void test_MotorEventBuffer_initial_state_empty(void) {
    TEST_ASSERT_FALSE(buffer.hasPending());
    TEST_ASSERT_EQUAL_UINT8(0, buffer.getPendingCount());
    TEST_ASSERT_FALSE(buffer.isMacrocyclePending());
}

// =============================================================================
// STAGE AND UNSTAGE TESTS
// =============================================================================

void test_MotorEventBuffer_stage_single_event(void) {
    bool result = buffer.stage(1000000, 1, 100, 50, 250, false);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(buffer.hasPending());
    TEST_ASSERT_EQUAL_UINT8(1, buffer.getPendingCount());
}

void test_MotorEventBuffer_unstage_single_event(void) {
    buffer.stage(1000000, 2, 80, 100, 300, false);

    StagedMotorEvent event;
    bool result = buffer.unstage(event);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT64(1000000, event.activateTimeUs);
    TEST_ASSERT_EQUAL_UINT8(2, event.finger);
    TEST_ASSERT_EQUAL_UINT8(80, event.amplitude);
    TEST_ASSERT_EQUAL_UINT16(100, event.durationMs);
    TEST_ASSERT_EQUAL_UINT16(300, event.frequencyHz);
    TEST_ASSERT_FALSE(event.isMacrocycleLast);
    TEST_ASSERT_TRUE(event.valid);
}

void test_MotorEventBuffer_unstage_empty_returns_false(void) {
    StagedMotorEvent event;
    bool result = buffer.unstage(event);
    TEST_ASSERT_FALSE(result);
}

void test_MotorEventBuffer_stage_multiple_events(void) {
    buffer.stage(1000000, 0, 100, 50, 250, false);
    buffer.stage(2000000, 1, 90, 60, 260, false);
    buffer.stage(3000000, 2, 80, 70, 270, false);

    TEST_ASSERT_EQUAL_UINT8(3, buffer.getPendingCount());
}

void test_MotorEventBuffer_fifo_order(void) {
    buffer.stage(1000000, 0, 100, 50, 250, false);
    buffer.stage(2000000, 1, 90, 60, 260, false);
    buffer.stage(3000000, 2, 80, 70, 270, false);

    StagedMotorEvent event;

    buffer.unstage(event);
    TEST_ASSERT_EQUAL_UINT64(1000000, event.activateTimeUs);
    TEST_ASSERT_EQUAL_UINT8(0, event.finger);

    buffer.unstage(event);
    TEST_ASSERT_EQUAL_UINT64(2000000, event.activateTimeUs);
    TEST_ASSERT_EQUAL_UINT8(1, event.finger);

    buffer.unstage(event);
    TEST_ASSERT_EQUAL_UINT64(3000000, event.activateTimeUs);
    TEST_ASSERT_EQUAL_UINT8(2, event.finger);

    TEST_ASSERT_FALSE(buffer.hasPending());
}

void test_MotorEventBuffer_buffer_full(void) {
    // Fill the buffer (MAX_STAGED - 1 because ring buffer wastes one slot)
    for (uint8_t i = 0; i < MotorEventBuffer::MAX_STAGED - 1; i++) {
        bool result = buffer.stage(i * 1000, i % 4, 100, 50, 250, false);
        TEST_ASSERT_TRUE(result);
    }

    // Next stage should fail
    bool result = buffer.stage(999999, 0, 100, 50, 250, false);
    TEST_ASSERT_FALSE(result);
}

void test_MotorEventBuffer_wrap_around(void) {
    // Stage and unstage to advance head/tail
    for (int i = 0; i < 10; i++) {
        buffer.stage(i * 1000, 0, 100, 50, 250, false);
        StagedMotorEvent event;
        buffer.unstage(event);
    }

    // Now stage more (should wrap around)
    buffer.stage(100000, 1, 80, 100, 300, false);
    buffer.stage(200000, 2, 70, 110, 310, false);

    TEST_ASSERT_EQUAL_UINT8(2, buffer.getPendingCount());

    StagedMotorEvent event;
    buffer.unstage(event);
    TEST_ASSERT_EQUAL_UINT64(100000, event.activateTimeUs);
    TEST_ASSERT_EQUAL_UINT8(1, event.finger);

    buffer.unstage(event);
    TEST_ASSERT_EQUAL_UINT64(200000, event.activateTimeUs);
    TEST_ASSERT_EQUAL_UINT8(2, event.finger);
}

// =============================================================================
// MACROCYCLE TESTS
// =============================================================================

void test_MotorEventBuffer_macrocycle_pending_initially_false(void) {
    TEST_ASSERT_FALSE(buffer.isMacrocyclePending());
}

void test_MotorEventBuffer_beginMacrocycle_sets_pending(void) {
    buffer.beginMacrocycle();
    TEST_ASSERT_TRUE(buffer.isMacrocyclePending());
}

void test_MotorEventBuffer_isMacrocycleLast_clears_pending(void) {
    buffer.beginMacrocycle();
    TEST_ASSERT_TRUE(buffer.isMacrocyclePending());

    // Stage event with isMacrocycleLast = false
    buffer.stage(1000000, 0, 100, 50, 250, false);

    StagedMotorEvent event;
    buffer.unstage(event);

    // Still pending after non-last event
    TEST_ASSERT_TRUE(buffer.isMacrocyclePending());

    // Stage event with isMacrocycleLast = true
    buffer.stage(2000000, 1, 100, 50, 250, true);

    buffer.unstage(event);
    TEST_ASSERT_TRUE(event.isMacrocycleLast);

    // Pending should now be cleared
    TEST_ASSERT_FALSE(buffer.isMacrocyclePending());
}

void test_MotorEventBuffer_full_macrocycle_batch(void) {
    buffer.beginMacrocycle();

    // Stage 12 events (typical macrocycle)
    for (uint8_t i = 0; i < 11; i++) {
        buffer.stage(i * 100000, i % 4, 100, 50, 250, false);
    }
    // Last event marks end of macrocycle
    buffer.stage(1100000, 3, 100, 50, 250, true);

    TEST_ASSERT_EQUAL_UINT8(12, buffer.getPendingCount());
    TEST_ASSERT_TRUE(buffer.isMacrocyclePending());

    // Unstage all events
    StagedMotorEvent event;
    for (uint8_t i = 0; i < 11; i++) {
        buffer.unstage(event);
        TEST_ASSERT_FALSE(event.isMacrocycleLast);
        TEST_ASSERT_TRUE(buffer.isMacrocyclePending());
    }

    // Last event
    buffer.unstage(event);
    TEST_ASSERT_TRUE(event.isMacrocycleLast);
    TEST_ASSERT_FALSE(buffer.isMacrocyclePending());
}

// =============================================================================
// CLEAR TESTS
// =============================================================================

void test_MotorEventBuffer_clear(void) {
    buffer.stage(1000000, 0, 100, 50, 250, false);
    buffer.stage(2000000, 1, 90, 60, 260, false);
    buffer.beginMacrocycle();

    buffer.clear();

    TEST_ASSERT_FALSE(buffer.hasPending());
    TEST_ASSERT_EQUAL_UINT8(0, buffer.getPendingCount());
    TEST_ASSERT_FALSE(buffer.isMacrocyclePending());
}

// =============================================================================
// PENDING COUNT TESTS
// =============================================================================

void test_MotorEventBuffer_getPendingCount_after_partial_unstage(void) {
    buffer.stage(1000000, 0, 100, 50, 250, false);
    buffer.stage(2000000, 1, 90, 60, 260, false);
    buffer.stage(3000000, 2, 80, 70, 270, false);

    TEST_ASSERT_EQUAL_UINT8(3, buffer.getPendingCount());

    StagedMotorEvent event;
    buffer.unstage(event);

    TEST_ASSERT_EQUAL_UINT8(2, buffer.getPendingCount());

    buffer.unstage(event);

    TEST_ASSERT_EQUAL_UINT8(1, buffer.getPendingCount());
}

void test_MotorEventBuffer_getPendingCount_with_wrap(void) {
    // Advance head/tail near end of buffer
    for (int i = 0; i < MotorEventBuffer::MAX_STAGED - 3; i++) {
        buffer.stage(i * 1000, 0, 100, 50, 250, false);
        StagedMotorEvent event;
        buffer.unstage(event);
    }

    // Stage events that will wrap around
    buffer.stage(1000000, 0, 100, 50, 250, false);
    buffer.stage(2000000, 1, 90, 60, 260, false);
    buffer.stage(3000000, 2, 80, 70, 270, false);
    buffer.stage(4000000, 3, 70, 80, 280, false);

    TEST_ASSERT_EQUAL_UINT8(4, buffer.getPendingCount());
}

// =============================================================================
// HAS PENDING TESTS
// =============================================================================

void test_MotorEventBuffer_hasPending_empty(void) {
    TEST_ASSERT_FALSE(buffer.hasPending());
}

void test_MotorEventBuffer_hasPending_with_events(void) {
    buffer.stage(1000000, 0, 100, 50, 250, false);
    TEST_ASSERT_TRUE(buffer.hasPending());
}

void test_MotorEventBuffer_hasPending_after_all_unstaged(void) {
    buffer.stage(1000000, 0, 100, 50, 250, false);

    StagedMotorEvent event;
    buffer.unstage(event);

    TEST_ASSERT_FALSE(buffer.hasPending());
}

// =============================================================================
// EDGE CASES
// =============================================================================

void test_MotorEventBuffer_stage_max_values(void) {
    bool result = buffer.stage(UINT64_MAX, 3, 100, UINT16_MAX, UINT16_MAX, true);
    TEST_ASSERT_TRUE(result);

    StagedMotorEvent event;
    buffer.unstage(event);

    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, event.activateTimeUs);
    TEST_ASSERT_EQUAL_UINT8(3, event.finger);
    TEST_ASSERT_EQUAL_UINT8(100, event.amplitude);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, event.durationMs);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, event.frequencyHz);
    TEST_ASSERT_TRUE(event.isMacrocycleLast);
}

void test_MotorEventBuffer_stage_zero_values(void) {
    bool result = buffer.stage(0, 0, 0, 0, 0, false);
    TEST_ASSERT_TRUE(result);

    StagedMotorEvent event;
    buffer.unstage(event);

    TEST_ASSERT_EQUAL_UINT64(0, event.activateTimeUs);
    TEST_ASSERT_EQUAL_UINT8(0, event.finger);
    TEST_ASSERT_EQUAL_UINT8(0, event.amplitude);
    TEST_ASSERT_EQUAL_UINT16(0, event.durationMs);
    TEST_ASSERT_EQUAL_UINT16(0, event.frequencyHz);
    TEST_ASSERT_FALSE(event.isMacrocycleLast);
}

// =============================================================================
// TEST RUNNER
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // StagedMotorEvent tests
    RUN_TEST(test_StagedMotorEvent_default_constructor);
    RUN_TEST(test_StagedMotorEvent_clear);

    // MotorEventBuffer constructor tests
    RUN_TEST(test_MotorEventBuffer_initial_state_empty);

    // Stage and unstage tests
    RUN_TEST(test_MotorEventBuffer_stage_single_event);
    RUN_TEST(test_MotorEventBuffer_unstage_single_event);
    RUN_TEST(test_MotorEventBuffer_unstage_empty_returns_false);
    RUN_TEST(test_MotorEventBuffer_stage_multiple_events);
    RUN_TEST(test_MotorEventBuffer_fifo_order);
    RUN_TEST(test_MotorEventBuffer_buffer_full);
    RUN_TEST(test_MotorEventBuffer_wrap_around);

    // Macrocycle tests
    RUN_TEST(test_MotorEventBuffer_macrocycle_pending_initially_false);
    RUN_TEST(test_MotorEventBuffer_beginMacrocycle_sets_pending);
    RUN_TEST(test_MotorEventBuffer_isMacrocycleLast_clears_pending);
    RUN_TEST(test_MotorEventBuffer_full_macrocycle_batch);

    // Clear tests
    RUN_TEST(test_MotorEventBuffer_clear);

    // Pending count tests
    RUN_TEST(test_MotorEventBuffer_getPendingCount_after_partial_unstage);
    RUN_TEST(test_MotorEventBuffer_getPendingCount_with_wrap);

    // Has pending tests
    RUN_TEST(test_MotorEventBuffer_hasPending_empty);
    RUN_TEST(test_MotorEventBuffer_hasPending_with_events);
    RUN_TEST(test_MotorEventBuffer_hasPending_after_all_unstaged);

    // Edge cases
    RUN_TEST(test_MotorEventBuffer_stage_max_values);
    RUN_TEST(test_MotorEventBuffer_stage_zero_values);

    return UNITY_END();
}
