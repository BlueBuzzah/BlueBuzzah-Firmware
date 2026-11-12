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

// Storage goes through the fs_backend abstraction; native builds link the
// in-memory fs_backend_mock.cpp (controlled via fsb::mock::*).
#include "fs_backend.h"
#include "profile_manager.h"

// Include source file directly for native testing
#include "../../src/profile_manager.cpp"

// =============================================================================
// TEST FIXTURES
// =============================================================================

static ProfileManager* profiles = nullptr;

void setUp(void) {
    fsb::mock::reset();
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
    TEST_ASSERT_EQUAL_UINT8(6, profiles->getProfileCount());
}

void test_ProfileManager_getProfileNames_returns_valid_pointers(void) {
    uint8_t count = 0;
    const char** names = profiles->getProfileNames(&count);

    TEST_ASSERT_EQUAL_UINT8(6, count);
    TEST_ASSERT_NOT_NULL(names);
    TEST_ASSERT_NOT_NULL(names[0]);
    TEST_ASSERT_NOT_NULL(names[1]);
    TEST_ASSERT_NOT_NULL(names[2]);
    TEST_ASSERT_NOT_NULL(names[3]);
    TEST_ASSERT_NOT_NULL(names[4]);
    TEST_ASSERT_NOT_NULL(names[5]);
}

void test_ProfileManager_getProfileNames_returns_correct_names(void) {
    uint8_t count = 0;
    const char** names = profiles->getProfileNames(&count);

    TEST_ASSERT_EQUAL_STRING("regular_vcr", names[0]);
    TEST_ASSERT_EQUAL_STRING("noisy_vcr", names[1]);
    TEST_ASSERT_EQUAL_STRING("hybrid_vcr", names[2]);
    TEST_ASSERT_EQUAL_STRING("custom_vcr", names[3]);
    TEST_ASSERT_EQUAL_STRING("gentle", names[4]);
    TEST_ASSERT_EQUAL_STRING("quick_test", names[5]);
}

// =============================================================================
// LOAD PROFILE BY ID TESTS
// =============================================================================

void test_loadProfile_valid_id_1_loads_regular_vcr(void) {
    TEST_ASSERT_TRUE(profiles->loadProfile(1));
    TEST_ASSERT_EQUAL_STRING("regular_vcr", profiles->getCurrentProfileName());
}

void test_loadProfile_valid_id_2_loads_noisy_vcr(void) {
    TEST_ASSERT_TRUE(profiles->loadProfile(2));
    TEST_ASSERT_EQUAL_STRING("noisy_vcr", profiles->getCurrentProfileName());
}

void test_loadProfile_valid_id_3_loads_hybrid_vcr(void) {
    TEST_ASSERT_TRUE(profiles->loadProfile(3));
    TEST_ASSERT_EQUAL_STRING("hybrid_vcr", profiles->getCurrentProfileName());
}

void test_loadProfile_valid_id_4_loads_custom_vcr(void) {
    TEST_ASSERT_TRUE(profiles->loadProfile(4));
    TEST_ASSERT_EQUAL_STRING("custom_vcr", profiles->getCurrentProfileName());
}

void test_loadProfile_valid_id_5_loads_gentle(void) {
    TEST_ASSERT_TRUE(profiles->loadProfile(5));
    TEST_ASSERT_EQUAL_STRING("gentle", profiles->getCurrentProfileName());
}

void test_loadProfile_valid_id_6_loads_quick_test(void) {
    TEST_ASSERT_TRUE(profiles->loadProfile(6));
    TEST_ASSERT_EQUAL_STRING("quick_test", profiles->getCurrentProfileName());
}

void test_loadProfile_invalid_id_0_returns_false(void) {
    TEST_ASSERT_FALSE(profiles->loadProfile(0));
}

void test_loadProfile_invalid_id_7_returns_false(void) {
    TEST_ASSERT_FALSE(profiles->loadProfile(7));
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
    TEST_ASSERT_EQUAL_STRING("regular_vcr", profile->name);
    TEST_ASSERT_EQUAL(ActuatorType::LRA, profile->actuatorType);
    TEST_ASSERT_EQUAL_UINT16(250, profile->frequencyHz);
}

void test_getCurrentProfile_noisy_vcr_has_correct_defaults(void) {
    profiles->loadProfile(2);  // noisy_vcr is now profile 2
    const TherapyProfile* p = profiles->getCurrentProfile();

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, p->timeOnMs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 67.0f, p->timeOffMs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 23.5f, p->jitterPercent);
    TEST_ASSERT_EQUAL_UINT8(100, p->amplitudeMin);
    TEST_ASSERT_EQUAL_UINT8(100, p->amplitudeMax);
    TEST_ASSERT_EQUAL_UINT16(120, p->sessionDurationMin);
    TEST_ASSERT_TRUE(p->mirrorPattern);
    TEST_ASSERT_EQUAL_UINT8(MAX_ACTUATORS, p->numFingers);
}

