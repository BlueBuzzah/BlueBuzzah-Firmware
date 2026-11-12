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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ping);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_command_is_case_insensitive);
    RUN_TEST(test_internal_message_ignored_without_phone_flag);
    RUN_TEST(test_reply_is_logged_with_menu_tx_marker);
    return UNITY_END();
}
