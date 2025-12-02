/**
 * @file test_profile_manager.cpp
 * @brief Unit tests for ProfileManager class
 *
 * Tests:
 * - Profile initialization and built-in profiles
 * - Profile loading by ID and name
 * - Parameter validation and modification
 * - Device role management
 */

#include <unity.h>
#include <Arduino.h>
#include <cstring>

// =============================================================================
// MOCK DEFINITIONS FOR LITTLEFS
// =============================================================================

// File open modes (from Adafruit_LittleFS)
#ifndef FILE_O_READ
#define FILE_O_READ  0x01
#define FILE_O_WRITE 0x02
#endif

// Mock InternalFS before including profile_manager
namespace Adafruit_LittleFS_Namespace {

class File {
public:
    File() : _isOpen(false) {}
    File(class MockInternalFS&) : _isOpen(false) {}  // Match actual usage

    bool open(const char*, uint8_t) { return false; }
    void close() { _isOpen = false; }
    size_t read(uint8_t*, size_t) { return 0; }
    size_t write(const uint8_t*, size_t) { return 0; }
    bool seek(uint32_t) { return false; }
    void flush() {}
    operator bool() const { return _isOpen; }

private:
    bool _isOpen;
};

class MockInternalFS {
public:
    bool begin() { return false; }  // Return false to skip storage
    bool exists(const char*) { return false; }
};

}  // namespace Adafruit_LittleFS_Namespace

// Define global InternalFS before including source
Adafruit_LittleFS_Namespace::MockInternalFS InternalFS;

// Prevent including real LittleFS headers
#define _ADAFRUIT_LITTLEFS_H_
#define _INTERNAL_FILESYSTEM_H_

// Use the namespace for File class
using Adafruit_LittleFS_Namespace::File;

// Now include headers
#include "profile_manager.h"

// Include source file directly for native testing
#include "../../src/profile_manager.cpp"

// =============================================================================
// TEST FIXTURES
// =============================================================================

static ProfileManager* profiles = nullptr;

void setUp(void) {
    profiles = new ProfileManager();
    profiles->begin(false);  // Don't load from storage
}

void tearDown(void) {
    delete profiles;
    profiles = nullptr;
}

// =============================================================================
// INITIALIZATION TESTS
// =============================================================================

void test_ProfileManager_constructor_defaults(void) {
    ProfileManager pm;
    TEST_ASSERT_EQUAL(DeviceRole::PRIMARY, pm.getDeviceRole());
    TEST_ASSERT_FALSE(pm.hasStoredRole());
}

void test_ProfileManager_begin_initializes_profiles(void) {
    TEST_ASSERT_EQUAL_UINT8(4, profiles->getProfileCount());
}

void test_ProfileManager_getProfileNames_returns_valid_pointers(void) {
    uint8_t count = 0;
    const char** names = profiles->getProfileNames(&count);

    TEST_ASSERT_EQUAL_UINT8(4, count);
    TEST_ASSERT_NOT_NULL(names);
    TEST_ASSERT_NOT_NULL(names[0]);
    TEST_ASSERT_NOT_NULL(names[1]);
    TEST_ASSERT_NOT_NULL(names[2]);
    TEST_ASSERT_NOT_NULL(names[3]);
}

void test_ProfileManager_getProfileNames_returns_correct_names(void) {
    uint8_t count = 0;
    const char** names = profiles->getProfileNames(&count);

    TEST_ASSERT_EQUAL_STRING("noisy_vcr", names[0]);
    TEST_ASSERT_EQUAL_STRING("standard_vcr", names[1]);
    TEST_ASSERT_EQUAL_STRING("gentle", names[2]);
    TEST_ASSERT_EQUAL_STRING("quick_test", names[3]);
}

// =============================================================================
// LOAD PROFILE BY ID TESTS
// =============================================================================

void test_loadProfile_valid_id_1_loads_noisy_vcr(void) {
    TEST_ASSERT_TRUE(profiles->loadProfile(1));
    TEST_ASSERT_EQUAL_STRING("noisy_vcr", profiles->getCurrentProfileName());
}

