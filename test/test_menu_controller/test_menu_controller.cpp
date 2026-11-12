/**
 * @file test_menu_controller.cpp
 * @brief Guards for the menu's real shared headers
 *
 * - internal_messages.h: INTERNAL_MESSAGES[] prefix routing. Command
 *   behaviour on the real MenuController is in test_menu_commands.
 * - motor_check_info.h: INFO MOTP/MOTS emission
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <string>
#include "internal_messages.h"
#include "motor_check_info.h"

void setUp(void) {}
void tearDown(void) {}

// Same prefix match MenuController::isInternalMessage applies to INTERNAL_MESSAGES[]
static bool isInternalMessage(const char* message) {
    if (!message || strlen(message) == 0) {
        return false;
    }
    for (uint8_t i = 0; i < INTERNAL_MESSAGE_COUNT; i++) {
        if (strncmp(message, INTERNAL_MESSAGES[i], strlen(INTERNAL_MESSAGES[i])) == 0) {
            return true;
        }
    }
    return false;
}

// =============================================================================
// INTERNAL MESSAGE TESTS
// =============================================================================

void test_isInternalMessage_null_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage(nullptr));
}

void test_isInternalMessage_empty_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage(""));
}

void test_isInternalMessage_PARAM_UPDATE_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("PARAM_UPDATE:INTENSITY:75"));
}

void test_isInternalMessage_SEED_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("SEED:12345"));
}

void test_isInternalMessage_SEED_ACK_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("SEED_ACK:12345"));
}

void test_isInternalMessage_GET_BATTERY_returns_true() {
    TEST_ASSERT_TRUE(isInternalMessage("GET_BATTERY"));
}

void test_isInternalMessage_BATRESPONSE_returns_true() {
    TEST_ASSERT_TRUE(isInternalMessage("BATRESPONSE:3.72:85"));
}

void test_isInternalMessage_GLOVE_ERROR_returns_true() {
    // The SECONDARY's fault report must never reach the menu as a phone command
    TEST_ASSERT_TRUE(isInternalMessage("GLOVE_ERROR"));
}

void test_isInternalMessage_MOTOR_STATUS_returns_true() {
    // The SECONDARY's motor check report must never reach the menu as a phone command
    TEST_ASSERT_TRUE(isInternalMessage("MOTOR_STATUS:OK,OK,OPEN,OPEN,OPEN"));
}

void test_isInternalMessage_ACK_PARAM_UPDATE_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("ACK_PARAM_UPDATE:INTENSITY"));
}

void test_isInternalMessage_SYNC_prefix_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("SYNC_ADJ:12345:67890"));
}

void test_isInternalMessage_IDENTIFY_prefix_returns_true() {
    TEST_ASSERT_TRUE(isInternalMessage("IDENTIFY:PRIMARY"));
}

void test_isInternalMessage_user_command_INFO_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("INFO"));
}

void test_isInternalMessage_user_command_BATTERY_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("BATTERY"));
}

void test_isInternalMessage_user_command_SESSION_START_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("SESSION_START"));
}

// The Updater reaches the firmware menu over USB serial precisely because these
// commands are NOT internal — handleSerialCommand falls through to
// onBLEMessage(0, ...) with fromPhone == false, and main.cpp's role gate then
// requires !menu.isInternalMessage(message). Adding any of these to
// INTERNAL_MESSAGES silently severs the desktop Updater from the device.
void test_isInternalMessage_updater_command_PROFILE_LIST_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("PROFILE_LIST"));
}

void test_isInternalMessage_updater_command_PROFILE_LOAD_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("PROFILE_LOAD:4"));
}

void test_isInternalMessage_updater_command_PROFILE_GET_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("PROFILE_GET"));
}

void test_isInternalMessage_updater_command_PROFILE_CUSTOM_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("PROFILE_CUSTOM:ON:100:OFF:67"));
}

void test_isInternalMessage_updater_command_PARAM_SET_returns_false() {
    TEST_ASSERT_FALSE(isInternalMessage("PARAM_SET:FINGERS:4"));
}

void test_isInternalMessage_PING_returns_true() {
    TEST_ASSERT_TRUE(isInternalMessage("PING"));
}

void test_isInternalMessage_partial_match_not_prefix_returns_false() {
    // "BUZ" doesn't match "BUZZ" prefix
    TEST_ASSERT_FALSE(isInternalMessage("BUZ"));
}

void test_isInternalMessage_case_sensitive() {
    // Internal messages are uppercase, lowercase should not match
    TEST_ASSERT_FALSE(isInternalMessage("buzz"));
    TEST_ASSERT_FALSE(isInternalMessage("Buzz"));
}

// =============================================================================
// MOTOR CHECK INFO TESTS
// =============================================================================

static const char* g_ownChecks = nullptr;
static const char* g_secondaryChecks = nullptr;
static const char* motorCheck(bool secondary) {
    return secondary ? g_secondaryChecks : g_ownChecks;
}

static std::string motorCheckLines(const char* (*callback)(bool), bool linked) {
    std::string out;
    addMotorCheckInfoLines(callback, linked, [&](const char* key, const char* value) {
        out += std::string(key) + ":" + value + "\n";
    });
    return out;
}

void test_motorCheckInfo_emits_MOTP_then_MOTS_when_linked() {
    g_ownChecks = "OK,OK,OPEN,OPEN,OPEN";
    g_secondaryChecks = "OK,OK,OK,OK,UNKNOWN";
    TEST_ASSERT_EQUAL_STRING("MOTP:OK,OK,OPEN,OPEN,OPEN\nMOTS:OK,OK,OK,OK,UNKNOWN\n",
                             motorCheckLines(motorCheck, true).c_str());
}

void test_motorCheckInfo_omits_MOTS_when_unlinked() {
    // The link, not just the callback, gates MOTS
    g_ownChecks = "OK,OK,OK,OK,OPEN";
    g_secondaryChecks = "OK,OK,OK,OK,OK";
    TEST_ASSERT_EQUAL_STRING("MOTP:OK,OK,OK,OK,OPEN\n", motorCheckLines(motorCheck, false).c_str());
}

void test_motorCheckInfo_omits_MOTS_without_secondary_report() {
    // SECONDARY hasn't sent MOTOR_STATUS: no key
    g_ownChecks = "OK,OK,OK,OK,OK";
    g_secondaryChecks = nullptr;
    TEST_ASSERT_EQUAL_STRING("MOTP:OK,OK,OK,OK,OK\n", motorCheckLines(motorCheck, true).c_str());
}

void test_motorCheckInfo_omits_MOTP_without_own_report() {
    g_ownChecks = nullptr;
    g_secondaryChecks = "OK,OK,OK,OK,OK";
    TEST_ASSERT_EQUAL_STRING("MOTS:OK,OK,OK,OK,OK\n", motorCheckLines(motorCheck, true).c_str());
    g_secondaryChecks = nullptr;
    TEST_ASSERT_EQUAL_STRING("", motorCheckLines(motorCheck, true).c_str());
}

void test_motorCheckInfo_omits_both_without_callback() {
    // v2 registers no callback: neither key
    TEST_ASSERT_EQUAL_STRING("", motorCheckLines(nullptr, true).c_str());
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_isInternalMessage_null_returns_false);
    RUN_TEST(test_isInternalMessage_empty_returns_false);
    RUN_TEST(test_isInternalMessage_PARAM_UPDATE_returns_false);
    RUN_TEST(test_isInternalMessage_SEED_returns_false);
    RUN_TEST(test_isInternalMessage_SEED_ACK_returns_false);
    RUN_TEST(test_isInternalMessage_GET_BATTERY_returns_true);
    RUN_TEST(test_isInternalMessage_BATRESPONSE_returns_true);
    RUN_TEST(test_isInternalMessage_GLOVE_ERROR_returns_true);
    RUN_TEST(test_isInternalMessage_MOTOR_STATUS_returns_true);
    RUN_TEST(test_isInternalMessage_ACK_PARAM_UPDATE_returns_false);
    RUN_TEST(test_isInternalMessage_SYNC_prefix_returns_false);
    RUN_TEST(test_isInternalMessage_IDENTIFY_prefix_returns_true);
    RUN_TEST(test_isInternalMessage_user_command_INFO_returns_false);
    RUN_TEST(test_isInternalMessage_user_command_BATTERY_returns_false);
    RUN_TEST(test_isInternalMessage_user_command_SESSION_START_returns_false);
    RUN_TEST(test_isInternalMessage_updater_command_PROFILE_LIST_returns_false);
    RUN_TEST(test_isInternalMessage_updater_command_PROFILE_LOAD_returns_false);
    RUN_TEST(test_isInternalMessage_updater_command_PROFILE_GET_returns_false);
    RUN_TEST(test_isInternalMessage_updater_command_PROFILE_CUSTOM_returns_false);
    RUN_TEST(test_isInternalMessage_updater_command_PARAM_SET_returns_false);
    RUN_TEST(test_isInternalMessage_PING_returns_true);
    RUN_TEST(test_isInternalMessage_partial_match_not_prefix_returns_false);
    RUN_TEST(test_isInternalMessage_case_sensitive);

    RUN_TEST(test_motorCheckInfo_emits_MOTP_then_MOTS_when_linked);
    RUN_TEST(test_motorCheckInfo_omits_MOTS_when_unlinked);
    RUN_TEST(test_motorCheckInfo_omits_MOTS_without_secondary_report);
    RUN_TEST(test_motorCheckInfo_omits_MOTP_without_own_report);
    RUN_TEST(test_motorCheckInfo_omits_both_without_callback);

    return UNITY_END();
}
