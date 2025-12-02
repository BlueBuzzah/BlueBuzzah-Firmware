/**
 * @file sync_protocol.cpp
 * @brief BlueBuzzah sync protocol - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "sync_protocol.h"
#include <string.h>
#include <stdlib.h>

// =============================================================================
// GLOBAL SEQUENCE GENERATOR
// =============================================================================

SequenceGenerator g_sequenceGenerator;

// =============================================================================
// COMMAND TYPE STRING MAPPINGS
// =============================================================================

struct CommandTypeMapping {
    SyncCommandType type;
    const char* str;
};

static const CommandTypeMapping COMMAND_MAPPINGS[] = {
    { SyncCommandType::START_SESSION,  "START_SESSION" },
    { SyncCommandType::PAUSE_SESSION,  "PAUSE_SESSION" },
    { SyncCommandType::RESUME_SESSION, "RESUME_SESSION" },
    { SyncCommandType::STOP_SESSION,   "STOP_SESSION" },
    { SyncCommandType::BUZZ,   "BUZZ" },
    { SyncCommandType::DEACTIVATE,     "DEACTIVATE" },
    { SyncCommandType::HEARTBEAT,      "HEARTBEAT" },
    { SyncCommandType::SYNC_ADJ,       "SYNC_ADJ" },
    { SyncCommandType::SYNC_ADJ_START, "SYNC_ADJ_START" },
    { SyncCommandType::FIRST_SYNC,     "FIRST_SYNC" },
    { SyncCommandType::ACK_SYNC_ADJ,   "ACK_SYNC_ADJ" },
    { SyncCommandType::SYNC_PROBE,     "SYNC_PROBE" },
    { SyncCommandType::SYNC_PROBE_ACK, "SYNC_PROBE_ACK" }
};

static const size_t COMMAND_MAPPINGS_COUNT = sizeof(COMMAND_MAPPINGS) / sizeof(COMMAND_MAPPINGS[0]);

// =============================================================================
// SYNC COMMAND - CONSTRUCTOR
// =============================================================================

SyncCommand::SyncCommand() :
    _type(SyncCommandType::HEARTBEAT),
    _sequenceId(0),
    _timestamp(0),
    _dataCount(0)
{
    setTimestampNow();
}

SyncCommand::SyncCommand(SyncCommandType type, uint32_t sequenceId) :
    _type(type),
    _sequenceId(sequenceId),
    _timestamp(0),
    _dataCount(0)
{
    setTimestampNow();
}

// =============================================================================
// SYNC COMMAND - SERIALIZATION
// =============================================================================

bool SyncCommand::serialize(char* buffer, size_t bufferSize) const {
    if (!buffer || bufferSize < 32) {
        return false;
    }

    // Get command type string
    const char* typeStr = getTypeString();
    if (!typeStr) {
        return false;
    }

    // Format: COMMAND_TYPE:sequence_id:timestamp
    // Note: %llu doesn't work on ARM Arduino, so we print timestamp as two 32-bit parts
    uint32_t tsHigh = (uint32_t)(_timestamp >> 32);
    uint32_t tsLow = (uint32_t)(_timestamp & 0xFFFFFFFF);
    int written;
    if (tsHigh > 0) {
        written = snprintf(buffer, bufferSize, "%s:%lu:%lu%09lu",
                           typeStr,
                           (unsigned long)_sequenceId,
                           (unsigned long)tsHigh,
                           (unsigned long)tsLow);
    } else {
        written = snprintf(buffer, bufferSize, "%s:%lu:%lu",
                           typeStr,
                           (unsigned long)_sequenceId,
                           (unsigned long)tsLow);
    }

    if (written < 0 || (size_t)written >= bufferSize) {
        return false;
    }

    // Append data if present
    if (_dataCount > 0) {
        size_t remaining = bufferSize - written;
        serializeData(buffer + written, remaining);
    }

    // Debug: show what was serialized
    Serial.printf("[SYNC] Serialized: %s (data pairs: %d)\n", buffer, _dataCount);

    return true;
}

void SyncCommand::serializeData(char* buffer, size_t bufferSize) const {
    if (_dataCount == 0 || bufferSize < 2) {
        return;
    }

    // Add delimiter before data
    size_t pos = 0;
    buffer[pos++] = SYNC_CMD_DELIMITER;

    // Add values only (positional encoding - no keys)
    for (uint8_t i = 0; i < _dataCount && pos < bufferSize - 1; i++) {
        if (i > 0 && pos < bufferSize - 1) {
            buffer[pos++] = SYNC_DATA_DELIMITER;
        }

        // Add value only
        const char* value = _data[i].value;
        while (*value && pos < bufferSize - 1) {
            buffer[pos++] = *value++;
        }
    }

    buffer[pos] = '\0';
}

// =============================================================================
// SYNC COMMAND - DESERIALIZATION
// =============================================================================

bool SyncCommand::deserialize(const char* message) {
    if (!message || strlen(message) < 3) {
        return false;
    }

    // Clear current data
    clearData();

    // Make a copy for parsing header fields (type:seq:timestamp)
    char buffer[MESSAGE_BUFFER_SIZE];
    strncpy(buffer, message, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // Find data payload position in original message (after 3rd colon)
    // Format: COMMAND:seq:timestamp:data
    const char* dataStart = nullptr;
    int colonCount = 0;
    for (const char* p = message; *p; p++) {
        if (*p == ':') {
            colonCount++;
            if (colonCount == 3) {
                dataStart = p + 1;  // Position after 3rd colon
                break;
            }
        }
    }

    // Parse command type (first token)
    char* token = strtok(buffer, ":");
    if (!token || !parseCommandType(token)) {
        return false;
    }

    // Parse sequence ID (second token)
    token = strtok(nullptr, ":");
    if (!token) {
        return false;
    }
    _sequenceId = strtoul(token, nullptr, 10);

    // Parse timestamp (third token)
    token = strtok(nullptr, ":");
    if (!token) {
        return false;
    }
    _timestamp = strtoull(token, nullptr, 10);

    // Parse data payload if present (found via colon counting above)
    if (dataStart && strlen(dataStart) > 0) {
        parseData(dataStart);
    }

    return true;
}

bool SyncCommand::parseCommandType(const char* typeStr) {
    for (size_t i = 0; i < COMMAND_MAPPINGS_COUNT; i++) {
        if (strcmp(typeStr, COMMAND_MAPPINGS[i].str) == 0) {
            _type = COMMAND_MAPPINGS[i].type;
            return true;
        }
    }
    return false;
}

bool SyncCommand::parseData(const char* dataStr) {
    if (!dataStr || strlen(dataStr) == 0) {
        return true;  // No data is valid
    }

    // Make a copy for parsing
    char buffer[MESSAGE_BUFFER_SIZE];
    strncpy(buffer, dataStr, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // Parse pipe-delimited positional values
    char* token = strtok(buffer, "|");
    uint8_t index = 0;
    char indexKey[4];

    while (token && _dataCount < SYNC_MAX_DATA_PAIRS) {
        snprintf(indexKey, sizeof(indexKey), "%d", index);
        setData(indexKey, token);
        index++;
        token = strtok(nullptr, "|");
    }

    return true;
}

// =============================================================================
// SYNC COMMAND - TYPE STRING
// =============================================================================

const char* SyncCommand::getTypeString() const {
    for (size_t i = 0; i < COMMAND_MAPPINGS_COUNT; i++) {
        if (COMMAND_MAPPINGS[i].type == _type) {
            return COMMAND_MAPPINGS[i].str;
        }
    }
    return "UNKNOWN";
}

// =============================================================================
// SYNC COMMAND - TIMESTAMP
// =============================================================================

void SyncCommand::setTimestampNow() {
    _timestamp = getMicros();
}

// =============================================================================
// SYNC COMMAND - DATA PAYLOAD
// =============================================================================

bool SyncCommand::setData(const char* key, const char* value) {
    if (!key || !value || _dataCount >= SYNC_MAX_DATA_PAIRS) {
        return false;
    }

    // Check if key already exists
    for (uint8_t i = 0; i < _dataCount; i++) {
        if (strcmp(_data[i].key, key) == 0) {
            // Update existing value
            strncpy(_data[i].value, value, SYNC_MAX_VALUE_LEN - 1);
            _data[i].value[SYNC_MAX_VALUE_LEN - 1] = '\0';
            return true;
        }
    }

    // Add new pair
    strncpy(_data[_dataCount].key, key, SYNC_MAX_KEY_LEN - 1);
    _data[_dataCount].key[SYNC_MAX_KEY_LEN - 1] = '\0';
    strncpy(_data[_dataCount].value, value, SYNC_MAX_VALUE_LEN - 1);
    _data[_dataCount].value[SYNC_MAX_VALUE_LEN - 1] = '\0';
    _dataCount++;

    return true;
}

bool SyncCommand::setData(const char* key, int32_t value) {
    char valueStr[16];
    snprintf(valueStr, sizeof(valueStr), "%ld", (long)value);
    return setData(key, valueStr);
}

const char* SyncCommand::getData(const char* key) const {
    for (uint8_t i = 0; i < _dataCount; i++) {
        if (strcmp(_data[i].key, key) == 0) {
            return _data[i].value;
        }
    }
    return nullptr;
}

int32_t SyncCommand::getDataInt(const char* key, int32_t defaultValue) const {
    const char* value = getData(key);
    if (!value) {
        return defaultValue;
    }
    return strtol(value, nullptr, 10);
}

bool SyncCommand::hasData(const char* key) const {
    return getData(key) != nullptr;
}

void SyncCommand::clearData() {
    _dataCount = 0;
    for (uint8_t i = 0; i < SYNC_MAX_DATA_PAIRS; i++) {
        memset(_data[i].key, 0, SYNC_MAX_KEY_LEN);
        memset(_data[i].value, 0, SYNC_MAX_VALUE_LEN);
    }
}

// =============================================================================
// SYNC COMMAND - FACTORY METHODS
// =============================================================================

SyncCommand SyncCommand::createHeartbeat(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::HEARTBEAT, sequenceId);
}

SyncCommand SyncCommand::createStartSession(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::START_SESSION, sequenceId);
}

SyncCommand SyncCommand::createPauseSession(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::PAUSE_SESSION, sequenceId);
}

SyncCommand SyncCommand::createResumeSession(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::RESUME_SESSION, sequenceId);
}

SyncCommand SyncCommand::createStopSession(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::STOP_SESSION, sequenceId);
}

SyncCommand SyncCommand::createBuzz(uint32_t sequenceId, uint8_t finger, uint8_t amplitude, uint64_t executeAt) {
    SyncCommand cmd(SyncCommandType::BUZZ, sequenceId);
    cmd.setData("0", (int32_t)finger);
    cmd.setData("1", (int32_t)amplitude);

    // Add scheduled execution time if specified (split 64-bit into 2x32-bit for ARM)
    if (executeAt > 0) {
        uint32_t execHigh = (uint32_t)(executeAt >> 32);
        uint32_t execLow = (uint32_t)(executeAt & 0xFFFFFFFF);
        char highStr[16], lowStr[16];
        snprintf(highStr, sizeof(highStr), "%lu", (unsigned long)execHigh);
        snprintf(lowStr, sizeof(lowStr), "%lu", (unsigned long)execLow);
        cmd.setData("2", highStr);
        cmd.setData("3", lowStr);
    }
    return cmd;
}

SyncCommand SyncCommand::createBuzz(uint32_t sequenceId, uint8_t finger, uint8_t amplitude) {
    // Backward compatible - immediate execution (executeAt = 0)
    return createBuzz(sequenceId, finger, amplitude, 0);
}

SyncCommand SyncCommand::createDeactivate(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::DEACTIVATE, sequenceId);
}

// =============================================================================
// SIMPLE SYNC PROTOCOL - IMPLEMENTATION
// =============================================================================

SimpleSyncProtocol::SimpleSyncProtocol() :
    _currentOffset(0),
    _lastSyncTime(0),
    _estimatedLatency(0),
    _rttSampleCount(0),
    _currentProbeSeq(0),
    _probeSentTime(0),
    _probingInProgress(false)
{
    memset(_rttSamples, 0, sizeof(_rttSamples));
}

int64_t SimpleSyncProtocol::calculateOffset(uint64_t primaryTime, uint64_t secondaryTime) {
    // Simple offset calculation: secondary - primary
    // Positive offset means SECONDARY clock is ahead
    _currentOffset = (int64_t)secondaryTime - (int64_t)primaryTime;
    _lastSyncTime = millis();
    return _currentOffset;
}

uint64_t SimpleSyncProtocol::applyCompensation(uint64_t timestamp) const {
    // Adjust timestamp by subtracting the offset
    // If SECONDARY is ahead (positive offset), we subtract to align
    return timestamp - _currentOffset;
}

uint32_t SimpleSyncProtocol::getTimeSinceSync() const {
    if (_lastSyncTime == 0) {
        return UINT32_MAX;  // Never synced
    }
    return millis() - _lastSyncTime;
}

void SimpleSyncProtocol::reset() {
    _currentOffset = 0;
    _lastSyncTime = 0;
    _estimatedLatency = 0;
}

uint32_t SimpleSyncProtocol::calculateRoundTrip(uint64_t sentTime, uint64_t receivedTime, uint64_t remoteTime) {
    // Round trip time = received - sent
    uint64_t rtt = receivedTime - sentTime;

    // One-way latency estimate = RTT / 2
    _estimatedLatency = (uint32_t)(rtt / 2);

    // Recalculate offset accounting for latency
    // Remote time was captured at: sentTime + latency
    // So offset = remoteTime - (sentTime + latency)
    _currentOffset = (int64_t)remoteTime - (int64_t)(sentTime + _estimatedLatency);
    _lastSyncTime = millis();

    return _estimatedLatency;
}

// =============================================================================
// SIMPLE SYNC PROTOCOL - SCHEDULED EXECUTION
// =============================================================================

uint64_t SimpleSyncProtocol::scheduleExecution(uint32_t bufferMs) const {
    // Schedule execution bufferMs in the future
    return getMicros() + ((uint64_t)bufferMs * 1000ULL);
}

uint64_t SimpleSyncProtocol::toLocalTime(uint64_t primaryScheduledTime) const {
    // Convert PRIMARY's time to SECONDARY's local time by applying offset
    // If SECONDARY is ahead (positive offset), add offset to get local time
    // If SECONDARY is behind (negative offset), subtract to get local time
    return primaryScheduledTime + _currentOffset;
}

bool SimpleSyncProtocol::waitUntil(uint64_t scheduledTime, uint32_t maxWaitUs) const {
    uint64_t startTime = getMicros();
    uint64_t deadline = startTime + maxWaitUs;

    // Check if scheduled time is already in the past
    if (scheduledTime <= startTime) {
        Serial.printf("[SYNC] waitUntil: late by %lu us\n",
                      (unsigned long)(startTime - scheduledTime));
        return false;
    }

    // Check if wait would exceed maximum
    if (scheduledTime > deadline) {
        Serial.printf("[SYNC] waitUntil: would exceed max wait (%lu us > %lu us)\n",
                      (unsigned long)(scheduledTime - startTime), (unsigned long)maxWaitUs);
        return false;
    }

    // Spin-wait until scheduled time (tight loop for precision)
    while (getMicros() < scheduledTime) {
        // Tight loop - no yield for maximum precision
    }

    // Drift logging moved to latency metrics system
    // Only log errors, not every successful execution
    #if DEBUG_ENABLED
    uint64_t actualTime = getMicros();
    int32_t drift = (int32_t)(actualTime - scheduledTime);
    Serial.printf("[SYNC] waitUntil drift: %ld us\n", (long)drift);
    #endif

    return true;
}

// =============================================================================
// SIMPLE SYNC PROTOCOL - RTT PROBING
// =============================================================================

bool SimpleSyncProtocol::startRttProbing() {
    // Reset RTT probing state
    _rttSampleCount = 0;
    _currentProbeSeq = 0;
    _probeSentTime = 0;
    _probingInProgress = true;
    memset(_rttSamples, 0, sizeof(_rttSamples));

    Serial.println(F("[SYNC] RTT probing started"));
    return true;
}

bool SimpleSyncProtocol::handleProbeAck(uint8_t seq, uint64_t originalT1) {
    // Verify sequence number matches expected
    if (seq != _currentProbeSeq) {
        Serial.printf("[SYNC] Probe ACK seq mismatch: expected %u, got %u\n",
                      _currentProbeSeq, seq);
        return false;
    }

    // Calculate RTT
    uint64_t now = getMicros();
    uint32_t rtt = (uint32_t)(now - originalT1);

    // Store RTT sample
    if (_rttSampleCount < SYNC_PROBE_COUNT) {
        _rttSamples[_rttSampleCount++] = rtt;
    }

    return true;
}

bool SimpleSyncProtocol::isProbingComplete() const {
    return _rttSampleCount >= SYNC_PROBE_COUNT;
}

uint32_t SimpleSyncProtocol::getMinRtt() const {
    if (_rttSampleCount == 0) return 0;

    uint32_t minVal = _rttSamples[0];
    for (uint8_t i = 1; i < _rttSampleCount; i++) {
        if (_rttSamples[i] < minVal) {
            minVal = _rttSamples[i];
        }
    }
    return minVal;
}

uint32_t SimpleSyncProtocol::getMaxRtt() const {
    if (_rttSampleCount == 0) return 0;

    uint32_t maxVal = _rttSamples[0];
    for (uint8_t i = 1; i < _rttSampleCount; i++) {
        if (_rttSamples[i] > maxVal) {
            maxVal = _rttSamples[i];
        }
    }
    return maxVal;
}

uint32_t SimpleSyncProtocol::getRttSpread() const {
    if (_rttSampleCount == 0) return 0;
    return getMaxRtt() - getMinRtt();
}

void SimpleSyncProtocol::finalizeSync() {
    if (_rttSampleCount == 0) {
        Serial.println(F("[SYNC] WARNING: No RTT samples collected"));
        _probingInProgress = false;
        return;
    }

    // Use minimum RTT as best estimate of true latency
    // (Minimum RTT = least congested, closest to actual hardware latency)
    uint32_t minRtt = getMinRtt();
    _estimatedLatency = minRtt / 2;  // One-way = RTT / 2
    _probingInProgress = false;

    Serial.printf("[SYNC] RTT probing finalized: min=%lu us, one-way=%lu us, spread=%lu us\n",
                  (unsigned long)minRtt,
                  (unsigned long)_estimatedLatency,
                  (unsigned long)getRttSpread());
}