void test_loadProfile_valid_id_2_loads_standard_vcr(void) {
    TEST_ASSERT_TRUE(profiles->loadProfile(2));
    TEST_ASSERT_EQUAL_STRING("standard_vcr", profiles->getCurrentProfileName());
}

void test_loadProfile_valid_id_3_loads_gentle(void) {
    TEST_ASSERT_TRUE(profiles->loadProfile(3));
    TEST_ASSERT_EQUAL_STRING("gentle", profiles->getCurrentProfileName());
}

void test_loadProfile_valid_id_4_loads_quick_test(void) {
    TEST_ASSERT_TRUE(profiles->loadProfile(4));
    TEST_ASSERT_EQUAL_STRING("quick_test", profiles->getCurrentProfileName());
}

void test_loadProfile_invalid_id_0_returns_false(void) {
    TEST_ASSERT_FALSE(profiles->loadProfile(0));
}

void test_loadProfile_invalid_id_5_returns_false(void) {
    TEST_ASSERT_FALSE(profiles->loadProfile(5));
}

void test_loadProfile_invalid_id_255_returns_false(void) {
    TEST_ASSERT_FALSE(profiles->loadProfile(255));
}

// =============================================================================
// LOAD PROFILE BY NAME TESTS
// =============================================================================

void test_loadProfileByName_exact_match(void) {
    TEST_ASSERT_TRUE(profiles->loadProfileByName("noisy_vcr"));
    TEST_ASSERT_EQUAL_STRING("noisy_vcr", profiles->getCurrentProfileName());
}

void test_loadProfileByName_case_insensitive_upper(void) {
    TEST_ASSERT_TRUE(profiles->loadProfileByName("NOISY_VCR"));
    TEST_ASSERT_EQUAL_STRING("noisy_vcr", profiles->getCurrentProfileName());
}

void test_loadProfileByName_case_insensitive_mixed(void) {
    TEST_ASSERT_TRUE(profiles->loadProfileByName("Noisy_VCR"));
    TEST_ASSERT_EQUAL_STRING("noisy_vcr", profiles->getCurrentProfileName());
}

void test_loadProfileByName_null_returns_false(void) {
    TEST_ASSERT_FALSE(profiles->loadProfileByName(nullptr));
}

void test_loadProfileByName_empty_returns_false(void) {
    TEST_ASSERT_FALSE(profiles->loadProfileByName(""));
}

void test_loadProfileByName_invalid_returns_false(void) {
    TEST_ASSERT_FALSE(profiles->loadProfileByName("nonexistent"));
}

void test_loadProfileByName_gentle(void) {
    TEST_ASSERT_TRUE(profiles->loadProfileByName("gentle"));
    TEST_ASSERT_EQUAL_STRING("gentle", profiles->getCurrentProfileName());
}

// =============================================================================
// GET CURRENT PROFILE TESTS
// =============================================================================

void test_getCurrentProfile_returns_profile_after_load(void) {
    profiles->loadProfile(1);
    const TherapyProfile* profile = profiles->getCurrentProfile();

    TEST_ASSERT_NOT_NULL(profile);
    TEST_ASSERT_EQUAL_STRING("noisy_vcr", profile->name);
    TEST_ASSERT_EQUAL(ActuatorType::LRA, profile->actuatorType);
    TEST_ASSERT_EQUAL_UINT16(175, profile->frequencyHz);
}

void test_getCurrentProfile_noisy_vcr_has_correct_defaults(void) {
    profiles->loadProfile(1);
    const TherapyProfile* p = profiles->getCurrentProfile();

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, p->timeOnMs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 67.0f, p->timeOffMs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 23.5f, p->jitterPercent);
    TEST_ASSERT_EQUAL_UINT8(50, p->amplitudeMin);
    TEST_ASSERT_EQUAL_UINT8(100, p->amplitudeMax);
    TEST_ASSERT_EQUAL_UINT16(120, p->sessionDurationMin);
    TEST_ASSERT_TRUE(p->mirrorPattern);
    TEST_ASSERT_EQUAL_UINT8(5, p->numFingers);
}

