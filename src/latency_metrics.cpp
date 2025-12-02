/**
 * @file latency_metrics.cpp
 * @brief Latency measurement and reporting implementation
 * @version 1.0.0
 */

#include "latency_metrics.h"
#include "config.h"

// Global instance
LatencyMetrics latencyMetrics;

// =============================================================================
// RESET AND STATE MANAGEMENT
// =============================================================================

void LatencyMetrics::reset() {
    // State
    enabled = false;
    verboseLogging = false;

    // Execution drift
    lastDrift_us = 0;
    minDrift_us = INT32_MAX;
    maxDrift_us = INT32_MIN;
    totalDrift_us = 0;
    sampleCount = 0;
    lateCount = 0;
    earlyCount = 0;

    // Ongoing RTT
    lastRtt_us = 0;
    minRtt_us = UINT32_MAX;
    maxRtt_us = 0;
    totalRtt_us = 0;
    rttSampleCount = 0;

    // Sync quality
    syncProbeCount = 0;
    syncMinRtt_us = UINT32_MAX;
    syncMaxRtt_us = 0;
    syncRttSpread_us = 0;
    calculatedOffset_us = 0;
}

void LatencyMetrics::enable(bool verbose) {
    if (!enabled) {
        // Reset metrics when first enabling
        reset();
        enabled = true;
    }
    verboseLogging = verbose;
    Serial.print(F("[LATENCY] Metrics ENABLED"));
    if (verbose) {
        Serial.println(F(" (verbose mode)"));
    } else {
        Serial.println(F(" (aggregated mode)"));
    }
}

void LatencyMetrics::disable() {
    if (enabled) {
        Serial.println(F("[LATENCY] Metrics DISABLED"));
        printReport();  // Print final report before disabling
    }
    enabled = false;
    verboseLogging = false;
}

// =============================================================================
// RECORDING METHODS
// =============================================================================

void LatencyMetrics::recordExecution(int32_t drift_us) {
    if (!enabled) return;

    lastDrift_us = drift_us;
    totalDrift_us += drift_us;
    sampleCount++;

    // Update min/max
    if (drift_us < minDrift_us) minDrift_us = drift_us;
    if (drift_us > maxDrift_us) maxDrift_us = drift_us;

    // Track late/early
    if (drift_us > (int32_t)LATENCY_LATE_THRESHOLD_US) {
        lateCount++;
    } else if (drift_us < 0) {
        earlyCount++;
    }

    // Verbose logging
    if (verboseLogging) {
        Serial.printf("[LATENCY] Execution drift: %+ld us%s\n",
                      (long)drift_us,
                      (drift_us > (int32_t)LATENCY_LATE_THRESHOLD_US) ? " (LATE)" : "");
    }
}

void LatencyMetrics::recordRtt(uint32_t rtt_us) {
    if (!enabled) return;

    lastRtt_us = rtt_us;
    totalRtt_us += rtt_us;
    rttSampleCount++;

    // Update min/max
    if (rtt_us < minRtt_us) minRtt_us = rtt_us;
    if (rtt_us > maxRtt_us) maxRtt_us = rtt_us;

    // Verbose logging
    if (verboseLogging) {
        Serial.printf("[LATENCY] RTT: %lu us (one-way: ~%lu us)\n",
                      (unsigned long)rtt_us,
                      (unsigned long)(rtt_us / 2));
    }
}

void LatencyMetrics::recordSyncProbe(uint32_t rtt_us) {
    // Always record sync probes (even if metrics disabled, sync quality is important)
    syncProbeCount++;

    // Update min/max for sync probing
    if (rtt_us < syncMinRtt_us) syncMinRtt_us = rtt_us;
    if (rtt_us > syncMaxRtt_us) syncMaxRtt_us = rtt_us;

    // Update spread
    if (syncMinRtt_us != UINT32_MAX && syncMaxRtt_us != 0) {
        syncRttSpread_us = syncMaxRtt_us - syncMinRtt_us;
    }
}

void LatencyMetrics::finalizeSyncProbing(int64_t offset_us) {
    calculatedOffset_us = offset_us;

    Serial.println(F(""));
    Serial.println(F("===== SYNC PROBING COMPLETE ====="));
    Serial.printf("  Probes:     %lu\n", (unsigned long)syncProbeCount);
    Serial.printf("  Min RTT:    %lu us (one-way: %lu us)\n",
                  (unsigned long)syncMinRtt_us,
                  (unsigned long)(syncMinRtt_us / 2));
    Serial.printf("  Max RTT:    %lu us\n", (unsigned long)syncMaxRtt_us);
    Serial.printf("  Spread:     %lu us\n", (unsigned long)syncRttSpread_us);
    Serial.printf("  Offset:     %+ld us\n", (long)calculatedOffset_us);
    Serial.printf("  Confidence: %s\n", getSyncConfidence());
    Serial.println(F("================================="));
    Serial.println(F(""));
}

