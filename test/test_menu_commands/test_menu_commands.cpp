/**
 * @file test_menu_commands.cpp
 * @brief Command-level tests against the real MenuController
 */

#include <unity.h>
#include "menu_harness.h"

void setUp(void) {
    fsb::mock::reset();
    mockResetTime();
    resetMicrosOverflow();
}

void tearDown(void) {}

void test_ping(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING("CMD:PING\nPONG:\n\x04", h.send("PING").c_str());
}

void test_unknown_command(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING("CMD:BOGUS\nERROR:Unknown command: BOGUS\n\x04", h.send("BOGUS").c_str());
    TEST_ASSERT_FALSE(h.lastHandled);
}

void test_command_is_case_insensitive(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING("CMD:PING\nPONG:\n\x04", h.send("ping").c_str());
}

void test_internal_message_ignored_without_phone_flag(void) {
    MenuHarness h;
    MockSerial::clearCaptured();
    h.menu.handleCommand("PING");
    TEST_ASSERT_EQUAL_STRING("", h.reply.c_str());
    TEST_ASSERT_EQUAL_STRING("", MockSerial::captured().c_str());
}

void test_reply_is_logged_with_menu_tx_marker(void) {
    MenuHarness h;
    h.send("PING");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, MockSerial::captured().find("[MENU-TX]"));
}

// Profiles and parameters

void test_profile_list(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING(
        "CMD:PROFILE_LIST\nPROFILE:1:regular_vcr\nPROFILE:2:noisy_vcr\nPROFILE:3:hybrid_vcr\n"
        "PROFILE:4:custom_vcr\nPROFILE:5:gentle\nPROFILE:6:quick_test\n\x04",
        h.send("PROFILE_LIST").c_str());
}

void test_profile_get_default(void) {
    MenuHarness h;
    const std::string expected =
        "CMD:PROFILE_GET\nTYPE:LRA\nFREQ:250\nON:100.0\nOFF:67.0\nSESSION:120\nAMPMIN:100\nAMPMAX:100\n"
        "PATTERN:rndp\nMIRROR:0\nJITTER:0.0\nFINGERS:" + std::to_string(MAX_ACTUATORS) + "\n\x04";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), h.send("PROFILE_GET").c_str());
}

void test_profile_load_custom(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING("CMD:PROFILE_LOAD\nSTATUS:REBOOTING\nPROFILE:custom_vcr\n\x04",
                             h.send("PROFILE_LOAD:4").c_str());
    TEST_ASSERT_EQUAL_INT(1, h.restartCalls);
}

void test_profile_load_persists(void) {
    MenuHarness h;
    h.send("PROFILE_LOAD:2");
    ProfileManager reloaded;
    reloaded.begin(true);
    TEST_ASSERT_EQUAL_UINT8(2, reloaded.getCurrentProfileId());
}

void test_profile_load_missing_id(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING("CMD:PROFILE_LOAD\nERROR:Profile ID required\n\x04", h.send("PROFILE_LOAD").c_str());
    TEST_ASSERT_EQUAL_INT(0, h.restartCalls);
}

void test_profile_load_out_of_range(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING("CMD:PROFILE_LOAD\nERROR:Invalid profile ID\n\x04", h.send("PROFILE_LOAD:7").c_str());
    TEST_ASSERT_EQUAL_STRING("CMD:PROFILE_LOAD\nERROR:Invalid profile ID\n\x04", h.send("PROFILE_LOAD:0").c_str());
    TEST_ASSERT_EQUAL_INT(0, h.restartCalls);
}

void test_profile_load_refused_during_session(void) {
    MenuHarness h;
    h.stateMachine.forceState(TherapyState::RUNNING);
    TEST_ASSERT_EQUAL_STRING("CMD:PROFILE_LOAD\nERROR:Session must be stopped before loading a profile\n\x04",
                             h.send("PROFILE_LOAD:2").c_str());
    TEST_ASSERT_EQUAL_INT(0, h.restartCalls);
}

void test_profile_custom_batch_applies(void) {
    MenuHarness h;
    h.send("PROFILE_LOAD:4");
    TEST_ASSERT_EQUAL_STRING("CMD:PROFILE_CUSTOM\nSTATUS:CUSTOM_LOADED\n\x04",
                             h.send("PROFILE_CUSTOM:ON:80:OFF:53").c_str());
    const std::string expected =
        "CMD:PROFILE_GET\nTYPE:LRA\nFREQ:250\nON:80.0\nOFF:53.0\nSESSION:120\nAMPMIN:100\nAMPMAX:100\n"
        "PATTERN:rndp\nMIRROR:0\nJITTER:0.0\nFINGERS:" + std::to_string(MAX_ACTUATORS) + "\n\x04";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), h.send("PROFILE_GET").c_str());
}

