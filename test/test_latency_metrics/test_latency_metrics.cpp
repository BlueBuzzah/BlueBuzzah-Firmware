/**
 * @file test_latency_metrics.cpp
 * @brief Unit tests for latency_metrics.h/cpp - Latency measurement and reporting
 */

#include <unity.h>
#include "latency_metrics.h"

// =============================================================================
// TEST FIXTURES
// =============================================================================

void setUp(void) {
    // Reset metrics before each test
    latencyMetrics.reset();
}

void tearDown(void) {
    // Clean up after each test
    latencyMetrics.reset();
}

// =============================================================================
// RESET TESTS
// =============================================================================

void test_reset_clears_enabled_flag(void) {
    latencyMetrics.enabled = true;
    latencyMetrics.reset();
    TEST_ASSERT_FALSE(latencyMetrics.enabled);
}

void test_reset_clears_verbose_logging(void) {
    latencyMetrics.verboseLogging = true;
    latencyMetrics.reset();
    TEST_ASSERT_FALSE(latencyMetrics.verboseLogging);
}

void test_reset_clears_drift_values(void) {
    latencyMetrics.lastDrift_us = 100;
    latencyMetrics.totalDrift_us = 1000;
    latencyMetrics.sampleCount = 10;
    latencyMetrics.reset();
    TEST_ASSERT_EQUAL_INT32(0, latencyMetrics.lastDrift_us);
    TEST_ASSERT_EQUAL_INT64(0, latencyMetrics.totalDrift_us);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.sampleCount);
}

void test_reset_initializes_drift_min_to_max(void) {
    latencyMetrics.minDrift_us = 0;
    latencyMetrics.reset();
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, latencyMetrics.minDrift_us);
}

void test_reset_initializes_drift_max_to_min(void) {
    latencyMetrics.maxDrift_us = 0;
    latencyMetrics.reset();
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, latencyMetrics.maxDrift_us);
}

void test_reset_clears_late_early_counts(void) {
    latencyMetrics.lateCount = 5;
    latencyMetrics.earlyCount = 3;
    latencyMetrics.reset();
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.lateCount);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.earlyCount);
}

void test_reset_clears_rtt_values(void) {
    latencyMetrics.lastRtt_us = 100;
    latencyMetrics.totalRtt_us = 1000;
    latencyMetrics.rttSampleCount = 10;
    latencyMetrics.reset();
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.lastRtt_us);
    TEST_ASSERT_EQUAL_UINT64(0, latencyMetrics.totalRtt_us);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.rttSampleCount);
}

void test_reset_initializes_rtt_min_to_max(void) {
    latencyMetrics.minRtt_us = 0;
    latencyMetrics.reset();
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, latencyMetrics.minRtt_us);
}

void test_reset_initializes_rtt_max_to_zero(void) {
    latencyMetrics.maxRtt_us = 1000;
    latencyMetrics.reset();
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.maxRtt_us);
}

void test_reset_clears_sync_probe_values(void) {
    latencyMetrics.syncProbeCount = 10;
    latencyMetrics.syncRttSpread_us = 5000;
    latencyMetrics.calculatedOffset_us = 12345;
    latencyMetrics.reset();
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.syncProbeCount);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.syncRttSpread_us);
    TEST_ASSERT_EQUAL_INT64(0, latencyMetrics.calculatedOffset_us);
}

void test_reset_initializes_sync_min_to_max(void) {
    latencyMetrics.syncMinRtt_us = 0;
    latencyMetrics.reset();
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, latencyMetrics.syncMinRtt_us);
}

void test_reset_initializes_sync_max_to_zero(void) {
    latencyMetrics.syncMaxRtt_us = 1000;
    latencyMetrics.reset();
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.syncMaxRtt_us);
}

void test_reset_is_idempotent(void) {
    latencyMetrics.reset();
    latencyMetrics.reset();
    latencyMetrics.reset();
    TEST_ASSERT_FALSE(latencyMetrics.enabled);
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, latencyMetrics.minDrift_us);
}

// =============================================================================
// ENABLE/DISABLE TESTS
// =============================================================================

void test_enable_sets_enabled_true(void) {
    latencyMetrics.enable();
    TEST_ASSERT_TRUE(latencyMetrics.enabled);
}