// =============================================================================
// COMPUTED METRICS
// =============================================================================

int32_t LatencyMetrics::getAverageDrift() const {
    if (sampleCount == 0) return 0;
    return (int32_t)(totalDrift_us / (int64_t)sampleCount);
}

uint32_t LatencyMetrics::getAverageRtt() const {
    if (rttSampleCount == 0) return 0;
    return (uint32_t)(totalRtt_us / (uint64_t)rttSampleCount);
}

uint32_t LatencyMetrics::getJitter() const {
    if (sampleCount == 0) return 0;
    if (minDrift_us == INT32_MAX || maxDrift_us == INT32_MIN) return 0;
    return (uint32_t)(maxDrift_us - minDrift_us);
}

const char* LatencyMetrics::getSyncConfidence() const {
    // Confidence based on RTT spread during sync probing
    // Lower spread = more stable BLE = better sync confidence
    if (syncProbeCount == 0) {
        return "UNKNOWN";
    } else if (syncRttSpread_us < 10000) {  // < 10ms spread
        return "HIGH";
    } else if (syncRttSpread_us < 20000) {  // < 20ms spread
        return "MEDIUM";
    } else {
        return "LOW";
    }
}

// =============================================================================
// REPORTING
// =============================================================================

void LatencyMetrics::printReport() const {
    Serial.println(F(""));
    Serial.println(F("========== LATENCY METRICS =========="));

    // Status
    Serial.print(F("Status: "));
    if (enabled) {
        Serial.print(F("ENABLED (verbose: "));
        Serial.print(verboseLogging ? F("ON") : F("OFF"));
        Serial.println(F(")"));
    } else {
        Serial.println(F("DISABLED"));
    }
    Serial.printf("Buzzes: %lu\n", (unsigned long)sampleCount);

    Serial.println(F("-------------------------------------"));

    // Sync quality section
    Serial.println(F("SYNC QUALITY (initial probing):"));
    if (syncProbeCount > 0) {
        Serial.printf("  Probes:     %lu\n", (unsigned long)syncProbeCount);
        Serial.printf("  Min RTT:    %lu us (one-way: %lu us)\n",
                      (unsigned long)syncMinRtt_us,
                      (unsigned long)(syncMinRtt_us / 2));
        Serial.printf("  Max RTT:    %lu us\n", (unsigned long)syncMaxRtt_us);
        Serial.printf("  Spread:     %lu us\n", (unsigned long)syncRttSpread_us);
        Serial.printf("  Offset:     %+ld us\n", (long)calculatedOffset_us);
        Serial.printf("  Confidence: %s\n", getSyncConfidence());
    } else {
        Serial.println(F("  (no sync probing data)"));
    }

    Serial.println(F("-------------------------------------"));

    // Execution drift section
    Serial.println(F("EXECUTION DRIFT:"));
    if (sampleCount > 0) {
        Serial.printf("  Last:    %+ld us\n", (long)lastDrift_us);
        Serial.printf("  Average: %+ld us\n", (long)getAverageDrift());
        Serial.printf("  Min:     %+ld us\n", (long)minDrift_us);
        Serial.printf("  Max:     %+ld us\n", (long)maxDrift_us);
        Serial.printf("  Jitter:  %lu us\n", (unsigned long)getJitter());

        // Late percentage
        float latePercent = (sampleCount > 0) ?
            (100.0f * lateCount / sampleCount) : 0.0f;
        Serial.printf("  Late (>%lu us): %lu (%.1f%%)\n",
                      (unsigned long)LATENCY_LATE_THRESHOLD_US,
                      (unsigned long)lateCount,
                      latePercent);

        if (earlyCount > 0) {
            Serial.printf("  Early (<0):  %lu (unexpected)\n",
                          (unsigned long)earlyCount);
        }
    } else {
        Serial.println(F("  (no execution data)"));
    }

    Serial.println(F("-------------------------------------"));

    // Ongoing RTT section
    Serial.println(F("ONGOING RTT (PRIMARY only):"));
    if (rttSampleCount > 0) {
        Serial.printf("  Last:    %lu us\n", (unsigned long)lastRtt_us);
        Serial.printf("  Average: %lu us\n", (unsigned long)getAverageRtt());
        Serial.printf("  Min:     %lu us\n", (unsigned long)minRtt_us);
        Serial.printf("  Max:     %lu us\n", (unsigned long)maxRtt_us);
        Serial.printf("  Samples: %lu\n", (unsigned long)rttSampleCount);
    } else {
        Serial.println(F("  (no RTT data)"));
    }

    Serial.println(F("====================================="));
    Serial.println(F(""));
}
