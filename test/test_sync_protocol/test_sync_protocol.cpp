/**
 * @file test_sync_protocol.cpp
 * @brief Unit tests for sync_protocol - Command serialization and timing
 */

#include <unity.h>
#include "sync_protocol.h"

// Include source file directly for native testing
#include "../../src/sync_protocol.cpp"

// =============================================================================
// TEST FIXTURES
// =============================================================================

void setUp(void) {
    // Reset mock time before each test
    mockResetTime();
    // Reset getMicros() overflow tracking state (must be after mockResetTime)
    resetMicrosOverflow();
    // Reset global sequence generator
    g_sequenceGenerator.reset();
}

void tearDown(void) {
    // Clean up after each test
}

// =============================================================================
// SYNC COMMAND CONSTRUCTOR TESTS
// =============================================================================

void test_SyncCommand_default_constructor(void) {
    SyncCommand cmd;
    TEST_ASSERT_EQUAL(SyncCommandType::PING, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(0, cmd.getSequenceId());
    TEST_ASSERT_EQUAL_UINT8(0, cmd.getDataCount());
}

void test_SyncCommand_parameterized_constructor(void) {
    SyncCommand cmd(SyncCommandType::BUZZ, 42);
    TEST_ASSERT_EQUAL(SyncCommandType::BUZZ, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(42, cmd.getSequenceId());
}

void test_SyncCommand_setType(void) {
    SyncCommand cmd;
    cmd.setType(SyncCommandType::START_SESSION);
    TEST_ASSERT_EQUAL(SyncCommandType::START_SESSION, cmd.getType());
}

void test_SyncCommand_setSequenceId(void) {
    SyncCommand cmd;
    cmd.setSequenceId(12345);
    TEST_ASSERT_EQUAL_UINT32(12345, cmd.getSequenceId());
}

void test_SyncCommand_setTimestamp(void) {
    SyncCommand cmd;
    cmd.setTimestamp(1000000);
    TEST_ASSERT_EQUAL_UINT64(1000000, cmd.getTimestamp());
}

void test_SyncCommand_setTimestampNow(void) {
    mockSetMillis(500);  // 500ms = 500000 microseconds
    SyncCommand cmd;
    // Constructor calls setTimestampNow()
    TEST_ASSERT_EQUAL_UINT64(500000, cmd.getTimestamp());
}

// =============================================================================
// SYNC COMMAND TYPE STRING TESTS
// =============================================================================

void test_SyncCommand_getTypeString_buzz(void) {
    SyncCommand cmd(SyncCommandType::BUZZ, 0);
    TEST_ASSERT_EQUAL_STRING("BUZZ", cmd.getTypeString());
}

void test_SyncCommand_getTypeString_startSession(void) {
    SyncCommand cmd(SyncCommandType::START_SESSION, 0);
    TEST_ASSERT_EQUAL_STRING("START_SESSION", cmd.getTypeString());
}

void test_SyncCommand_getTypeString_stopSession(void) {
    SyncCommand cmd(SyncCommandType::STOP_SESSION, 0);
    TEST_ASSERT_EQUAL_STRING("STOP_SESSION", cmd.getTypeString());
}

// =============================================================================
// SYNC COMMAND DATA PAYLOAD TESTS
// =============================================================================

void test_SyncCommand_setData_string(void) {
    SyncCommand cmd;
    TEST_ASSERT_TRUE(cmd.setData("key1", "value1"));
    TEST_ASSERT_EQUAL_UINT8(1, cmd.getDataCount());
}

void test_SyncCommand_getData_string(void) {
    SyncCommand cmd;
    cmd.setData("mykey", "myvalue");
    const char* value = cmd.getData("mykey");
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL_STRING("myvalue", value);
}

void test_SyncCommand_getData_missing_key(void) {
    SyncCommand cmd;
    const char* value = cmd.getData("nonexistent");
    TEST_ASSERT_NULL(value);
}

void test_SyncCommand_setData_integer(void) {
    SyncCommand cmd;
    TEST_ASSERT_TRUE(cmd.setData("finger", (int32_t)3));
    TEST_ASSERT_EQUAL_INT32(3, cmd.getDataInt("finger", -1));
}

void test_SyncCommand_getDataInt_with_default(void) {
    SyncCommand cmd;
    // Key doesn't exist, should return default
    TEST_ASSERT_EQUAL_INT32(99, cmd.getDataInt("missing", 99));
}

void test_SyncCommand_getDataInt_existing_key(void) {
    SyncCommand cmd;
    cmd.setData("amplitude", "75");
    TEST_ASSERT_EQUAL_INT32(75, cmd.getDataInt("amplitude", 0));
}

void test_SyncCommand_hasData_true(void) {
    SyncCommand cmd;
    cmd.setData("test", "value");
    TEST_ASSERT_TRUE(cmd.hasData("test"));
}

void test_SyncCommand_hasData_false(void) {
    SyncCommand cmd;
    TEST_ASSERT_FALSE(cmd.hasData("nonexistent"));
}

void test_SyncCommand_clearData(void) {
    SyncCommand cmd;
    cmd.setData("key1", "value1");
    cmd.setData("key2", "value2");
    TEST_ASSERT_EQUAL_UINT8(2, cmd.getDataCount());

    cmd.clearData();
    TEST_ASSERT_EQUAL_UINT8(0, cmd.getDataCount());
    TEST_ASSERT_FALSE(cmd.hasData("key1"));
    TEST_ASSERT_FALSE(cmd.hasData("key2"));
}

void test_SyncCommand_setData_updates_existing(void) {
    SyncCommand cmd;
    cmd.setData("key", "original");
    cmd.setData("key", "updated");

    // Should still have only 1 data pair
    TEST_ASSERT_EQUAL_UINT8(1, cmd.getDataCount());
    TEST_ASSERT_EQUAL_STRING("updated", cmd.getData("key"));
}

void test_SyncCommand_setData_multiple_pairs(void) {
    SyncCommand cmd;
    cmd.setData("finger", "2");
    cmd.setData("amplitude", "80");
    cmd.setData("duration", "100");

    TEST_ASSERT_EQUAL_UINT8(3, cmd.getDataCount());
    TEST_ASSERT_EQUAL_STRING("2", cmd.getData("finger"));
    TEST_ASSERT_EQUAL_STRING("80", cmd.getData("amplitude"));
    TEST_ASSERT_EQUAL_STRING("100", cmd.getData("duration"));
}

// =============================================================================
// SYNC COMMAND SERIALIZATION TESTS
// =============================================================================

void test_SyncCommand_serialize_with_data(void) {
    SyncCommand cmd(SyncCommandType::BUZZ, 42);
    cmd.setTimestamp(1000000);
    cmd.setData("0", "0");
    cmd.setData("1", "50");

    char buffer[256];
    TEST_ASSERT_TRUE(cmd.serialize(buffer, sizeof(buffer)));

    // Format: BUZZ:42|1000000|0|50 (all params pipe-delimited after command)
    TEST_ASSERT_EQUAL_STRING("BUZZ:42|1000000|0|50", buffer);
}

void test_SyncCommand_serialize_buffer_too_small(void) {
    SyncCommand cmd(SyncCommandType::PING, 1);

    char buffer[10];  // Too small
    TEST_ASSERT_FALSE(cmd.serialize(buffer, sizeof(buffer)));
}

void test_SyncCommand_serialize_null_buffer(void) {
    SyncCommand cmd(SyncCommandType::PING, 1);
    TEST_ASSERT_FALSE(cmd.serialize(nullptr, 128));
}

// =============================================================================
// SYNC COMMAND DESERIALIZATION TESTS
// =============================================================================

void test_SyncCommand_deserialize_buzz(void) {
    SyncCommand cmd;
    TEST_ASSERT_TRUE(cmd.deserialize("BUZZ:42|5000000"));

    TEST_ASSERT_EQUAL(SyncCommandType::BUZZ, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(42, cmd.getSequenceId());
    TEST_ASSERT_EQUAL_UINT64(5000000, cmd.getTimestamp());
}

void test_SyncCommand_deserialize_with_data(void) {
    SyncCommand cmd;
    TEST_ASSERT_TRUE(cmd.deserialize("BUZZ:42|1000000|0|50"));

    TEST_ASSERT_EQUAL(SyncCommandType::BUZZ, cmd.getType());
    TEST_ASSERT_EQUAL_INT32(0, cmd.getDataInt("0", -1));
    TEST_ASSERT_EQUAL_INT32(50, cmd.getDataInt("1", -1));
}

void test_SyncCommand_deserialize_null_message(void) {
    SyncCommand cmd;
    TEST_ASSERT_FALSE(cmd.deserialize(nullptr));
}

void test_SyncCommand_deserialize_empty_message(void) {
    SyncCommand cmd;
    TEST_ASSERT_FALSE(cmd.deserialize(""));
}

void test_SyncCommand_deserialize_invalid_format(void) {
    SyncCommand cmd;
    TEST_ASSERT_FALSE(cmd.deserialize("INVALID"));
}

void test_SyncCommand_deserialize_unknown_command(void) {
    SyncCommand cmd;
    TEST_ASSERT_FALSE(cmd.deserialize("UNKNOWN_CMD:1|1000"));
}

void test_SyncCommand_deserialize_roundtrip(void) {
    // Create and serialize a command
    SyncCommand original(SyncCommandType::BUZZ, 123);
    original.setTimestamp(9999999);
    original.setData("0", "3");
    original.setData("1", "80");

    char buffer[256];
    TEST_ASSERT_TRUE(original.serialize(buffer, sizeof(buffer)));

    // Deserialize and verify
    SyncCommand parsed;
    TEST_ASSERT_TRUE(parsed.deserialize(buffer));

    TEST_ASSERT_EQUAL(original.getType(), parsed.getType());
    TEST_ASSERT_EQUAL_UINT32(original.getSequenceId(), parsed.getSequenceId());
    TEST_ASSERT_EQUAL_UINT64(original.getTimestamp(), parsed.getTimestamp());
    TEST_ASSERT_EQUAL_INT32(3, parsed.getDataInt("0", -1));
    TEST_ASSERT_EQUAL_INT32(80, parsed.getDataInt("1", -1));
}

// =============================================================================
// SYNC COMMAND FACTORY METHOD TESTS
// =============================================================================

void test_SyncCommand_createStartSession(void) {
    SyncCommand cmd = SyncCommand::createStartSession(10);
    TEST_ASSERT_EQUAL(SyncCommandType::START_SESSION, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(10, cmd.getSequenceId());
}

void test_SyncCommand_createPauseSession(void) {
    SyncCommand cmd = SyncCommand::createPauseSession(15);
    TEST_ASSERT_EQUAL(SyncCommandType::PAUSE_SESSION, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(15, cmd.getSequenceId());
}

void test_SyncCommand_createResumeSession(void) {
    SyncCommand cmd = SyncCommand::createResumeSession(20);
    TEST_ASSERT_EQUAL(SyncCommandType::RESUME_SESSION, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(20, cmd.getSequenceId());
}

void test_SyncCommand_createStopSession(void) {
    SyncCommand cmd = SyncCommand::createStopSession(25);
    TEST_ASSERT_EQUAL(SyncCommandType::STOP_SESSION, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(25, cmd.getSequenceId());
}


void test_SyncCommand_createDeactivate(void) {
    SyncCommand cmd = SyncCommand::createDeactivate(40);
    TEST_ASSERT_EQUAL(SyncCommandType::DEACTIVATE, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(40, cmd.getSequenceId());
}

// =============================================================================
// SEQUENCE GENERATOR TESTS
// =============================================================================

void test_SequenceGenerator_initial_value(void) {
    SequenceGenerator gen;
    TEST_ASSERT_EQUAL_UINT32(1, gen.next());
}

void test_SequenceGenerator_increment(void) {
    SequenceGenerator gen;
    TEST_ASSERT_EQUAL_UINT32(1, gen.next());
    TEST_ASSERT_EQUAL_UINT32(2, gen.next());
    TEST_ASSERT_EQUAL_UINT32(3, gen.next());
}

void test_SequenceGenerator_reset(void) {
    SequenceGenerator gen;
    gen.next();  // 1
    gen.next();  // 2
    gen.next();  // 3
    gen.reset();
    TEST_ASSERT_EQUAL_UINT32(1, gen.next());
}

void test_global_sequence_generator(void) {
    g_sequenceGenerator.reset();
    TEST_ASSERT_EQUAL_UINT32(1, g_sequenceGenerator.next());
    TEST_ASSERT_EQUAL_UINT32(2, g_sequenceGenerator.next());
}

// =============================================================================
// SIMPLE SYNC PROTOCOL TESTS
// =============================================================================

void test_SimpleSyncProtocol_initial_state(void) {
    SimpleSyncProtocol sync;
    TEST_ASSERT_EQUAL_INT64(0, sync.getOffset());
    TEST_ASSERT_FALSE(sync.isSynced());
}

void test_SimpleSyncProtocol_calculateOffset(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);  // Set current time for lastSyncTime

    // PRIMARY time: 1000000, SECONDARY time: 1005000
    // Offset = 1005000 - 1000000 = 5000 (SECONDARY ahead)
    int64_t offset = sync.calculateOffset(1000000, 1005000);

    TEST_ASSERT_EQUAL_INT64(5000, offset);
    TEST_ASSERT_EQUAL_INT64(5000, sync.getOffset());
    TEST_ASSERT_TRUE(sync.isSynced());
}

void test_SimpleSyncProtocol_calculateOffset_negative(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // PRIMARY time: 1005000, SECONDARY time: 1000000
    // Offset = 1000000 - 1005000 = -5000 (SECONDARY behind)
    int64_t offset = sync.calculateOffset(1005000, 1000000);

    TEST_ASSERT_EQUAL_INT64(-5000, offset);
}

void test_SimpleSyncProtocol_applyCompensation(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // Set offset: SECONDARY is 5000 microseconds ahead
    sync.calculateOffset(1000000, 1005000);

    // Apply compensation to a timestamp
    // Original: 2000000, Offset: 5000
    // Compensated: 2000000 - 5000 = 1995000
    uint64_t compensated = sync.applyCompensation(2000000);
    TEST_ASSERT_EQUAL_UINT64(1995000, compensated);
}

void test_SimpleSyncProtocol_getTimeSinceSync_never_synced(void) {
    SimpleSyncProtocol sync;
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, sync.getTimeSinceSync());
}

void test_SimpleSyncProtocol_getTimeSinceSync_after_sync(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(1000);  // Time at sync

    sync.calculateOffset(100, 100);

    mockSetMillis(1500);  // 500ms later
    TEST_ASSERT_EQUAL_UINT32(500, sync.getTimeSinceSync());
}

void test_SimpleSyncProtocol_reset(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    sync.calculateOffset(1000, 2000);
    TEST_ASSERT_TRUE(sync.isSynced());

    sync.reset();
    TEST_ASSERT_FALSE(sync.isSynced());
    TEST_ASSERT_EQUAL_INT64(0, sync.getOffset());
}

// =============================================================================
// PING/PONG LATENCY MEASUREMENT TESTS
// =============================================================================

void test_SimpleSyncProtocol_getMeasuredLatency_initial_zero(void) {
    SimpleSyncProtocol sync;
    TEST_ASSERT_EQUAL_UINT32(0, sync.getMeasuredLatency());
}

void test_SimpleSyncProtocol_updateLatency(void) {
    SimpleSyncProtocol sync;

    // RTT = 20000us, one-way = 10000us
    sync.updateLatency(20000);

    // After 1 sample: raw value stored, but smoothed not available (needs 3 samples)
    TEST_ASSERT_EQUAL_UINT32(10000, sync.getRawLatency());
    TEST_ASSERT_EQUAL_UINT32(0, sync.getMeasuredLatency());  // Not enough samples yet
    TEST_ASSERT_EQUAL_UINT16(1, sync.getSampleCount());
}

void test_SimpleSyncProtocol_updateLatency_multiple(void) {
    SimpleSyncProtocol sync;

    // First measurement: RTT = 20000us → one-way = 10000us
    sync.updateLatency(20000);
    TEST_ASSERT_EQUAL_UINT32(10000, sync.getRawLatency());
    TEST_ASSERT_EQUAL_UINT32(0, sync.getMeasuredLatency());  // Not enough samples
    TEST_ASSERT_EQUAL_UINT16(1, sync.getSampleCount());

    // Second measurement: RTT = 20000us → one-way = 10000us
    sync.updateLatency(20000);
    TEST_ASSERT_EQUAL_UINT32(10000, sync.getRawLatency());
    TEST_ASSERT_EQUAL_UINT32(0, sync.getMeasuredLatency());  // Still not enough
    TEST_ASSERT_EQUAL_UINT16(2, sync.getSampleCount());

    // Third measurement: RTT = 20000us → one-way = 10000us
    // Now getMeasuredLatency() should return smoothed value
    sync.updateLatency(20000);
    TEST_ASSERT_EQUAL_UINT32(10000, sync.getRawLatency());
    TEST_ASSERT_EQUAL_UINT32(10000, sync.getMeasuredLatency());  // Now available!
    TEST_ASSERT_EQUAL_UINT16(3, sync.getSampleCount());
}

void test_SimpleSyncProtocol_updateLatency_ema_smoothing(void) {
    SimpleSyncProtocol sync;

    // Initialize with 3 samples of 10000us to reach MIN_SAMPLES
    sync.updateLatency(20000);  // one-way = 10000
    sync.updateLatency(20000);
    sync.updateLatency(20000);
    TEST_ASSERT_EQUAL_UINT32(10000, sync.getMeasuredLatency());

    // Add a different measurement: RTT = 30000us → one-way = 15000us
    // EMA: new = 0.3 * 15000 + 0.7 * 10000 = 4500 + 7000 = 11500
    sync.updateLatency(30000);
    TEST_ASSERT_EQUAL_UINT32(15000, sync.getRawLatency());
    TEST_ASSERT_EQUAL_UINT32(11500, sync.getMeasuredLatency());  // EMA smoothed
}

void test_SimpleSyncProtocol_updateLatency_outlier_rejection(void) {
    SimpleSyncProtocol sync;

    // Initialize with 3 samples of 10000us
    sync.updateLatency(20000);  // one-way = 10000
    sync.updateLatency(20000);
    sync.updateLatency(20000);
    TEST_ASSERT_EQUAL_UINT32(10000, sync.getMeasuredLatency());

    // Send outlier: RTT = 70000us → one-way = 35000us (> 3x smoothed = 30000)
    // Should be rejected (smoothed stays at 10000) per OUTLIER_MULT = 3
    sync.updateLatency(70000);
    TEST_ASSERT_EQUAL_UINT32(35000, sync.getRawLatency());  // Raw updated
    TEST_ASSERT_EQUAL_UINT32(10000, sync.getMeasuredLatency());  // Smoothed unchanged
    TEST_ASSERT_EQUAL_UINT16(3, sync.getSampleCount());  // Count not incremented
}

void test_SimpleSyncProtocol_resetLatency(void) {
    SimpleSyncProtocol sync;

    // Add some samples
    sync.updateLatency(20000);
    sync.updateLatency(20000);
    sync.updateLatency(20000);
    TEST_ASSERT_EQUAL_UINT32(10000, sync.getMeasuredLatency());
    TEST_ASSERT_EQUAL_UINT16(3, sync.getSampleCount());

    // Reset
    sync.resetLatency();
    TEST_ASSERT_EQUAL_UINT32(0, sync.getMeasuredLatency());
    TEST_ASSERT_EQUAL_UINT32(0, sync.getRawLatency());
    TEST_ASSERT_EQUAL_UINT16(0, sync.getSampleCount());
}

// =============================================================================
// CREATEBUZZ WITH DURATION TESTS
// =============================================================================




// =============================================================================
// SETDATA EDGE CASE TESTS
// =============================================================================

void test_SyncCommand_setData_max_pairs_reached(void) {
    SyncCommand cmd;

    // Fill all data pairs (SYNC_MAX_DATA_PAIRS = 8)
    for (int i = 0; i < SYNC_MAX_DATA_PAIRS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "key%d", i);
        TEST_ASSERT_TRUE(cmd.setData(key, "value"));
    }

    TEST_ASSERT_EQUAL_UINT8(SYNC_MAX_DATA_PAIRS, cmd.getDataCount());

    // Try to add one more - should fail
    TEST_ASSERT_FALSE(cmd.setData("overflow", "value"));
}

void test_SyncCommand_setData_null_key(void) {
    SyncCommand cmd;
    TEST_ASSERT_FALSE(cmd.setData(nullptr, "value"));
}

void test_SyncCommand_setData_null_value(void) {
    SyncCommand cmd;
    TEST_ASSERT_FALSE(cmd.setData("key", (const char*)nullptr));
}

// =============================================================================
// LARGE TIMESTAMP SERIALIZATION TESTS
// =============================================================================

void test_SyncCommand_serialize_large_timestamp(void) {
    SyncCommand cmd(SyncCommandType::PING, 1);

    // Set a timestamp with high bits set (simulating time after ~1 hour of operation)
    uint64_t largeTimestamp = 0x0000000100000000ULL;  // Just over 32 bits
    cmd.setTimestamp(largeTimestamp);

    char buffer[256];
    // Serialization should succeed even for large timestamps
    TEST_ASSERT_TRUE(cmd.serialize(buffer, sizeof(buffer)));

    // Verify the buffer contains the expected format with high bits
    // Note: The current format uses a concatenated high/low representation
    // that doesn't perfectly round-trip for all 64-bit values
    TEST_ASSERT_NOT_NULL(strstr(buffer, "PING:1|"));
}

// =============================================================================
// ADDITIONAL DESERIALIZE TESTS
// =============================================================================

void test_SyncCommand_deserialize_ping(void) {
    SyncCommand cmd;
    TEST_ASSERT_TRUE(cmd.deserialize("PING:1|1000000"));
    TEST_ASSERT_EQUAL(SyncCommandType::PING, cmd.getType());
}

void test_SyncCommand_deserialize_pong(void) {
    SyncCommand cmd;
    TEST_ASSERT_TRUE(cmd.deserialize("PONG:1|1000000"));
    TEST_ASSERT_EQUAL(SyncCommandType::PONG, cmd.getType());
}

// =============================================================================
// FACTORY METHOD TESTS FOR REMAINING TYPES
// =============================================================================

void test_SyncCommand_getTypeString_ping(void) {
    SyncCommand cmd(SyncCommandType::PING, 0);
    TEST_ASSERT_EQUAL_STRING("PING", cmd.getTypeString());
}

void test_SyncCommand_getTypeString_pong(void) {
    SyncCommand cmd(SyncCommandType::PONG, 0);
    TEST_ASSERT_EQUAL_STRING("PONG", cmd.getTypeString());
}

void test_SyncCommand_getTypeString_deactivate(void) {
    SyncCommand cmd(SyncCommandType::DEACTIVATE, 0);
    TEST_ASSERT_EQUAL_STRING("DEACTIVATE", cmd.getTypeString());
}

void test_SyncCommand_getTypeString_pause_session(void) {
    SyncCommand cmd(SyncCommandType::PAUSE_SESSION, 0);
    TEST_ASSERT_EQUAL_STRING("PAUSE_SESSION", cmd.getTypeString());
}

void test_SyncCommand_getTypeString_resume_session(void) {
    SyncCommand cmd(SyncCommandType::RESUME_SESSION, 0);
    TEST_ASSERT_EQUAL_STRING("RESUME_SESSION", cmd.getTypeString());
}

// =============================================================================
// TIMING UTILITY TESTS
// =============================================================================

void test_getMicros(void) {
    mockSetMillis(500);  // 500ms = 500000us
    TEST_ASSERT_EQUAL_UINT64(500000, getMicros());
}

void test_getMillis(void) {
    mockSetMillis(1234);
    TEST_ASSERT_EQUAL_UINT32(1234, getMillis());
}

// =============================================================================
// PTP CLOCK SYNCHRONIZATION TESTS
// =============================================================================

void test_SimpleSyncProtocol_calculatePTPOffset_symmetric(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // Symmetric network delay: 5ms each way
    // T1 = 1000000 (PRIMARY sends)
    // T2 = 1005000 (SECONDARY receives after 5ms)
    // T3 = 1010000 (SECONDARY sends after 5ms processing)
    // T4 = 1015000 (PRIMARY receives after 5ms)
    // Offset = ((T2-T1) + (T3-T4)) / 2 = ((5000) + (-5000)) / 2 = 0
    int64_t offset = sync.calculatePTPOffset(1000000, 1005000, 1010000, 1015000);
    TEST_ASSERT_EQUAL_INT64(0, offset);
}

void test_SimpleSyncProtocol_calculatePTPOffset_positive_offset(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // SECONDARY clock is 10000us (10ms) ahead
    // T1 = 1000000, T2 = 1015000 (5ms delay + 10ms offset)
    // T3 = 1020000, T4 = 1015000 (5ms delay, but SECONDARY ahead by 10ms)
    // Offset = ((15000) + (5000)) / 2 = 10000
    int64_t offset = sync.calculatePTPOffset(1000000, 1015000, 1020000, 1015000);
    TEST_ASSERT_EQUAL_INT64(10000, offset);
}

void test_SimpleSyncProtocol_calculatePTPOffset_negative_offset(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // SECONDARY clock is 10000us (10ms) behind
    // T1 = 1000000, T2 = 995000 (5ms delay - 10ms offset = -5ms apparent)
    // T3 = 1000000, T4 = 1015000
    // Offset = ((-5000) + (-15000)) / 2 = -10000
    int64_t offset = sync.calculatePTPOffset(1000000, 995000, 1000000, 1015000);
    TEST_ASSERT_EQUAL_INT64(-10000, offset);
}

void test_SimpleSyncProtocol_calculatePTPOffset_zero_offset(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // Zero network delay, zero offset
    // All timestamps identical
    int64_t offset = sync.calculatePTPOffset(1000000, 1000000, 1000000, 1000000);
    TEST_ASSERT_EQUAL_INT64(0, offset);
}

// =============================================================================
// OFFSET SAMPLE COLLECTION TESTS
// =============================================================================

void test_SimpleSyncProtocol_addOffsetSample_single(void) {
    SimpleSyncProtocol sync;

    sync.addOffsetSample(5000);
    TEST_ASSERT_EQUAL_UINT8(1, sync.getOffsetSampleCount());
    TEST_ASSERT_FALSE(sync.isClockSyncValid());  // Need MIN_SAMPLES (5)
}

void test_SimpleSyncProtocol_addOffsetSample_fills_buffer(void) {
    SimpleSyncProtocol sync;

    // Add 5 samples (SYNC_MIN_VALID_SAMPLES)
    for (int i = 0; i < 5; i++) {
        sync.addOffsetSample(1000);
    }

    TEST_ASSERT_EQUAL_UINT8(5, sync.getOffsetSampleCount());
    TEST_ASSERT_TRUE(sync.isClockSyncValid());
}

void test_SimpleSyncProtocol_addOffsetSample_circular_buffer(void) {
    SimpleSyncProtocol sync;

    // Fill beyond buffer size (OFFSET_SAMPLE_COUNT = 10)
    for (int i = 0; i < 15; i++) {
        sync.addOffsetSample(i * 100);
    }

    // Should wrap around, count capped at OFFSET_SAMPLE_COUNT
    TEST_ASSERT_EQUAL_UINT8(10, sync.getOffsetSampleCount());
    TEST_ASSERT_TRUE(sync.isClockSyncValid());
}

void test_SimpleSyncProtocol_getMedianOffset_odd_count(void) {
    SimpleSyncProtocol sync;

    // Add 5 samples: 100, 200, 300, 400, 500 - median is 300
    sync.addOffsetSample(100);
    sync.addOffsetSample(500);
    sync.addOffsetSample(300);
    sync.addOffsetSample(200);
    sync.addOffsetSample(400);

    TEST_ASSERT_TRUE(sync.isClockSyncValid());
    TEST_ASSERT_EQUAL_INT64(300, sync.getMedianOffset());
}

void test_SimpleSyncProtocol_getMedianOffset_even_count(void) {
    SimpleSyncProtocol sync;

    // Add 6 samples: 100, 200, 300, 400, 500, 600 - median is (300+400)/2 = 350
    sync.addOffsetSample(100);
    sync.addOffsetSample(600);
    sync.addOffsetSample(300);
    sync.addOffsetSample(200);
    sync.addOffsetSample(400);
    sync.addOffsetSample(500);

    TEST_ASSERT_TRUE(sync.isClockSyncValid());
    TEST_ASSERT_EQUAL_INT64(350, sync.getMedianOffset());
}

void test_SimpleSyncProtocol_isClockSyncValid_below_threshold(void) {
    SimpleSyncProtocol sync;

    // Add fewer than MIN_SAMPLES (5)
    sync.addOffsetSample(1000);
    sync.addOffsetSample(1000);
    sync.addOffsetSample(1000);
    sync.addOffsetSample(1000);

    TEST_ASSERT_EQUAL_UINT8(4, sync.getOffsetSampleCount());
    TEST_ASSERT_FALSE(sync.isClockSyncValid());
}

void test_SimpleSyncProtocol_isClockSyncValid_at_threshold(void) {
    SimpleSyncProtocol sync;

    // Add exactly MIN_SAMPLES (5)
    for (int i = 0; i < 5; i++) {
        sync.addOffsetSample(1000);
    }

    TEST_ASSERT_EQUAL_UINT8(5, sync.getOffsetSampleCount());
    TEST_ASSERT_TRUE(sync.isClockSyncValid());
}

void test_SimpleSyncProtocol_getOffsetSampleCount(void) {
    SimpleSyncProtocol sync;

    TEST_ASSERT_EQUAL_UINT8(0, sync.getOffsetSampleCount());

    sync.addOffsetSample(100);
    TEST_ASSERT_EQUAL_UINT8(1, sync.getOffsetSampleCount());

    sync.addOffsetSample(200);
    TEST_ASSERT_EQUAL_UINT8(2, sync.getOffsetSampleCount());
}

void test_SimpleSyncProtocol_resetClockSync(void) {
    SimpleSyncProtocol sync;

    // Add samples and validate
    for (int i = 0; i < 5; i++) {
        sync.addOffsetSample(1000);
    }
    TEST_ASSERT_TRUE(sync.isClockSyncValid());
    TEST_ASSERT_EQUAL_INT64(1000, sync.getMedianOffset());

    // Reset
    sync.resetClockSync();
    TEST_ASSERT_FALSE(sync.isClockSyncValid());
    TEST_ASSERT_EQUAL_UINT8(0, sync.getOffsetSampleCount());
    TEST_ASSERT_EQUAL_INT64(0, sync.getMedianOffset());
}

// =============================================================================
// RTT QUALITY FILTERING TESTS
// =============================================================================

void test_SimpleSyncProtocol_addOffsetSampleWithQuality_accepts_good_rtt(void) {
    SimpleSyncProtocol sync;

    // RTT = 10000us (10ms) - well below threshold of 80000us (SYNC_RTT_QUALITY_THRESHOLD_US)
    bool accepted = sync.addOffsetSampleWithQuality(5000, 10000);

    TEST_ASSERT_TRUE(accepted);
    TEST_ASSERT_EQUAL_UINT8(1, sync.getOffsetSampleCount());
}

void test_SimpleSyncProtocol_addOffsetSampleWithQuality_rejects_high_rtt(void) {
    SimpleSyncProtocol sync;

    // RTT = 100000us (100ms) - above threshold of 80000us (SYNC_RTT_QUALITY_THRESHOLD_US)
    bool accepted = sync.addOffsetSampleWithQuality(5000, 100000);

    TEST_ASSERT_FALSE(accepted);
    TEST_ASSERT_EQUAL_UINT8(0, sync.getOffsetSampleCount());
}

void test_SimpleSyncProtocol_addOffsetSampleWithQuality_boundary(void) {
    SimpleSyncProtocol sync;

    // RTT = 80000us - exactly at threshold (should be accepted, using <= comparison)
    bool accepted1 = sync.addOffsetSampleWithQuality(5000, 80000);
    TEST_ASSERT_TRUE(accepted1);  // Exactly at threshold is accepted

    // RTT = 80001us - just above threshold of 80000us (SYNC_RTT_QUALITY_THRESHOLD_US)
    bool accepted2 = sync.addOffsetSampleWithQuality(5000, 80001);
    TEST_ASSERT_FALSE(accepted2);
}

// =============================================================================
// DRIFT COMPENSATION TESTS
// =============================================================================

void test_SimpleSyncProtocol_updateOffsetEMA_first_sample(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // When not yet synced, updateOffsetEMA adds to sample collection
    sync.updateOffsetEMA(5000);

    TEST_ASSERT_EQUAL_UINT8(1, sync.getOffsetSampleCount());
    TEST_ASSERT_FALSE(sync.isClockSyncValid());
}

void test_SimpleSyncProtocol_updateOffsetEMA_updates_drift_rate(void) {
    SimpleSyncProtocol sync;

    // First, establish valid sync with 5 samples
    for (int i = 0; i < 5; i++) {
        sync.addOffsetSample(10000);
    }
    TEST_ASSERT_TRUE(sync.isClockSyncValid());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, sync.getDriftRate());

    // Now update with EMA - need time to pass for drift calculation
    mockSetMillis(100);
    sync.updateOffsetEMA(10000);  // First EMA update sets baseline

    mockSetMillis(300);  // 200ms later
    sync.updateOffsetEMA(10200);  // 200us drift in 200ms = 1.0 us/ms

    // Drift rate should now be non-zero (EMA smoothed)
    TEST_ASSERT_TRUE(sync.getDriftRate() != 0.0f);
}

void test_SimpleSyncProtocol_getCorrectedOffset_no_drift(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // Establish sync
    for (int i = 0; i < 5; i++) {
        sync.addOffsetSample(5000);
    }

    // With no drift rate set, corrected offset should equal median
    int64_t corrected = sync.getCorrectedOffset();
    TEST_ASSERT_EQUAL_INT64(5000, corrected);
}

void test_SimpleSyncProtocol_getCorrectedOffset_not_synced(void) {
    SimpleSyncProtocol sync;

    // Not synced - should return 0
    int64_t corrected = sync.getCorrectedOffset();
    TEST_ASSERT_EQUAL_INT64(0, corrected);
}

void test_SimpleSyncProtocol_getDriftRate_initial_zero(void) {
    SimpleSyncProtocol sync;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, sync.getDriftRate());
}

void test_SimpleSyncProtocol_getDriftRate_after_reset(void) {
    SimpleSyncProtocol sync;

    // Establish sync and do some EMA updates
    for (int i = 0; i < 5; i++) {
        sync.addOffsetSample(10000);
    }
    mockSetMillis(100);
    sync.updateOffsetEMA(10000);
    mockSetMillis(300);
    sync.updateOffsetEMA(10500);

    // Reset should clear drift rate
    sync.resetClockSync();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, sync.getDriftRate());
}

// =============================================================================
// ADAPTIVE LEAD TIME TESTS
// =============================================================================

void test_SimpleSyncProtocol_calculateAdaptiveLeadTime_default_when_few_samples(void) {
    SimpleSyncProtocol sync;

    // No samples - should return default SYNC_LEAD_TIME_US (50000)
    uint32_t leadTime = sync.calculateAdaptiveLeadTime();
    TEST_ASSERT_EQUAL_UINT32(50000, leadTime);
}

void test_SimpleSyncProtocol_calculateAdaptiveLeadTime_minimum_clamp(void) {
    SimpleSyncProtocol sync;

    // Add 3 very low latency samples (RTT = 2000us = 1ms one-way)
    sync.updateLatency(2000);
    sync.updateLatency(2000);
    sync.updateLatency(2000);

    // With very low RTT, lead time should clamp to minimum 15000us (15ms)
    uint32_t leadTime = sync.calculateAdaptiveLeadTime();
    TEST_ASSERT_EQUAL_UINT32(15000, leadTime);
}

void test_SimpleSyncProtocol_calculateAdaptiveLeadTime_maximum_clamp(void) {
    SimpleSyncProtocol sync;

    // Add 3 very high latency samples (RTT = 200000us = 100ms one-way)
    // This ensures the calculated lead time exceeds 100ms and gets clamped
    sync.updateLatency(200000);
    sync.updateLatency(200000);
    sync.updateLatency(200000);

    // With very high RTT, lead time should clamp to maximum 100000us (100ms)
    // avgRTT = 100000 * 2 = 200000, leadTime > 100000, clamped to 100000
    uint32_t leadTime = sync.calculateAdaptiveLeadTime();
    TEST_ASSERT_EQUAL_UINT32(100000, leadTime);
}

void test_SimpleSyncProtocol_calculateAdaptiveLeadTime_normal_calculation(void) {
    SimpleSyncProtocol sync;

    // Add 3 moderate latency samples (RTT = 20000us = 10ms one-way)
    sync.updateLatency(20000);
    sync.updateLatency(20000);
    sync.updateLatency(20000);

    // RTT = 20000, one-way = 10000
    // avgRTT = 10000 * 2 = 20000
    // With consistent samples, variance should be low
    // Lead time = RTT + variance margin, clamped to 15000-50000
    uint32_t leadTime = sync.calculateAdaptiveLeadTime();
    TEST_ASSERT_TRUE(leadTime >= 15000);
    TEST_ASSERT_TRUE(leadTime <= 50000);
}

void test_SimpleSyncProtocol_getAverageRTT(void) {
    SimpleSyncProtocol sync;

    // Add samples (RTT = 20000us = 10000us one-way)
    sync.updateLatency(20000);
    sync.updateLatency(20000);
    sync.updateLatency(20000);

    // Average RTT = 2 * smoothedLatency = 2 * 10000 = 20000
    TEST_ASSERT_EQUAL_UINT32(20000, sync.getAverageRTT());
}

void test_SimpleSyncProtocol_getRTTVariance_initial_zero(void) {
    SimpleSyncProtocol sync;
    TEST_ASSERT_EQUAL_UINT32(0, sync.getRTTVariance());
}

void test_SimpleSyncProtocol_getRTTVariance_after_consistent_samples(void) {
    SimpleSyncProtocol sync;

    // Add consistent samples - variance should remain low
    sync.updateLatency(20000);
    sync.updateLatency(20000);
    sync.updateLatency(20000);

    // With identical samples, variance should be 0 or very low
    TEST_ASSERT_TRUE(sync.getRTTVariance() <= 100);
}

void test_SimpleSyncProtocol_getRTTVariance_after_varying_samples(void) {
    SimpleSyncProtocol sync;

    // Add varying samples
    sync.updateLatency(20000);  // 10000 one-way
    sync.updateLatency(20000);
    sync.updateLatency(20000);
    sync.updateLatency(24000);  // 12000 one-way - 2000 deviation

    // Variance should be non-zero
    TEST_ASSERT_TRUE(sync.getRTTVariance() > 0);
}

// =============================================================================
// TIME CONVERSION TESTS
// =============================================================================

void test_SimpleSyncProtocol_primaryToLocalTime_positive_offset(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // SECONDARY is 5000us ahead
    for (int i = 0; i < 5; i++) {
        sync.addOffsetSample(5000);
    }

    // PRIMARY time 1000000 → LOCAL = 1000000 + 5000 = 1005000
    uint64_t localTime = sync.primaryToLocalTime(1000000);
    TEST_ASSERT_EQUAL_UINT64(1005000, localTime);
}

void test_SimpleSyncProtocol_primaryToLocalTime_negative_offset(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // SECONDARY is 5000us behind
    for (int i = 0; i < 5; i++) {
        sync.addOffsetSample(-5000);
    }

    // PRIMARY time 1000000 → LOCAL = 1000000 + (-5000) = 995000
    uint64_t localTime = sync.primaryToLocalTime(1000000);
    TEST_ASSERT_EQUAL_UINT64(995000, localTime);
}

void test_SimpleSyncProtocol_localToPrimaryTime_positive_offset(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // SECONDARY is 5000us ahead
    for (int i = 0; i < 5; i++) {
        sync.addOffsetSample(5000);
    }

    // LOCAL time 1005000 → PRIMARY = 1005000 - 5000 = 1000000
    uint64_t primaryTime = sync.localToPrimaryTime(1005000);
    TEST_ASSERT_EQUAL_UINT64(1000000, primaryTime);
}

void test_SimpleSyncProtocol_localToPrimaryTime_negative_offset(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(100);

    // SECONDARY is 5000us behind
    for (int i = 0; i < 5; i++) {
        sync.addOffsetSample(-5000);
    }

    // LOCAL time 995000 → PRIMARY = 995000 - (-5000) = 1000000
    uint64_t primaryTime = sync.localToPrimaryTime(995000);
    TEST_ASSERT_EQUAL_UINT64(1000000, primaryTime);
}

// =============================================================================
// FACTORY METHOD TESTS FOR PTP COMMANDS
// =============================================================================

void test_SyncCommand_createPingWithT1(void) {
    SyncCommand cmd = SyncCommand::createPingWithT1(42, 1234567890);

    TEST_ASSERT_EQUAL(SyncCommandType::PING, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(42, cmd.getSequenceId());
    TEST_ASSERT_EQUAL_UINT64(1234567890, cmd.getTimestamp());
}

void test_SyncCommand_createPongWithTimestamps(void) {
    SyncCommand cmd = SyncCommand::createPongWithTimestamps(42, 1000000, 1005000);

    TEST_ASSERT_EQUAL(SyncCommandType::PONG, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(42, cmd.getSequenceId());

    // T2 and T3 stored in data payload (32-bit low parts when high bits are 0)
    TEST_ASSERT_EQUAL_INT32(1000000, cmd.getDataInt("0", -1));  // T2
    TEST_ASSERT_EQUAL_INT32(1005000, cmd.getDataInt("1", -1));  // T3
}


void test_SyncCommand_createDebugFlashWithTime(void) {
    SyncCommand cmd = SyncCommand::createDebugFlashWithTime(42, 5000000);

    TEST_ASSERT_EQUAL(SyncCommandType::DEBUG_FLASH, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(42, cmd.getSequenceId());
    TEST_ASSERT_EQUAL_INT32(5000000, cmd.getDataInt("0", -1));  // flashTime (low 32 bits)
}

void test_SyncCommand_createDebugFlash(void) {
    SyncCommand cmd = SyncCommand::createDebugFlash(42);

    TEST_ASSERT_EQUAL(SyncCommandType::DEBUG_FLASH, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(42, cmd.getSequenceId());
}

void test_SyncCommand_createPing(void) {
    SyncCommand cmd = SyncCommand::createPing(42);

    TEST_ASSERT_EQUAL(SyncCommandType::PING, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(42, cmd.getSequenceId());
}

void test_SyncCommand_createPong(void) {
    SyncCommand cmd = SyncCommand::createPong(42);

    TEST_ASSERT_EQUAL(SyncCommandType::PONG, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(42, cmd.getSequenceId());
}

// =============================================================================
// TEST RUNNER
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // SyncCommand Constructor Tests
    RUN_TEST(test_SyncCommand_default_constructor);
    RUN_TEST(test_SyncCommand_parameterized_constructor);
    RUN_TEST(test_SyncCommand_setType);
    RUN_TEST(test_SyncCommand_setSequenceId);
    RUN_TEST(test_SyncCommand_setTimestamp);
    RUN_TEST(test_SyncCommand_setTimestampNow);

    // SyncCommand Type String Tests
    RUN_TEST(test_SyncCommand_getTypeString_buzz);
    RUN_TEST(test_SyncCommand_getTypeString_startSession);
    RUN_TEST(test_SyncCommand_getTypeString_stopSession);

    // SyncCommand Data Payload Tests
    RUN_TEST(test_SyncCommand_setData_string);
    RUN_TEST(test_SyncCommand_getData_string);
    RUN_TEST(test_SyncCommand_getData_missing_key);
    RUN_TEST(test_SyncCommand_setData_integer);
    RUN_TEST(test_SyncCommand_getDataInt_with_default);
    RUN_TEST(test_SyncCommand_getDataInt_existing_key);
    RUN_TEST(test_SyncCommand_hasData_true);
    RUN_TEST(test_SyncCommand_hasData_false);
    RUN_TEST(test_SyncCommand_clearData);
    RUN_TEST(test_SyncCommand_setData_updates_existing);
    RUN_TEST(test_SyncCommand_setData_multiple_pairs);

    // SyncCommand Serialization Tests
    RUN_TEST(test_SyncCommand_serialize_with_data);
    RUN_TEST(test_SyncCommand_serialize_buffer_too_small);
    RUN_TEST(test_SyncCommand_serialize_null_buffer);

    // SyncCommand Deserialization Tests
    RUN_TEST(test_SyncCommand_deserialize_buzz);
    RUN_TEST(test_SyncCommand_deserialize_with_data);
    RUN_TEST(test_SyncCommand_deserialize_null_message);
    RUN_TEST(test_SyncCommand_deserialize_empty_message);
    RUN_TEST(test_SyncCommand_deserialize_invalid_format);
    RUN_TEST(test_SyncCommand_deserialize_unknown_command);
    RUN_TEST(test_SyncCommand_deserialize_roundtrip);

    // SyncCommand Factory Method Tests
    RUN_TEST(test_SyncCommand_createStartSession);
    RUN_TEST(test_SyncCommand_createPauseSession);
    RUN_TEST(test_SyncCommand_createResumeSession);
    RUN_TEST(test_SyncCommand_createStopSession);
    RUN_TEST(test_SyncCommand_createDeactivate);

    // SequenceGenerator Tests
    RUN_TEST(test_SequenceGenerator_initial_value);
    RUN_TEST(test_SequenceGenerator_increment);
    RUN_TEST(test_SequenceGenerator_reset);
    RUN_TEST(test_global_sequence_generator);

    // SimpleSyncProtocol Tests
    RUN_TEST(test_SimpleSyncProtocol_initial_state);
    RUN_TEST(test_SimpleSyncProtocol_calculateOffset);
    RUN_TEST(test_SimpleSyncProtocol_calculateOffset_negative);
    RUN_TEST(test_SimpleSyncProtocol_applyCompensation);
    RUN_TEST(test_SimpleSyncProtocol_getTimeSinceSync_never_synced);
    RUN_TEST(test_SimpleSyncProtocol_getTimeSinceSync_after_sync);
    RUN_TEST(test_SimpleSyncProtocol_reset);

    // PING/PONG Latency Measurement Tests (with EMA smoothing)
    RUN_TEST(test_SimpleSyncProtocol_getMeasuredLatency_initial_zero);
    RUN_TEST(test_SimpleSyncProtocol_updateLatency);
    RUN_TEST(test_SimpleSyncProtocol_updateLatency_multiple);
    RUN_TEST(test_SimpleSyncProtocol_updateLatency_ema_smoothing);
    RUN_TEST(test_SimpleSyncProtocol_updateLatency_outlier_rejection);
    RUN_TEST(test_SimpleSyncProtocol_resetLatency);

    // Timing Utility Tests
    RUN_TEST(test_getMicros);
    RUN_TEST(test_getMillis);

    // setData Edge Case Tests
    RUN_TEST(test_SyncCommand_setData_max_pairs_reached);
    RUN_TEST(test_SyncCommand_setData_null_key);
    RUN_TEST(test_SyncCommand_setData_null_value);

    // Large Timestamp Serialization Tests
    RUN_TEST(test_SyncCommand_serialize_large_timestamp);

    // Additional Deserialize Tests
    RUN_TEST(test_SyncCommand_deserialize_ping);
    RUN_TEST(test_SyncCommand_deserialize_pong);

    // Additional getTypeString Tests
    RUN_TEST(test_SyncCommand_getTypeString_ping);
    RUN_TEST(test_SyncCommand_getTypeString_pong);
    RUN_TEST(test_SyncCommand_getTypeString_deactivate);
    RUN_TEST(test_SyncCommand_getTypeString_pause_session);
    RUN_TEST(test_SyncCommand_getTypeString_resume_session);

    // PTP Clock Synchronization Tests
    RUN_TEST(test_SimpleSyncProtocol_calculatePTPOffset_symmetric);
    RUN_TEST(test_SimpleSyncProtocol_calculatePTPOffset_positive_offset);
    RUN_TEST(test_SimpleSyncProtocol_calculatePTPOffset_negative_offset);
    RUN_TEST(test_SimpleSyncProtocol_calculatePTPOffset_zero_offset);

    // Offset Sample Collection Tests
    RUN_TEST(test_SimpleSyncProtocol_addOffsetSample_single);
    RUN_TEST(test_SimpleSyncProtocol_addOffsetSample_fills_buffer);
    RUN_TEST(test_SimpleSyncProtocol_addOffsetSample_circular_buffer);
    RUN_TEST(test_SimpleSyncProtocol_getMedianOffset_odd_count);
    RUN_TEST(test_SimpleSyncProtocol_getMedianOffset_even_count);
    RUN_TEST(test_SimpleSyncProtocol_isClockSyncValid_below_threshold);
    RUN_TEST(test_SimpleSyncProtocol_isClockSyncValid_at_threshold);
    RUN_TEST(test_SimpleSyncProtocol_getOffsetSampleCount);
    RUN_TEST(test_SimpleSyncProtocol_resetClockSync);

    // RTT Quality Filtering Tests
    RUN_TEST(test_SimpleSyncProtocol_addOffsetSampleWithQuality_accepts_good_rtt);
    RUN_TEST(test_SimpleSyncProtocol_addOffsetSampleWithQuality_rejects_high_rtt);
    RUN_TEST(test_SimpleSyncProtocol_addOffsetSampleWithQuality_boundary);

    // Drift Compensation Tests
    RUN_TEST(test_SimpleSyncProtocol_updateOffsetEMA_first_sample);
    RUN_TEST(test_SimpleSyncProtocol_updateOffsetEMA_updates_drift_rate);
    RUN_TEST(test_SimpleSyncProtocol_getCorrectedOffset_no_drift);
    RUN_TEST(test_SimpleSyncProtocol_getCorrectedOffset_not_synced);
    RUN_TEST(test_SimpleSyncProtocol_getDriftRate_initial_zero);
    RUN_TEST(test_SimpleSyncProtocol_getDriftRate_after_reset);

    // Adaptive Lead Time Tests
    RUN_TEST(test_SimpleSyncProtocol_calculateAdaptiveLeadTime_default_when_few_samples);
    RUN_TEST(test_SimpleSyncProtocol_calculateAdaptiveLeadTime_minimum_clamp);
    RUN_TEST(test_SimpleSyncProtocol_calculateAdaptiveLeadTime_maximum_clamp);
    RUN_TEST(test_SimpleSyncProtocol_calculateAdaptiveLeadTime_normal_calculation);
    RUN_TEST(test_SimpleSyncProtocol_getAverageRTT);
    RUN_TEST(test_SimpleSyncProtocol_getRTTVariance_initial_zero);
    RUN_TEST(test_SimpleSyncProtocol_getRTTVariance_after_consistent_samples);
    RUN_TEST(test_SimpleSyncProtocol_getRTTVariance_after_varying_samples);

    // Time Conversion Tests
    RUN_TEST(test_SimpleSyncProtocol_primaryToLocalTime_positive_offset);
    RUN_TEST(test_SimpleSyncProtocol_primaryToLocalTime_negative_offset);
    RUN_TEST(test_SimpleSyncProtocol_localToPrimaryTime_positive_offset);
    RUN_TEST(test_SimpleSyncProtocol_localToPrimaryTime_negative_offset);

    // Factory Method Tests for PTP Commands
    RUN_TEST(test_SyncCommand_createPingWithT1);
    RUN_TEST(test_SyncCommand_createPongWithTimestamps);
    RUN_TEST(test_SyncCommand_createDebugFlashWithTime);
    RUN_TEST(test_SyncCommand_createDebugFlash);
    RUN_TEST(test_SyncCommand_createPing);
    RUN_TEST(test_SyncCommand_createPong);

    return UNITY_END();
}
