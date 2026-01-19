/**
 * @file sync_protocol.h
 * @brief BlueBuzzah sync protocol - Command serialization and timing
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * Implements the synchronization protocol between PRIMARY and SECONDARY devices:
 * - Command serialization/deserialization
 * - Timestamp handling (microsecond precision)
 * - Message framing with EOT terminator
 *
 * Message Format: COMMAND_TYPE:sequence_id|timestamp[|param|param...]\n
 * Example: BUZZ:42|1000000|0|100
 */

#ifndef SYNC_PROTOCOL_H
#define SYNC_PROTOCOL_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

// =============================================================================
// PROTOCOL CONSTANTS
// =============================================================================

#define SYNC_CMD_DELIMITER ':'
#define SYNC_DATA_DELIMITER '|'
#define SYNC_MAX_DATA_PAIRS 8
#define SYNC_MAX_KEY_LEN 16
#define SYNC_MAX_VALUE_LEN 32

// =============================================================================
// SYNC COMMAND DATA
// =============================================================================

/**
 * @brief Key-value pair for command data payload
 */
struct SyncDataPair {
    char key[SYNC_MAX_KEY_LEN];
    char value[SYNC_MAX_VALUE_LEN];

    SyncDataPair() {
        memset(key, 0, sizeof(key));
        memset(value, 0, sizeof(value));
    }
};

// =============================================================================
// SYNC COMMAND CLASS
// =============================================================================

/**
 * @brief Represents a synchronization command between PRIMARY and SECONDARY
 *
 * Commands follow the format: COMMAND_TYPE:seq|timestamp[|param|param...]
 * Single colon separates command type, all parameters are pipe-delimited.
 *
 * Usage:
 *   // Create command
 *   SyncCommand cmd = SyncCommand::createPing(42, micros());
 *
 *   // Serialize
 *   char buffer[128];
 *   cmd.serialize(buffer, sizeof(buffer));
 *   // Result: "PING:42|1234567890"
 *
 *   // Parse received message
 *   SyncCommand received;
 *   if (received.deserialize("PING:1|1234567890")) {
 *       Serial.println(received.getTypeString());
 *   }
 */
class SyncCommand {
public:
    SyncCommand();
    explicit SyncCommand(SyncCommandType type, uint32_t sequenceId = 0);

    /**
     * @brief Serialize command to string
     * @param buffer Output buffer
     * @param bufferSize Size of output buffer
     * @return true if serialization successful
     */
    bool serialize(char* buffer, size_t bufferSize) const;

    /**
     * @brief Deserialize command from string
     * @param message Input message string
     * @return true if parsing successful
     */
    bool deserialize(const char* message);

    // =========================================================================
    // COMMAND PROPERTIES
    // =========================================================================

    /**
     * @brief Get command type
     */
    SyncCommandType getType() const { return _type; }

    /**
     * @brief Get command type as string
     */
    const char* getTypeString() const;

    /**
     * @brief Get sequence ID
     */
    uint32_t getSequenceId() const { return _sequenceId; }

    /**
     * @brief Get timestamp (microseconds)
     */
    uint64_t getTimestamp() const { return _timestamp; }

    /**
     * @brief Set command type
     */
    void setType(SyncCommandType type) { _type = type; }

    /**
     * @brief Set sequence ID
     */
    void setSequenceId(uint32_t id) { _sequenceId = id; }

    /**
     * @brief Set timestamp (microseconds)
     */
    void setTimestamp(uint64_t ts) { _timestamp = ts; }

    /**
     * @brief Set timestamp to current time
     */
    void setTimestampNow();

    // =========================================================================
    // DATA PAYLOAD
    // =========================================================================

    /**
     * @brief Set a key-value data pair
     * @param key Key string
     * @param value Value string
     * @return true if added successfully
     */
    bool setData(const char* key, const char* value);

    /**
     * @brief Set integer data (signed)
     * @param key Key string
     * @param value Integer value
     * @return true if added successfully
     */
    bool setData(const char* key, int32_t value);

    /**
     * @brief Set unsigned integer data
     * @param key Key string
     * @param value Unsigned integer value
     * @return true if added successfully
     * @note Use this for timestamp low-bits and other unsigned 32-bit values
     *       to avoid sign extension when values exceed 2^31
     */
    bool setDataUnsigned(const char* key, uint32_t value);

    /**
     * @brief Get data value by key
     * @param key Key to look up
     * @return Value string or nullptr if not found
     */
    const char* getData(const char* key) const;

