/**
 * @file test_motor_check_info.cpp
 * @brief Unit tests for addMotorCheckInfoLines (INFO MOTP/MOTS emission)
 */

#include <unity.h>
#include <string>
#include "motor_check_info.h"

void setUp(void) {}
void tearDown(void) {}

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


    RUN_TEST(test_motorCheckInfo_emits_MOTP_then_MOTS_when_linked);
    RUN_TEST(test_motorCheckInfo_omits_MOTS_when_unlinked);
    RUN_TEST(test_motorCheckInfo_omits_MOTS_without_secondary_report);
    RUN_TEST(test_motorCheckInfo_omits_MOTP_without_own_report);
    RUN_TEST(test_motorCheckInfo_omits_both_without_callback);

    return UNITY_END();
}