void test_getCurrentProfile_custom_vcr_has_correct_defaults(void) {
    profiles->loadProfile(4);  // custom_vcr defaults match regular_vcr
    const TherapyProfile* p = profiles->getCurrentProfile();

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, p->timeOnMs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 67.0f, p->timeOffMs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, p->jitterPercent);
    TEST_ASSERT_EQUAL_UINT8(100, p->amplitudeMin);
    TEST_ASSERT_EQUAL_UINT8(100, p->amplitudeMax);
    TEST_ASSERT_FALSE(p->mirrorPattern);
    TEST_ASSERT_EQUAL_UINT16(250, p->frequencyHz);
    TEST_ASSERT_EQUAL_UINT16(120, p->sessionDurationMin);
    TEST_ASSERT_EQUAL_STRING("rndp", p->patternType);
    TEST_ASSERT_EQUAL_UINT8(MAX_ACTUATORS, p->numFingers);
}

void test_getCurrentProfile_gentle_has_correct_values(void) {
    profiles->loadProfile(5);  // gentle is now profile 5
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
    profiles->loadProfile(6);  // quick_test is now profile 6
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
    TEST_ASSERT_TRUE(profiles->setParameter("TYPE", "ERM"));
    TEST_ASSERT_EQUAL(ActuatorType::ERM, profiles->getCurrentProfile()->actuatorType);
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

// =============================================================================
// SET PARAMETER TESTS - ON/OFF TIME
// =============================================================================

void test_setParameter_ON_valid_range(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("ON", "150.5"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 150.5f, p->timeOnMs);
}

void test_setParameter_rejects_nan(void) {
    profiles->loadProfile(1);
    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FALSE(profiles->setParameter("ON", "nan"));
    TEST_ASSERT_FALSE(profiles->setParameter("OFF", "nan"));
    TEST_ASSERT_FALSE(profiles->setParameter("JITTER", "nan"));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, p->timeOnMs);
    TEST_ASSERT_EQUAL_FLOAT(67.0f, p->timeOffMs);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p->jitterPercent);
}

void test_setParameter_rejects_inf(void) {
    profiles->loadProfile(1);
    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FALSE(profiles->setParameter("ON", "inf"));
    TEST_ASSERT_FALSE(profiles->setParameter("ON", "-inf"));
    TEST_ASSERT_FALSE(profiles->setParameter("OFF", "inf"));
    TEST_ASSERT_FALSE(profiles->setParameter("OFF", "-inf"));
    TEST_ASSERT_FALSE(profiles->setParameter("JITTER", "inf"));
    TEST_ASSERT_FALSE(profiles->setParameter("JITTER", "-inf"));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, p->timeOnMs);
    TEST_ASSERT_EQUAL_FLOAT(67.0f, p->timeOffMs);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p->jitterPercent);
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
    // regular_vcr defaults amplitudeMin to 100; lower it first so the
    // AMPMIN/AMPMAX cross-check does not reject an otherwise in-range AMPMAX.
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMIN", "20"));
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
    profiles->loadProfile(5);  // gentle starts as "sequential"
    TEST_ASSERT_TRUE(profiles->setParameter("PATTERN", "rndp"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_STRING("rndp", p->patternType);
}

void test_setParameter_PATTERN_sequential(void) {
    profiles->loadProfile(1);  // regular_vcr starts as "rndp"
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
    TEST_ASSERT_TRUE(profiles->setParameter("JITTER", "25.0"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, p->jitterPercent);
}

void test_setParameter_MIRROR_enable(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("MIRROR", "1"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_TRUE(p->mirrorPattern);
}

void test_setParameter_MIRROR_disable(void) {
    profiles->loadProfile(2);  // noisy_vcr starts mirrored
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

void test_setParameter_AMPMAX_invalid_above_100(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("AMPMAX", "101"));
}

void test_setParameter_AMPMAX_20_is_valid(void) {
    profiles->loadProfile(1);
    // regular_vcr defaults amplitudeMin to 100; lower it first so the
    // AMPMIN/AMPMAX cross-check does not reject the new boundary value.
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMIN", "20"));
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMAX", "20"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(20, p->amplitudeMax);
}

void test_setParameter_JITTER_zero_is_valid(void) {
    profiles->loadProfile(2);  // noisy_vcr starts at 23.5
    TEST_ASSERT_TRUE(profiles->setParameter("JITTER", "0"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, p->jitterPercent);
}

void test_setParameter_JITTER_50_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("JITTER", "50"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, p->jitterPercent);
}

void test_setParameter_ON_50_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("ON", "50"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, p->timeOnMs);
}

void test_setParameter_ON_200_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("ON", "200"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 200.0f, p->timeOnMs);
}

void test_setParameter_OFF_30_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("OFF", "30"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 30.0f, p->timeOffMs);
}

