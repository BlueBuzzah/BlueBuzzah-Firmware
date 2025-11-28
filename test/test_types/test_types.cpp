/**
 * @file test_types.cpp
 * @brief Unit tests for types.h - Enums, structs, and helper functions
 */

#include <unity.h>
#include "types.h"

// =============================================================================
// TEST FIXTURES
// =============================================================================

void setUp(void) {
    // Reset any state before each test
}

void tearDown(void) {
    // Clean up after each test
}

// =============================================================================
// DEVICE ROLE TESTS
// =============================================================================

void test_deviceRoleToString_primary(void) {
    TEST_ASSERT_EQUAL_STRING("PRIMARY", deviceRoleToString(DeviceRole::PRIMARY));
}

void test_deviceRoleToString_secondary(void) {
    TEST_ASSERT_EQUAL_STRING("SECONDARY", deviceRoleToString(DeviceRole::SECONDARY));
}

void test_deviceRoleToTag_primary(void) {
    TEST_ASSERT_EQUAL_STRING("[PRIMARY]", deviceRoleToTag(DeviceRole::PRIMARY));
}

void test_deviceRoleToTag_secondary(void) {
    TEST_ASSERT_EQUAL_STRING("[SECONDARY]", deviceRoleToTag(DeviceRole::SECONDARY));
}

// =============================================================================
// THERAPY STATE STRING TESTS
// =============================================================================

void test_therapyStateToString_idle(void) {
    TEST_ASSERT_EQUAL_STRING("IDLE", therapyStateToString(TherapyState::IDLE));
}

void test_therapyStateToString_connecting(void) {
    TEST_ASSERT_EQUAL_STRING("CONNECTING", therapyStateToString(TherapyState::CONNECTING));
}

void test_therapyStateToString_ready(void) {
    TEST_ASSERT_EQUAL_STRING("READY", therapyStateToString(TherapyState::READY));
}

void test_therapyStateToString_running(void) {
    TEST_ASSERT_EQUAL_STRING("RUNNING", therapyStateToString(TherapyState::RUNNING));
}

void test_therapyStateToString_paused(void) {
    TEST_ASSERT_EQUAL_STRING("PAUSED", therapyStateToString(TherapyState::PAUSED));
}

void test_therapyStateToString_stopping(void) {
    TEST_ASSERT_EQUAL_STRING("STOPPING", therapyStateToString(TherapyState::STOPPING));
}

void test_therapyStateToString_error(void) {
    TEST_ASSERT_EQUAL_STRING("ERROR", therapyStateToString(TherapyState::ERROR));
}

void test_therapyStateToString_lowBattery(void) {
    TEST_ASSERT_EQUAL_STRING("LOW_BATTERY", therapyStateToString(TherapyState::LOW_BATTERY));
}

void test_therapyStateToString_criticalBattery(void) {
    TEST_ASSERT_EQUAL_STRING("CRITICAL_BATTERY", therapyStateToString(TherapyState::CRITICAL_BATTERY));
}

void test_therapyStateToString_connectionLost(void) {
    TEST_ASSERT_EQUAL_STRING("CONNECTION_LOST", therapyStateToString(TherapyState::CONNECTION_LOST));
}

void test_therapyStateToString_phoneDisconnected(void) {
    TEST_ASSERT_EQUAL_STRING("PHONE_DISCONNECTED", therapyStateToString(TherapyState::PHONE_DISCONNECTED));
}

// =============================================================================
// ACTIVE STATE TESTS
// =============================================================================

void test_isActiveState_running_returns_true(void) {
    TEST_ASSERT_TRUE(isActiveState(TherapyState::RUNNING));
}

void test_isActiveState_paused_returns_true(void) {
    TEST_ASSERT_TRUE(isActiveState(TherapyState::PAUSED));
}

void test_isActiveState_lowBattery_returns_true(void) {
    TEST_ASSERT_TRUE(isActiveState(TherapyState::LOW_BATTERY));
}

void test_isActiveState_idle_returns_false(void) {
    TEST_ASSERT_FALSE(isActiveState(TherapyState::IDLE));
}

void test_isActiveState_ready_returns_false(void) {
    TEST_ASSERT_FALSE(isActiveState(TherapyState::READY));
}

void test_isActiveState_error_returns_false(void) {
    TEST_ASSERT_FALSE(isActiveState(TherapyState::ERROR));
}

