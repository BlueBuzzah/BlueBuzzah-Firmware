/**
 * @file test_board_config.cpp
 * @brief Pins each board's finger-to-mux-channel wiring (board_config.h)
 */

#include <unity.h>
#include "config.h"

void setUp(void) {}
void tearDown(void) {}

#if defined(BOARD_BLUEBUZZAH_NRF52)

// v2 PCB: channel 0 drives the pinky pads, channel 3 the index pads
void test_fingerChannel_v2_isReversed(void) {
    TEST_ASSERT_EQUAL(3, FINGER_CHANNEL(FINGER_INDEX));
    TEST_ASSERT_EQUAL(2, FINGER_CHANNEL(FINGER_MIDDLE));
    TEST_ASSERT_EQUAL(1, FINGER_CHANNEL(FINGER_RING));
    TEST_ASSERT_EQUAL(0, FINGER_CHANNEL(FINGER_PINKY));
}

#else

// v3 PCB: finger N is channel N (its JST port labels are reversed instead)
void test_fingerChannel_v3_isFingerIndex(void) {
    TEST_ASSERT_EQUAL(0, FINGER_CHANNEL(FINGER_INDEX));
    TEST_ASSERT_EQUAL(1, FINGER_CHANNEL(FINGER_MIDDLE));
    TEST_ASSERT_EQUAL(2, FINGER_CHANNEL(FINGER_RING));
    TEST_ASSERT_EQUAL(3, FINGER_CHANNEL(FINGER_PINKY));
    TEST_ASSERT_EQUAL(4, FINGER_CHANNEL(FINGER_THUMB));
}

#endif

int main(int argc, char **argv) {
    UNITY_BEGIN();
#if defined(BOARD_BLUEBUZZAH_NRF52)
    RUN_TEST(test_fingerChannel_v2_isReversed);
#else
    RUN_TEST(test_fingerChannel_v3_isFingerIndex);
#endif
    return UNITY_END();
}