void test_enable_default_verbose_false(void) {
    latencyMetrics.enable();
    TEST_ASSERT_FALSE(latencyMetrics.verboseLogging);
}

void test_enable_with_verbose_true(void) {
    latencyMetrics.enable(true);
    TEST_ASSERT_TRUE(latencyMetrics.verboseLogging);
}

void test_enable_with_verbose_false_explicit(void) {
    latencyMetrics.enable(false);
    TEST_ASSERT_FALSE(latencyMetrics.verboseLogging);
}

void test_enable_first_time_resets_metrics(void) {
    // Set some values
    latencyMetrics.sampleCount = 100;
    latencyMetrics.lateCount = 50;
    // Enable should reset
    latencyMetrics.enable();
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.sampleCount);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.lateCount);
}

void test_enable_already_enabled_no_reset(void) {
    // Enable first time
    latencyMetrics.enable();
    // Record some data
    latencyMetrics.recordExecution(100);
    TEST_ASSERT_EQUAL_UINT32(1, latencyMetrics.sampleCount);
    // Enable again - should NOT reset
    latencyMetrics.enable();
    TEST_ASSERT_EQUAL_UINT32(1, latencyMetrics.sampleCount);
}

void test_enable_updates_verbose_when_already_enabled(void) {
    latencyMetrics.enable(false);
    TEST_ASSERT_FALSE(latencyMetrics.verboseLogging);
    // Enable again with verbose
    latencyMetrics.enable(true);
    TEST_ASSERT_TRUE(latencyMetrics.verboseLogging);
}

void test_disable_sets_enabled_false(void) {
    latencyMetrics.enable();
    latencyMetrics.disable();
    TEST_ASSERT_FALSE(latencyMetrics.enabled);
}

void test_disable_sets_verbose_false(void) {
    latencyMetrics.enable(true);
    latencyMetrics.disable();
    TEST_ASSERT_FALSE(latencyMetrics.verboseLogging);
}

void test_disable_when_already_disabled_is_safe(void) {
    latencyMetrics.disable();
    latencyMetrics.disable();
    TEST_ASSERT_FALSE(latencyMetrics.enabled);
}

// =============================================================================
// RECORD EXECUTION TESTS
// =============================================================================

void test_recordExecution_disabled_returns_early(void) {
    // Ensure disabled
    latencyMetrics.enabled = false;
    latencyMetrics.recordExecution(100);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.sampleCount);
}

void test_recordExecution_updates_last_drift(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(500);
    TEST_ASSERT_EQUAL_INT32(500, latencyMetrics.lastDrift_us);
}

void test_recordExecution_accumulates_total_drift(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(100);
    latencyMetrics.recordExecution(200);
    latencyMetrics.recordExecution(300);
    TEST_ASSERT_EQUAL_INT64(600, latencyMetrics.totalDrift_us);
}

void test_recordExecution_increments_sample_count(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(100);
    latencyMetrics.recordExecution(200);
    TEST_ASSERT_EQUAL_UINT32(2, latencyMetrics.sampleCount);
}

void test_recordExecution_updates_min_drift(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(500);
    latencyMetrics.recordExecution(100);
    latencyMetrics.recordExecution(300);
    TEST_ASSERT_EQUAL_INT32(100, latencyMetrics.minDrift_us);
}

void test_recordExecution_updates_max_drift(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(100);
    latencyMetrics.recordExecution(500);
    latencyMetrics.recordExecution(300);
    TEST_ASSERT_EQUAL_INT32(500, latencyMetrics.maxDrift_us);
}

void test_recordExecution_increments_late_count_above_threshold(void) {
    latencyMetrics.enable();
    // LATENCY_LATE_THRESHOLD_US = 1000
    latencyMetrics.recordExecution(1001);  // Late
    latencyMetrics.recordExecution(2000);  // Late
    latencyMetrics.recordExecution(500);   // Not late
    TEST_ASSERT_EQUAL_UINT32(2, latencyMetrics.lateCount);
}

void test_recordExecution_at_threshold_not_late(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(1000);  // Exactly at threshold - NOT late
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.lateCount);
}

void test_recordExecution_increments_early_count_negative(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(-100);  // Early
    latencyMetrics.recordExecution(-50);   // Early
    latencyMetrics.recordExecution(100);   // Not early
    TEST_ASSERT_EQUAL_UINT32(2, latencyMetrics.earlyCount);
}

