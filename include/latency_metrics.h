/**
 * @file latency_metrics.h
 * @brief Latency measurement and reporting for sync analysis
 * @version 1.0.0
 *
 * Provides runtime-toggleable latency metrics collection for measuring
 * execution drift and BLE timing across PRIMARY/SECONDARY gloves.
 */

#ifndef LATENCY_METRICS_H
#define LATENCY_METRICS_H

#include <Arduino.h>
#include <stdint.h>

// Forward declaration for config constants
#ifndef LATENCY_LATE_THRESHOLD_US
#define LATENCY_LATE_THRESHOLD_US 1000  // >1ms considered "late"
#endif

/**
 * @brief Latency metrics collection and reporting
 *
 * Tracks execution drift (actual vs scheduled time), BLE RTT timing,
 * and sync quality metrics from initial RTT probing.
 */
struct LatencyMetrics {
    // ==========================================================================
    // STATE
    // ==========================================================================

    bool enabled;           ///< Whether metrics collection is active
    bool verboseLogging;    ///< Whether to log each individual buzz

    // ==========================================================================
    // EXECUTION DRIFT (actual - scheduled, microseconds)
    // ==========================================================================

    int32_t lastDrift_us;   ///< Most recent execution drift
    int32_t minDrift_us;    ///< Minimum observed drift
    int32_t maxDrift_us;    ///< Maximum observed drift
    int64_t totalDrift_us;  ///< Sum of all drifts (for average calculation)
    uint32_t sampleCount;   ///< Number of execution samples

    // Late/early tracking
    uint32_t lateCount;     ///< Count of executions with drift > threshold
    uint32_t earlyCount;    ///< Count of executions with negative drift

    // ==========================================================================
    // BLE RTT TIMING (ongoing, PRIMARY only)
    // ==========================================================================

    uint32_t lastRtt_us;    ///< Most recent RTT measurement
    uint32_t minRtt_us;     ///< Minimum observed RTT (best latency estimate)
    uint32_t maxRtt_us;     ///< Maximum observed RTT
    uint64_t totalRtt_us;   ///< Sum of all RTTs (for average calculation)
    uint32_t rttSampleCount;///< Number of RTT samples

    // ==========================================================================
    // SYNC QUALITY (from initial RTT probing)
    // ==========================================================================

    uint32_t syncProbeCount;    ///< Number of probes completed during sync
    uint32_t syncMinRtt_us;     ///< Minimum RTT from probing (used for offset)
    uint32_t syncMaxRtt_us;     ///< Maximum RTT from probing
    uint32_t syncRttSpread_us;  ///< max - min (lower = more stable BLE)
    int64_t calculatedOffset_us;///< Final calculated clock offset

    // ==========================================================================
    // METHODS
    // ==========================================================================

    /**
     * @brief Reset all metrics to initial state
     */
    void reset();

    /**
     * @brief Enable metrics collection
     * @param verbose If true, log each individual buzz to Serial
     */
    void enable(bool verbose = false);

    /**
     * @brief Disable metrics collection
     */
    void disable();

    /**
     * @brief Record an execution drift measurement
     * @param drift_us Drift in microseconds (actual - scheduled)
     */
    void recordExecution(int32_t drift_us);

    /**
     * @brief Record an RTT measurement (ongoing, during therapy)
     * @param rtt_us Round-trip time in microseconds
     */
    void recordRtt(uint32_t rtt_us);

    /**
     * @brief Record an RTT probe during initial sync
     * @param rtt_us Round-trip time in microseconds
     */
    void recordSyncProbe(uint32_t rtt_us);

    /**
     * @brief Finalize sync probing and record calculated offset
     * @param offset_us Calculated clock offset in microseconds
     */
    void finalizeSyncProbing(int64_t offset_us);

    /**
     * @brief Get average execution drift
     * @return Average drift in microseconds, or 0 if no samples
     */
    int32_t getAverageDrift() const;

    /**
     * @brief Get average RTT
     * @return Average RTT in microseconds, or 0 if no samples
     */
    uint32_t getAverageRtt() const;

    /**
     * @brief Get execution jitter (max - min drift)
     * @return Jitter in microseconds
     */
    uint32_t getJitter() const;

    /**
     * @brief Get sync confidence level as string
     * @return "HIGH", "MEDIUM", or "LOW" based on RTT spread
     */
    const char* getSyncConfidence() const;

    /**
     * @brief Print full metrics report to Serial
     */
    void printReport() const;
};

// Global instance declaration (defined in latency_metrics.cpp)
extern LatencyMetrics latencyMetrics;

#endif // LATENCY_METRICS_H
