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
 * Message Format: COMMAND_TYPE:sequence_id:timestamp[:key|value|key|value...]\n
 * Example: BUZZ:42:1000000:0|100
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
 * Commands follow the format: COMMAND_TYPE:sequence_id:timestamp[:data]
 * Data payload uses pipe-delimited key-value pairs: key1|value1|key2|value2
 *
 * Usage:
 *   // Create command
 *   SyncCommand cmd = SyncCommand::createBuzz(42, 0, 100);
 *
 *   // Serialize
 *   char buffer[128];
 *   cmd.serialize(buffer, sizeof(buffer));
 *   // Result: "BUZZ:42:1234567890:0|100"
 *
 *   // Parse received message
 *   SyncCommand received;
 *   if (received.deserialize("HEARTBEAT:1:1234567890")) {
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
     * @brief Set integer data
     * @param key Key string
     * @param value Integer value
     * @return true if added successfully
     */
    bool setData(const char* key, int32_t value);

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
     * @brief Create HEARTBEAT command
     */
    static SyncCommand createHeartbeat(uint32_t sequenceId = 0);

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
     * @brief Create BUZZ command with motor activation duration
     * @param sequenceId Sequence ID for the command
     * @param finger Finger index (0-3)
     * @param amplitude Amplitude percentage (0-100)
     * @param durationMs How long motor should stay active (TIME_ON from profile)
     */
    static SyncCommand createBuzz(uint32_t sequenceId, uint8_t finger, uint8_t amplitude, uint32_t durationMs);

    /**
     * @brief Create DEACTIVATE command
     */
    static SyncCommand createDeactivate(uint32_t sequenceId = 0);

    /**
     * @brief Create PING command for latency measurement
     * @param sequenceId Sequence ID for the command
     */
    static SyncCommand createPing(uint32_t sequenceId);

    /**
     * @brief Create PONG response to PING
     * @param sequenceId Sequence ID (echoes the PING's sequence ID)
     */
    static SyncCommand createPong(uint32_t sequenceId);

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
 * @brief Get current time in microseconds
 */
inline uint64_t getMicros() {
    return (uint64_t)micros();
}

/**
 * @brief Get current time in milliseconds (high precision)
 */
inline uint32_t getMillis() {
    return millis();
}

// =============================================================================
// SEQUENCE ID GENERATOR
// =============================================================================

/**
 * @brief Thread-safe sequence ID generator
 */
class SequenceGenerator {
public:
    SequenceGenerator() : _nextId(1) {}

    /**
     * @brief Get next sequence ID
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
     */
    void updateLatency(uint32_t rttUs) {
        uint32_t oneWay = rttUs / 2;
        _measuredLatencyUs = oneWay;  // Store raw for diagnostics

        // First sample - initialize
        if (_sampleCount == 0) {
            _smoothedLatencyUs = oneWay;
            _sampleCount = 1;
            return;
        }

        // Outlier rejection: ignore if > 2x current smoothed
        if (oneWay > OUTLIER_MULT * _smoothedLatencyUs) {
            // Spike detected - don't update smoothed value
            return;
        }

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

private:
    // EMA tuning constants
    static constexpr uint16_t MIN_SAMPLES = 3;        // Minimum before using smoothed
    static constexpr uint32_t OUTLIER_MULT = 2;       // Reject if > 2x average
    static constexpr uint8_t EMA_ALPHA_NUM = 3;       // α = 3/10 = 0.3
    static constexpr uint8_t EMA_ALPHA_DEN = 10;

    int64_t _currentOffset;       // Current clock offset (microseconds)
    uint32_t _lastSyncTime;       // Time of last sync (millis)

    // Latency measurement with EMA smoothing
    uint32_t _measuredLatencyUs;  // Raw one-way latency (most recent)
    uint32_t _smoothedLatencyUs;  // EMA-smoothed latency (used for compensation)
    uint16_t _sampleCount;        // Number of valid samples received
};

#endif // SYNC_PROTOCOL_H