void test_setParameter_AMPMIN_below_20_is_rejected(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("AMPMIN", "19"));
}

void test_setParameter_AMPMIN_20_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMIN", "20"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(20, p->amplitudeMin);
}

void test_setParameter_AMPMIN_above_AMPMAX_is_rejected(void) {
    // gentle defaults to amplitudeMin=30/amplitudeMax=70, both clear of the
    // 60/70 values below, so the cross-check result here reflects only the
    // writes this test makes, not whatever the loaded profile happened to hold.
    profiles->loadProfile(5);
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMAX", "60"));
    TEST_ASSERT_FALSE(profiles->setParameter("AMPMIN", "70"));

    // The rejected write must not have taken effect.
    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(60, p->amplitudeMax);
}

void test_setParameter_AMPMAX_below_AMPMIN_is_rejected(void) {
    profiles->loadProfile(5);
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMIN", "70"));
    TEST_ASSERT_FALSE(profiles->setParameter("AMPMAX", "60"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(70, p->amplitudeMin);
}

void test_setParameter_AMPMIN_equal_AMPMAX_is_valid(void) {
    profiles->loadProfile(5);
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMAX", "70"));
    TEST_ASSERT_TRUE(profiles->setParameter("AMPMIN", "70"));
}

void test_setParameter_FINGERS_1_is_valid(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("FINGERS", "1"));

    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(1, p->numFingers);
}

// =============================================================================
// SET PARAMETER TESTS - EXACT BOUNDARIES
// =============================================================================

void test_setParameter_ON_rejects_49_9_and_200_1(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("ON", "49.9"));
    TEST_ASSERT_FALSE(profiles->setParameter("ON", "200.1"));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, profiles->getCurrentProfile()->timeOnMs);
}

void test_setParameter_OFF_rejects_29_9_and_200_1(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("OFF", "29.9"));
    TEST_ASSERT_FALSE(profiles->setParameter("OFF", "200.1"));
    TEST_ASSERT_EQUAL_FLOAT(67.0f, profiles->getCurrentProfile()->timeOffMs);
}

void test_setParameter_JITTER_rejects_minus_0_1_and_50_1(void) {
    profiles->loadProfile(2);
    TEST_ASSERT_FALSE(profiles->setParameter("JITTER", "-0.1"));
    TEST_ASSERT_FALSE(profiles->setParameter("JITTER", "50.1"));
    TEST_ASSERT_EQUAL_FLOAT(23.5f, profiles->getCurrentProfile()->jitterPercent);
}

void test_setParameter_FREQ_rejects_49_and_301(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("FREQ", "49"));
    TEST_ASSERT_FALSE(profiles->setParameter("FREQ", "301"));
    TEST_ASSERT_EQUAL_UINT16(250, profiles->getCurrentProfile()->frequencyHz);
}

void test_setParameter_SESSION_rejects_0_and_241(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_FALSE(profiles->setParameter("SESSION", "0"));
    TEST_ASSERT_FALSE(profiles->setParameter("SESSION", "241"));
    TEST_ASSERT_EQUAL_UINT16(120, profiles->getCurrentProfile()->sessionDurationMin);
}

void test_setParameter_FINGERS_rejects_0_and_above_MAX_ACTUATORS(void) {
    profiles->loadProfile(1);
    char above[4];
    snprintf(above, sizeof(above), "%d", MAX_ACTUATORS + 1);
    TEST_ASSERT_FALSE(profiles->setParameter("FINGERS", "0"));
    TEST_ASSERT_FALSE(profiles->setParameter("FINGERS", above));
    TEST_ASSERT_EQUAL_UINT8(MAX_ACTUATORS, profiles->getCurrentProfile()->numFingers);
}

void test_setParameter_FINGERS_accepts_MAX_ACTUATORS(void) {
    profiles->loadProfile(1);
    TEST_ASSERT_TRUE(profiles->setParameter("FINGERS", "1"));
    char max[4];
    snprintf(max, sizeof(max), "%d", MAX_ACTUATORS);
    TEST_ASSERT_TRUE(profiles->setParameter("FINGERS", max));
    TEST_ASSERT_EQUAL_UINT8(MAX_ACTUATORS, profiles->getCurrentProfile()->numFingers);
}

// =============================================================================
// RESET TO DEFAULTS TESTS
// =============================================================================

