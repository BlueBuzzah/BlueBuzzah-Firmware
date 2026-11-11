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
    for (uint8_t i = 0; i < 7; i++) {
        TEST_ASSERT_TRUE(queue.enqueue(DeferredWorkType::HAPTIC_PULSE, i));
    }
    TEST_ASSERT_FALSE(queue.enqueue(DeferredWorkType::HAPTIC_PULSE, 99));
    TEST_ASSERT_EQUAL_UINT8(7, queue.getPendingCount());

    // Draining one slot makes room for exactly one more
    TEST_ASSERT_TRUE(queue.processOne());
    TEST_ASSERT_TRUE(queue.enqueue(DeferredWorkType::HAPTIC_PULSE, 7));
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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_enqueue_refuses_when_full);
    RUN_TEST(test_processOne_runs_items_in_order);
    return UNITY_END();
}