void test_isActiveState_stopping_returns_false(void) {
    TEST_ASSERT_FALSE(isActiveState(TherapyState::STOPPING));
}

// =============================================================================
// ERROR STATE TESTS
// =============================================================================

void test_isErrorState_error_returns_true(void) {
    TEST_ASSERT_TRUE(isErrorState(TherapyState::ERROR));
}

void test_isErrorState_criticalBattery_returns_true(void) {
    TEST_ASSERT_TRUE(isErrorState(TherapyState::CRITICAL_BATTERY));
}

void test_isErrorState_connectionLost_returns_true(void) {
    TEST_ASSERT_TRUE(isErrorState(TherapyState::CONNECTION_LOST));
}

void test_isErrorState_running_returns_false(void) {
    TEST_ASSERT_FALSE(isErrorState(TherapyState::RUNNING));
}

void test_isErrorState_idle_returns_false(void) {
    TEST_ASSERT_FALSE(isErrorState(TherapyState::IDLE));
}

void test_isErrorState_lowBattery_returns_false(void) {
    TEST_ASSERT_FALSE(isErrorState(TherapyState::LOW_BATTERY));
}

// =============================================================================
// STATE TRIGGER STRING TESTS
// =============================================================================

void test_stateTriggerToString_connected(void) {
    TEST_ASSERT_EQUAL_STRING("CONNECTED", stateTriggerToString(StateTrigger::CONNECTED));
}

void test_stateTriggerToString_disconnected(void) {
    TEST_ASSERT_EQUAL_STRING("DISCONNECTED", stateTriggerToString(StateTrigger::DISCONNECTED));
}

void test_stateTriggerToString_startSession(void) {
    TEST_ASSERT_EQUAL_STRING("START_SESSION", stateTriggerToString(StateTrigger::START_SESSION));
}

void test_stateTriggerToString_stopSession(void) {
    TEST_ASSERT_EQUAL_STRING("STOP_SESSION", stateTriggerToString(StateTrigger::STOP_SESSION));
}

void test_stateTriggerToString_emergencyStop(void) {
    TEST_ASSERT_EQUAL_STRING("EMERGENCY_STOP", stateTriggerToString(StateTrigger::EMERGENCY_STOP));
}

// =============================================================================
// BOOT RESULT TESTS
// =============================================================================

void test_isBootSuccess_failed_returns_false(void) {
    TEST_ASSERT_FALSE(isBootSuccess(BootResult::FAILED));
}

void test_isBootSuccess_noPhone_returns_true(void) {
    TEST_ASSERT_TRUE(isBootSuccess(BootResult::SUCCESS_NO_PHONE));
}

void test_isBootSuccess_withPhone_returns_true(void) {
    TEST_ASSERT_TRUE(isBootSuccess(BootResult::SUCCESS_WITH_PHONE));
}

void test_isBootSuccess_success_returns_true(void) {
    TEST_ASSERT_TRUE(isBootSuccess(BootResult::SUCCESS));
}

// =============================================================================
// SYNC COMMAND TYPE STRING TESTS
// =============================================================================

void test_syncCommandTypeToString_startSession(void) {
    TEST_ASSERT_EQUAL_STRING("START_SESSION", syncCommandTypeToString(SyncCommandType::START_SESSION));
}

void test_syncCommandTypeToString_executeBuzz(void) {
    TEST_ASSERT_EQUAL_STRING("EXECUTE_BUZZ", syncCommandTypeToString(SyncCommandType::EXECUTE_BUZZ));
}

void test_syncCommandTypeToString_heartbeat(void) {
    TEST_ASSERT_EQUAL_STRING("HEARTBEAT", syncCommandTypeToString(SyncCommandType::HEARTBEAT));
}

void test_syncCommandTypeToString_deactivate(void) {
    TEST_ASSERT_EQUAL_STRING("DEACTIVATE", syncCommandTypeToString(SyncCommandType::DEACTIVATE));
}

// =============================================================================
// RGB COLOR TESTS
// =============================================================================

void test_RGBColor_default_constructor(void) {
    RGBColor color;
    TEST_ASSERT_EQUAL_UINT8(0, color.r);
    TEST_ASSERT_EQUAL_UINT8(0, color.g);
    TEST_ASSERT_EQUAL_UINT8(0, color.b);
}

