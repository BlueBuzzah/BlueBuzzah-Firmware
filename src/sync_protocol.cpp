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
    { SyncCommandType::BUZZ,           "BUZZ" },
    { SyncCommandType::DEACTIVATE,     "DEACTIVATE" },
    { SyncCommandType::HEARTBEAT,      "HEARTBEAT" },
    { SyncCommandType::PING,           "PING" },
    { SyncCommandType::PONG,           "PONG" },
    { SyncCommandType::DEBUG_FLASH,    "DEBUG_FLASH" }
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

    // Format: COMMAND_TYPE:sequence_id|timestamp[|data...]
    // All parameters after the command type are pipe-delimited
    // Note: %llu doesn't work on ARM Arduino, so we print timestamp as two 32-bit parts
    uint32_t tsHigh = (uint32_t)(_timestamp >> 32);
    uint32_t tsLow = (uint32_t)(_timestamp & 0xFFFFFFFF);
    int written;
    if (tsHigh > 0) {
        written = snprintf(buffer, bufferSize, "%s:%lu|%lu%09lu",
                           typeStr,
                           (unsigned long)_sequenceId,
                           (unsigned long)tsHigh,
                           (unsigned long)tsLow);
    } else {
        written = snprintf(buffer, bufferSize, "%s:%lu|%lu",
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

    return true;
}

void SyncCommand::serializeData(char* buffer, size_t bufferSize) const {
    if (_dataCount == 0 || bufferSize < 2) {
        return;
    }

    // Add pipe delimiter before data (consistent with other parameters)
    size_t pos = 0;
    buffer[pos++] = SYNC_DATA_DELIMITER;

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

    // Make a copy for parsing
    char buffer[MESSAGE_BUFFER_SIZE];
    strncpy(buffer, message, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // New format: COMMAND:seq|timestamp|param|param|...
    // Find the colon that separates command type from parameters
    char* colonPos = strchr(buffer, ':');
    if (!colonPos) {
        return false;
    }

    // Null-terminate command type and parse it
    *colonPos = '\0';
    if (!parseCommandType(buffer)) {
        return false;
    }

    // Everything after the colon is pipe-delimited: seq|timestamp|data...
    char* params = colonPos + 1;
    if (!params || *params == '\0') {
        return false;  // Need at least seq|timestamp
    }

    // Parse sequence ID (first pipe-delimited token)
    char* token = strtok(params, "|");
    if (!token) {
        return false;
    }
    _sequenceId = strtoul(token, nullptr, 10);

    // Parse timestamp (second pipe-delimited token)
    token = strtok(nullptr, "|");
    if (!token) {
        return false;
    }
    _timestamp = strtoull(token, nullptr, 10);

    // Parse remaining pipe-delimited data parameters
    uint8_t index = 0;
    char indexKey[4];
    token = strtok(nullptr, "|");
    while (token && _dataCount < SYNC_MAX_DATA_PAIRS) {
        snprintf(indexKey, sizeof(indexKey), "%d", index);
        setData(indexKey, token);
        index++;
        token = strtok(nullptr, "|");
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

SyncCommand SyncCommand::createBuzz(uint32_t sequenceId, uint8_t finger, uint8_t amplitude, uint32_t durationMs, uint16_t frequencyHz) {
    SyncCommand cmd(SyncCommandType::BUZZ, sequenceId);
    cmd.setData("0", (int32_t)finger);
    cmd.setData("1", (int32_t)amplitude);
    cmd.setData("2", (int32_t)durationMs);
    cmd.setData("3", (int32_t)frequencyHz);
    return cmd;
}

SyncCommand SyncCommand::createBuzzWithTime(uint32_t sequenceId, uint8_t finger, uint8_t amplitude, uint32_t durationMs, uint16_t frequencyHz, uint64_t activateTime) {
    SyncCommand cmd(SyncCommandType::BUZZ, sequenceId);
    cmd.setData("0", (int32_t)finger);
    cmd.setData("1", (int32_t)amplitude);
    cmd.setData("2", (int32_t)durationMs);
    cmd.setData("3", (int32_t)frequencyHz);

    // Store activation time (for <71 minute uptime, only low 32 bits needed)
    uint32_t timeHigh = (uint32_t)(activateTime >> 32);
    uint32_t timeLow = (uint32_t)(activateTime & 0xFFFFFFFF);

    if (timeHigh == 0) {
        // Simple format: finger|amp|dur|freq|activateTimeLow
        cmd.setData("4", (int32_t)timeLow);
    } else {
        // Full 64-bit: finger|amp|dur|freq|timeHigh|timeLow
        cmd.setData("4", (int32_t)timeHigh);
        cmd.setData("5", (int32_t)timeLow);
    }
    return cmd;
}

SyncCommand SyncCommand::createDeactivate(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::DEACTIVATE, sequenceId);
}

SyncCommand SyncCommand::createPing(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::PING, sequenceId);
}

SyncCommand SyncCommand::createPingWithT1(uint32_t sequenceId, uint64_t t1) {
    SyncCommand cmd(SyncCommandType::PING, sequenceId);
    cmd.setTimestamp(t1);  // Use timestamp field for T1
    return cmd;
}

SyncCommand SyncCommand::createPong(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::PONG, sequenceId);
}

SyncCommand SyncCommand::createPongWithTimestamps(uint32_t sequenceId, uint64_t t2, uint64_t t3) {
    SyncCommand cmd(SyncCommandType::PONG, sequenceId);
    // Store T2 and T3 as data payload (ARM can't handle %llu, so split into high/low)
    uint32_t t2High = (uint32_t)(t2 >> 32);
    uint32_t t2Low = (uint32_t)(t2 & 0xFFFFFFFF);
    uint32_t t3High = (uint32_t)(t3 >> 32);
    uint32_t t3Low = (uint32_t)(t3 & 0xFFFFFFFF);

    // For simplicity, if high bits are 0 (typical for <71 minute uptime), just use low bits
    // Format: T2Low|T3Low (or T2High:T2Low|T3High:T3Low if high bits needed)
    if (t2High == 0 && t3High == 0) {
        cmd.setData("0", (int32_t)t2Low);
        cmd.setData("1", (int32_t)t3Low);
    } else {
        // Full 64-bit encoding: T2High|T2Low|T3High|T3Low
        cmd.setData("0", (int32_t)t2High);
        cmd.setData("1", (int32_t)t2Low);
        cmd.setData("2", (int32_t)t3High);
        cmd.setData("3", (int32_t)t3Low);
    }
    return cmd;
}

SyncCommand SyncCommand::createDebugFlash(uint32_t sequenceId) {
    return SyncCommand(SyncCommandType::DEBUG_FLASH, sequenceId);
}

SyncCommand SyncCommand::createDebugFlashWithTime(uint32_t sequenceId, uint64_t flashTime) {
    SyncCommand cmd(SyncCommandType::DEBUG_FLASH, sequenceId);
    // Store activation time in data payload
    uint32_t timeHigh = (uint32_t)(flashTime >> 32);
    uint32_t timeLow = (uint32_t)(flashTime & 0xFFFFFFFF);

    if (timeHigh == 0) {
        cmd.setData("0", (int32_t)timeLow);
    } else {
        cmd.setData("0", (int32_t)timeHigh);
        cmd.setData("1", (int32_t)timeLow);
    }
    return cmd;
}

// =============================================================================
// SIMPLE SYNC PROTOCOL - IMPLEMENTATION
// =============================================================================

SimpleSyncProtocol::SimpleSyncProtocol() :
    _currentOffset(0),
    _lastSyncTime(0),
    _measuredLatencyUs(0),
    _smoothedLatencyUs(0),
    _rttVariance(0),
    _sampleCount(0),
    _offsetSampleIndex(0),
    _offsetSampleCount(0),
    _medianOffset(0),
    _clockSyncValid(false),
    _lastMeasuredOffset(0),
    _lastOffsetTime(0),
    _driftRateUsPerMs(0.0f)
{
    memset(_offsetSamples, 0, sizeof(_offsetSamples));
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
    _measuredLatencyUs = 0;
    _smoothedLatencyUs = 0;
    _sampleCount = 0;
    resetClockSync();
}

// =============================================================================
// PTP CLOCK SYNCHRONIZATION - IMPLEMENTATION
// =============================================================================

int64_t SimpleSyncProtocol::calculatePTPOffset(uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4) {
    // IEEE 1588 PTP clock offset formula:
    // offset = ((T2 - T1) + (T3 - T4)) / 2
    //
    // This formula is mathematically independent of network asymmetry.
    // Positive offset means SECONDARY clock is ahead of PRIMARY.

    int64_t term1 = (int64_t)t2 - (int64_t)t1;  // Forward delay + offset
    int64_t term2 = (int64_t)t3 - (int64_t)t4;  // Reverse delay - offset

    int64_t offset = (term1 + term2) / 2;

    _lastSyncTime = millis();
    return offset;
}

void SimpleSyncProtocol::addOffsetSample(int64_t offset) {
    // Add sample to circular buffer
    _offsetSamples[_offsetSampleIndex] = offset;
    _offsetSampleIndex = static_cast<uint8_t>((_offsetSampleIndex + 1) % OFFSET_SAMPLE_COUNT);

    if (_offsetSampleCount < OFFSET_SAMPLE_COUNT) {
        _offsetSampleCount++;
    }

    // Compute median when we have enough samples
    if (_offsetSampleCount >= SYNC_MIN_VALID_SAMPLES) {
        // Copy samples to temp array for sorting
        int64_t sorted[OFFSET_SAMPLE_COUNT];
        for (uint8_t i = 0; i < _offsetSampleCount; i++) {
            sorted[i] = _offsetSamples[i];
        }

        // Simple insertion sort (small array, O(n^2) is fine)
        for (uint8_t i = 1; i < _offsetSampleCount; i++) {
            int64_t key = sorted[i];
            int8_t j = static_cast<int8_t>(i - 1);
            while (j >= 0 && sorted[j] > key) {
                sorted[j + 1] = sorted[j];
                j--;
            }
            sorted[j + 1] = key;
        }

        // Get median
        if (_offsetSampleCount % 2 == 0) {
            // Even count: average of two middle values
            uint8_t mid = _offsetSampleCount / 2;
            _medianOffset = (sorted[mid - 1] + sorted[mid]) / 2;
        } else {
            // Odd count: middle value
            _medianOffset = sorted[_offsetSampleCount / 2];
        }

        _clockSyncValid = true;
    }
}

int64_t SimpleSyncProtocol::getMedianOffset() const {
    if (!_clockSyncValid) {
        return 0;
    }
    return _medianOffset;
}

void SimpleSyncProtocol::updateOffsetEMA(int64_t offset) {
    // Slow EMA update for continuous offset maintenance
    // α = 0.1 (SYNC_OFFSET_EMA_ALPHA_NUM / SYNC_OFFSET_EMA_ALPHA_DEN)
    if (!_clockSyncValid) {
        // Not yet synced - use sample collection instead
        addOffsetSample(offset);
        return;
    }

    uint32_t now = millis();

    // Update drift rate estimate if we have a previous measurement
    if (_lastOffsetTime > 0) {
        uint32_t elapsed = now - _lastOffsetTime;
        if (elapsed >= 100) {  // Avoid division issues with small intervals
            int64_t delta = offset - _lastMeasuredOffset;
            float newRate = (float)delta / (float)elapsed;  // μs per ms

            // EMA smooth the drift rate (α = 0.3 for reasonable responsiveness)
            _driftRateUsPerMs = 0.3f * newRate + 0.7f * _driftRateUsPerMs;
        }
    }

    // Store for next drift calculation
    _lastMeasuredOffset = offset;
    _lastOffsetTime = now;

    // EMA: new = α * measured + (1-α) * previous
    _medianOffset = (SYNC_OFFSET_EMA_ALPHA_NUM * offset +
                     (SYNC_OFFSET_EMA_ALPHA_DEN - SYNC_OFFSET_EMA_ALPHA_NUM) * _medianOffset)
                    / SYNC_OFFSET_EMA_ALPHA_DEN;

    _lastSyncTime = now;
}

int64_t SimpleSyncProtocol::getCorrectedOffset() const {
    if (!_clockSyncValid) {
        return 0;
    }

    // Apply drift compensation based on time since last measurement
    if (_lastOffsetTime > 0) {
        uint32_t elapsed = millis() - _lastOffsetTime;
        int64_t driftCorrection = (int64_t)(_driftRateUsPerMs * (float)elapsed);
        return _medianOffset + driftCorrection;
    }

    return _medianOffset;
}

void SimpleSyncProtocol::resetClockSync() {
    memset(_offsetSamples, 0, sizeof(_offsetSamples));
    _offsetSampleIndex = 0;
    _offsetSampleCount = 0;
    _medianOffset = 0;
    _clockSyncValid = false;
    _lastMeasuredOffset = 0;
    _lastOffsetTime = 0;
    _driftRateUsPerMs = 0.0f;
}

bool SimpleSyncProtocol::addOffsetSampleWithQuality(int64_t offset, uint32_t rttUs) {
    // Reject samples with excessive RTT - these likely have asymmetric delays
    // due to retransmissions, connection event misalignment, or radio interference
    if (rttUs > SYNC_RTT_QUALITY_THRESHOLD_US) {
        // Sample rejected - RTT too high for reliable offset measurement
        return false;
    }

    // Sample accepted - add to median calculation
    addOffsetSample(offset);
    return true;
}

uint32_t SimpleSyncProtocol::calculateAdaptiveLeadTime() const {
    // If not enough samples, use conservative default
    if (_sampleCount < MIN_SAMPLES) {
        return SYNC_LEAD_TIME_US;  // Default 50ms
    }

    // Calculate lead time based on RTT statistics:
    // Lead time = RTT + 3σ margin (ensures message arrives before target time)
    //
    // Note: _smoothedLatencyUs is one-way latency, so RTT = 2 * _smoothedLatencyUs
    // _rttVariance is already one-way variance, so RTT variance scale = 2 * _rttVariance
    uint32_t avgRTT = _smoothedLatencyUs * 2;  // RTT = 2 * one-way
    uint32_t margin = _rttVariance * 6;        // 3-sigma * 2 (one-way to RTT scale)

    // Lead time = RTT + margin (NOT 2x RTT - that was a bug!)
    uint32_t leadTime = avgRTT + margin;

    // Clamp to reasonable bounds:
    // - Minimum 15ms: ensures enough time for BLE connection event + transmission
    // - Maximum 50ms: MUST be less than TIME_ON (100ms) to prevent deactivation
    //   before activation. Using 50ms gives 50ms buffer for activation timing.
    if (leadTime < 15000) {
        leadTime = 15000;
    } else if (leadTime > 50000) {
        leadTime = 50000;
    }

    return leadTime;
}
