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
        loadProfile(1);  // Load first profile (Noisy vCR)
    }

    Serial.printf("[PROFILE] Initialized with %d profiles\n", _profileCount);
    return true;
}

void ProfileManager::initBuiltInProfiles() {
    _profileCount = 0;

    // Profile 1: Noisy vCR (Default)
    TherapyProfile& noisy = _builtInProfiles[_profileCount];
    strcpy(noisy.name, "noisy_vcr");
    strcpy(noisy.description, "Noisy vCR with 23.5% jitter");
    noisy.actuatorType = ActuatorType::LRA;
    noisy.frequencyHz = 175;
    noisy.timeOnMs = 100.0f;
    noisy.timeOffMs = 67.0f;
    noisy.jitterPercent = 23.5f;
    noisy.amplitudeMin = 50;
    noisy.amplitudeMax = 100;
    noisy.sessionDurationMin = 120;
    strcpy(noisy.patternType, "rndp");
    noisy.mirrorPattern = true;
    noisy.numFingers = 5;
    noisy.isDefault = true;
    _profileNames[_profileCount] = _builtInProfiles[_profileCount].name;
    _profileCount++;

    // Profile 2: Standard vCR (No jitter)
    TherapyProfile& standard = _builtInProfiles[_profileCount];
    strcpy(standard.name, "standard_vcr");
    strcpy(standard.description, "Standard vCR without jitter");
    standard.actuatorType = ActuatorType::LRA;
    standard.frequencyHz = 175;
    standard.timeOnMs = 100.0f;
    standard.timeOffMs = 67.0f;
    standard.jitterPercent = 0.0f;
    standard.amplitudeMin = 50;
    standard.amplitudeMax = 100;
    standard.sessionDurationMin = 120;
    strcpy(standard.patternType, "rndp");
    standard.mirrorPattern = true;
    standard.numFingers = 5;
    standard.isDefault = false;
    _profileNames[_profileCount] = _builtInProfiles[_profileCount].name;
    _profileCount++;

    // Profile 3: Gentle (Lower amplitude)
    TherapyProfile& gentle = _builtInProfiles[_profileCount];
    strcpy(gentle.name, "gentle");
    strcpy(gentle.description, "Gentle therapy with lower amplitude");
    gentle.actuatorType = ActuatorType::LRA;
    gentle.frequencyHz = 175;
    gentle.timeOnMs = 80.0f;
    gentle.timeOffMs = 87.0f;
    gentle.jitterPercent = 15.0f;
    gentle.amplitudeMin = 30;
    gentle.amplitudeMax = 70;
    gentle.sessionDurationMin = 60;
    strcpy(gentle.patternType, "sequential");
    gentle.mirrorPattern = true;
    gentle.numFingers = 5;
    gentle.isDefault = false;
    _profileNames[_profileCount] = _builtInProfiles[_profileCount].name;
    _profileCount++;

    // Profile 4: Quick Test (Short duration)
    TherapyProfile& quick = _builtInProfiles[_profileCount];
    strcpy(quick.name, "quick_test");
    strcpy(quick.description, "Quick test session (5 minutes)");
    quick.actuatorType = ActuatorType::LRA;
    quick.frequencyHz = 175;
    quick.timeOnMs = 100.0f;
    quick.timeOffMs = 67.0f;
    quick.jitterPercent = 23.5f;
    quick.amplitudeMin = 50;
    quick.amplitudeMax = 100;
    quick.sessionDurationMin = 5;
    strcpy(quick.patternType, "rndp");
    quick.mirrorPattern = true;
    quick.numFingers = 5;
    quick.isDefault = false;
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
        if (fingers < 1 || fingers > 5) return false;
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

    SettingsData data;
    memset(&data, 0, sizeof(data));

    // Header
    data.magic = SETTINGS_MAGIC;
    data.version = SETTINGS_VERSION;

    // Device role
    data.role = (_deviceRole == DeviceRole::SECONDARY) ? 1 : 0;

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
    File file(InternalFS);
    if (!file.open(SETTINGS_FILE, FILE_O_WRITE)) {
        Serial.println(F("[SETTINGS] Failed to open file for writing"));
        return false;
    }

    size_t written = file.write((uint8_t*)&data, sizeof(data));
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