void test_RGBColor_parameterized_constructor(void) {
    RGBColor color(128, 64, 32);
    TEST_ASSERT_EQUAL_UINT8(128, color.r);
    TEST_ASSERT_EQUAL_UINT8(64, color.g);
    TEST_ASSERT_EQUAL_UINT8(32, color.b);
}

void test_RGBColor_equality_same_colors(void) {
    RGBColor a(255, 128, 64);
    RGBColor b(255, 128, 64);
    TEST_ASSERT_TRUE(a == b);
}

void test_RGBColor_equality_different_colors(void) {
    RGBColor a(255, 128, 64);
    RGBColor b(255, 128, 0);
    TEST_ASSERT_FALSE(a == b);
}

void test_RGBColor_inequality_different_colors(void) {
    RGBColor a(255, 128, 64);
    RGBColor b(255, 128, 0);
    TEST_ASSERT_TRUE(a != b);
}

void test_RGBColor_inequality_same_colors(void) {
    RGBColor a(255, 128, 64);
    RGBColor b(255, 128, 64);
    TEST_ASSERT_FALSE(a != b);
}

void test_RGBColor_predefined_red(void) {
    TEST_ASSERT_EQUAL_UINT8(255, Colors::RED.r);
    TEST_ASSERT_EQUAL_UINT8(0, Colors::RED.g);
    TEST_ASSERT_EQUAL_UINT8(0, Colors::RED.b);
}

void test_RGBColor_predefined_green(void) {
    TEST_ASSERT_EQUAL_UINT8(0, Colors::GREEN.r);
    TEST_ASSERT_EQUAL_UINT8(255, Colors::GREEN.g);
    TEST_ASSERT_EQUAL_UINT8(0, Colors::GREEN.b);
}

void test_RGBColor_predefined_blue(void) {
    TEST_ASSERT_EQUAL_UINT8(0, Colors::BLUE.r);
    TEST_ASSERT_EQUAL_UINT8(0, Colors::BLUE.g);
    TEST_ASSERT_EQUAL_UINT8(255, Colors::BLUE.b);
}

// =============================================================================
// BATTERY STATUS TESTS
// =============================================================================

void test_BatteryStatus_default_constructor(void) {
    BatteryStatus status;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, status.voltage);
    TEST_ASSERT_EQUAL_UINT8(0, status.percentage);
    TEST_ASSERT_FALSE(status.isLow);
    TEST_ASSERT_FALSE(status.isCritical);
}

void test_BatteryStatus_statusString_critical(void) {
    BatteryStatus status;
    status.isCritical = true;
    status.isLow = true;  // Critical takes precedence
    TEST_ASSERT_EQUAL_STRING("CRITICAL", status.statusString());
}

void test_BatteryStatus_statusString_low(void) {
    BatteryStatus status;
    status.isLow = true;
    status.isCritical = false;
    TEST_ASSERT_EQUAL_STRING("LOW", status.statusString());
}

void test_BatteryStatus_statusString_ok(void) {
    BatteryStatus status;
    status.isLow = false;
    status.isCritical = false;
    TEST_ASSERT_EQUAL_STRING("OK", status.statusString());
}

void test_BatteryStatus_isOk_when_ok(void) {
    BatteryStatus status;
    status.isLow = false;
    status.isCritical = false;
    TEST_ASSERT_TRUE(status.isOk());
}

void test_BatteryStatus_isOk_when_low(void) {
    BatteryStatus status;
    status.isLow = true;
    TEST_ASSERT_FALSE(status.isOk());
}

void test_BatteryStatus_isOk_when_critical(void) {
    BatteryStatus status;
    status.isCritical = true;
    TEST_ASSERT_FALSE(status.isOk());
}

void test_BatteryStatus_requiresAction_when_ok(void) {
    BatteryStatus status;
    status.isLow = false;
    status.isCritical = false;
    TEST_ASSERT_FALSE(status.requiresAction());
}

void test_BatteryStatus_requiresAction_when_low(void) {
    BatteryStatus status;
    status.isLow = true;
    TEST_ASSERT_TRUE(status.requiresAction());
}

void test_BatteryStatus_requiresAction_when_critical(void) {
    BatteryStatus status;
    status.isCritical = true;
    TEST_ASSERT_TRUE(status.requiresAction());
}

// =============================================================================
// DEVICE CONFIG TESTS
// =============================================================================

