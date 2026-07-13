#include <unity.h>
#include "vbat_estimator.h"

void setUp() {}
void tearDown() {}

// DRV2605 datasheet: VDD = raw * 5.6V / 255
void test_raw_to_volts_zero() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vbatRawToVolts(0));
}

void test_raw_to_volts_full_scale() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.6f, vbatRawToVolts(255));
}

void test_raw_to_volts_typical_lipo() {
    // 3.7V nominal -> raw = 3.7 * 255 / 5.6 = 168.48 -> raw 168 = 3.689V
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.689f, vbatRawToVolts(168));
}

void test_first_burst_sets_estimate_directly() {
    VbatEstimator e;
    TEST_ASSERT_FALSE(e.hasReading());
    uint8_t burst[] = {168, 168, 168, 168, 168};
    float v = e.addBurst(burst, 5);
    TEST_ASSERT_TRUE(e.hasReading());
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.689f, v);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.689f, e.voltage());
}

void test_median_rejects_sag_outlier() {
    VbatEstimator e;
    // One sample caught a motor-pulse sag (raw 120 ~= 2.6V); median ignores it
    uint8_t burst[] = {168, 167, 120, 168, 169};
    float v = e.addBurst(burst, 5);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.689f, v);  // median = 168
}

void test_empty_burst_keeps_previous_estimate() {
    VbatEstimator e;
    uint8_t burst[] = {168, 168, 168};
    e.addBurst(burst, 3);
    float before = e.voltage();
    float v = e.addBurst(nullptr, 0);  // bus busy / canary tripped
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, before, v);
    TEST_ASSERT_TRUE(e.hasReading());
}

void test_empty_burst_on_fresh_estimator_reports_no_reading() {
    VbatEstimator e;
    float v = e.addBurst(nullptr, 0);
    TEST_ASSERT_FALSE(e.hasReading());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

void test_ema_smooths_subsequent_bursts() {
    VbatEstimator e;
    uint8_t b1[] = {150, 150, 150};  // 3.294V
    uint8_t b2[] = {170, 170, 170};  // 3.733V
    e.addBurst(b1, 3);
    float v = e.addBurst(b2, 3);
    // EMA alpha 0.3: 0.7*3.294 + 0.3*3.733 = 3.426
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.426f, v);
}

void test_even_count_median_uses_upper_middle() {
    VbatEstimator e;
    // sorted {100,100,200,200}; sorted[4/2] = sorted[2] = 200
    uint8_t burst[] = {100, 200, 200, 100};
    float v = e.addBurst(burst, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 4.392f, v);
}

void test_truncates_past_max_burst() {
    VbatEstimator e;
    // First 16 samples are 100, last 4 (indices 16-19) are 255 and must be ignored
    uint8_t burst[20] = {100, 100, 100, 100, 100, 100, 100, 100,
                          100, 100, 100, 100, 100, 100, 100, 100,
                          255, 255, 255, 255};
    float v = e.addBurst(burst, 20);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 2.196f, v);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_raw_to_volts_zero);
    RUN_TEST(test_raw_to_volts_full_scale);
    RUN_TEST(test_raw_to_volts_typical_lipo);
    RUN_TEST(test_first_burst_sets_estimate_directly);
    RUN_TEST(test_median_rejects_sag_outlier);
    RUN_TEST(test_empty_burst_keeps_previous_estimate);
    RUN_TEST(test_empty_burst_on_fresh_estimator_reports_no_reading);
    RUN_TEST(test_ema_smooths_subsequent_bursts);
    RUN_TEST(test_even_count_median_uses_upper_middle);
    RUN_TEST(test_truncates_past_max_burst);
    return UNITY_END();
}