void test_recordExecution_zero_drift_not_early(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(0);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.earlyCount);
}

void test_recordExecution_negative_min_positive_max(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(-500);
    latencyMetrics.recordExecution(500);
    TEST_ASSERT_EQUAL_INT32(-500, latencyMetrics.minDrift_us);
    TEST_ASSERT_EQUAL_INT32(500, latencyMetrics.maxDrift_us);
}

// =============================================================================
// RECORD RTT TESTS
// =============================================================================

void test_recordRtt_disabled_returns_early(void) {
    latencyMetrics.enabled = false;
    latencyMetrics.recordRtt(1000);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.rttSampleCount);
}

void test_recordRtt_updates_last_rtt(void) {
    latencyMetrics.enable();
    latencyMetrics.recordRtt(5000);
    TEST_ASSERT_EQUAL_UINT32(5000, latencyMetrics.lastRtt_us);
}

void test_recordRtt_accumulates_total(void) {
    latencyMetrics.enable();
    latencyMetrics.recordRtt(1000);
    latencyMetrics.recordRtt(2000);
    latencyMetrics.recordRtt(3000);
    TEST_ASSERT_EQUAL_UINT64(6000, latencyMetrics.totalRtt_us);
}

void test_recordRtt_increments_sample_count(void) {
    latencyMetrics.enable();
    latencyMetrics.recordRtt(1000);
    latencyMetrics.recordRtt(2000);
    TEST_ASSERT_EQUAL_UINT32(2, latencyMetrics.rttSampleCount);
}

void test_recordRtt_updates_min(void) {
    latencyMetrics.enable();
    latencyMetrics.recordRtt(5000);
    latencyMetrics.recordRtt(2000);
    latencyMetrics.recordRtt(3000);
    TEST_ASSERT_EQUAL_UINT32(2000, latencyMetrics.minRtt_us);
}

void test_recordRtt_updates_max(void) {
    latencyMetrics.enable();
    latencyMetrics.recordRtt(2000);
    latencyMetrics.recordRtt(5000);
    latencyMetrics.recordRtt(3000);
    TEST_ASSERT_EQUAL_UINT32(5000, latencyMetrics.maxRtt_us);
}

void test_recordRtt_zero_value(void) {
    latencyMetrics.enable();
    latencyMetrics.recordRtt(0);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.lastRtt_us);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.minRtt_us);
}

// =============================================================================
// RECORD SYNC PROBE TESTS
// =============================================================================

void test_recordSyncProbe_records_when_disabled(void) {
    latencyMetrics.enabled = false;
    latencyMetrics.recordSyncProbe(5000);
    TEST_ASSERT_EQUAL_UINT32(1, latencyMetrics.syncProbeCount);
}

void test_recordSyncProbe_increments_count(void) {
    latencyMetrics.recordSyncProbe(5000);
    latencyMetrics.recordSyncProbe(6000);
    latencyMetrics.recordSyncProbe(4000);
    TEST_ASSERT_EQUAL_UINT32(3, latencyMetrics.syncProbeCount);
}

void test_recordSyncProbe_updates_min(void) {
    latencyMetrics.recordSyncProbe(5000);
    latencyMetrics.recordSyncProbe(3000);
    latencyMetrics.recordSyncProbe(6000);
    TEST_ASSERT_EQUAL_UINT32(3000, latencyMetrics.syncMinRtt_us);
}

void test_recordSyncProbe_updates_max(void) {
    latencyMetrics.recordSyncProbe(3000);
    latencyMetrics.recordSyncProbe(6000);
    latencyMetrics.recordSyncProbe(5000);
    TEST_ASSERT_EQUAL_UINT32(6000, latencyMetrics.syncMaxRtt_us);
}

void test_recordSyncProbe_calculates_spread(void) {
    latencyMetrics.recordSyncProbe(3000);
    latencyMetrics.recordSyncProbe(8000);
    TEST_ASSERT_EQUAL_UINT32(5000, latencyMetrics.syncRttSpread_us);
}

void test_recordSyncProbe_first_probe_sets_both_min_max(void) {
    latencyMetrics.recordSyncProbe(5000);
    TEST_ASSERT_EQUAL_UINT32(5000, latencyMetrics.syncMinRtt_us);
    TEST_ASSERT_EQUAL_UINT32(5000, latencyMetrics.syncMaxRtt_us);
}