    /**
     * @brief Get integer data value by key
     * @param key Key to look up
     * @param defaultValue Value to return if key not found
     * @return Integer value or defaultValue
     */
    int32_t getDataInt(const char* key, int32_t defaultValue = 0) const;

    /**
     * @brief Get unsigned integer data value by key
     * @param key Key to look up
     * @param defaultValue Value to return if key not found
     * @return Unsigned integer value or defaultValue
     *
     * NOTE: Use this for timestamp high/low values to avoid sign extension
     * issues when values exceed 2^31 (after ~35 minutes uptime).
     */
    uint32_t getDataUnsigned(const char* key, uint32_t defaultValue = 0) const;

    /**
     * @brief Check if data key exists
     */
    bool hasData(const char* key) const;

    /**
     * @brief Clear all data pairs
     */
    void clearData();

    /**
     * @brief Get number of data pairs
     */
    uint8_t getDataCount() const { return _dataCount; }

    // =========================================================================
    // CONVENIENCE FACTORIES
    // =========================================================================

    /**
     * @brief Create START_SESSION command
     */
    static SyncCommand createStartSession(uint32_t sequenceId = 0);

    /**
     * @brief Create PAUSE_SESSION command
     */
    static SyncCommand createPauseSession(uint32_t sequenceId = 0);

    /**
     * @brief Create RESUME_SESSION command
     */
    static SyncCommand createResumeSession(uint32_t sequenceId = 0);

    /**
     * @brief Create STOP_SESSION command
     */
    static SyncCommand createStopSession(uint32_t sequenceId = 0);

    /**
     * @brief Create DEACTIVATE command
     */
    static SyncCommand createDeactivate(uint32_t sequenceId = 0);

    /**
     * @brief Create PING command for latency measurement (legacy)
     * @param sequenceId Sequence ID for the command
     */
    static SyncCommand createPing(uint32_t sequenceId);

    /**
     * @brief Create PING command with explicit T1 timestamp for PTP sync
     * @param sequenceId Sequence ID for the command
     * @param t1 PRIMARY's send timestamp in microseconds
     */
    static SyncCommand createPingWithT1(uint32_t sequenceId, uint64_t t1);

    /**
     * @brief Create PONG response to PING (legacy)
     * @param sequenceId Sequence ID (echoes the PING's sequence ID)
     */
    static SyncCommand createPong(uint32_t sequenceId);

    /**
     * @brief Create PONG response with T2 and T3 timestamps for PTP sync
     * @param sequenceId Sequence ID (echoes the PING's sequence ID)
     * @param t2 SECONDARY's receive timestamp in microseconds
     * @param t3 SECONDARY's send timestamp in microseconds
     */
    static SyncCommand createPongWithTimestamps(uint32_t sequenceId, uint64_t t2, uint64_t t3);

    /**
     * @brief Create DEBUG_FLASH command for synchronized LED flash
     * @param sequenceId Sequence ID for the command
     */
    static SyncCommand createDebugFlash(uint32_t sequenceId);

    /**
     * @brief Create DEBUG_FLASH command with scheduled activation time
     * @param sequenceId Sequence ID for the command
     * @param flashTime Absolute time to trigger flash (in PRIMARY clock, microseconds)
     */
    static SyncCommand createDebugFlashWithTime(uint32_t sequenceId, uint64_t flashTime);

    /**
     * @brief Create MACROCYCLE_ACK response
     * @param sequenceId Sequence ID (should match received MACROCYCLE)
     */
    static SyncCommand createMacrocycleAck(uint32_t sequenceId);

    // =========================================================================
    // MACROCYCLE SERIALIZATION (hybrid text header + binary payload)
    // =========================================================================

    /**
     * @brief Serialize a macrocycle to buffer with hybrid format
     *
     * Format: MC:seq|baseTime|count|<binary_payload>
     * Binary payload is MacrocycleEvent array (6 bytes per event).
     *
     * @param buffer Output buffer (must be at least 120 bytes for 12 events)
     * @param bufferSize Size of output buffer
     * @param macrocycle Macrocycle to serialize
     * @return true if serialization successful
     */
    static bool serializeMacrocycle(char* buffer, size_t bufferSize, const Macrocycle& macrocycle);

    /**
     * @brief Calculate serialized size of a macrocycle
     * @param macrocycle Macrocycle to measure
     * @return Total serialized size in bytes
     */
    static size_t getMacrocycleSerializedSize(const Macrocycle& macrocycle);

