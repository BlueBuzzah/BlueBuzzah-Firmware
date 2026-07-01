/**
 * @file test_roundtrip.cpp
 * @brief Unit tests for the fs_backend abstraction (native in-memory mock)
 */

#include <unity.h>
#include "fs_backend.h"

void setUp(void) {
    fsb::mock::reset();
}

void tearDown(void) {}

void test_write_then_read(void) {
    const uint8_t data[] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(fsb::begin());
    TEST_ASSERT_TRUE(fsb::writeFile("/p.bin", data, 4));
    TEST_ASSERT_TRUE(fsb::exists("/p.bin"));
    uint8_t buf[8];
    size_t n = 0;
    TEST_ASSERT_TRUE(fsb::readFile("/p.bin", buf, 8, n));
    TEST_ASSERT_EQUAL_UINT(4, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, buf, 4);
}

void test_overwrite_replaces_content(void) {
    const uint8_t first[] = {9, 9, 9, 9, 9, 9};
    const uint8_t second[] = {7, 7};
    TEST_ASSERT_TRUE(fsb::begin());
    TEST_ASSERT_TRUE(fsb::writeFile("/p.bin", first, 6));
    TEST_ASSERT_TRUE(fsb::writeFile("/p.bin", second, 2));
    uint8_t buf[8];
    size_t n = 0;
    TEST_ASSERT_TRUE(fsb::readFile("/p.bin", buf, 8, n));
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second, buf, 2);
}

void test_remove_deletes_file(void) {
    const uint8_t data[] = {5};
    TEST_ASSERT_TRUE(fsb::begin());
    TEST_ASSERT_TRUE(fsb::writeFile("/p.bin", data, 1));
    TEST_ASSERT_TRUE(fsb::removeFile("/p.bin"));
    TEST_ASSERT_FALSE(fsb::exists("/p.bin"));
}

void test_read_missing_file_fails(void) {
    TEST_ASSERT_TRUE(fsb::begin());
    uint8_t buf[4];
    size_t n = 0;
    TEST_ASSERT_FALSE(fsb::readFile("/missing.bin", buf, 4, n));
    TEST_ASSERT_FALSE(fsb::exists("/missing.bin"));
}

void test_begin_can_fail(void) {
    fsb::mock::setBeginResult(false);
    TEST_ASSERT_FALSE(fsb::begin());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_write_then_read);
    RUN_TEST(test_overwrite_replaces_content);
    RUN_TEST(test_remove_deletes_file);
    RUN_TEST(test_read_missing_file_fails);
    RUN_TEST(test_begin_can_fail);
    return UNITY_END();
}