void test_recordSyncProbe_zero_rtt(void) {
    latencyMetrics.recordSyncProbe(0);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.syncMinRtt_us);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.syncMaxRtt_us);
    // Note: spread won't be calculated when max is 0 (guard condition)
}

// =============================================================================
// FINALIZE SYNC PROBING TESTS
// =============================================================================

void test_finalizeSyncProbing_sets_offset(void) {
    latencyMetrics.finalizeSyncProbing(12345);
    TEST_ASSERT_EQUAL_INT64(12345, latencyMetrics.calculatedOffset_us);
}

void test_finalizeSyncProbing_negative_offset(void) {
    latencyMetrics.finalizeSyncProbing(-5000);
    TEST_ASSERT_EQUAL_INT64(-5000, latencyMetrics.calculatedOffset_us);
}

void test_finalizeSyncProbing_zero_offset(void) {
    latencyMetrics.finalizeSyncProbing(0);
    TEST_ASSERT_EQUAL_INT64(0, latencyMetrics.calculatedOffset_us);
}

void test_finalizeSyncProbing_large_positive_offset(void) {
    int64_t largeOffset = 1000000000LL;  // 1 second
    latencyMetrics.finalizeSyncProbing(largeOffset);
    TEST_ASSERT_EQUAL_INT64(largeOffset, latencyMetrics.calculatedOffset_us);
}

void test_finalizeSyncProbing_large_negative_offset(void) {
    int64_t largeOffset = -1000000000LL;  // -1 second
    latencyMetrics.finalizeSyncProbing(largeOffset);
    TEST_ASSERT_EQUAL_INT64(largeOffset, latencyMetrics.calculatedOffset_us);
}

// =============================================================================
// GET AVERAGE DRIFT TESTS
// =============================================================================

void test_getAverageDrift_no_samples_returns_zero(void) {
    TEST_ASSERT_EQUAL_INT32(0, latencyMetrics.getAverageDrift());
}

void test_getAverageDrift_single_sample(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(500);
    TEST_ASSERT_EQUAL_INT32(500, latencyMetrics.getAverageDrift());
}

void test_getAverageDrift_multiple_samples(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(100);
    latencyMetrics.recordExecution(200);
    latencyMetrics.recordExecution(300);
    // Average = 600 / 3 = 200
    TEST_ASSERT_EQUAL_INT32(200, latencyMetrics.getAverageDrift());
}

void test_getAverageDrift_negative_average(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(-100);
    latencyMetrics.recordExecution(-200);
    latencyMetrics.recordExecution(-300);
    // Average = -600 / 3 = -200
    TEST_ASSERT_EQUAL_INT32(-200, latencyMetrics.getAverageDrift());
}

void test_getAverageDrift_mixed_positive_negative(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(100);
    latencyMetrics.recordExecution(-100);
    // Average = 0 / 2 = 0
    TEST_ASSERT_EQUAL_INT32(0, latencyMetrics.getAverageDrift());
}

void test_getAverageDrift_truncates_not_rounds(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(10);
    latencyMetrics.recordExecution(10);
    latencyMetrics.recordExecution(10);
    latencyMetrics.recordExecution(7);
    // Total = 37, count = 4, average = 37/4 = 9.25 -> truncates to 9
    TEST_ASSERT_EQUAL_INT32(9, latencyMetrics.getAverageDrift());
}

// =============================================================================
// GET AVERAGE RTT TESTS
// =============================================================================

void test_getAverageRtt_no_samples_returns_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.getAverageRtt());
}

void test_getAverageRtt_single_sample(void) {
    latencyMetrics.enable();
    latencyMetrics.recordRtt(5000);
    TEST_ASSERT_EQUAL_UINT32(5000, latencyMetrics.getAverageRtt());
}

void test_getAverageRtt_multiple_samples(void) {
    latencyMetrics.enable();
    latencyMetrics.recordRtt(1000);
    latencyMetrics.recordRtt(2000);
    latencyMetrics.recordRtt(3000);
    // Average = 6000 / 3 = 2000
    TEST_ASSERT_EQUAL_UINT32(2000, latencyMetrics.getAverageRtt());
}