    /**
     * @brief Deserialize a macrocycle from hybrid format message
     *
     * @param message Input message (text header + binary payload)
     * @param messageLen Total message length (including binary)
     * @param macrocycle Output macrocycle struct
     * @return true if deserialization successful
     */
    static bool deserializeMacrocycle(const char* message, size_t messageLen, Macrocycle& macrocycle);

private:
    SyncCommandType _type;
    uint32_t _sequenceId;
    uint64_t _timestamp;  // Microseconds

    SyncDataPair _data[SYNC_MAX_DATA_PAIRS];
    uint8_t _dataCount;

    // Parsing helpers
    bool parseCommandType(const char* typeStr);
    void serializeData(char* buffer, size_t bufferSize) const;
    bool parseData(const char* dataStr);
};

// =============================================================================
// TIMING UTILITIES
// =============================================================================

/**
 * @brief Get current time in microseconds (64-bit, overflow-safe)
 *
 * Tracks overflow of the 32-bit micros() counter to provide a true 64-bit
 * timestamp. Must be called at least once per 71 minutes to detect overflow.
 * Safe for main loop context only (not ISR-safe).
 */
uint64_t getMicros();

/**
 * @brief Reset getMicros() overflow tracking state
 *
 * Resets internal state used for tracking 32-bit micros() overflow.
 * Required for unit tests where mock time may be set to arbitrary values.
 * Should NOT be called in production code.
 */
void resetMicrosOverflow();

/**
 * @brief Get current time in milliseconds (high precision)
 */
inline uint32_t getMillis() {
    return millis();
}

/**
 * @brief Get current time in milliseconds (64-bit, overflow-safe)
 *
 * Tracks overflow of the 32-bit millis() counter to provide a true 64-bit
 * timestamp. Must be called at least once per 49.7 days to detect overflow.
 * Interrupt-safe (uses same protection pattern as getMicros()).
 */
uint64_t getMillis64();

// =============================================================================
// SEQUENCE ID GENERATOR
// =============================================================================

/**
 * @brief Sequence ID generator for BLE message tracking
 *
 * @note NOT thread-safe. Call only from main loop context.
 *       FreeRTOS tasks should not call next() directly.
 */
class SequenceGenerator {
public:
    SequenceGenerator() : _nextId(1) {}

    /**
     * @brief Get next sequence ID
     * @note NOT thread-safe - call only from main loop context
     */
    uint32_t next() { return _nextId++; }

    /**
     * @brief Reset sequence to initial value
     */
    void reset() { _nextId = 1; }

private:
    uint32_t _nextId;
};

// Global sequence generator
extern SequenceGenerator g_sequenceGenerator;

// =============================================================================
// SIMPLE SYNC PROTOCOL
// =============================================================================

/**
 * @brief Simple synchronization protocol for timestamp-based coordination
 *
 * Lightweight alternative to full NTP-style protocol, suitable for
 * memory-constrained devices with basic sync requirements.
 *
 * Usage:
 *   SimpleSyncProtocol sync;
 *
 *   // On receiving timestamp from PRIMARY
 *   int64_t offset = sync.calculateOffset(primaryTime, localTime);
 *
 *   // Compensate execution time
 *   uint64_t compensatedTime = sync.applyCompensation(timestamp);
 */
class SimpleSyncProtocol {
public:
    SimpleSyncProtocol();

    /**
     * @brief Calculate clock offset between PRIMARY and SECONDARY
     * @param primaryTime PRIMARY device timestamp (microseconds)
     * @param secondaryTime SECONDARY device timestamp (microseconds)
     * @return Clock offset in microseconds (positive = SECONDARY ahead)
     */
    int64_t calculateOffset(uint64_t primaryTime, uint64_t secondaryTime);

    /**
     * @brief Apply time compensation to a timestamp
     * @param timestamp Original timestamp (microseconds)
     * @return Compensated timestamp (microseconds)
     */
    uint64_t applyCompensation(uint64_t timestamp) const;

    /**
     * @brief Get current clock offset
     * @return Offset in microseconds
     */
    int64_t getOffset() const { return _currentOffset; }

    /**
     * @brief Check if sync has been performed
     */
    bool isSynced() const { return _lastSyncTime != 0; }

    /**
     * @brief Get the time of last sync measurement (millis)
     */
    uint32_t getLastSyncTime() const { return _lastSyncTime; }

