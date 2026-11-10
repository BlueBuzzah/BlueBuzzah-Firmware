/**
 * @file test_board_config.cpp
 * @brief Pins each board's finger-to-mux-channel wiring (board_config.h)
 */

#include <unity.h>
#include <fstream>
#include <sstream>
#include <string>
#include "config.h"

void setUp(void) {}
void tearDown(void) {}

#if defined(BOARD_BLUEBUZZAH_NRF52)

// v2 PCB: channel 0 drives the pinky pads, channel 3 the index pads
void test_fingerChannel_v2_isReversed(void) {
    TEST_ASSERT_EQUAL(3, FINGER_CHANNEL(FINGER_INDEX));
    TEST_ASSERT_EQUAL(2, FINGER_CHANNEL(FINGER_MIDDLE));
    TEST_ASSERT_EQUAL(1, FINGER_CHANNEL(FINGER_RING));
    TEST_ASSERT_EQUAL(0, FINGER_CHANNEL(FINGER_PINKY));
}

#else

// v3 PCB: mux channel 0 is the port labeled 5, channel 4 the port labeled 1.
// Reversing the channels keeps the labels in finger order: index is port 1.
void test_fingerChannel_v3_isReversed(void) {
    TEST_ASSERT_EQUAL(4, FINGER_CHANNEL(FINGER_INDEX));
    TEST_ASSERT_EQUAL(3, FINGER_CHANNEL(FINGER_MIDDLE));
    TEST_ASSERT_EQUAL(2, FINGER_CHANNEL(FINGER_RING));
    TEST_ASSERT_EQUAL(1, FINGER_CHANNEL(FINGER_PINKY));
    TEST_ASSERT_EQUAL(0, FINGER_CHANNEL(FINGER_THUMB));
}

void test_silkPort_v3_isFingerPlusOne(void) {
    TEST_ASSERT_EQUAL(1, MOTOR_SILK_PORT(FINGER_INDEX));
    TEST_ASSERT_EQUAL(2, MOTOR_SILK_PORT(FINGER_MIDDLE));
    TEST_ASSERT_EQUAL(3, MOTOR_SILK_PORT(FINGER_RING));
    TEST_ASSERT_EQUAL(4, MOTOR_SILK_PORT(FINGER_PINKY));
    TEST_ASSERT_EQUAL(5, MOTOR_SILK_PORT(FINGER_THUMB));
}

#endif

// hardware.cpp isn't built for native tests, so scan its source instead. Mux
// selection is exclusive (one channel open at a time, so two drivers at 0x5A are
// never connected together) and goes through FINGER_CHANNEL(), or fingers land on
// the wrong motors.
void test_hardware_selectsOneChannelAtATime(void) {
    std::string path = __FILE__;
    path = path.substr(0, path.rfind("test/")) + "src/hardware.cpp";
    std::ifstream file(path);
    TEST_ASSERT_TRUE_MESSAGE(file.good(), path.c_str());
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();

    // openChannel() ORs into the open mask, which can connect two drivers at the same address
    TEST_ASSERT_EQUAL(std::string::npos, source.find("_tca.openChannel("));

    const std::string call = "_tca.writeRegister(";
    size_t at = source.find(call);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, at);
    TEST_ASSERT_EQUAL_STRING("static_cast<uint8_t>(1u << FINGER_CHANNEL(",
        source.substr(at + call.size(), 42).c_str());
    TEST_ASSERT_EQUAL(std::string::npos, source.find(call, at + 1));  // selectChannel() is the only caller
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
#if defined(BOARD_BLUEBUZZAH_NRF52)
    RUN_TEST(test_fingerChannel_v2_isReversed);
#else
    RUN_TEST(test_fingerChannel_v3_isReversed);
    RUN_TEST(test_silkPort_v3_isFingerPlusOne);
#endif
    RUN_TEST(test_hardware_selectsOneChannelAtATime);
    return UNITY_END();
}