void test_resetToDefaults_restores_builtin_values(void) {
    profiles->loadProfile(1);  // regular_vcr

    // Modify some parameters
    profiles->setParameter("FREQ", "200");
    profiles->setParameter("JITTER", "50");

    // Reset
    profiles->resetToDefaults();

    // Verify original values restored
    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT16(250, p->frequencyHz);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, p->jitterPercent);  // regular_vcr has 0% jitter
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
// CUSTOM OVERRIDE TESTS
// =============================================================================

void test_customOverride_roundtrip(void) {
    profiles->loadProfile(CUSTOM_PROFILE_ID);
    profiles->setParameter("ON", "80");
    profiles->setParameter("OFF", "53");
    profiles->setParameter("JITTER", "30");
    profiles->setParameter("FINGERS", "3");
    TEST_ASSERT_TRUE(profiles->saveCustomOverride());

    // Reload Custom from the built-in table; the override must be reapplied.
    profiles->loadProfile(1);
    profiles->loadProfile(CUSTOM_PROFILE_ID);

    const TherapyProfile* profile = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_FLOAT(80.0f, profile->timeOnMs);
    TEST_ASSERT_EQUAL_FLOAT(53.0f, profile->timeOffMs);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, profile->jitterPercent);
    TEST_ASSERT_EQUAL_UINT8(3, profile->numFingers);
}

void test_customOverride_survives_switching_to_another_profile(void) {
    // This is the exact scenario the single settings.bin block could not survive.
    profiles->loadProfile(CUSTOM_PROFILE_ID);
    profiles->setParameter("ON", "80");
    TEST_ASSERT_TRUE(profiles->saveCustomOverride());

    profiles->loadProfile(1);                    // Regular
    TEST_ASSERT_TRUE(profiles->saveSettings());  // as PROFILE_LOAD does
    profiles->loadProfile(CUSTOM_PROFILE_ID);

    TEST_ASSERT_EQUAL_FLOAT(80.0f, profiles->getCurrentProfile()->timeOnMs);
}

void test_customOverride_absent_leaves_builtin_defaults(void) {
    profiles->loadProfile(CUSTOM_PROFILE_ID);
    // custom_vcr built-in defaults
    const TherapyProfile* profile = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_FLOAT(100.0f, profile->timeOnMs);
    TEST_ASSERT_EQUAL_UINT8(100, profile->amplitudeMin);
}

void test_customOverride_does_not_affect_preset_profiles(void) {
    profiles->loadProfile(CUSTOM_PROFILE_ID);
    profiles->setParameter("ON", "80");
    TEST_ASSERT_TRUE(profiles->saveCustomOverride());

    profiles->loadProfile(1);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, profiles->getCurrentProfile()->timeOnMs);
}

void test_clearCustomOverride_restores_builtin_defaults(void) {
    profiles->loadProfile(CUSTOM_PROFILE_ID);
    profiles->setParameter("ON", "80");
    profiles->saveCustomOverride();

    TEST_ASSERT_TRUE(profiles->clearCustomOverride());
    profiles->loadProfile(CUSTOM_PROFILE_ID);

    TEST_ASSERT_EQUAL_FLOAT(100.0f, profiles->getCurrentProfile()->timeOnMs);
}

void test_saveCustomOverride_returns_false_without_storage(void) {
    fsb::mock::setBeginResult(false);
    ProfileManager* p = new ProfileManager();
    p->begin(false);
    TEST_ASSERT_FALSE(p->saveCustomOverride());
    delete p;
}

void test_customOverride_survives_reboot(void) {
    // The boot path (loadSettings) does not route through loadProfile, so it
    // needs its own overlay call. Verifies settings.bin recording profileId 4
    // does not silently revert Custom to built-in defaults after a reboot.
    profiles->loadProfile(CUSTOM_PROFILE_ID);
    profiles->setParameter("ON", "80");
    TEST_ASSERT_TRUE(profiles->saveCustomOverride());
    TEST_ASSERT_TRUE(profiles->saveSettings());  // Records profileId 4 in settings.bin

    ProfileManager pm2;
    pm2.begin(true);  // Load from (mock) storage

    const TherapyProfile* profile = pm2.getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(CUSTOM_PROFILE_ID, pm2.getCurrentProfileId());
    TEST_ASSERT_EQUAL_FLOAT(80.0f, profile->timeOnMs);
}