void test_getCurrentProfile_gentle_has_correct_values(void) {
    profiles->loadProfile(3);
    const TherapyProfile* p = profiles->getCurrentProfile();

    TEST_ASSERT_EQUAL_STRING("gentle", p->name);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 80.0f, p->timeOnMs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 87.0f, p->timeOffMs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 15.0f, p->jitterPercent);
    TEST_ASSERT_EQUAL_UINT8(30, p->amplitudeMin);
    TEST_ASSERT_EQUAL_UINT8(70, p->amplitudeMax);
    TEST_ASSERT_EQUAL_STRING("sequential", p->patternType);
}

void test_getCurrentProfile_quick_test_has_5_minute_duration(void) {
    profiles->loadProfile(4);
    const TherapyProfile* p = profiles->getCurrentProfile();

    TEST_ASSERT_EQUAL_STRING("quick_test", p->name);
    TEST_ASSERT_EQUAL_UINT16(5, p->sessionDurationMin);
}

void test_getCurrentProfile_returns_null_when_not_loaded(void) {
    ProfileManager pm;
    // Don't call begin() or loadProfile() - profile not loaded
    const TherapyProfile* p = pm.getCurrentProfile();
    TEST_ASSERT_NULL(p);
}

void test_getCurrentProfileName_returns_none_when_not_loaded(void) {
    ProfileManager pm;
    // Don't call begin() or loadProfile() - profile not loaded
    TEST_ASSERT_EQUAL_STRING("none", pm.getCurrentProfileName());
}

void test_getProfileNames_with_null_count(void) {
    // Verify getProfileNames handles null count parameter
    const char** names = profiles->getProfileNames(nullptr);
    TEST_ASSERT_NOT_NULL(names);
    TEST_ASSERT_NOT_NULL(names[0]);
}

// =============================================================================
// SET PARAMETER TESTS - TYPE
// =============================================================================

void test_setParameter_TYPE_valid_LRA(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("TYPE", "LRA"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL(ActuatorType::LRA, p->actuatorType);
}

void test_setParameter_TYPE_valid_ERM(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("TYPE", "ERM"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL(ActuatorType::ERM, p->actuatorType);
}

void test_setParameter_TYPE_case_insensitive(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("type", "erm"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL(ActuatorType::ERM, p->actuatorType);
}

void test_setParameter_TYPE_invalid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("TYPE", "INVALID"));
}

// =============================================================================
// SET PARAMETER TESTS - FREQ
// =============================================================================

void test_setParameter_FREQ_valid_min(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("FREQ", "50"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT16(50, p->frequencyHz);
}

void test_setParameter_FREQ_valid_max(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("FREQ", "300"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT16(300, p->frequencyHz);
}

void test_setParameter_FREQ_invalid_below_50(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("FREQ", "49"));
}

void test_setParameter_FREQ_invalid_above_300(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("FREQ", "301"));
}

// =============================================================================
// SET PARAMETER TESTS - ON/OFF TIME
// =============================================================================

void test_setParameter_ON_valid_range(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("ON", "150.5"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 150.5f, p->timeOnMs);
}

void test_setParameter_ON_invalid_below_10(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("ON", "9"));
}

void test_setParameter_ON_invalid_above_1000(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("ON", "1001"));
}

void test_setParameter_OFF_valid_range(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("OFF", "200"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 200.0f, p->timeOffMs);
}

// =============================================================================
// SET PARAMETER TESTS - SESSION
// =============================================================================

void test_setParameter_SESSION_valid_min(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("SESSION", "1"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT16(1, p->sessionDurationMin);
}

void test_setParameter_SESSION_valid_max(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("SESSION", "240"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT16(240, p->sessionDurationMin);
}

void test_setParameter_SESSION_invalid_zero(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("SESSION", "0"));
}

void test_setParameter_SESSION_invalid_above_240(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("SESSION", "241"));
}

// =============================================================================
// SET PARAMETER TESTS - AMPLITUDE
// =============================================================================

void test_setParameter_AMPMIN_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMIN", "25"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(25, p->amplitudeMin);
}

void test_setParameter_AMPMAX_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMAX", "75"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(75, p->amplitudeMax);
}