    /**
     * @brief Get time since last sync in milliseconds
     */
    uint32_t getTimeSinceSync() const;

    /**
     * @brief Reset synchronization state
     */
    void reset();

    // =========================================================================
    // PING/PONG LATENCY MEASUREMENT (with EMA smoothing)
    // =========================================================================

    /**
     * @brief Update measured latency from RTT with EMA smoothing
     * @param rttUs Round-trip time in microseconds
     *
     * Uses exponential moving average to smooth latency values and
     * rejects outliers that are > 2x the current smoothed value.
     * Also tracks RTT variance for adaptive lead time calculation.
     */
    void updateLatency(uint32_t rttUs) {
        uint32_t oneWay = rttUs / 2;
        _measuredLatencyUs = oneWay;  // Store raw for diagnostics

        // First sample - initialize
        if (_sampleCount == 0) {
            _smoothedLatencyUs = oneWay;
            _rttVariance = 0;
            _sampleCount = 1;
            return;
        }

        // Outlier rejection: ignore if > 3x current smoothed (only after stable baseline)
        // Don't reject during initial sampling - need to build up _sampleCount first
        if (_sampleCount >= MIN_SAMPLES && oneWay > OUTLIER_MULT * _smoothedLatencyUs) {
            // Spike detected - don't update smoothed value
            return;
        }

        // Track variance (deviation from smoothed mean)
        // variance = |current - smoothed|
        uint32_t deviation = (oneWay > _smoothedLatencyUs) ?
                             (oneWay - _smoothedLatencyUs) :
                             (_smoothedLatencyUs - oneWay);

        // EMA smooth the variance (same alpha as latency)
        _rttVariance = (EMA_ALPHA_NUM * deviation +
                        (EMA_ALPHA_DEN - EMA_ALPHA_NUM) * _rttVariance)
                       / EMA_ALPHA_DEN;

        // EMA: new = α * measured + (1-α) * previous
        // Using integer math: new = (α_num * measured + (den - α_num) * prev) / den
        _smoothedLatencyUs = (EMA_ALPHA_NUM * oneWay +
                              (EMA_ALPHA_DEN - EMA_ALPHA_NUM) * _smoothedLatencyUs)
                             / EMA_ALPHA_DEN;

        if (_sampleCount < 0xFFFF) _sampleCount++;
    }

    /**
     * @brief Get smoothed one-way latency for sync compensation
     * @return Smoothed latency in microseconds (0 if not enough samples)
     */
    uint32_t getMeasuredLatency() const {
        // Return smoothed value only if we have enough samples
        if (_sampleCount >= MIN_SAMPLES) {
            return _smoothedLatencyUs;
        }
        return 0;  // Not enough data yet - no compensation
    }

    /**
     * @brief Get raw (unsmoothed) latency for diagnostics
     * @return Raw one-way latency from most recent measurement
     */
    uint32_t getRawLatency() const { return _measuredLatencyUs; }

    /**
     * @brief Get number of valid latency samples collected
     */
    uint16_t getSampleCount() const { return _sampleCount; }

    /**
     * @brief Reset latency measurement state (e.g., on reconnection)
     */
    void resetLatency() {
        _measuredLatencyUs = 0;
        _smoothedLatencyUs = 0;
        _sampleCount = 0;
    }

    // =========================================================================
    // PTP-STYLE CLOCK SYNCHRONIZATION
    // =========================================================================

    /**
     * @brief Calculate clock offset using IEEE 1588 PTP formula
     *
     * Uses 4 timestamps to calculate offset independent of network asymmetry:
     *   offset = ((T2 - T1) + (T3 - T4)) / 2
     *
     * @param t1 PRIMARY send timestamp (PRIMARY clock)
     * @param t2 SECONDARY receive timestamp (SECONDARY clock)
     * @param t3 SECONDARY send timestamp (SECONDARY clock)
     * @param t4 PRIMARY receive timestamp (PRIMARY clock)
     * @return Clock offset in microseconds (positive = SECONDARY ahead)
     */
    int64_t calculatePTPOffset(uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4);

    /**
     * @brief Add a clock offset sample for median filtering
     * @param offset Clock offset sample in microseconds
     */
    void addOffsetSample(int64_t offset);

    /**
     * @brief Add a clock offset sample with RTT-based quality filtering
     *
     * Samples with high RTT (indicating retransmissions or asymmetric delay)
     * are rejected to improve offset accuracy.
     *
     * @param offset Clock offset sample in microseconds
     * @param rttUs Round-trip time in microseconds
     * @return true if sample was accepted, false if rejected due to high RTT
     */
    bool addOffsetSampleWithQuality(int64_t offset, uint32_t rttUs);