void test_customOverride_does_not_persist_pattern(void) {
    // CustomOverrideData has no patternType field. setParameter still accepts
    // PATTERN (it is a general-purpose entry point used by other profiles too),
    // but the persistence layer must not silently retain it - the handler-level
    // rejection (menu_controller) is what actually blocks this from the phone/
    // serial, and this test proves the underlying data path can't launder it
    // through even if that rejection were ever bypassed.
    profiles->loadProfile(CUSTOM_PROFILE_ID);
    TEST_ASSERT_TRUE(profiles->setParameter("PATTERN", "sequential"));
    TEST_ASSERT_TRUE(profiles->saveCustomOverride());

    profiles->loadProfile(1);
    profiles->loadProfile(CUSTOM_PROFILE_ID);

    const TherapyProfile* profile = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_STRING("rndp", profile->patternType);
}

// =============================================================================
// STORAGE TESTS
// =============================================================================

void test_isStorageAvailable_false_when_mount_fails(void) {
    fsb::mock::setBeginResult(false);
    ProfileManager pm;
    pm.begin(false);
    TEST_ASSERT_FALSE(pm.isStorageAvailable());
}

void test_saveSettings_returns_false_without_storage(void) {
    fsb::mock::setBeginResult(false);
    ProfileManager pm;
    pm.begin(false);
    TEST_ASSERT_FALSE(pm.saveSettings());
}

void test_loadSettings_returns_false_without_storage(void) {
    fsb::mock::setBeginResult(false);
    ProfileManager pm;
    pm.begin(false);
    TEST_ASSERT_FALSE(pm.loadSettings());
}

void test_settings_roundtrip_with_storage(void) {
    profiles->setDeviceRole(DeviceRole::SECONDARY);
    TEST_ASSERT_TRUE(profiles->saveSettings());

    ProfileManager pm2;
    pm2.begin(true);  // Load from (mock) storage
    TEST_ASSERT_TRUE(pm2.hasStoredRole());
    TEST_ASSERT_EQUAL(DeviceRole::SECONDARY, pm2.getDeviceRole());
}

void test_loadSettings_preset_ignores_saved_parameters(void) {
    // settings.bin from a firmware whose Noisy defaults differed: every
    // parameter is in range but none matches the current built-in.
    SettingsData data{};
    data.magic              = SETTINGS_MAGIC;
    data.version            = SETTINGS_VERSION;
    data.role               = 1;  // SECONDARY
    data.profileId          = 2;  // Noisy vCR
    data.actuatorType       = 1;  // ERM
    data.timeOnMs           = 150.0f;
    data.timeOffMs          = 120.0f;
    data.jitterPercent      = 10.0f;
    data.amplitudeMin       = 60;
    data.amplitudeMax       = 80;
    data.sessionDurationMin = 60;
    data.numFingers         = 2;
    data.frequencyHz        = 200;
    data.mirrorPattern      = 0;
    data.therapyLedOff      = 1;
    data.debugMode          = 1;
    strncpy(data.patternType, "seq", sizeof(data.patternType) - 1);

    fsb::writeFile(SETTINGS_FILE,
                   reinterpret_cast<const uint8_t*>(&data), sizeof(data));

    ProfileManager* p = new ProfileManager();
    p->begin(true);  // Load from storage

    // Role, profile and flags come from settings.bin...
    TEST_ASSERT_EQUAL_UINT8(2, p->getCurrentProfileId());
    TEST_ASSERT_EQUAL(DeviceRole::SECONDARY, p->getDeviceRole());
    TEST_ASSERT_TRUE(p->getTherapyLedOff());
    TEST_ASSERT_TRUE(p->getDebugMode());

    // ...parameters come from the built-in Noisy profile only.
    const TherapyProfile* profile = p->getCurrentProfile();
    TEST_ASSERT_EQUAL(ActuatorType::LRA, profile->actuatorType);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, profile->timeOnMs);
    TEST_ASSERT_EQUAL_FLOAT(67.0f,  profile->timeOffMs);
    TEST_ASSERT_EQUAL_FLOAT(23.5f,  profile->jitterPercent);
    TEST_ASSERT_EQUAL_UINT8(100,    profile->amplitudeMin);
    TEST_ASSERT_EQUAL_UINT8(100,    profile->amplitudeMax);
    TEST_ASSERT_EQUAL_UINT16(120,   profile->sessionDurationMin);
    TEST_ASSERT_EQUAL_UINT8(MAX_ACTUATORS, profile->numFingers);
    TEST_ASSERT_EQUAL_UINT16(250,   profile->frequencyHz);
    TEST_ASSERT_TRUE(profile->mirrorPattern);
    TEST_ASSERT_EQUAL_STRING("rndp", profile->patternType);
    delete p;
}