void test_setParameter_AMPMIN_invalid_above_100(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("AMPMIN", "101"));
}

// =============================================================================
// SET PARAMETER TESTS - PATTERN
// =============================================================================

void test_setParameter_PATTERN_rndp(void) {
    profiles->loadProfile(3);  // Start with gentle (sequential)
    TEST_ASSERT_TRUE(profiles->setParameter("PATTERN", "rndp"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_STRING("rndp", p->patternType);
}

void test_setParameter_PATTERN_sequential(void) {
    profiles->loadProfile(1);  // Start with noisy (rndp)
    TEST_ASSERT_TRUE(profiles->setParameter("PATTERN", "sequential"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_STRING("sequential", p->patternType);
}

void test_setParameter_PATTERN_mirrored(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("PATTERN", "mirrored"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_STRING("mirrored", p->patternType);
}

void test_setParameter_PATTERN_invalid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("PATTERN", "invalid"));
}

// =============================================================================
// SET PARAMETER TESTS - JITTER & MIRROR & FINGERS
// =============================================================================

void test_setParameter_JITTER_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("JITTER", "50.5"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.5f, p->jitterPercent);
}

void test_setParameter_JITTER_invalid_negative(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("JITTER", "-1"));
}

void test_setParameter_JITTER_invalid_above_100(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("JITTER", "101"));
}

void test_setParameter_MIRROR_enable(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("MIRROR", "1"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_TRUE(p->mirrorPattern);
}

void test_setParameter_MIRROR_disable(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("MIRROR", "0"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FALSE(p->mirrorPattern);
}

void test_setParameter_FINGERS_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("FINGERS", "3"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(3, p->numFingers);
}

void test_setParameter_FINGERS_invalid_zero(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("FINGERS", "0"));
}

void test_setParameter_FINGERS_invalid_above_5(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("FINGERS", "6"));
}

// =============================================================================
// SET PARAMETER TESTS - ERROR CASES
// =============================================================================

void test_setParameter_unknown_param_returns_false(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("UNKNOWN", "value"));
}

void test_setParameter_null_param_returns_false(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter(nullptr, "value"));
}

void test_setParameter_null_value_returns_false(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("FREQ", nullptr));
}

void test_setParameter_without_profile_loaded_returns_false(void) {
    // Create fresh instance without loading profile
    ProfileManager pm;
    // Don't call begin() or loadProfile() to ensure _profileLoaded = false
    TEST_ASSERT_FALSE(pm.setParameter("FREQ", "100"));
}

// =============================================================================
// SET PARAMETER TESTS - ADDITIONAL BOUNDARY CONDITIONS
// =============================================================================

void test_setParameter_OFF_invalid_below_10(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("OFF", "9"));
}

void test_setParameter_OFF_invalid_above_1000(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("OFF", "1001"));
}

void test_setParameter_AMPMAX_invalid_above_100(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("AMPMAX", "101"));
}

void test_setParameter_AMPMIN_zero_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMIN", "0"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(0, p->amplitudeMin);
}

void test_setParameter_AMPMAX_zero_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMAX", "0"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(0, p->amplitudeMax);
}

void test_setParameter_JITTER_zero_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("JITTER", "0"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, p->jitterPercent);
}

void test_setParameter_JITTER_100_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("JITTER", "100"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, p->jitterPercent);
}

void test_setParameter_ON_10_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("ON", "10"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, p->timeOnMs);
}

void test_setParameter_ON_1000_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("ON", "1000"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1000.0f, p->timeOnMs);
}

void test_setParameter_OFF_10_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("OFF", "10"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, p->timeOffMs);
}

void test_setParameter_OFF_1000_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("OFF", "1000"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1000.0f, p->timeOffMs);
}

void test_setParameter_FINGERS_1_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("FINGERS", "1"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(1, p->numFingers);
}

void test_setParameter_FINGERS_5_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("FINGERS", "5"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(5, p->numFingers);
}

// =============================================================================
// RESET TO DEFAULTS TESTS
// =============================================================================

