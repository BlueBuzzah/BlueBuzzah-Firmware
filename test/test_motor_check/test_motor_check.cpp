/**
 * @file test_motor_check.cpp
 * @brief Boot motor check tokens: text, wire format and therapy finger count
 *        (motor_check.h)
 */

#include <unity.h>
#include "motor_check.h"

void setUp(void) {}
void tearDown(void) {}

void test_token_text(void) {
    TEST_ASSERT_EQUAL_STRING("OK", motorCheckToken(MotorCheck::OK));
    TEST_ASSERT_EQUAL_STRING("OPEN", motorCheckToken(MotorCheck::OPEN));
    TEST_ASSERT_EQUAL_STRING("NODRIVER", motorCheckToken(MotorCheck::NODRIVER));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", motorCheckToken(MotorCheck::UNKNOWN));
}

void test_format_joins_tokens_with_commas(void) {
    const MotorCheck checks[] = {MotorCheck::OK, MotorCheck::OK, MotorCheck::OPEN,
                                 MotorCheck::NODRIVER, MotorCheck::UNKNOWN};
    char out[64];
    TEST_ASSERT_TRUE(formatMotorChecks(checks, 5, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("OK,OK,OPEN,NODRIVER,UNKNOWN", out);
}

void test_format_worst_case_fits_buffer_size(void) {
    MotorCheck checks[5];
    for (auto& c : checks) c = MotorCheck::NODRIVER;
    char out[MOTOR_CHECKS_BUFFER_SIZE];
    TEST_ASSERT_TRUE(formatMotorChecks(checks, 5, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("NODRIVER,NODRIVER,NODRIVER,NODRIVER,NODRIVER", out);
}

void test_format_round_trips_through_parse(void) {
    const MotorCheck checks[] = {MotorCheck::UNKNOWN, MotorCheck::OK,
                                 MotorCheck::NODRIVER, MotorCheck::OPEN};
    char out[64];
    TEST_ASSERT_TRUE(formatMotorChecks(checks, 4, out, sizeof(out)));
    MotorCheck parsed[4];
    TEST_ASSERT_TRUE(parseMotorChecks(out, parsed, 4));
    for (uint8_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL(static_cast<uint8_t>(checks[i]), static_cast<uint8_t>(parsed[i]));
    }
}

void test_format_short_buffer_fails_and_stays_terminated(void) {
    const MotorCheck checks[] = {MotorCheck::OK, MotorCheck::OPEN, MotorCheck::OPEN};
    char out[8];
    memset(out, 'x', sizeof(out));
    TEST_ASSERT_FALSE(formatMotorChecks(checks, 3, out, sizeof(out)));
    TEST_ASSERT_TRUE(memchr(out, '\0', sizeof(out)) != nullptr);
}

void test_format_rejects_null_and_zero_length(void) {
    const MotorCheck checks[] = {MotorCheck::OK};
    char out[8];
    TEST_ASSERT_FALSE(formatMotorChecks(checks, 1, nullptr, sizeof(out)));
    TEST_ASSERT_FALSE(formatMotorChecks(checks, 1, out, 0));
    TEST_ASSERT_FALSE(formatMotorChecks(nullptr, 1, out, sizeof(out)));
}

void test_parse_valid_list(void) {
    MotorCheck parsed[5];
    TEST_ASSERT_TRUE(parseMotorChecks("OK,OK,OPEN,OPEN,NODRIVER", parsed, 5));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(MotorCheck::OK), static_cast<uint8_t>(parsed[0]));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(MotorCheck::OK), static_cast<uint8_t>(parsed[1]));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(MotorCheck::OPEN), static_cast<uint8_t>(parsed[2]));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(MotorCheck::OPEN), static_cast<uint8_t>(parsed[3]));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(MotorCheck::NODRIVER), static_cast<uint8_t>(parsed[4]));
}

void test_parse_wrong_count_fails(void) {
    MotorCheck parsed[5];
    TEST_ASSERT_FALSE(parseMotorChecks("OK,OK,OK,OK", parsed, 5));     // too few
    TEST_ASSERT_FALSE(parseMotorChecks("OK,OK,OK,OK,OK,OK", parsed, 5));  // too many
    TEST_ASSERT_FALSE(parseMotorChecks("OK,OK,OK,OK,", parsed, 5));    // empty last token
}

void test_parse_unknown_token_fails(void) {
    MotorCheck parsed[4];
    TEST_ASSERT_FALSE(parseMotorChecks("OK,OK,BROKEN,OK", parsed, 4));
    TEST_ASSERT_FALSE(parseMotorChecks("OK,OK,OKAY,OK", parsed, 4));   // prefix of a token isn't it
    TEST_ASSERT_FALSE(parseMotorChecks("OK,OK,ok,OK", parsed, 4));     // tokens are upper case
}

void test_parse_empty_and_null_fail(void) {
    MotorCheck parsed[4];
    TEST_ASSERT_FALSE(parseMotorChecks("", parsed, 4));
    TEST_ASSERT_FALSE(parseMotorChecks(nullptr, parsed, 4));
    TEST_ASSERT_FALSE(parseMotorChecks("OK,OK,OK,OK", nullptr, 4));
}

#if defined(BOARD_BLUEBUZZAH_NRF52)

// v2 has no thumb: always index to little
void test_therapyFingerCount_v2_isAlwaysFour(void) {
    TEST_ASSERT_EQUAL(4, therapyFingerCount(false));
    TEST_ASSERT_EQUAL(4, therapyFingerCount(true));
}

#else

// v3: index to little, plus the thumb only when the check found it
void test_therapyFingerCount_v3_addsThumbWhenPresent(void) {
    TEST_ASSERT_EQUAL(4, therapyFingerCount(false));
    TEST_ASSERT_EQUAL(5, therapyFingerCount(true));
}

#endif

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_token_text);
    RUN_TEST(test_format_joins_tokens_with_commas);
    RUN_TEST(test_format_worst_case_fits_buffer_size);
    RUN_TEST(test_format_round_trips_through_parse);
    RUN_TEST(test_format_short_buffer_fails_and_stays_terminated);
    RUN_TEST(test_format_rejects_null_and_zero_length);
    RUN_TEST(test_parse_valid_list);
    RUN_TEST(test_parse_wrong_count_fails);
    RUN_TEST(test_parse_unknown_token_fails);
    RUN_TEST(test_parse_empty_and_null_fail);
#if defined(BOARD_BLUEBUZZAH_NRF52)
    RUN_TEST(test_therapyFingerCount_v2_isAlwaysFour);
#else
    RUN_TEST(test_therapyFingerCount_v3_addsThumbWhenPresent);
#endif
    return UNITY_END();
}