// settings.bin as written by firmware whose Custom defaults were 23.5% jitter
// and 70-100% amplitude.
static void writeStaleCustomSettings(void) {
    SettingsData data{};
    data.magic              = SETTINGS_MAGIC;
    data.version            = SETTINGS_VERSION;
    data.role               = 1;  // SECONDARY
    data.profileId          = CUSTOM_PROFILE_ID;
    data.timeOnMs           = 100.0f;
    data.timeOffMs          = 67.0f;
    data.jitterPercent      = 23.5f;
    data.amplitudeMin       = 70;
    data.amplitudeMax       = 100;
    data.sessionDurationMin = 120;
    data.numFingers         = MAX_ACTUATORS;
    data.frequencyHz        = 250;
    strncpy(data.patternType, "rndp", sizeof(data.patternType) - 1);
    fsb::writeFile(SETTINGS_FILE,
                   reinterpret_cast<const uint8_t*>(&data), sizeof(data));
}

void test_loadSettings_custom_ignores_stale_settings_parameters(void) {
    writeStaleCustomSettings();  // no custom.bin

    ProfileManager* p = new ProfileManager();
    p->begin(true);

    const TherapyProfile* profile = p->getCurrentProfile();
    TEST_ASSERT_EQUAL_UINT8(CUSTOM_PROFILE_ID, p->getCurrentProfileId());
    TEST_ASSERT_EQUAL(DeviceRole::SECONDARY, p->getDeviceRole());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, profile->jitterPercent);
    TEST_ASSERT_EQUAL_UINT8(100,  profile->amplitudeMin);
    TEST_ASSERT_EQUAL_UINT8(100,  profile->amplitudeMax);
    delete p;
}

void test_loadSettings_custom_override_wins_over_settings(void) {
    writeStaleCustomSettings();

    CustomOverrideData custom{};
    custom.magic              = CUSTOM_OVERRIDE_MAGIC;
    custom.version            = CUSTOM_OVERRIDE_VERSION;
    custom.timeOnMs           = 100.0f;
    custom.timeOffMs          = 67.0f;
    custom.jitterPercent      = 10.0f;
    custom.amplitudeMin       = 60;
    custom.amplitudeMax       = 90;
    custom.sessionDurationMin = 120;
    custom.numFingers         = MAX_ACTUATORS;
    custom.mirrorPattern      = 0;
    fsb::writeFile(CUSTOM_OVERRIDE_FILE,
                   reinterpret_cast<const uint8_t*>(&custom), sizeof(custom));

    ProfileManager* p = new ProfileManager();
    p->begin(true);

    const TherapyProfile* profile = p->getCurrentProfile();
    TEST_ASSERT_EQUAL_FLOAT(10.0f, profile->jitterPercent);
    TEST_ASSERT_EQUAL_UINT8(60,    profile->amplitudeMin);
    TEST_ASSERT_EQUAL_UINT8(90,    profile->amplitudeMax);
    delete p;
}

// =============================================================================
// PERSISTENCE FAILURE TESTS
// =============================================================================

void test_saveSettings_returns_false_when_write_fails(void) {
    fsb::mock::setWriteResult(false);
    TEST_ASSERT_FALSE(profiles->saveSettings());
}

void test_loadSettings_rejects_bad_magic(void) {
    SettingsData data{};
    data.magic     = SETTINGS_MAGIC ^ 1;
    data.version   = SETTINGS_VERSION;
    data.role      = 1;
    data.profileId = 2;
    fsb::writeFile(SETTINGS_FILE, reinterpret_cast<const uint8_t*>(&data), sizeof(data));

    profiles->loadProfile(5);
    TEST_ASSERT_FALSE(profiles->loadSettings());
    TEST_ASSERT_EQUAL_UINT8(5, profiles->getCurrentProfileId());
    TEST_ASSERT_EQUAL(DeviceRole::PRIMARY, profiles->getDeviceRole());
}

void test_loadSettings_rejects_short_file(void) {
    SettingsData data{};
    data.magic     = SETTINGS_MAGIC;
    data.version   = SETTINGS_VERSION;
    data.role      = 1;
    data.profileId = 2;
    fsb::writeFile(SETTINGS_FILE, reinterpret_cast<const uint8_t*>(&data), sizeof(data) - 1);

    profiles->loadProfile(5);
    TEST_ASSERT_FALSE(profiles->loadSettings());
    TEST_ASSERT_EQUAL_UINT8(5, profiles->getCurrentProfileId());
    TEST_ASSERT_EQUAL(DeviceRole::PRIMARY, profiles->getDeviceRole());
}

void test_saveCustomOverride_returns_false_when_write_fails(void) {
    profiles->loadProfile(CUSTOM_PROFILE_ID);
    fsb::mock::setWriteResult(false);
    TEST_ASSERT_FALSE(profiles->saveCustomOverride());
    TEST_ASSERT_FALSE(fsb::exists(CUSTOM_OVERRIDE_FILE));
}