void test_resetToDefaults_restores_builtin_values(void) {
    profiles->loadProfile(1);

    // Modify some parameters
    profiles->setParameter("FREQ", "200");
    profiles->setParameter("JITTER", "50");

    // Reset
    profiles->resetToDefaults();

    // Verify original values restored
    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT16(175, p->frequencyHz);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 23.5f, p->jitterPercent);
}

// =============================================================================
// DEVICE ROLE TESTS
// =============================================================================

void test_setDeviceRole_PRIMARY(void) {
    profiles->setDeviceRole(DeviceRole::PRIMARY);
    TEST_ASSERT_EQUAL(DeviceRole::PRIMARY, profiles->getDeviceRole());
}

void test_setDeviceRole_SECONDARY(void) {
    profiles->setDeviceRole(DeviceRole::SECONDARY);
    TEST_ASSERT_EQUAL(DeviceRole::SECONDARY, profiles->getDeviceRole());
}

void test_hasStoredRole_false_initially(void) {
    ProfileManager pm;
    pm.begin(false);
    TEST_ASSERT_FALSE(pm.hasStoredRole());
}

// =============================================================================
// STORAGE TESTS
// =============================================================================

void test_isStorageAvailable_false_with_mock(void) {
    // Our mock InternalFS.begin() returns false
    TEST_ASSERT_FALSE(profiles->isStorageAvailable());
}

void test_saveSettings_returns_false_without_storage(void) {
    TEST_ASSERT_FALSE(profiles->saveSettings());
}

void test_loadSettings_returns_false_without_storage(void) {
    TEST_ASSERT_FALSE(profiles->loadSettings());
}