void test_profile_custom_bad_value_rejects_whole_batch(void) {
    MenuHarness h;
    h.send("PROFILE_LOAD:4");
    const std::string before = h.send("PROFILE_GET");
    TEST_ASSERT_EQUAL_STRING("CMD:PROFILE_CUSTOM\nERROR:Invalid parameter: ON\n\x04",
                             h.send("PROFILE_CUSTOM:ON:5:OFF:53").c_str());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), h.send("PROFILE_GET").c_str());
}

void test_profile_custom_locked_key_rejects_whole_batch(void) {
    MenuHarness h;
    h.send("PROFILE_LOAD:4");
    const std::string before = h.send("PROFILE_GET");
    TEST_ASSERT_EQUAL_STRING("CMD:PROFILE_CUSTOM\nERROR:Parameter FREQ is not adjustable on the Custom profile\n\x04",
                             h.send("PROFILE_CUSTOM:ON:80:FREQ:200").c_str());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), h.send("PROFILE_GET").c_str());
}

void test_param_set_on_custom(void) {
    MenuHarness h;
    h.send("PROFILE_LOAD:4");
    TEST_ASSERT_EQUAL_STRING("CMD:PARAM_SET\nPARAM:ON\nVALUE:80\n\x04", h.send("PARAM_SET:ON:80").c_str());
}

void test_param_set_rejects_nan(void) {
    MenuHarness h;
    h.send("PROFILE_LOAD:4");
    TEST_ASSERT_EQUAL_STRING("CMD:PARAM_SET\nERROR:Invalid parameter name or value out of range\n\x04",
                             h.send("PARAM_SET:ON:nan").c_str());
}

void test_param_set_requires_custom_loaded(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING("CMD:PARAM_SET\nERROR:Custom profile must be loaded before editing parameters\n\x04",
                             h.send("PARAM_SET:ON:80").c_str());
}

void test_param_set_refused_during_session(void) {
    MenuHarness h;
    h.send("PROFILE_LOAD:4");
    h.ble.secondaryConnected = true;
    h.send("SESSION_START");
    TEST_ASSERT_EQUAL_STRING("CMD:PARAM_SET\nERROR:Cannot modify parameters during active session\n\x04",
                             h.send("PARAM_SET:ON:80").c_str());
}

void test_param_set_missing_value(void) {
    MenuHarness h;
    h.send("PROFILE_LOAD:4");
    TEST_ASSERT_EQUAL_STRING("CMD:PARAM_SET\nERROR:Parameter name and value required\n\x04",
                             h.send("PARAM_SET:ON").c_str());
}

void test_param_set_locked_param(void) {
    MenuHarness h;
    h.send("PROFILE_LOAD:4");
    TEST_ASSERT_EQUAL_STRING("CMD:PARAM_SET\nERROR:Parameter FREQ is not adjustable on the Custom profile\n\x04",
                             h.send("PARAM_SET:FREQ:200").c_str());
}

void test_param_set_reports_save_failure(void) {
    MenuHarness h;
    h.send("PROFILE_LOAD:4");
    fsb::mock::setWriteResult(false);
    TEST_ASSERT_EQUAL_STRING("CMD:PARAM_SET\nERROR:Parameter applied but could not be saved\n\x04",
                             h.send("PARAM_SET:ON:80").c_str());
}

// Sessions

static void startSession(MenuHarness& h) {
    h.ble.secondaryConnected = true;
    h.send("SESSION_START");
}

void test_session_start_success(void) {
    MenuHarness h;
    h.ble.secondaryConnected = true;
    const std::string reply = h.send("SESSION_START");
    const std::string expected = "CMD:SESSION_START\nSESSION_STATUS:RUNNING\nELAPSED:0\nTOTAL:7200\nSESSION_ID:" +
                                 std::to_string(h.therapy.getSessionId()) + "\n\x04";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), reply.c_str());
    TEST_ASSERT_TRUE(h.therapy.getSessionId() > 0);
    TEST_ASSERT_EQUAL_INT(0, h.ble.lastToSecondary.rfind("START_SESSION", 0));
    TEST_ASSERT_EQUAL_STRING("RUNNING", therapyStateToString(h.stateMachine.getCurrentState()));
}

void test_session_start_refused_glove_error(void) {
    MenuHarness h;
    h.ble.secondaryConnected = true;
    h.gloveError = "Primary glove error";
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_START\nERROR:Primary glove error\n\x04", h.send("SESSION_START").c_str());
    TEST_ASSERT_FALSE(h.therapy.isRunning());
}

void test_session_start_refused_battery(void) {
    MenuHarness h;
    h.ble.secondaryConnected = true;
    h.batteryAllows = false;
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_START\nERROR:Battery too low\n\x04", h.send("SESSION_START").c_str());
    TEST_ASSERT_FALSE(h.therapy.isRunning());
}

void test_session_start_refused_no_secondary(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_START\nERROR:Secondary not connected\n\x04", h.send("SESSION_START").c_str());
    TEST_ASSERT_FALSE(h.therapy.isRunning());
}