static CustomOverrideData makeValidOverride(void) {
    CustomOverrideData data{};
    data.magic              = CUSTOM_OVERRIDE_MAGIC;
    data.version            = CUSTOM_OVERRIDE_VERSION;
    data.timeOnMs           = 80.0f;
    data.timeOffMs          = 53.0f;
    data.jitterPercent      = 10.0f;
    data.amplitudeMin       = 60;
    data.amplitudeMax       = 90;
    data.sessionDurationMin = 60;
    data.numFingers         = 2;
    data.mirrorPattern      = 1;
    return data;
}

static void assertCustomDefaultsUnchanged(const TherapyProfile* p) {
    TEST_ASSERT_EQUAL_FLOAT(100.0f, p->timeOnMs);
    TEST_ASSERT_EQUAL_FLOAT(67.0f, p->timeOffMs);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p->jitterPercent);
    TEST_ASSERT_EQUAL_UINT8(100, p->amplitudeMin);
    TEST_ASSERT_EQUAL_UINT8(100, p->amplitudeMax);
    TEST_ASSERT_EQUAL_UINT16(120, p->sessionDurationMin);
    TEST_ASSERT_EQUAL_UINT8(MAX_ACTUATORS, p->numFingers);
    TEST_ASSERT_FALSE(p->mirrorPattern);
}

void test_loadCustomOverride_rejects_bad_magic(void) {
    CustomOverrideData data = makeValidOverride();
    data.magic = CUSTOM_OVERRIDE_MAGIC ^ 1;
    fsb::writeFile(CUSTOM_OVERRIDE_FILE, reinterpret_cast<const uint8_t*>(&data), sizeof(data));

    profiles->loadProfile(CUSTOM_PROFILE_ID);
    TEST_ASSERT_FALSE(profiles->loadCustomOverride());
    assertCustomDefaultsUnchanged(profiles->getCurrentProfile());
}

void test_loadCustomOverride_rejects_wrong_version(void) {
    CustomOverrideData data = makeValidOverride();
    data.version = CUSTOM_OVERRIDE_VERSION + 1;
    fsb::writeFile(CUSTOM_OVERRIDE_FILE, reinterpret_cast<const uint8_t*>(&data), sizeof(data));

    profiles->loadProfile(CUSTOM_PROFILE_ID);
    TEST_ASSERT_FALSE(profiles->loadCustomOverride());
    assertCustomDefaultsUnchanged(profiles->getCurrentProfile());
}

void test_loadCustomOverride_rejects_short_file(void) {
    CustomOverrideData data = makeValidOverride();
    fsb::writeFile(CUSTOM_OVERRIDE_FILE, reinterpret_cast<const uint8_t*>(&data), sizeof(data) - 1);

    profiles->loadProfile(CUSTOM_PROFILE_ID);
    TEST_ASSERT_FALSE(profiles->loadCustomOverride());
    assertCustomDefaultsUnchanged(profiles->getCurrentProfile());
}

