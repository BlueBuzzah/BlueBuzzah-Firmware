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

// Scheduled execution constants
#define SYNC_EXECUTION_BUFFER_MS    50      // Schedule execution 50ms in future
#define SYNC_MAX_WAIT_US            100000  // Max spin-wait time (100ms)

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
     * @brief Create BUZZ command with scheduled execution time
     * @param sequenceId Sequence ID for the command
     * @param finger Finger index (0-4)
     * @param amplitude Amplitude percentage (0-100)
     * @param executeAt Scheduled execution time in microseconds (0 = immediate)
     */
    static SyncCommand createBuzz(uint32_t sequenceId, uint8_t finger, uint8_t amplitude, uint64_t executeAt);

    /**
     * @brief Create BUZZ command for immediate execution (backward compatible)
     * @param sequenceId Sequence ID for the command
     * @param finger Finger index (0-4)
     * @param amplitude Amplitude percentage (0-100)
     */
    static SyncCommand createBuzz(uint32_t sequenceId, uint8_t finger, uint8_t amplitude);

    /**
     * @brief Create BUZZED command (acknowledgment for BUZZ)
     */
    static SyncCommand createBuzzed(uint32_t sequenceId);

    /**
     * @brief Create DEACTIVATE command
     */
    static SyncCommand createDeactivate(uint32_t sequenceId = 0);

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
    volatile uint32_t _nextId;
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

    /**
     * @brief Calculate round-trip time from ping-pong exchange
     * @param sentTime Time when ping was sent
     * @param receivedTime Time when pong was received
     * @param remoteTime Remote device's timestamp in pong
     * @return Estimated one-way latency in microseconds
     */
    uint32_t calculateRoundTrip(uint64_t sentTime, uint64_t receivedTime, uint64_t remoteTime);

    // =========================================================================
    // SCHEDULED EXECUTION METHODS
    // =========================================================================

    /**
     * @brief Schedule execution time in the future
     * @param bufferMs Milliseconds to schedule ahead (default 50ms)
     * @return Scheduled execution time in microseconds
     */
    uint64_t scheduleExecution(uint32_t bufferMs = SYNC_EXECUTION_BUFFER_MS) const;

    /**
     * @brief Convert PRIMARY's scheduled time to local time
     * @param primaryScheduledTime Execution time in PRIMARY's clock (microseconds)
     * @return Equivalent time in local clock (microseconds)
     */
    uint64_t toLocalTime(uint64_t primaryScheduledTime) const;

    /**
     * @brief Spin-wait until scheduled execution time
     * @param scheduledTime Target time in local clock (microseconds)
     * @param maxWaitUs Maximum wait time before timeout (default 100ms)
     * @return true if reached target time, false if timeout
     */
    bool waitUntil(uint64_t scheduledTime, uint32_t maxWaitUs = SYNC_MAX_WAIT_US) const;

    /**
     * @brief Get estimated one-way latency
     * @return Latency in microseconds
     */
    uint32_t getEstimatedLatency() const { return _estimatedLatency; }

private:
    int64_t _currentOffset;     // Current clock offset (microseconds)
    uint32_t _lastSyncTime;     // Time of last sync (millis)
    uint32_t _estimatedLatency; // Estimated one-way latency (microseconds)
};

#endif // SYNC_PROTOCOL_H
