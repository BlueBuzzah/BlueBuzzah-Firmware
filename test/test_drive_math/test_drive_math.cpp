/**
 * @file test_drive_math.cpp
 * @brief Motor drive and battery calculations (drive_math.h)
 */

#include <unity.h>
#include "drive_math.h"

void setUp(void) {}
void tearDown(void) {}

void test_amplitudeToRtp_scales_percent_to_register_range(void) {
    TEST_ASSERT_EQUAL_UINT8(0, amplitudeToRtp(0));
    TEST_ASSERT_EQUAL_UINT8(63, amplitudeToRtp(50));
    TEST_ASSERT_EQUAL_UINT8(127, amplitudeToRtp(100));
}

void test_amplitudeToRtp_clamps_above_100_percent(void) {
    TEST_ASSERT_EQUAL_UINT8(127, amplitudeToRtp(101));
    TEST_ASSERT_EQUAL_UINT8(127, amplitudeToRtp(255));
}

void test_olLraPeriod_is_10153_over_frequency(void) {
    TEST_ASSERT_EQUAL_UINT8(40, olLraPeriod(250));
    TEST_ASSERT_EQUAL_UINT8(203, olLraPeriod(50));
    TEST_ASSERT_EQUAL_UINT8(33, olLraPeriod(300));
}

void test_battery_full_at_and_above_top_of_curve(void) {
    TEST_ASSERT_EQUAL_UINT8(100, batteryPercentFromVoltage(4.20f));
    TEST_ASSERT_EQUAL_UINT8(100, batteryPercentFromVoltage(4.50f));
}

void test_battery_empty_at_and_below_bottom_of_curve(void) {
    TEST_ASSERT_EQUAL_UINT8(0, batteryPercentFromVoltage(3.27f));
    TEST_ASSERT_EQUAL_UINT8(0, batteryPercentFromVoltage(0.0f));
}

void test_battery_curve_point_returns_its_percentage(void) {
    TEST_ASSERT_EQUAL_UINT8(50, batteryPercentFromVoltage(3.84f));
}

void test_battery_interpolates_between_points(void) {
    // halfway between 3.27 V (0%) and 3.61 V (5%) is 2.5%, truncated to 2
    TEST_ASSERT_EQUAL_UINT8(2, batteryPercentFromVoltage(3.44f));
}

void test_battery_percent_never_decreases_as_voltage_rises(void) {
    uint8_t previous = 0;
    for (int step = 0; step <= 105; step++) {  // 3.20 V to 4.25 V in 0.01 V steps
        const uint8_t percent = batteryPercentFromVoltage(3.20f + step * 0.01f);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(previous, percent);
        previous = percent;
    }
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_amplitudeToRtp_scales_percent_to_register_range);
    RUN_TEST(test_amplitudeToRtp_clamps_above_100_percent);
    RUN_TEST(test_olLraPeriod_is_10153_over_frequency);
    RUN_TEST(test_battery_full_at_and_above_top_of_curve);
    RUN_TEST(test_battery_empty_at_and_below_bottom_of_curve);
    RUN_TEST(test_battery_curve_point_returns_its_percentage);
    RUN_TEST(test_battery_interpolates_between_points);
    RUN_TEST(test_battery_percent_never_decreases_as_voltage_rises);
    return UNITY_END();
}