    /**
     * @brief Get the median clock offset from collected samples
     * @return Median offset in microseconds (0 if not enough samples)
     */
    int64_t getMedianOffset() const;

    /**
     * @brief Check if clock synchronization is valid (enough stable samples)
     * @return true if sync is valid and can be used for scheduling
     */
    bool isClockSyncValid() const { return _clockSyncValid; }

    /**
     * @brief Get number of clock offset samples collected
     */
    uint8_t getOffsetSampleCount() const { return _offsetSampleCount; }

    /**
     * @brief Update clock offset using slow EMA (for continuous maintenance)
     * @param offset New offset measurement in microseconds
     */
    void updateOffsetEMA(int64_t offset);

    /**
     * @brief Reset clock synchronization state
     */
    void resetClockSync();

    /**
     * @brief Get drift-corrected clock offset
     *
     * Returns the median offset plus compensation for estimated crystal drift
     * since the last measurement. This provides more accurate offset between
     * sync events.
     *
     * @return Corrected offset in microseconds
     */
    int64_t getCorrectedOffset() const;

    /**
     * @brief Get estimated drift rate
     * @return Drift rate in microseconds per millisecond
     */
    float getDriftRate() const { return _driftRateUsPerMs; }

    /**
     * @brief Get average RTT in microseconds
     */
    uint32_t getAverageRTT() const { return _smoothedLatencyUs * 2; }

    /**
     * @brief Get RTT variance estimate
     * @return Variance estimate in microseconds
     */
    uint32_t getRTTVariance() const { return _rttVariance; }

    /**
     * @brief Calculate adaptive lead time based on RTT statistics
     *
     * Returns a lead time that's RTT + 3-sigma safety margin,
     * clamped to reasonable bounds (15-50ms).
     *
     * The maximum is capped at 50ms because lead time MUST be less than
     * TIME_ON (100ms) to prevent the TherapyEngine from calling deactivate()
     * before the scheduled activation fires.
     *
     * @return Adaptive lead time in microseconds
     */
    uint32_t calculateAdaptiveLeadTime() const;

    /**
     * @brief Convert PRIMARY clock time to SECONDARY local time
     * @param primaryTime Timestamp in PRIMARY clock (microseconds)
     * @return Equivalent time in SECONDARY clock (microseconds)
     */
    uint64_t primaryToLocalTime(uint64_t primaryTime) const {
        // offset = SECONDARY - PRIMARY, so local = primary + offset
        // Use drift-corrected offset for better accuracy
        return primaryTime + getCorrectedOffset();
    }

    /**
     * @brief Convert SECONDARY local time to PRIMARY clock time
     * @param localTime Timestamp in SECONDARY clock (microseconds)
     * @return Equivalent time in PRIMARY clock (microseconds)
     */
    uint64_t localToPrimaryTime(uint64_t localTime) const {
        return localTime - getCorrectedOffset();
    }

    // =========================================================================
    // WARM-START SYNC (Quick Recovery After Brief Disconnects)
    // =========================================================================

    /**
     * @brief Attempt warm-start sync using cached offset from recent disconnect
     *
     * Should be called on reconnection. If cache is valid (disconnect was <30s ago),
     * enters warm-start mode requiring only 3 confirmatory samples instead of 5.
     *
     * @return true if warm-start mode activated, false if cold start required
     */
    bool tryWarmStart();

    /**
     * @brief Explicitly invalidate warm-start cache
     *
     * Call this for intentional full resets (user-initiated, factory reset, etc.)
     */
    void invalidateWarmStartCache();

    /**
     * @brief Check if warm-start mode is active
     */
    bool isWarmStartMode() const { return _warmStartMode; }

    /**
     * @brief Get projected offset using cached value and drift rate
     * @return Projected offset in microseconds (0 if cache invalid)
     */
    int64_t getProjectedOffset() const;

    // =========================================================================
    // PATH ASYMMETRY TRACKING (Phase 1: Measurement Only)
    // =========================================================================