void test_getAverageRtt_truncates_not_rounds(void) {
    latencyMetrics.enable();
    latencyMetrics.recordRtt(10);
    latencyMetrics.recordRtt(10);
    latencyMetrics.recordRtt(10);
    latencyMetrics.recordRtt(7);
    // Total = 37, count = 4, average = 37/4 = 9.25 -> truncates to 9
    TEST_ASSERT_EQUAL_UINT32(9, latencyMetrics.getAverageRtt());
}

// =============================================================================
// GET JITTER TESTS
// =============================================================================

void test_getJitter_no_samples_returns_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.getJitter());
}

void test_getJitter_single_sample(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(500);
    // Single sample: min=max=500, jitter = 0
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.getJitter());
}

void test_getJitter_two_samples_same_value(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(500);
    latencyMetrics.recordExecution(500);
    TEST_ASSERT_EQUAL_UINT32(0, latencyMetrics.getJitter());
}

void test_getJitter_positive_range(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(100);
    latencyMetrics.recordExecution(500);
    // Jitter = 500 - 100 = 400
    TEST_ASSERT_EQUAL_UINT32(400, latencyMetrics.getJitter());
}

void test_getJitter_negative_to_positive_range(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(-200);
    latencyMetrics.recordExecution(300);
    // Jitter = 300 - (-200) = 500
    TEST_ASSERT_EQUAL_UINT32(500, latencyMetrics.getJitter());
}

void test_getJitter_all_negative_range(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(-500);
    latencyMetrics.recordExecution(-100);
    // Jitter = -100 - (-500) = 400
    TEST_ASSERT_EQUAL_UINT32(400, latencyMetrics.getJitter());
}

// =============================================================================
// GET SYNC CONFIDENCE TESTS
// =============================================================================

void test_getSyncConfidence_no_probes_returns_unknown(void) {
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", latencyMetrics.getSyncConfidence());
}

void test_getSyncConfidence_spread_zero_returns_high(void) {
    latencyMetrics.recordSyncProbe(5000);
    latencyMetrics.recordSyncProbe(5000);  // Same value, spread = 0
    TEST_ASSERT_EQUAL_STRING("HIGH", latencyMetrics.getSyncConfidence());
}

void test_getSyncConfidence_spread_below_10ms_returns_high(void) {
    latencyMetrics.recordSyncProbe(5000);
    latencyMetrics.recordSyncProbe(14000);  // spread = 9000 < 10000
    TEST_ASSERT_EQUAL_STRING("HIGH", latencyMetrics.getSyncConfidence());
}

void test_getSyncConfidence_spread_at_9999_returns_high(void) {
    latencyMetrics.recordSyncProbe(0);
    latencyMetrics.recordSyncProbe(9999);  // spread = 9999 < 10000
    TEST_ASSERT_EQUAL_STRING("HIGH", latencyMetrics.getSyncConfidence());
}

void test_getSyncConfidence_spread_at_10000_returns_medium(void) {
    latencyMetrics.recordSyncProbe(0);
    latencyMetrics.recordSyncProbe(10000);  // spread = 10000 == threshold
    TEST_ASSERT_EQUAL_STRING("MEDIUM", latencyMetrics.getSyncConfidence());
}

void test_getSyncConfidence_spread_between_10_20ms_returns_medium(void) {
    latencyMetrics.recordSyncProbe(0);
    latencyMetrics.recordSyncProbe(15000);  // spread = 15000
    TEST_ASSERT_EQUAL_STRING("MEDIUM", latencyMetrics.getSyncConfidence());
}

void test_getSyncConfidence_spread_at_19999_returns_medium(void) {
    latencyMetrics.recordSyncProbe(0);
    latencyMetrics.recordSyncProbe(19999);  // spread = 19999 < 20000
    TEST_ASSERT_EQUAL_STRING("MEDIUM", latencyMetrics.getSyncConfidence());
}

void test_getSyncConfidence_spread_at_20000_returns_low(void) {
    latencyMetrics.recordSyncProbe(0);
    latencyMetrics.recordSyncProbe(20000);  // spread = 20000 == threshold
    TEST_ASSERT_EQUAL_STRING("LOW", latencyMetrics.getSyncConfidence());
}

