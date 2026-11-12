/**
 * @file test_deferred_queue.cpp
 * @brief Unit tests for DeferredQueue capacity and FIFO order
 *
 * deferred_queue.cpp is excluded from the native build, so it's pulled in
 * directly. Native critical sections are no-ops: this checks the queue's
 * logic, not the producer race (that needs hardware).
 */

#include <unity.h>
#include "../../src/deferred_queue.cpp"

static DeferredQueue queue;
static uint8_t executed[16];
static uint8_t executedCount;

static void recordExecutor(DeferredWorkType, uint8_t p1, uint8_t, uint32_t) {
    executed[executedCount++] = p1;
}

void setUp(void) {
    queue.clear();
    queue.setExecutor(recordExecutor);
    executedCount = 0;
}

void tearDown(void) {}

void test_enqueue_refuses_when_full(void) {
    // One slot stays empty to tell full from empty: MAX_WORK - 1 usable
    for (uint8_t i = 0; i < DeferredQueue::MAX_WORK - 1; i++) {
        TEST_ASSERT_TRUE(queue.enqueue(DeferredWorkType::HAPTIC_PULSE, i));
    }
    TEST_ASSERT_FALSE(queue.enqueue(DeferredWorkType::HAPTIC_PULSE, 99));
    TEST_ASSERT_EQUAL_UINT8(DeferredQueue::MAX_WORK - 1, queue.getPendingCount());

    // Draining one slot makes room for exactly one more
    TEST_ASSERT_TRUE(queue.processOne());
    TEST_ASSERT_TRUE(queue.enqueue(DeferredWorkType::HAPTIC_PULSE, DeferredQueue::MAX_WORK - 1));
    TEST_ASSERT_FALSE(queue.enqueue(DeferredWorkType::HAPTIC_PULSE, 99));
}

void test_processOne_runs_items_in_order(void) {
    for (uint8_t i = 0; i < 5; i++) {
        queue.enqueue(DeferredWorkType::HAPTIC_PULSE, i);
    }
    while (queue.processOne()) {}

    TEST_ASSERT_EQUAL_UINT8(5, executedCount);
    for (uint8_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, executed[i]);
    }
    TEST_ASSERT_FALSE(queue.hasPending());
}

void test_getPendingCount_after_wraparound(void) {
    for (uint8_t i = 0; i < 7; i++) queue.enqueue(DeferredWorkType::HAPTIC_PULSE, i);
    for (uint8_t i = 0; i < 5; i++) queue.processOne();
    // Head wraps past the end of the ring: 2 left + 4 new
    for (uint8_t i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(queue.enqueue(DeferredWorkType::HAPTIC_PULSE, 10 + i));
    }
    TEST_ASSERT_EQUAL_UINT8(6, queue.getPendingCount());

    // Order survives the wrap
    while (queue.processOne()) {}
    TEST_ASSERT_EQUAL_UINT8(5 + 6, executedCount);
    TEST_ASSERT_EQUAL_UINT8(5, executed[5]);
    TEST_ASSERT_EQUAL_UINT8(6, executed[6]);
    TEST_ASSERT_EQUAL_UINT8(10, executed[7]);
    TEST_ASSERT_EQUAL_UINT8(13, executed[10]);
}

void test_processOne_without_executor_drops_item(void) {
    queue.setExecutor(nullptr);
    queue.enqueue(DeferredWorkType::HAPTIC_PULSE, 1);
    TEST_ASSERT_TRUE(queue.processOne());
    TEST_ASSERT_EQUAL_UINT8(0, queue.getPendingCount());
    TEST_ASSERT_EQUAL_UINT8(0, executedCount);
}

void test_clear_discards_pending(void) {
    queue.enqueue(DeferredWorkType::HAPTIC_PULSE, 1);
    queue.enqueue(DeferredWorkType::HAPTIC_PULSE, 2);
    queue.clear();
    TEST_ASSERT_FALSE(queue.hasPending());
    TEST_ASSERT_EQUAL_UINT8(0, queue.getPendingCount());
    TEST_ASSERT_FALSE(queue.processOne());
    TEST_ASSERT_EQUAL_UINT8(0, executedCount);
}

static DeferredWorkType seenType;
static uint8_t seenP1;
static uint8_t seenP2;
static uint32_t seenP3;

static void captureExecutor(DeferredWorkType type, uint8_t p1, uint8_t p2, uint32_t p3) {
    seenType = type;
    seenP1 = p1;
    seenP2 = p2;
    seenP3 = p3;
}

void test_executor_receives_type_and_all_params(void) {
    queue.setExecutor(captureExecutor);
    queue.enqueue(DeferredWorkType::LED_FLASH, 1, 2, 0xDEADBEEF);
    TEST_ASSERT_TRUE(queue.processOne());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeferredWorkType::LED_FLASH),
                            static_cast<uint8_t>(seenType));
    TEST_ASSERT_EQUAL_UINT8(1, seenP1);
    TEST_ASSERT_EQUAL_UINT8(2, seenP2);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, seenP3);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_enqueue_refuses_when_full);
    RUN_TEST(test_processOne_runs_items_in_order);
    RUN_TEST(test_getPendingCount_after_wraparound);
    RUN_TEST(test_processOne_without_executor_drops_item);
    RUN_TEST(test_clear_discards_pending);
    RUN_TEST(test_executor_receives_type_and_all_params);
    return UNITY_END();
}
