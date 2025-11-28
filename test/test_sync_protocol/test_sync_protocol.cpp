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
    TEST_ASSERT_EQUAL(SyncCommandType::HEARTBEAT, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(0, cmd.getSequenceId());
    TEST_ASSERT_EQUAL_UINT8(0, cmd.getDataCount());
}

void test_SyncCommand_parameterized_constructor(void) {
    SyncCommand cmd(SyncCommandType::EXECUTE_BUZZ, 42);
    TEST_ASSERT_EQUAL(SyncCommandType::EXECUTE_BUZZ, cmd.getType());
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

void test_SyncCommand_getTypeString_heartbeat(void) {
    SyncCommand cmd(SyncCommandType::HEARTBEAT, 0);
    TEST_ASSERT_EQUAL_STRING("HEARTBEAT", cmd.getTypeString());
}

void test_SyncCommand_getTypeString_executeBuzz(void) {
    SyncCommand cmd(SyncCommandType::EXECUTE_BUZZ, 0);
    TEST_ASSERT_EQUAL_STRING("EXECUTE_BUZZ", cmd.getTypeString());
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

void test_SyncCommand_serialize_heartbeat(void) {
    mockSetMillis(0);  // Timestamp will be 0
    SyncCommand cmd(SyncCommandType::HEARTBEAT, 1);
    cmd.setTimestamp(0);  // Ensure timestamp is 0

    char buffer[128];
    TEST_ASSERT_TRUE(cmd.serialize(buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("HEARTBEAT:1:0", buffer);
}

void test_SyncCommand_serialize_with_data(void) {
    SyncCommand cmd(SyncCommandType::EXECUTE_BUZZ, 42);
    cmd.setTimestamp(1000000);
    cmd.setData("finger", "0");
    cmd.setData("amplitude", "50");

    char buffer[256];
    TEST_ASSERT_TRUE(cmd.serialize(buffer, sizeof(buffer)));

    // Format: EXECUTE_BUZZ:42:1000000:finger|0|amplitude|50
    TEST_ASSERT_TRUE(strstr(buffer, "EXECUTE_BUZZ:42:1000000:") != nullptr);
    TEST_ASSERT_TRUE(strstr(buffer, "finger|0") != nullptr);
    TEST_ASSERT_TRUE(strstr(buffer, "amplitude|50") != nullptr);
}

void test_SyncCommand_serialize_buffer_too_small(void) {
    SyncCommand cmd(SyncCommandType::HEARTBEAT, 1);

    char buffer[10];  // Too small
    TEST_ASSERT_FALSE(cmd.serialize(buffer, sizeof(buffer)));
}

void test_SyncCommand_serialize_null_buffer(void) {
    SyncCommand cmd(SyncCommandType::HEARTBEAT, 1);
    TEST_ASSERT_FALSE(cmd.serialize(nullptr, 128));
}

// =============================================================================
// SYNC COMMAND DESERIALIZATION TESTS
// =============================================================================

void test_SyncCommand_deserialize_heartbeat(void) {
    SyncCommand cmd;
    TEST_ASSERT_TRUE(cmd.deserialize("HEARTBEAT:1:1000000"));

    TEST_ASSERT_EQUAL(SyncCommandType::HEARTBEAT, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(1, cmd.getSequenceId());
    TEST_ASSERT_EQUAL_UINT64(1000000, cmd.getTimestamp());
}

void test_SyncCommand_deserialize_execute_buzz(void) {
    SyncCommand cmd;
    TEST_ASSERT_TRUE(cmd.deserialize("EXECUTE_BUZZ:42:5000000"));

    TEST_ASSERT_EQUAL(SyncCommandType::EXECUTE_BUZZ, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(42, cmd.getSequenceId());
    TEST_ASSERT_EQUAL_UINT64(5000000, cmd.getTimestamp());
}

void test_SyncCommand_deserialize_with_data(void) {
    SyncCommand cmd;
    TEST_ASSERT_TRUE(cmd.deserialize("EXECUTE_BUZZ:42:1000000:finger|0|amplitude|50"));

    TEST_ASSERT_EQUAL(SyncCommandType::EXECUTE_BUZZ, cmd.getType());
    TEST_ASSERT_EQUAL_INT32(0, cmd.getDataInt("finger", -1));
    TEST_ASSERT_EQUAL_INT32(50, cmd.getDataInt("amplitude", -1));
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
    TEST_ASSERT_FALSE(cmd.deserialize("UNKNOWN_CMD:1:1000"));
}

void test_SyncCommand_deserialize_roundtrip(void) {
    // Create and serialize a command
    SyncCommand original(SyncCommandType::EXECUTE_BUZZ, 123);
    original.setTimestamp(9999999);
    original.setData("finger", "3");
    original.setData("amplitude", "80");

    char buffer[256];
    TEST_ASSERT_TRUE(original.serialize(buffer, sizeof(buffer)));

    // Deserialize and verify
    SyncCommand parsed;
    TEST_ASSERT_TRUE(parsed.deserialize(buffer));

    TEST_ASSERT_EQUAL(original.getType(), parsed.getType());
    TEST_ASSERT_EQUAL_UINT32(original.getSequenceId(), parsed.getSequenceId());
    TEST_ASSERT_EQUAL_UINT64(original.getTimestamp(), parsed.getTimestamp());
    TEST_ASSERT_EQUAL_INT32(3, parsed.getDataInt("finger", -1));
    TEST_ASSERT_EQUAL_INT32(80, parsed.getDataInt("amplitude", -1));
}

// =============================================================================
// SYNC COMMAND FACTORY METHOD TESTS
// =============================================================================

void test_SyncCommand_createHeartbeat(void) {
    SyncCommand cmd = SyncCommand::createHeartbeat(5);
    TEST_ASSERT_EQUAL(SyncCommandType::HEARTBEAT, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(5, cmd.getSequenceId());
}

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

void test_SyncCommand_createExecuteBuzz(void) {
    SyncCommand cmd = SyncCommand::createExecuteBuzz(30, 2, 75);
    TEST_ASSERT_EQUAL(SyncCommandType::EXECUTE_BUZZ, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(30, cmd.getSequenceId());
    TEST_ASSERT_EQUAL_INT32(2, cmd.getDataInt("finger", -1));
    TEST_ASSERT_EQUAL_INT32(75, cmd.getDataInt("amplitude", -1));
}

void test_SyncCommand_createBuzzComplete(void) {
    SyncCommand cmd = SyncCommand::createBuzzComplete(35);
    TEST_ASSERT_EQUAL(SyncCommandType::BUZZ_COMPLETE, cmd.getType());
    TEST_ASSERT_EQUAL_UINT32(35, cmd.getSequenceId());
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

void test_SimpleSyncProtocol_calculateRoundTrip(void) {
    SimpleSyncProtocol sync;
    mockSetMillis(200);

    // Sent: 1000000, Received: 1010000 (RTT = 10000 us)
    // Remote time: 1005000 (captured midway)
    // One-way latency = 10000 / 2 = 5000
    uint32_t latency = sync.calculateRoundTrip(1000000, 1010000, 1005000);

    TEST_ASSERT_EQUAL_UINT32(5000, latency);
    TEST_ASSERT_TRUE(sync.isSynced());
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
    RUN_TEST(test_SyncCommand_getTypeString_heartbeat);
    RUN_TEST(test_SyncCommand_getTypeString_executeBuzz);
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
    RUN_TEST(test_SyncCommand_serialize_heartbeat);
    RUN_TEST(test_SyncCommand_serialize_with_data);
    RUN_TEST(test_SyncCommand_serialize_buffer_too_small);
    RUN_TEST(test_SyncCommand_serialize_null_buffer);

    // SyncCommand Deserialization Tests
    RUN_TEST(test_SyncCommand_deserialize_heartbeat);
    RUN_TEST(test_SyncCommand_deserialize_execute_buzz);
    RUN_TEST(test_SyncCommand_deserialize_with_data);
    RUN_TEST(test_SyncCommand_deserialize_null_message);
    RUN_TEST(test_SyncCommand_deserialize_empty_message);
    RUN_TEST(test_SyncCommand_deserialize_invalid_format);
    RUN_TEST(test_SyncCommand_deserialize_unknown_command);
    RUN_TEST(test_SyncCommand_deserialize_roundtrip);

    // SyncCommand Factory Method Tests
    RUN_TEST(test_SyncCommand_createHeartbeat);
    RUN_TEST(test_SyncCommand_createStartSession);
    RUN_TEST(test_SyncCommand_createPauseSession);
    RUN_TEST(test_SyncCommand_createResumeSession);
    RUN_TEST(test_SyncCommand_createStopSession);
    RUN_TEST(test_SyncCommand_createExecuteBuzz);
    RUN_TEST(test_SyncCommand_createBuzzComplete);
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
    RUN_TEST(test_SimpleSyncProtocol_calculateRoundTrip);

    // Timing Utility Tests
    RUN_TEST(test_getMicros);
    RUN_TEST(test_getMillis);

    return UNITY_END();
}