void test_getSyncConfidence_spread_above_20ms_returns_low(void) {
    latencyMetrics.recordSyncProbe(0);
    latencyMetrics.recordSyncProbe(50000);  // spread = 50000 > 20000
    TEST_ASSERT_EQUAL_STRING("LOW", latencyMetrics.getSyncConfidence());
}

// =============================================================================
// PRINT REPORT TESTS (verify it doesn't crash, output not captured)
// =============================================================================

void test_printReport_empty_metrics_no_crash(void) {
    latencyMetrics.printReport();
    TEST_PASS();
}

void test_printReport_with_execution_data_no_crash(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(100);
    latencyMetrics.recordExecution(500);
    latencyMetrics.printReport();
    TEST_PASS();
}

void test_printReport_with_rtt_data_no_crash(void) {
    latencyMetrics.enable();
    latencyMetrics.recordRtt(5000);
    latencyMetrics.recordRtt(3000);
    latencyMetrics.printReport();
    TEST_PASS();
}

void test_printReport_with_sync_data_no_crash(void) {
    latencyMetrics.recordSyncProbe(5000);
    latencyMetrics.recordSyncProbe(8000);
    latencyMetrics.finalizeSyncProbing(1000);
    latencyMetrics.printReport();
    TEST_PASS();
}

void test_printReport_with_all_data_no_crash(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(100);
    latencyMetrics.recordExecution(-50);  // Early
    latencyMetrics.recordExecution(2000);  // Late
    latencyMetrics.recordRtt(5000);
    latencyMetrics.recordSyncProbe(3000);
    latencyMetrics.recordSyncProbe(8000);
    latencyMetrics.finalizeSyncProbing(-500);
    latencyMetrics.printReport();
    TEST_PASS();
}

void test_printReport_verbose_mode_no_crash(void) {
    latencyMetrics.enable(true);
    latencyMetrics.recordExecution(100);
    latencyMetrics.printReport();
    TEST_PASS();
}

void test_printReport_early_count_displayed(void) {
    latencyMetrics.enable();
    latencyMetrics.recordExecution(-100);
    latencyMetrics.recordExecution(-50);
    latencyMetrics.printReport();
    TEST_ASSERT_EQUAL_UINT32(2, latencyMetrics.earlyCount);
}

