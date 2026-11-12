/**
 * @file test_activation_queue.cpp
 * @brief Host tests for ActivationQueue pairing, ordering and rollback
 */

#include <unity.h>
#include <Arduino.h>
#include "freertos_fake.h"

// Keep the real hardware.h (Wire, Adafruit) out of the build
#define HARDWARE_H
class HapticController {};

#include "../../src/profile_manager.cpp"
ProfileManager profiles;  // activation_queue.cpp declares `extern ProfileManager profiles`
#include "../../src/activation_queue.cpp"

static HapticController haptic;
static int fakeTaskStorage;
static TaskHandle_t fakeTaskHandle = &fakeTaskStorage;

void setUp(void) {
    g_fakeSemaphoreTakeResult = pdTRUE;
    activationQueue.begin(&haptic, fakeTaskHandle);  // also clears the queue
    g_fakeTaskNotifyCount = 0;
}

void tearDown(void) {}

void test_enqueue_adds_activate_and_deactivate(void) {
    TEST_ASSERT_TRUE(activationQueue.enqueue(1000, 2, 80, 100, 250));
    TEST_ASSERT_EQUAL_UINT8(2, activationQueue.eventCount());

    MotorEvent ev;
    TEST_ASSERT_TRUE(activationQueue.dequeueNextEvent(ev));
    TEST_ASSERT_TRUE(ev.type == MotorEventType::ACTIVATE);
    TEST_ASSERT_EQUAL_UINT64(1000, ev.timeUs);
    TEST_ASSERT_EQUAL_UINT8(2, ev.finger);
    TEST_ASSERT_EQUAL_UINT8(80, ev.amplitude);
    TEST_ASSERT_EQUAL_UINT16(250, ev.frequencyHz);

    TEST_ASSERT_TRUE(activationQueue.dequeueNextEvent(ev));
    TEST_ASSERT_TRUE(ev.type == MotorEventType::DEACTIVATE);
    TEST_ASSERT_EQUAL_UINT64(101000, ev.timeUs);
    TEST_ASSERT_EQUAL_UINT8(2, ev.finger);
}

void test_dequeue_returns_events_in_time_order(void) {
    TEST_ASSERT_TRUE(activationQueue.enqueue(5000, 0, 50, 1, 250));
    TEST_ASSERT_TRUE(activationQueue.enqueue(1000, 1, 50, 1, 250));

    const uint64_t expected[] = {1000, 2000, 5000, 6000};
    MotorEvent ev;
    for (uint64_t t : expected) {
        TEST_ASSERT_TRUE(activationQueue.dequeueNextEvent(ev));
        TEST_ASSERT_EQUAL_UINT64(t, ev.timeUs);
    }
    TEST_ASSERT_FALSE(activationQueue.dequeueNextEvent(ev));
}

void test_enqueue_fails_when_full_and_rolls_back(void) {
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_TRUE(activationQueue.enqueue(1000 + i, 0, 50, 1, 250));
    }
    TEST_ASSERT_EQUAL_UINT8(64, activationQueue.eventCount());
    TEST_ASSERT_FALSE(activationQueue.enqueue(9000, 0, 50, 1, 250));
    TEST_ASSERT_EQUAL_UINT8(64, activationQueue.eventCount());
}

void test_enqueue_rolls_back_when_only_one_slot_left(void) {
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_TRUE(activationQueue.enqueue(1000 + i, 0, 50, 1, 250));
    }
    MotorEvent ev;
    TEST_ASSERT_TRUE(activationQueue.dequeueNextEvent(ev));
    TEST_ASSERT_EQUAL_UINT8(63, activationQueue.eventCount());

    // Room for the activate but not the deactivate
    TEST_ASSERT_FALSE(activationQueue.enqueue(9000, 0, 50, 1, 250));
    TEST_ASSERT_EQUAL_UINT8(63, activationQueue.eventCount());
}

void test_enqueue_fails_when_mutex_unavailable(void) {
    g_fakeSemaphoreTakeResult = 0;
    TEST_ASSERT_FALSE(activationQueue.enqueue(1000, 0, 50, 1, 250));
    TEST_ASSERT_EQUAL_UINT8(0, activationQueue.eventCount());
}

void test_enqueue_notifies_motor_task(void) {
    TEST_ASSERT_TRUE(activationQueue.enqueue(1000, 0, 50, 1, 250));
    TEST_ASSERT_EQUAL_INT(1, g_fakeTaskNotifyCount);
}

void test_clearActivations_keeps_deactivations(void) {
    TEST_ASSERT_TRUE(activationQueue.enqueue(1000, 0, 50, 1, 250));
    TEST_ASSERT_TRUE(activationQueue.enqueue(3000, 1, 50, 1, 250));
    TEST_ASSERT_TRUE(activationQueue.enqueue(5000, 2, 50, 1, 250));

    activationQueue.clearActivations();

    TEST_ASSERT_EQUAL_UINT8(3, activationQueue.eventCount());
    MotorEvent ev;
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(activationQueue.dequeueNextEvent(ev));
        TEST_ASSERT_TRUE(ev.type == MotorEventType::DEACTIVATE);
    }
}

void test_clear_empties_queue(void) {
    TEST_ASSERT_TRUE(activationQueue.enqueue(1000, 0, 50, 1, 250));
    activationQueue.clear();
    TEST_ASSERT_TRUE(activationQueue.isEmpty());
    TEST_ASSERT_EQUAL_UINT8(0, activationQueue.eventCount());
    TEST_ASSERT_TRUE(activationQueue.getNextEventTime() == UINT64_MAX);
}

void test_peek_does_not_remove(void) {
    TEST_ASSERT_TRUE(activationQueue.enqueue(1000, 3, 50, 1, 250));

    MotorEvent ev;
    TEST_ASSERT_TRUE(activationQueue.peekNextEvent(ev));
    TEST_ASSERT_EQUAL_UINT64(1000, ev.timeUs);
    TEST_ASSERT_EQUAL_UINT8(3, ev.finger);
    TEST_ASSERT_EQUAL_UINT8(2, activationQueue.eventCount());
    TEST_ASSERT_EQUAL_UINT64(1000, activationQueue.getNextEventTime());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_enqueue_adds_activate_and_deactivate);
    RUN_TEST(test_dequeue_returns_events_in_time_order);
    RUN_TEST(test_enqueue_fails_when_full_and_rolls_back);
    RUN_TEST(test_enqueue_rolls_back_when_only_one_slot_left);
    RUN_TEST(test_enqueue_fails_when_mutex_unavailable);
    RUN_TEST(test_enqueue_notifies_motor_task);
    RUN_TEST(test_clearActivations_keeps_deactivations);
    RUN_TEST(test_clear_empties_queue);
    RUN_TEST(test_peek_does_not_remove);
    return UNITY_END();
}