void test_DeviceConfig_default_constructor(void) {
    DeviceConfig config;
    TEST_ASSERT_EQUAL(DeviceRole::PRIMARY, config.role);
    TEST_ASSERT_EQUAL_STRING("BlueBuzzah", config.bleName);
    TEST_ASSERT_EQUAL_STRING("[PRIMARY]", config.deviceTag);
}

void test_DeviceConfig_isPrimary_when_primary(void) {
    DeviceConfig config;
    config.role = DeviceRole::PRIMARY;
    TEST_ASSERT_TRUE(config.isPrimary());
    TEST_ASSERT_FALSE(config.isSecondary());
}

void test_DeviceConfig_isSecondary_when_secondary(void) {
    DeviceConfig config;
    config.role = DeviceRole::SECONDARY;
    TEST_ASSERT_FALSE(config.isPrimary());
    TEST_ASSERT_TRUE(config.isSecondary());
}

// =============================================================================
// CONNECTION STATE TESTS
// =============================================================================

void test_ConnectionState_default_constructor(void) {
    ConnectionState state;
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, state.phoneConnHandle);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, state.secondaryConnHandle);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, state.primaryConnHandle);
}

void test_ConnectionState_isPhoneConnected_when_connected(void) {
    ConnectionState state;
    state.phoneConnHandle = 0x0001;
    TEST_ASSERT_TRUE(state.isPhoneConnected());
}

void test_ConnectionState_isPhoneConnected_when_disconnected(void) {
    ConnectionState state;
    TEST_ASSERT_FALSE(state.isPhoneConnected());
}

void test_ConnectionState_isSecondaryConnected_when_connected(void) {
    ConnectionState state;
    state.secondaryConnHandle = 0x0002;
    TEST_ASSERT_TRUE(state.isSecondaryConnected());
}

void test_ConnectionState_isSecondaryConnected_when_disconnected(void) {
    ConnectionState state;
    TEST_ASSERT_FALSE(state.isSecondaryConnected());
}

void test_ConnectionState_isPrimaryConnected_when_connected(void) {
    ConnectionState state;
    state.primaryConnHandle = 0x0003;
    TEST_ASSERT_TRUE(state.isPrimaryConnected());
}

void test_ConnectionState_isPrimaryConnected_when_disconnected(void) {
    ConnectionState state;
    TEST_ASSERT_FALSE(state.isPrimaryConnected());
}

void test_ConnectionState_clearPhone(void) {
    ConnectionState state;
    state.phoneConnHandle = 0x0001;
    state.clearPhone();
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, state.phoneConnHandle);
}

void test_ConnectionState_clearSecondary(void) {
    ConnectionState state;
    state.secondaryConnHandle = 0x0002;
    state.clearSecondary();
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, state.secondaryConnHandle);
}

void test_ConnectionState_clearPrimary(void) {
    ConnectionState state;
    state.primaryConnHandle = 0x0003;
    state.clearPrimary();
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, state.primaryConnHandle);
}

void test_ConnectionState_clearAll(void) {
    ConnectionState state;
    state.phoneConnHandle = 0x0001;
    state.secondaryConnHandle = 0x0002;
    state.primaryConnHandle = 0x0003;
    state.clearAll();
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, state.phoneConnHandle);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, state.secondaryConnHandle);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, state.primaryConnHandle);
}