    /**
     * @brief Record path asymmetry from PTP timestamps
     *
     * Asymmetry = (outbound delay) - (return delay) = (T2-T1) - (T4-T3)
     * Positive asymmetry means PRIMARY->SECONDARY path is slower.
     *
     * @param t1 PRIMARY send timestamp
     * @param t2 SECONDARY receive timestamp
     * @param t3 SECONDARY send timestamp
     * @param t4 PRIMARY receive timestamp
     * @param phoneConnected Whether phone was connected during this measurement
     */
    void recordAsymmetry(uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4, bool phoneConnected);

    /**
     * @brief Get most recent raw asymmetry measurement
     * @return Asymmetry in microseconds (positive = PRIMARY->SECONDARY slower)
     */
    int64_t getRawAsymmetry() const { return _lastAsymmetry; }

    /**
     * @brief Get EMA-smoothed asymmetry estimate
     * @return Smoothed asymmetry in microseconds
     */
    int64_t getSmoothedAsymmetry() const { return _smoothedAsymmetry; }

    /**
     * @brief Get asymmetry variance estimate
     * @return Variance in microseconds
     */
    uint32_t getAsymmetryVariance() const { return _asymmetryVariance; }

    /**
     * @brief Get number of asymmetry samples collected
     */
    uint16_t getAsymmetrySampleCount() const { return _asymmetrySampleCount; }

    /**
     * @brief Check if phone was connected during last asymmetry measurement
     * @return true if phone was connected
     * @note Reserved for Phase 2 asymmetry compensation - correlates asymmetry with load state
     */
    bool wasPhoneConnectedDuringSync() const { return _phoneConnectedDuringSync; }

    /**
     * @brief Reset asymmetry tracking state
     */
    void resetAsymmetryTracking();

private:
    // EMA tuning constants
    static constexpr uint16_t MIN_SAMPLES = 3;        // Minimum before using smoothed
    static constexpr uint32_t OUTLIER_MULT = 3;       // Reject if > 3x average
    static constexpr uint8_t EMA_ALPHA_NUM = 3;       // α = 3/10 = 0.3
    static constexpr uint8_t EMA_ALPHA_DEN = 10;

    // PTP clock sync constants
    static constexpr uint8_t OFFSET_SAMPLE_COUNT = 10;

    int64_t _currentOffset;       // Current clock offset (microseconds)
    uint32_t _lastSyncTime;       // Time of last sync (millis)

    // Latency measurement with EMA smoothing
    uint32_t _measuredLatencyUs;  // Raw one-way latency (most recent)
    uint32_t _smoothedLatencyUs;  // EMA-smoothed latency (used for compensation)
    uint32_t _rttVariance;        // EMA-smoothed RTT variance (for adaptive lead time)
    uint16_t _sampleCount;        // Number of valid samples received

    // PTP clock synchronization
    int64_t _offsetSamples[OFFSET_SAMPLE_COUNT];  // Circular buffer of offset samples
    uint8_t _offsetSampleIndex;   // Next write position in circular buffer
    uint8_t _offsetSampleCount;   // Number of valid samples (0 to OFFSET_SAMPLE_COUNT)
    int64_t _medianOffset;        // Computed median offset
    bool _clockSyncValid;         // True when enough stable samples collected

    // Drift rate compensation
    int64_t _lastMeasuredOffset;  // Previous offset measurement for drift calculation
    uint32_t _lastOffsetTime;     // Time of last offset measurement (millis)
    float _driftRateUsPerMs;      // Estimated drift rate (microseconds per millisecond)

    // Warm-start cache for quick reconnection
    struct WarmStartCache {
        int64_t cachedOffset;        // Last known median offset
        float cachedDriftRate;       // Last known drift rate (us/ms)
        uint32_t cacheTimestamp;     // millis() when cached
        bool isValid;                // Cache contains usable data

        WarmStartCache() : cachedOffset(0), cachedDriftRate(0.0f),
                           cacheTimestamp(0), isValid(false) {}
    } _warmStartCache;

    bool _warmStartMode;             // Currently in warm-start recovery
    uint8_t _warmStartConfirmed;     // Number of validated confirmatory samples

    // Path asymmetry tracking (Phase 1: measurement only)
    int64_t _lastAsymmetry;          // Most recent asymmetry measurement (µs)
    int64_t _smoothedAsymmetry;      // EMA-smoothed asymmetry (µs)
    uint32_t _asymmetryVariance;     // EMA-smoothed variance (µs)
    uint16_t _asymmetrySampleCount;  // Valid asymmetry samples collected
    bool _phoneConnectedDuringSync;  // Phone connection state at measurement
};

#endif // SYNC_PROTOCOL_H