void test_session_start_refused_sync_not_ready(void) {
    MenuHarness h;
    h.ble.secondaryConnected = true;
    h.syncReady = false;
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_START\nERROR:Sync not ready\n\x04", h.send("SESSION_START").c_str());
    TEST_ASSERT_FALSE(h.therapy.isRunning());
}

void test_session_start_refused_when_running(void) {
    MenuHarness h;
    startSession(h);
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_START\nERROR:Session already active\n\x04", h.send("SESSION_START").c_str());
}

void test_session_start_refusal_order(void) {
    MenuHarness h;
    h.ble.secondaryConnected = true;
    h.gloveError = "Primary glove error";
    h.batteryAllows = false;
    h.syncReady = false;
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_START\nERROR:Primary glove error\n\x04", h.send("SESSION_START").c_str());
}

void test_session_start_secondary_checked_before_battery(void) {
    MenuHarness h;
    h.batteryAllows = false;
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_START\nERROR:Secondary not connected\n\x04", h.send("SESSION_START").c_str());
}

void test_session_pause_resume_stop_from_running(void) {
    MenuHarness h;
    startSession(h);
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_PAUSE\nSESSION_STATUS:PAUSED\n\x04", h.send("SESSION_PAUSE").c_str());
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_RESUME\nSESSION_STATUS:RUNNING\n\x04", h.send("SESSION_RESUME").c_str());
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_STOP\nSESSION_STATUS:IDLE\n\x04", h.send("SESSION_STOP").c_str());
    TEST_ASSERT_EQUAL_INT(1, h.sessionStopCalls);
}

void test_session_pause_refused_when_idle(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_PAUSE\nERROR:No active session\n\x04", h.send("SESSION_PAUSE").c_str());
}

void test_session_resume_refused_when_not_paused(void) {
    MenuHarness h;
    startSession(h);
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_RESUME\nERROR:Session is not paused\n\x04", h.send("SESSION_RESUME").c_str());
}

void test_session_status_states(void) {
    MenuHarness h;
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_STATUS\nSESSION_STATUS:IDLE\nELAPSED:0\nTOTAL:0\nPROGRESS:0\n\x04",
                             h.send("SESSION_STATUS").c_str());
    h.stateMachine.forceState(TherapyState::RUNNING);
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_STATUS\nSESSION_STATUS:RUNNING\nELAPSED:0\nTOTAL:0\nPROGRESS:0\n\x04",
                             h.send("SESSION_STATUS").c_str());
    h.stateMachine.forceState(TherapyState::PAUSED);
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_STATUS\nSESSION_STATUS:PAUSED\nELAPSED:0\nTOTAL:0\nPROGRESS:0\n\x04",
                             h.send("SESSION_STATUS").c_str());
    h.stateMachine.forceState(TherapyState::LOW_BATTERY);
    TEST_ASSERT_EQUAL_STRING("CMD:SESSION_STATUS\nSESSION_STATUS:LOW_BATTERY\nELAPSED:0\nTOTAL:0\nPROGRESS:0\n\x04",
                             h.send("SESSION_STATUS").c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ping);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_command_is_case_insensitive);
    RUN_TEST(test_internal_message_ignored_without_phone_flag);
    RUN_TEST(test_reply_is_logged_with_menu_tx_marker);
    // Profiles and parameters
    RUN_TEST(test_profile_list);
    RUN_TEST(test_profile_get_default);
    RUN_TEST(test_profile_load_custom);
    RUN_TEST(test_profile_load_persists);
    RUN_TEST(test_profile_load_missing_id);
    RUN_TEST(test_profile_load_out_of_range);
    RUN_TEST(test_profile_load_refused_during_session);
    RUN_TEST(test_profile_custom_batch_applies);
    RUN_TEST(test_profile_custom_bad_value_rejects_whole_batch);
    RUN_TEST(test_profile_custom_locked_key_rejects_whole_batch);
    RUN_TEST(test_param_set_on_custom);
    RUN_TEST(test_param_set_rejects_nan);
    RUN_TEST(test_param_set_requires_custom_loaded);
    RUN_TEST(test_param_set_refused_during_session);
    RUN_TEST(test_param_set_missing_value);
    RUN_TEST(test_param_set_locked_param);
    RUN_TEST(test_param_set_reports_save_failure);
    // Sessions
    RUN_TEST(test_session_start_success);
    RUN_TEST(test_session_start_refused_glove_error);
    RUN_TEST(test_session_start_refused_battery);
    RUN_TEST(test_session_start_refused_no_secondary);
    RUN_TEST(test_session_start_refused_sync_not_ready);
    RUN_TEST(test_session_start_refused_when_running);
    RUN_TEST(test_session_start_refusal_order);
    RUN_TEST(test_session_start_secondary_checked_before_battery);
    RUN_TEST(test_session_pause_resume_stop_from_running);
    RUN_TEST(test_session_pause_refused_when_idle);
    RUN_TEST(test_session_resume_refused_when_not_paused);
    RUN_TEST(test_session_status_states);
    return UNITY_END();
}
