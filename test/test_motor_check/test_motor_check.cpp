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
    // MAX_ACTUATORS x "NODRIVER" needs 9 bytes a token (comma or NUL)
    MotorCheck checks[MAX_ACTUATORS];
    for (auto& c : checks) c = MotorCheck::NODRIVER;
    char out[MOTOR_CHECKS_BUFFER_SIZE];
    TEST_ASSERT_TRUE(formatMotorChecks(checks, MAX_ACTUATORS, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(MAX_ACTUATORS * 9 - 1, strlen(out));
    MotorCheck parsed[MAX_ACTUATORS];
    TEST_ASSERT_TRUE(parseMotorChecks(out, parsed, MAX_ACTUATORS));
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        TEST_ASSERT_EQUAL(static_cast<uint8_t>(MotorCheck::NODRIVER), static_cast<uint8_t>(parsed[i]));
    }
}

void test_format_worst_case_exact_boundary(void) {
    constexpr size_t EXACT = MAX_ACTUATORS * 9;
    MotorCheck checks[MAX_ACTUATORS];
    for (auto& c : checks) c = MotorCheck::NODRIVER;
    char exact[EXACT];
    TEST_ASSERT_TRUE(formatMotorChecks(checks, MAX_ACTUATORS, exact, sizeof(exact)));
    char oneShort[EXACT - 1];
    TEST_ASSERT_FALSE(formatMotorChecks(checks, MAX_ACTUATORS, oneShort, sizeof(oneShort)));
    TEST_ASSERT_TRUE(memchr(oneShort, '\0', sizeof(oneShort)) != nullptr);
}

void test_format_boundary_needs_room_for_nul(void) {
    // "OK,OPEN" is 7 characters plus the NUL
    const MotorCheck checks[] = {MotorCheck::OK, MotorCheck::OPEN};
    char tooShort[7];
    memset(tooShort, 'x', sizeof(tooShort));
    TEST_ASSERT_FALSE(formatMotorChecks(checks, 2, tooShort, sizeof(tooShort)));
    TEST_ASSERT_TRUE(memchr(tooShort, '\0', sizeof(tooShort)) != nullptr);
    char fits[8];
    TEST_ASSERT_TRUE(formatMotorChecks(checks, 2, fits, sizeof(fits)));
    TEST_ASSERT_EQUAL_STRING("OK,OPEN", fits);
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

void test_parse_partial_and_empty_tokens_fail(void) {
    MotorCheck parsed[4];
    TEST_ASSERT_FALSE(parseMotorChecks("OK,OPE,OK,OK", parsed, 4));     // truncated token
    TEST_ASSERT_FALSE(parseMotorChecks("NODRIV,OK,OK,OK", parsed, 4));
    TEST_ASSERT_FALSE(parseMotorChecks(",OK,OK,OK", parsed, 4));        // empty first token
    TEST_ASSERT_FALSE(parseMotorChecks("OK,,OK,OK", parsed, 4));        // empty middle token
}

void test_parse_zero_count_fails(void) {
    MotorCheck parsed[1];
    TEST_ASSERT_FALSE(parseMotorChecks("OK", parsed, 0));
}

void test_parse_empty_and_null_fail(void) {
    MotorCheck parsed[4];
    TEST_ASSERT_FALSE(parseMotorChecks("", parsed, 4));
    TEST_ASSERT_FALSE(parseMotorChecks(nullptr, parsed, 4));
    TEST_ASSERT_FALSE(parseMotorChecks("OK,OK,OK,OK", nullptr, 4));
}

static const MotorCheck ALL_CHECKS[] = {MotorCheck::OK, MotorCheck::OPEN,
                                        MotorCheck::NODRIVER, MotorCheck::UNKNOWN};

// Index to pinky warn on OPEN or NODRIVER, never on OK or UNKNOWN
void test_problem_base_fingers(void) {
    for (uint8_t f = 0; f < MOTOR_CHECK_BASE_FINGERS; f++) {
        for (MotorCheck c : ALL_CHECKS) {
            const bool expected = (c == MotorCheck::OPEN || c == MotorCheck::NODRIVER);
            TEST_ASSERT_EQUAL_MESSAGE(expected, isMotorCheckProblem(f, c), motorCheckToken(c));
        }
    }
}

// The thumb warns only on NODRIVER: an empty thumb port is normal
void test_problem_thumb(void) {
    TEST_ASSERT_FALSE(isMotorCheckProblem(4, MotorCheck::OK));
    TEST_ASSERT_FALSE(isMotorCheckProblem(4, MotorCheck::OPEN));
    TEST_ASSERT_TRUE(isMotorCheckProblem(4, MotorCheck::NODRIVER));
    TEST_ASSERT_FALSE(isMotorCheckProblem(4, MotorCheck::UNKNOWN));
}

void test_count_problems(void) {
    // 4-motor v3 glove: empty thumb port, no warning
    const MotorCheck fourMotor[] = {MotorCheck::OK, MotorCheck::OK, MotorCheck::OK,
                                    MotorCheck::OK, MotorCheck::OPEN};
    TEST_ASSERT_EQUAL(0, countMotorCheckProblems(fourMotor, 5));
    // Supply dip: every finger UNKNOWN, no warning
    const MotorCheck dipped[] = {MotorCheck::UNKNOWN, MotorCheck::UNKNOWN, MotorCheck::UNKNOWN,
                                 MotorCheck::UNKNOWN, MotorCheck::UNKNOWN};
    TEST_ASSERT_EQUAL(0, countMotorCheckProblems(dipped, 5));
    const MotorCheck mixed[] = {MotorCheck::OK, MotorCheck::OK, MotorCheck::OPEN,
                                MotorCheck::NODRIVER, MotorCheck::NODRIVER};
    TEST_ASSERT_EQUAL(3, countMotorCheckProblems(mixed, 5));
    TEST_ASSERT_EQUAL(1, countMotorCheckProblems(mixed, 3));
    TEST_ASSERT_EQUAL(0, countMotorCheckProblems(nullptr, 5));
}

#if defined(BOARD_BLUEBUZZAH_NRF52)

// v2 has no thumb: always index to pinky
void test_therapyFingerCount_v2_isAlwaysFour(void) {
    TEST_ASSERT_EQUAL(4, therapyFingerCount(false));
    TEST_ASSERT_EQUAL(4, therapyFingerCount(true));
}

#else

// v3: index to pinky, plus the thumb only when the check found it
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
    RUN_TEST(test_format_worst_case_exact_boundary);
    RUN_TEST(test_format_boundary_needs_room_for_nul);
    RUN_TEST(test_format_round_trips_through_parse);
    RUN_TEST(test_format_short_buffer_fails_and_stays_terminated);
    RUN_TEST(test_format_rejects_null_and_zero_length);
    RUN_TEST(test_parse_valid_list);
    RUN_TEST(test_parse_wrong_count_fails);
    RUN_TEST(test_parse_unknown_token_fails);
    RUN_TEST(test_parse_partial_and_empty_tokens_fail);
    RUN_TEST(test_parse_zero_count_fails);
    RUN_TEST(test_parse_empty_and_null_fail);
    RUN_TEST(test_problem_base_fingers);
    RUN_TEST(test_problem_thumb);
    RUN_TEST(test_count_problems);
#if defined(BOARD_BLUEBUZZAH_NRF52)
    RUN_TEST(test_therapyFingerCount_v2_isAlwaysFour);
#else
    RUN_TEST(test_therapyFingerCount_v3_addsThumbWhenPresent);
#endif
    return UNITY_END();
}
