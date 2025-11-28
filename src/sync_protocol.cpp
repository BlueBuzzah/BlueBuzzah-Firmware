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
    { SyncCommandType::EXECUTE_BUZZ,   "EXECUTE_BUZZ" },
    { SyncCommandType::DEACTIVATE,     "DEACTIVATE" },
    { SyncCommandType::HEARTBEAT,      "HEARTBEAT" },
    { SyncCommandType::SYNC_ADJ,       "SYNC_ADJ" },
    { SyncCommandType::SYNC_ADJ_START, "SYNC_ADJ_START" },
    { SyncCommandType::BUZZ_COMPLETE,  "BUZZ_COMPLETE" },
    { SyncCommandType::FIRST_SYNC,     "FIRST_SYNC" },
    { SyncCommandType::ACK_SYNC_ADJ,   "ACK_SYNC_ADJ" }
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
    int written = snprintf(buffer, bufferSize, "%s:%lu:%llu",
                           typeStr,
                           (unsigned long)_sequenceId,
                           (unsigned long long)_timestamp);

    if (written < 0 || (size_t)written >= bufferSize) {
        return false;
    }

    // Append data if present
    if (_dataCount > 0) {
        size_t remaining = bufferSize - written;
        serializeData(buffer + written, remaining);
    }

    return true;
}

void SyncCommand::serializeData(char* buffer, size_t bufferSize) const {
    if (_dataCount == 0 || bufferSize < 2) {
        return;
    }

    // Add delimiter before data
    size_t pos = 0;
    buffer[pos++] = SYNC_CMD_DELIMITER;

    // Add key-value pairs
    for (uint8_t i = 0; i < _dataCount && pos < bufferSize - 1; i++) {
        if (i > 0 && pos < bufferSize - 1) {
            buffer[pos++] = SYNC_DATA_DELIMITER;
        }

        // Add key
        const char* key = _data[i].key;
        while (*key && pos < bufferSize - 1) {
            buffer[pos++] = *key++;
        }

        // Add delimiter
        if (pos < bufferSize - 1) {
            buffer[pos++] = SYNC_DATA_DELIMITER;
        }

        // Add value
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

    // Make a copy for parsing
    char buffer[MESSAGE_BUFFER_SIZE];
    strncpy(buffer, message, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

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

    // Parse data payload if present (remaining content after third colon)
    token = strtok(nullptr, "");
    if (token && strlen(token) > 0) {
        parseData(token);
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

    // Parse pipe-delimited key|value pairs
    char* token = strtok(buffer, "|");
    char* key = nullptr;

    while (token && _dataCount < SYNC_MAX_DATA_PAIRS) {
        if (!key) {
            // This is a key
            key = token;
        } else {
            // This is a value - store the pair
            setData(key, token);
            key = nullptr;
        }
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

SyncCommand SyncCommand::createExecuteBuzz(uint32_t sequenceId, uint8_t finger, uint8_t amplitude) {
    SyncCommand cmd(SyncCommandType::EXECUTE_BUZZ, sequenceId);
    cmd.setData("finger", (int32_t)finger);
    cmd.setData("amplitude", (int32_t)amplitude);
    return cmd;
}

SyncCommand SyncCommand::createBuzzComplete(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::BUZZ_COMPLETE, sequenceId);
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
    _estimatedLatency(0)
{
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