void test_loadCustomOverride_ignores_nan_fields(void) {
    CustomOverrideData data = makeValidOverride();
    const uint32_t nanBits = 0x7FC00000u;
    memcpy(&data.timeOnMs, &nanBits, sizeof(nanBits));
    fsb::writeFile(CUSTOM_OVERRIDE_FILE, reinterpret_cast<const uint8_t*>(&data), sizeof(data));

    // Like an out-of-range field: the NaN keeps the built-in value, the rest apply.
    profiles->loadProfile(CUSTOM_PROFILE_ID);
    const TherapyProfile* p = profiles->getCurrentProfile();
    TEST_ASSERT_EQUAL_FLOAT(100.0f, p->timeOnMs);
    TEST_ASSERT_EQUAL_FLOAT(53.0f, p->timeOffMs);
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
    RUN_TEST(test_loadProfile_valid_id_1_loads_regular_vcr);
    RUN_TEST(test_loadProfile_valid_id_2_loads_noisy_vcr);
    RUN_TEST(test_loadProfile_valid_id_3_loads_hybrid_vcr);
    RUN_TEST(test_loadProfile_valid_id_4_loads_custom_vcr);
    RUN_TEST(test_loadProfile_valid_id_5_loads_gentle);
    RUN_TEST(test_loadProfile_valid_id_6_loads_quick_test);
    RUN_TEST(test_loadProfile_invalid_id_0_returns_false);
    RUN_TEST(test_loadProfile_invalid_id_7_returns_false);
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
    RUN_TEST(test_getCurrentProfile_custom_vcr_has_correct_defaults);
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

    // Set Parameter Tests - ON/OFF
    RUN_TEST(test_setParameter_ON_valid_range);
    RUN_TEST(test_setParameter_rejects_nan);
    RUN_TEST(test_setParameter_rejects_inf);
    RUN_TEST(test_setParameter_OFF_valid_range);

    // Set Parameter Tests - SESSION
    RUN_TEST(test_setParameter_SESSION_valid_min);
    RUN_TEST(test_setParameter_SESSION_valid_max);

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
    RUN_TEST(test_setParameter_MIRROR_enable);
    RUN_TEST(test_setParameter_MIRROR_disable);
    RUN_TEST(test_setParameter_FINGERS_valid);

    // Set Parameter Tests - Error Cases
    RUN_TEST(test_setParameter_unknown_param_returns_false);
    RUN_TEST(test_setParameter_null_param_returns_false);
    RUN_TEST(test_setParameter_null_value_returns_false);
    RUN_TEST(test_setParameter_without_profile_loaded_returns_false);

    // Set Parameter Tests - Additional Boundary Conditions
    RUN_TEST(test_setParameter_AMPMAX_invalid_above_100);
    RUN_TEST(test_setParameter_AMPMAX_20_is_valid);
    RUN_TEST(test_setParameter_JITTER_zero_is_valid);
    RUN_TEST(test_setParameter_JITTER_50_is_valid);
    RUN_TEST(test_setParameter_ON_50_is_valid);
    RUN_TEST(test_setParameter_ON_200_is_valid);
    RUN_TEST(test_setParameter_OFF_30_is_valid);
    RUN_TEST(test_setParameter_AMPMIN_below_20_is_rejected);
    RUN_TEST(test_setParameter_AMPMIN_20_is_valid);
    RUN_TEST(test_setParameter_AMPMIN_above_AMPMAX_is_rejected);
    RUN_TEST(test_setParameter_AMPMAX_below_AMPMIN_is_rejected);
    RUN_TEST(test_setParameter_AMPMIN_equal_AMPMAX_is_valid);
    RUN_TEST(test_setParameter_FINGERS_1_is_valid);

    RUN_TEST(test_setParameter_ON_rejects_49_9_and_200_1);
    RUN_TEST(test_setParameter_OFF_rejects_29_9_and_200_1);
    RUN_TEST(test_setParameter_JITTER_rejects_minus_0_1_and_50_1);
    RUN_TEST(test_setParameter_FREQ_rejects_49_and_301);
    RUN_TEST(test_setParameter_SESSION_rejects_0_and_241);
    RUN_TEST(test_setParameter_FINGERS_rejects_0_and_above_MAX_ACTUATORS);
    RUN_TEST(test_setParameter_FINGERS_accepts_MAX_ACTUATORS);

    // Reset to Defaults Tests
    RUN_TEST(test_resetToDefaults_restores_builtin_values);

    // Device Role Tests
    RUN_TEST(test_setDeviceRole_PRIMARY);
    RUN_TEST(test_setDeviceRole_SECONDARY);
    RUN_TEST(test_hasStoredRole_false_initially);

    // Custom Override Tests
    RUN_TEST(test_customOverride_roundtrip);
    RUN_TEST(test_customOverride_survives_switching_to_another_profile);
    RUN_TEST(test_customOverride_absent_leaves_builtin_defaults);
    RUN_TEST(test_customOverride_does_not_affect_preset_profiles);
    RUN_TEST(test_clearCustomOverride_restores_builtin_defaults);
    RUN_TEST(test_saveCustomOverride_returns_false_without_storage);
    RUN_TEST(test_customOverride_survives_reboot);
    RUN_TEST(test_customOverride_does_not_persist_pattern);

    // Storage Tests
    RUN_TEST(test_isStorageAvailable_false_when_mount_fails);
    RUN_TEST(test_settings_roundtrip_with_storage);
    RUN_TEST(test_loadSettings_preset_ignores_saved_parameters);
    RUN_TEST(test_loadSettings_custom_ignores_stale_settings_parameters);
    RUN_TEST(test_loadSettings_custom_override_wins_over_settings);
    RUN_TEST(test_saveSettings_returns_false_without_storage);
    RUN_TEST(test_loadSettings_returns_false_without_storage);

    // Persistence Failure Tests
    RUN_TEST(test_saveSettings_returns_false_when_write_fails);
    RUN_TEST(test_loadSettings_rejects_bad_magic);
    RUN_TEST(test_loadSettings_rejects_short_file);
    RUN_TEST(test_saveCustomOverride_returns_false_when_write_fails);
    RUN_TEST(test_loadCustomOverride_rejects_bad_magic);
    RUN_TEST(test_loadCustomOverride_rejects_wrong_version);
    RUN_TEST(test_loadCustomOverride_rejects_short_file);
    RUN_TEST(test_loadCustomOverride_ignores_nan_fields);

    return UNITY_END();
}