// =============================================================================
// MAIN - RUN ALL TESTS
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Initialization Tests
    RUN_TEST(test_ProfileManager_constructor_defaults);
    RUN_TEST(test_ProfileManager_begin_initializes_profiles);
    RUN_TEST(test_ProfileManager_getProfileNames_returns_valid_pointers);
    RUN_TEST(test_ProfileManager_getProfileNames_returns_correct_names);

    // Load Profile by ID Tests
    RUN_TEST(test_loadProfile_valid_id_1_loads_noisy_vcr);
    RUN_TEST(test_loadProfile_valid_id_2_loads_standard_vcr);
    RUN_TEST(test_loadProfile_valid_id_3_loads_gentle);
    RUN_TEST(test_loadProfile_valid_id_4_loads_quick_test);
    RUN_TEST(test_loadProfile_invalid_id_0_returns_false);
    RUN_TEST(test_loadProfile_invalid_id_5_returns_false);
    RUN_TEST(test_loadProfile_invalid_id_255_returns_false);

    // Load Profile by Name Tests
    RUN_TEST(test_loadProfileByName_exact_match);
    RUN_TEST(test_loadProfileByName_case_insensitive_upper);
    RUN_TEST(test_loadProfileByName_case_insensitive_mixed);
    RUN_TEST(test_loadProfileByName_null_returns_false);
    RUN_TEST(test_loadProfileByName_empty_returns_false);
    RUN_TEST(test_loadProfileByName_invalid_returns_false);
    RUN_TEST(test_loadProfileByName_gentle);

    // Get Current Profile Tests
    RUN_TEST(test_getCurrentProfile_returns_profile_after_load);
    RUN_TEST(test_getCurrentProfile_noisy_vcr_has_correct_defaults);
    RUN_TEST(test_getCurrentProfile_gentle_has_correct_values);
    RUN_TEST(test_getCurrentProfile_quick_test_has_5_minute_duration);
    RUN_TEST(test_getCurrentProfile_returns_null_when_not_loaded);
    RUN_TEST(test_getCurrentProfileName_returns_none_when_not_loaded);
    RUN_TEST(test_getProfileNames_with_null_count);

    // Set Parameter Tests - TYPE
    RUN_TEST(test_setParameter_TYPE_valid_LRA);
    RUN_TEST(test_setParameter_TYPE_valid_ERM);
    RUN_TEST(test_setParameter_TYPE_case_insensitive);
    RUN_TEST(test_setParameter_TYPE_invalid);

    // Set Parameter Tests - FREQ
    RUN_TEST(test_setParameter_FREQ_valid_min);
    RUN_TEST(test_setParameter_FREQ_valid_max);
    RUN_TEST(test_setParameter_FREQ_invalid_below_50);
    RUN_TEST(test_setParameter_FREQ_invalid_above_300);

    // Set Parameter Tests - ON/OFF
    RUN_TEST(test_setParameter_ON_valid_range);
    RUN_TEST(test_setParameter_ON_invalid_below_10);
    RUN_TEST(test_setParameter_ON_invalid_above_1000);
    RUN_TEST(test_setParameter_OFF_valid_range);

    // Set Parameter Tests - SESSION
    RUN_TEST(test_setParameter_SESSION_valid_min);
    RUN_TEST(test_setParameter_SESSION_valid_max);
    RUN_TEST(test_setParameter_SESSION_invalid_zero);
    RUN_TEST(test_setParameter_SESSION_invalid_above_240);

    // Set Parameter Tests - AMPLITUDE
    RUN_TEST(test_setParameter_AMPMIN_valid);
    RUN_TEST(test_setParameter_AMPMAX_valid);
    RUN_TEST(test_setParameter_AMPMIN_invalid_above_100);

    // Set Parameter Tests - PATTERN
    RUN_TEST(test_setParameter_PATTERN_rndp);
    RUN_TEST(test_setParameter_PATTERN_sequential);
    RUN_TEST(test_setParameter_PATTERN_mirrored);
    RUN_TEST(test_setParameter_PATTERN_invalid);

    // Set Parameter Tests - JITTER/MIRROR/FINGERS
    RUN_TEST(test_setParameter_JITTER_valid);
    RUN_TEST(test_setParameter_JITTER_invalid_negative);
    RUN_TEST(test_setParameter_JITTER_invalid_above_100);
    RUN_TEST(test_setParameter_MIRROR_enable);
    RUN_TEST(test_setParameter_MIRROR_disable);
    RUN_TEST(test_setParameter_FINGERS_valid);
    RUN_TEST(test_setParameter_FINGERS_invalid_zero);
    RUN_TEST(test_setParameter_FINGERS_invalid_above_5);

    // Set Parameter Tests - Error Cases
    RUN_TEST(test_setParameter_unknown_param_returns_false);
    RUN_TEST(test_setParameter_null_param_returns_false);
    RUN_TEST(test_setParameter_null_value_returns_false);
    RUN_TEST(test_setParameter_without_profile_loaded_returns_false);

    // Set Parameter Tests - Additional Boundary Conditions
    RUN_TEST(test_setParameter_OFF_invalid_below_10);
    RUN_TEST(test_setParameter_OFF_invalid_above_1000);
    RUN_TEST(test_setParameter_AMPMAX_invalid_above_100);
    RUN_TEST(test_setParameter_AMPMIN_zero_is_valid);
    RUN_TEST(test_setParameter_AMPMAX_zero_is_valid);
    RUN_TEST(test_setParameter_JITTER_zero_is_valid);
    RUN_TEST(test_setParameter_JITTER_100_is_valid);
    RUN_TEST(test_setParameter_ON_10_is_valid);
    RUN_TEST(test_setParameter_ON_1000_is_valid);
    RUN_TEST(test_setParameter_OFF_10_is_valid);
    RUN_TEST(test_setParameter_OFF_1000_is_valid);
    RUN_TEST(test_setParameter_FINGERS_1_is_valid);
    RUN_TEST(test_setParameter_FINGERS_5_is_valid);

    // Reset to Defaults Tests
    RUN_TEST(test_resetToDefaults_restores_builtin_values);

    // Device Role Tests
    RUN_TEST(test_setDeviceRole_PRIMARY);
    RUN_TEST(test_setDeviceRole_SECONDARY);
    RUN_TEST(test_hasStoredRole_false_initially);

    // Storage Tests
    RUN_TEST(test_isStorageAvailable_false_with_mock);
    RUN_TEST(test_saveSettings_returns_false_without_storage);
    RUN_TEST(test_loadSettings_returns_false_without_storage);

    return UNITY_END();
}
