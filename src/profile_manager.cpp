/**
 * @file profile_manager.cpp
 * @brief BlueBuzzah therapy profile manager - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "profile_manager.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

// =============================================================================
// CONSTRUCTOR
// =============================================================================

ProfileManager::ProfileManager() :
    _profileCount(0),
    _currentProfileId(0),
    _profileLoaded(false),
    _storageAvailable(false),
    _deviceRole(DeviceRole::PRIMARY),
    _roleFromSettings(false)
{
    memset(_profileNames, 0, sizeof(_profileNames));
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool ProfileManager::begin(bool loadFromStorage) {
    // Initialize built-in profiles
    initBuiltInProfiles();

    // Try to initialize InternalFileSystem
    if (InternalFS.begin()) {
        _storageAvailable = true;
        Serial.println(F("[PROFILE] InternalFS mounted"));

        // Load settings if requested
        if (loadFromStorage) {
            loadSettings();
        }
    } else {
        _storageAvailable = false;
        Serial.println(F("[PROFILE] InternalFS not available, using defaults"));
    }

    // Load default profile if none loaded
    if (!_profileLoaded) {
        loadProfile(1);  // Load first profile (Regular vCR)
    }

    Serial.printf("[PROFILE] Initialized with %d profiles\n", _profileCount);
    return true;
}

void ProfileManager::initBuiltInProfiles() {
    _profileCount = 0;

    // =========================================================================
    // V1 ORIGINAL PROFILES (research-based vCR therapy)
    // =========================================================================

    // Profile 1: Regular vCR (Default) - Non-mirrored, no jitter
    // Reference: Original v1 defaults_RegVCR.py
    TherapyProfile& regular = _builtInProfiles[_profileCount];
    strcpy(regular.name, "regular_vcr");
    strcpy(regular.description, "Regular vCR - non-mirrored, no jitter");
    regular.actuatorType = ActuatorType::LRA;
    regular.frequencyHz = 250;
    regular.timeOnMs = 100.0f;
    regular.timeOffMs = 67.0f;
    regular.jitterPercent = 0.0f;
    regular.amplitudeMin = 100;
    regular.amplitudeMax = 100;
    regular.sessionDurationMin = 120;
    strcpy(regular.patternType, "rndp");
    regular.mirrorPattern = false;
    regular.numFingers = 4;
    regular.isDefault = true;
    regular.frequencyRandomization = false;
    regular.frequencyMin = 210;
    regular.frequencyMax = 260;
    _profileNames[_profileCount] = _builtInProfiles[_profileCount].name;
    _profileCount++;

    // Profile 2: Noisy vCR - Mirrored with 23.5% jitter
    // Reference: Original v1 defaults_NoisyVCR.py
    TherapyProfile& noisy = _builtInProfiles[_profileCount];
    strcpy(noisy.name, "noisy_vcr");
    strcpy(noisy.description, "Noisy vCR - mirrored with 23.5% jitter");
    noisy.actuatorType = ActuatorType::LRA;
    noisy.frequencyHz = 250;
    noisy.timeOnMs = 100.0f;
    noisy.timeOffMs = 67.0f;
    noisy.jitterPercent = 23.5f;
    noisy.amplitudeMin = 100;
    noisy.amplitudeMax = 100;
    noisy.sessionDurationMin = 120;
    strcpy(noisy.patternType, "rndp");
    noisy.mirrorPattern = true;
    noisy.numFingers = 4;
    noisy.isDefault = false;
    noisy.frequencyRandomization = false;
    noisy.frequencyMin = 210;
    noisy.frequencyMax = 260;
    _profileNames[_profileCount] = _builtInProfiles[_profileCount].name;
    _profileCount++;

    // Profile 3: Hybrid vCR - Non-mirrored with 23.5% jitter
    // Reference: Original v1 defaults_HybridVCR.py
    TherapyProfile& hybrid = _builtInProfiles[_profileCount];
    strcpy(hybrid.name, "hybrid_vcr");
    strcpy(hybrid.description, "Hybrid vCR - non-mirrored with 23.5% jitter");
    hybrid.actuatorType = ActuatorType::LRA;
    hybrid.frequencyHz = 250;
    hybrid.timeOnMs = 100.0f;
    hybrid.timeOffMs = 67.0f;
    hybrid.jitterPercent = 23.5f;
    hybrid.amplitudeMin = 100;
    hybrid.amplitudeMax = 100;
    hybrid.sessionDurationMin = 120;
    strcpy(hybrid.patternType, "rndp");
    hybrid.mirrorPattern = false;
    hybrid.numFingers = 4;
    hybrid.isDefault = false;
    hybrid.frequencyRandomization = false;
    hybrid.frequencyMin = 210;
    hybrid.frequencyMax = 260;
    _profileNames[_profileCount] = _builtInProfiles[_profileCount].name;
    _profileCount++;

    // Profile 4: Custom vCR - Non-mirrored, jitter, amplitude range, freq randomization
    // Reference: Original v1 defaults_CustomVCR.py
    TherapyProfile& custom = _builtInProfiles[_profileCount];
    strcpy(custom.name, "custom_vcr");
    strcpy(custom.description, "Custom vCR - variable amplitude & frequency");
    custom.actuatorType = ActuatorType::LRA;
    custom.frequencyHz = 250;
    custom.timeOnMs = 100.0f;
    custom.timeOffMs = 67.0f;
    custom.jitterPercent = 23.5f;
    custom.amplitudeMin = 70;
    custom.amplitudeMax = 100;
    custom.sessionDurationMin = 120;
    strcpy(custom.patternType, "rndp");
    custom.mirrorPattern = false;
    custom.numFingers = 4;
    custom.isDefault = false;
    custom.frequencyRandomization = true;
    custom.frequencyMin = 210;
    custom.frequencyMax = 260;
    _profileNames[_profileCount] = _builtInProfiles[_profileCount].name;
    _profileCount++;

    // =========================================================================
    // V2 ADDITIONAL PROFILES (convenience/testing)
    // =========================================================================

    // Profile 5: Gentle (Lower amplitude, v2 addition)
    TherapyProfile& gentle = _builtInProfiles[_profileCount];
    strcpy(gentle.name, "gentle");
    strcpy(gentle.description, "Gentle therapy with lower amplitude (v2)");
    gentle.actuatorType = ActuatorType::LRA;
    gentle.frequencyHz = 250;
    gentle.timeOnMs = 80.0f;
    gentle.timeOffMs = 87.0f;
    gentle.jitterPercent = 15.0f;
    gentle.amplitudeMin = 30;
    gentle.amplitudeMax = 70;
    gentle.sessionDurationMin = 60;
    strcpy(gentle.patternType, "sequential");
    gentle.mirrorPattern = true;
    gentle.numFingers = 4;
    gentle.isDefault = false;
    gentle.frequencyRandomization = false;
    gentle.frequencyMin = 210;
    gentle.frequencyMax = 260;
    _profileNames[_profileCount] = _builtInProfiles[_profileCount].name;
    _profileCount++;

    // Profile 6: Quick Test (Short duration, v2 addition)
    TherapyProfile& quick = _builtInProfiles[_profileCount];
    strcpy(quick.name, "quick_test");
    strcpy(quick.description, "Quick test session - 5 minutes (v2)");
    quick.actuatorType = ActuatorType::LRA;
    quick.frequencyHz = 250;
    quick.timeOnMs = 100.0f;
    quick.timeOffMs = 67.0f;
    quick.jitterPercent = 23.5f;
    quick.amplitudeMin = 50;
    quick.amplitudeMax = 100;
    quick.sessionDurationMin = 5;
    strcpy(quick.patternType, "rndp");
    quick.mirrorPattern = true;
    quick.numFingers = 4;
    quick.isDefault = false;
    quick.frequencyRandomization = false;
    quick.frequencyMin = 210;
    quick.frequencyMax = 260;
    _profileNames[_profileCount] = _builtInProfiles[_profileCount].name;
    _profileCount++;
}

// =============================================================================
// PROFILE ACCESS
// =============================================================================

const char** ProfileManager::getProfileNames(uint8_t* count) {
    if (count) {
        *count = _profileCount;
    }
    return _profileNames;
}

bool ProfileManager::loadProfile(uint8_t profileId) {
    if (profileId < 1 || profileId > _profileCount) {
        Serial.printf("[PROFILE] Invalid profile ID: %d\n", profileId);
        return false;
    }

    // Copy built-in profile to current
    _currentProfile = _builtInProfiles[profileId - 1];
    _currentProfileId = profileId;
    _profileLoaded = true;

    Serial.printf("[PROFILE] Loaded: %s (%s)\n",
                  _currentProfile.name,
                  _currentProfile.description);

    return true;
}

bool ProfileManager::loadProfileByName(const char* name) {
    if (!name) {
        return false;
    }

    for (uint8_t i = 0; i < _profileCount; i++) {
        if (strcasecmp(_builtInProfiles[i].name, name) == 0) {
            return loadProfile(i + 1);
        }
    }

    Serial.printf("[PROFILE] Profile not found: %s\n", name);
    return false;
}

const TherapyProfile* ProfileManager::getCurrentProfile() const {
    if (!_profileLoaded) {
        return nullptr;
    }
    return &_currentProfile;
}

const char* ProfileManager::getCurrentProfileName() const {
    if (!_profileLoaded) {
        return "none";
    }
    return _currentProfile.name;
}

// =============================================================================
// PARAMETER MODIFICATION
// =============================================================================

bool ProfileManager::setParameter(const char* paramName, const char* value) {
    if (!paramName || !value || !_profileLoaded) {
        return false;
    }

    // Convert param name to uppercase for comparison
    char paramUpper[32];
    strncpy(paramUpper, paramName, sizeof(paramUpper) - 1);
    paramUpper[sizeof(paramUpper) - 1] = '\0';
    for (char* c = paramUpper; *c; c++) {
        *c = toupper(*c);
    }

    // Validate and set parameter
    if (strcmp(paramUpper, "TYPE") == 0) {
        if (strcasecmp(value, "LRA") == 0) {
            _currentProfile.actuatorType = ActuatorType::LRA;
        } else if (strcasecmp(value, "ERM") == 0) {
            _currentProfile.actuatorType = ActuatorType::ERM;
        } else {
            return false;
        }
    }
    else if (strcmp(paramUpper, "FREQ") == 0) {
        int freq = atoi(value);
        if (freq < 50 || freq > 300) return false;
        _currentProfile.frequencyHz = freq;
    }
    else if (strcmp(paramUpper, "ON") == 0) {
        float onTime = atof(value);
        if (onTime < 10.0f || onTime > 1000.0f) return false;
        _currentProfile.timeOnMs = onTime;
    }
    else if (strcmp(paramUpper, "OFF") == 0) {
        float offTime = atof(value);
        if (offTime < 10.0f || offTime > 1000.0f) return false;
        _currentProfile.timeOffMs = offTime;
    }
    else if (strcmp(paramUpper, "SESSION") == 0) {
        int duration = atoi(value);
        if (duration < 1 || duration > 240) return false;
        _currentProfile.sessionDurationMin = duration;
    }
    else if (strcmp(paramUpper, "AMPMIN") == 0) {
        int amp = atoi(value);
        if (amp < 0 || amp > 100) return false;
        _currentProfile.amplitudeMin = amp;
    }
    else if (strcmp(paramUpper, "AMPMAX") == 0) {
        int amp = atoi(value);
        if (amp < 0 || amp > 100) return false;
        _currentProfile.amplitudeMax = amp;
    }
    else if (strcmp(paramUpper, "PATTERN") == 0) {
        if (strcasecmp(value, "rndp") == 0 ||
            strcasecmp(value, "sequential") == 0 ||
            strcasecmp(value, "mirrored") == 0) {
            strncpy(_currentProfile.patternType, value, PATTERN_TYPE_MAX - 1);
            _currentProfile.patternType[PATTERN_TYPE_MAX - 1] = '\0';
            // Lowercase for consistency
            for (char* c = _currentProfile.patternType; *c; c++) {
                *c = tolower(*c);
            }
        } else {
            return false;
        }
    }
    else if (strcmp(paramUpper, "MIRROR") == 0) {
        int mirror = atoi(value);
        _currentProfile.mirrorPattern = (mirror != 0);
    }
    else if (strcmp(paramUpper, "JITTER") == 0) {
        float jitter = atof(value);
        if (jitter < 0.0f || jitter > 100.0f) return false;
        _currentProfile.jitterPercent = jitter;
    }
    else if (strcmp(paramUpper, "FINGERS") == 0) {
        int fingers = atoi(value);
        if (fingers < 1 || fingers > 4) return false;
        _currentProfile.numFingers = fingers;
    }
    else {
        Serial.printf("[PROFILE] Unknown parameter: %s\n", paramName);
        return false;
    }

    Serial.printf("[PROFILE] Set %s = %s\n", paramUpper, value);
    return true;
}

void ProfileManager::resetToDefaults() {
    if (_currentProfileId > 0 && _currentProfileId <= _profileCount) {
        _currentProfile = _builtInProfiles[_currentProfileId - 1];
        Serial.printf("[PROFILE] Reset to defaults: %s\n", _currentProfile.name);
    }
}

// =============================================================================
// SETTINGS PERSISTENCE (Binary format)
// =============================================================================

bool ProfileManager::saveSettings() {
    if (!_storageAvailable) {
        return false;
    }

    SettingsData data{};

    // Header
    data.magic = SETTINGS_MAGIC;
    data.version = SETTINGS_VERSION;

    // Device role
    data.role = (_deviceRole == DeviceRole::SECONDARY) ? 1 : 0;
    Serial.printf("[SETTINGS] Saving role: %s (value=%d)\n",
                  deviceRoleToString(_deviceRole), data.role);

    // Profile data
    data.profileId = _currentProfileId;

    if (_profileLoaded) {
        data.actuatorType = (_currentProfile.actuatorType == ActuatorType::ERM) ? 1 : 0;
        data.frequencyHz = _currentProfile.frequencyHz;
        data.timeOnMs = _currentProfile.timeOnMs;
        data.timeOffMs = _currentProfile.timeOffMs;
        data.jitterPercent = _currentProfile.jitterPercent;
        data.amplitudeMin = _currentProfile.amplitudeMin;
        data.amplitudeMax = _currentProfile.amplitudeMax;
        data.sessionDurationMin = _currentProfile.sessionDurationMin;
        strncpy(data.patternType, _currentProfile.patternType, sizeof(data.patternType) - 1);
        data.mirrorPattern = _currentProfile.mirrorPattern ? 1 : 0;
        data.numFingers = _currentProfile.numFingers;
    }

    // Write binary data
    // Note: FILE_O_WRITE seeks to end-of-file, so we must seek(0) to overwrite
    File file(InternalFS);
    if (!file.open(SETTINGS_FILE, FILE_O_WRITE)) {
        Serial.println(F("[SETTINGS] Failed to open file for writing"));
        return false;
    }

    // Seek to beginning to overwrite (FILE_O_WRITE positions at EOF)
    file.seek(0);

    size_t written = file.write((uint8_t*)&data, sizeof(data));
    file.flush();  // Ensure data is written to flash before close
    file.close();

    if (written != sizeof(data)) {
        Serial.println(F("[SETTINGS] Write failed"));
        return false;
    }

    Serial.println(F("[SETTINGS] Saved"));
    return true;
}

bool ProfileManager::loadSettings() {
    if (!_storageAvailable) {
        return false;
    }

    if (!InternalFS.exists(SETTINGS_FILE)) {
        Serial.println(F("[SETTINGS] No settings file found"));
        return false;
    }

    File file(InternalFS);
    if (!file.open(SETTINGS_FILE, FILE_O_READ)) {
        Serial.println(F("[SETTINGS] Failed to open file"));
        return false;
    }

    SettingsData data;
    size_t bytesRead = file.read((uint8_t*)&data, sizeof(data));
    file.close();

    if (bytesRead != sizeof(data) || data.magic != SETTINGS_MAGIC) {
        Serial.println(F("[SETTINGS] Invalid file format"));
        return false;
    }

    // Load device role
    _deviceRole = (data.role == 1) ? DeviceRole::SECONDARY : DeviceRole::PRIMARY;
    _roleFromSettings = true;
    Serial.printf("[SETTINGS] Role: %s\n", deviceRoleToString(_deviceRole));

    // Load profile
    if (data.profileId > 0 && data.profileId <= _profileCount) {
        _currentProfile = _builtInProfiles[data.profileId - 1];
        _currentProfileId = data.profileId;

        // Apply saved customizations
        _currentProfile.actuatorType = (data.actuatorType == 1) ? ActuatorType::ERM : ActuatorType::LRA;
        _currentProfile.frequencyHz = data.frequencyHz;
        _currentProfile.timeOnMs = data.timeOnMs;
        _currentProfile.timeOffMs = data.timeOffMs;
        _currentProfile.jitterPercent = data.jitterPercent;
        _currentProfile.amplitudeMin = data.amplitudeMin;
        _currentProfile.amplitudeMax = data.amplitudeMax;
        _currentProfile.sessionDurationMin = data.sessionDurationMin;
        strncpy(_currentProfile.patternType, data.patternType, PATTERN_TYPE_MAX - 1);
        _currentProfile.mirrorPattern = (data.mirrorPattern != 0);
        _currentProfile.numFingers = data.numFingers;

        _profileLoaded = true;
    }

    Serial.printf("[SETTINGS] Loaded profile: %s\n", _currentProfile.name);
    return true;
}