// =============================================================================
// TEST RUNNER
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Reset Tests
    RUN_TEST(test_reset_clears_enabled_flag);
    RUN_TEST(test_reset_clears_verbose_logging);
    RUN_TEST(test_reset_clears_drift_values);
    RUN_TEST(test_reset_initializes_drift_min_to_max);
    RUN_TEST(test_reset_initializes_drift_max_to_min);
    RUN_TEST(test_reset_clears_late_early_counts);
    RUN_TEST(test_reset_clears_rtt_values);
    RUN_TEST(test_reset_initializes_rtt_min_to_max);
    RUN_TEST(test_reset_initializes_rtt_max_to_zero);
    RUN_TEST(test_reset_clears_sync_probe_values);
    RUN_TEST(test_reset_initializes_sync_min_to_max);
    RUN_TEST(test_reset_initializes_sync_max_to_zero);
    RUN_TEST(test_reset_is_idempotent);

    // Enable/Disable Tests
    RUN_TEST(test_enable_sets_enabled_true);
    RUN_TEST(test_enable_default_verbose_false);
    RUN_TEST(test_enable_with_verbose_true);
    RUN_TEST(test_enable_with_verbose_false_explicit);
    RUN_TEST(test_enable_first_time_resets_metrics);
    RUN_TEST(test_enable_already_enabled_no_reset);
    RUN_TEST(test_enable_updates_verbose_when_already_enabled);
    RUN_TEST(test_disable_sets_enabled_false);
    RUN_TEST(test_disable_sets_verbose_false);
    RUN_TEST(test_disable_when_already_disabled_is_safe);

    // Record Execution Tests
    RUN_TEST(test_recordExecution_disabled_returns_early);
    RUN_TEST(test_recordExecution_updates_last_drift);
    RUN_TEST(test_recordExecution_accumulates_total_drift);
    RUN_TEST(test_recordExecution_increments_sample_count);
    RUN_TEST(test_recordExecution_updates_min_drift);
    RUN_TEST(test_recordExecution_updates_max_drift);
    RUN_TEST(test_recordExecution_increments_late_count_above_threshold);
    RUN_TEST(test_recordExecution_at_threshold_not_late);
    RUN_TEST(test_recordExecution_increments_early_count_negative);
    RUN_TEST(test_recordExecution_zero_drift_not_early);
    RUN_TEST(test_recordExecution_negative_min_positive_max);

    // Record RTT Tests
    RUN_TEST(test_recordRtt_disabled_returns_early);
    RUN_TEST(test_recordRtt_updates_last_rtt);
    RUN_TEST(test_recordRtt_accumulates_total);
    RUN_TEST(test_recordRtt_increments_sample_count);
    RUN_TEST(test_recordRtt_updates_min);
    RUN_TEST(test_recordRtt_updates_max);
    RUN_TEST(test_recordRtt_zero_value);

    // Record Sync Probe Tests
    RUN_TEST(test_recordSyncProbe_records_when_disabled);
    RUN_TEST(test_recordSyncProbe_increments_count);
    RUN_TEST(test_recordSyncProbe_updates_min);
    RUN_TEST(test_recordSyncProbe_updates_max);
    RUN_TEST(test_recordSyncProbe_calculates_spread);
    RUN_TEST(test_recordSyncProbe_first_probe_sets_both_min_max);
    RUN_TEST(test_recordSyncProbe_zero_rtt);

    // Finalize Sync Probing Tests
    RUN_TEST(test_finalizeSyncProbing_sets_offset);
    RUN_TEST(test_finalizeSyncProbing_negative_offset);
    RUN_TEST(test_finalizeSyncProbing_zero_offset);
    RUN_TEST(test_finalizeSyncProbing_large_positive_offset);
    RUN_TEST(test_finalizeSyncProbing_large_negative_offset);

    // Get Average Drift Tests
    RUN_TEST(test_getAverageDrift_no_samples_returns_zero);
    RUN_TEST(test_getAverageDrift_single_sample);
    RUN_TEST(test_getAverageDrift_multiple_samples);
    RUN_TEST(test_getAverageDrift_negative_average);
    RUN_TEST(test_getAverageDrift_mixed_positive_negative);
    RUN_TEST(test_getAverageDrift_truncates_not_rounds);

    // Get Average RTT Tests
    RUN_TEST(test_getAverageRtt_no_samples_returns_zero);
    RUN_TEST(test_getAverageRtt_single_sample);
    RUN_TEST(test_getAverageRtt_multiple_samples);
    RUN_TEST(test_getAverageRtt_truncates_not_rounds);

    // Get Jitter Tests
    RUN_TEST(test_getJitter_no_samples_returns_zero);
    RUN_TEST(test_getJitter_single_sample);
    RUN_TEST(test_getJitter_two_samples_same_value);
    RUN_TEST(test_getJitter_positive_range);
    RUN_TEST(test_getJitter_negative_to_positive_range);
    RUN_TEST(test_getJitter_all_negative_range);

    // Get Sync Confidence Tests
    RUN_TEST(test_getSyncConfidence_no_probes_returns_unknown);
    RUN_TEST(test_getSyncConfidence_spread_zero_returns_high);
    RUN_TEST(test_getSyncConfidence_spread_below_10ms_returns_high);
    RUN_TEST(test_getSyncConfidence_spread_at_9999_returns_high);
    RUN_TEST(test_getSyncConfidence_spread_at_10000_returns_medium);
    RUN_TEST(test_getSyncConfidence_spread_between_10_20ms_returns_medium);
    RUN_TEST(test_getSyncConfidence_spread_at_19999_returns_medium);
    RUN_TEST(test_getSyncConfidence_spread_at_20000_returns_low);
    RUN_TEST(test_getSyncConfidence_spread_above_20ms_returns_low);

    // Print Report Tests
    RUN_TEST(test_printReport_empty_metrics_no_crash);
    RUN_TEST(test_printReport_with_execution_data_no_crash);
    RUN_TEST(test_printReport_with_rtt_data_no_crash);
    RUN_TEST(test_printReport_with_sync_data_no_crash);
    RUN_TEST(test_printReport_with_all_data_no_crash);
    RUN_TEST(test_printReport_verbose_mode_no_crash);
    RUN_TEST(test_printReport_early_count_displayed);

    return UNITY_END();
}