// =============================================================================
// TEST RUNNER
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Device Role Tests
    RUN_TEST(test_deviceRoleToString_primary);
    RUN_TEST(test_deviceRoleToString_secondary);
    RUN_TEST(test_deviceRoleToTag_primary);
    RUN_TEST(test_deviceRoleToTag_secondary);

    // Therapy State String Tests
    RUN_TEST(test_therapyStateToString_idle);
    RUN_TEST(test_therapyStateToString_connecting);
    RUN_TEST(test_therapyStateToString_ready);
    RUN_TEST(test_therapyStateToString_running);
    RUN_TEST(test_therapyStateToString_paused);
    RUN_TEST(test_therapyStateToString_stopping);
    RUN_TEST(test_therapyStateToString_error);
    RUN_TEST(test_therapyStateToString_lowBattery);
    RUN_TEST(test_therapyStateToString_criticalBattery);
    RUN_TEST(test_therapyStateToString_connectionLost);
    RUN_TEST(test_therapyStateToString_phoneDisconnected);

    // Active State Tests
    RUN_TEST(test_isActiveState_running_returns_true);
    RUN_TEST(test_isActiveState_paused_returns_true);
    RUN_TEST(test_isActiveState_lowBattery_returns_true);
    RUN_TEST(test_isActiveState_idle_returns_false);
    RUN_TEST(test_isActiveState_ready_returns_false);
    RUN_TEST(test_isActiveState_error_returns_false);
    RUN_TEST(test_isActiveState_stopping_returns_false);

    // Error State Tests
    RUN_TEST(test_isErrorState_error_returns_true);
    RUN_TEST(test_isErrorState_criticalBattery_returns_true);
    RUN_TEST(test_isErrorState_connectionLost_returns_true);
    RUN_TEST(test_isErrorState_running_returns_false);
    RUN_TEST(test_isErrorState_idle_returns_false);
    RUN_TEST(test_isErrorState_lowBattery_returns_false);

    // State Trigger String Tests
    RUN_TEST(test_stateTriggerToString_connected);
    RUN_TEST(test_stateTriggerToString_disconnected);
    RUN_TEST(test_stateTriggerToString_startSession);
    RUN_TEST(test_stateTriggerToString_stopSession);
    RUN_TEST(test_stateTriggerToString_emergencyStop);

    // Boot Result Tests
    RUN_TEST(test_isBootSuccess_failed_returns_false);
    RUN_TEST(test_isBootSuccess_noPhone_returns_true);
    RUN_TEST(test_isBootSuccess_withPhone_returns_true);
    RUN_TEST(test_isBootSuccess_success_returns_true);

    // Sync Command Type String Tests
    RUN_TEST(test_syncCommandTypeToString_startSession);
    RUN_TEST(test_syncCommandTypeToString_executeBuzz);
    RUN_TEST(test_syncCommandTypeToString_heartbeat);
    RUN_TEST(test_syncCommandTypeToString_deactivate);

    // RGB Color Tests
    RUN_TEST(test_RGBColor_default_constructor);
    RUN_TEST(test_RGBColor_parameterized_constructor);
    RUN_TEST(test_RGBColor_equality_same_colors);
    RUN_TEST(test_RGBColor_equality_different_colors);
    RUN_TEST(test_RGBColor_inequality_different_colors);
    RUN_TEST(test_RGBColor_inequality_same_colors);
    RUN_TEST(test_RGBColor_predefined_red);
    RUN_TEST(test_RGBColor_predefined_green);
    RUN_TEST(test_RGBColor_predefined_blue);

    // Battery Status Tests
    RUN_TEST(test_BatteryStatus_default_constructor);
    RUN_TEST(test_BatteryStatus_statusString_critical);
    RUN_TEST(test_BatteryStatus_statusString_low);
    RUN_TEST(test_BatteryStatus_statusString_ok);
    RUN_TEST(test_BatteryStatus_isOk_when_ok);
    RUN_TEST(test_BatteryStatus_isOk_when_low);
    RUN_TEST(test_BatteryStatus_isOk_when_critical);
    RUN_TEST(test_BatteryStatus_requiresAction_when_ok);
    RUN_TEST(test_BatteryStatus_requiresAction_when_low);
    RUN_TEST(test_BatteryStatus_requiresAction_when_critical);

    // Device Config Tests
    RUN_TEST(test_DeviceConfig_default_constructor);
    RUN_TEST(test_DeviceConfig_isPrimary_when_primary);
    RUN_TEST(test_DeviceConfig_isSecondary_when_secondary);

    // Connection State Tests
    RUN_TEST(test_ConnectionState_default_constructor);
    RUN_TEST(test_ConnectionState_isPhoneConnected_when_connected);
    RUN_TEST(test_ConnectionState_isPhoneConnected_when_disconnected);
    RUN_TEST(test_ConnectionState_isSecondaryConnected_when_connected);
    RUN_TEST(test_ConnectionState_isSecondaryConnected_when_disconnected);
    RUN_TEST(test_ConnectionState_isPrimaryConnected_when_connected);
    RUN_TEST(test_ConnectionState_isPrimaryConnected_when_disconnected);
    RUN_TEST(test_ConnectionState_clearPhone);
    RUN_TEST(test_ConnectionState_clearSecondary);
    RUN_TEST(test_ConnectionState_clearPrimary);
    RUN_TEST(test_ConnectionState_clearAll);

    return UNITY_END();
}
