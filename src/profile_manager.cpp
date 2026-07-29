/**
 * @file profile_manager.cpp
 * @brief BlueBuzzah therapy profile manager - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "profile_manager.h"
#include "config.h"
#include "fs_backend.h"

// =============================================================================
// CONSTRUCTOR
// =============================================================================

ProfileManager::ProfileManager() :
    _profileCount(0),
    _currentProfileId(0),
    _profileLoaded(false),
    _storageAvailable(false),
    _deviceRole(DeviceRole::PRIMARY),
    _roleFromSettings(false),
    _therapyLedOff(false),
    _debugMode(false)
{
    memset(_profileNames, 0, sizeof(_profileNames));
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool ProfileManager::begin(bool loadFromStorage) {
    // Initialize built-in profiles
    initBuiltInProfiles();

    // Try to mount the storage backend
    if (fsb::begin()) {
        _storageAvailable = true;
        Serial.println(F("[PROFILE] Storage mounted"));

        // Load settings if requested
        if (loadFromStorage) {
            loadSettings();
        }
    } else {
        _storageAvailable = false;
        Serial.println(F("[PROFILE] Storage not available, using defaults"));
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
    regular.numFingers = MAX_ACTUATORS;
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
    noisy.numFingers = MAX_ACTUATORS;
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
    hybrid.numFingers = MAX_ACTUATORS;
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
    custom.numFingers = MAX_ACTUATORS;
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
    gentle.numFingers = MAX_ACTUATORS;
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
    quick.numFingers = MAX_ACTUATORS;
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

    // Custom carries user-edited parameters persisted outside settings.bin.
    if (profileId == CUSTOM_PROFILE_ID) {
        loadCustomOverride();
    }

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
        *c = static_cast<char>(toupper(*c));
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
        if (freq < PARAM_MIN_FREQUENCY_HZ || freq > PARAM_MAX_FREQUENCY_HZ) return false;
        _currentProfile.frequencyHz = static_cast<uint16_t>(freq);
    }
    else if (strcmp(paramUpper, "ON") == 0) {
        float onTime = static_cast<float>(atof(value));
        if (onTime < PARAM_MIN_TIME_ON_MS || onTime > PARAM_MAX_TIME_ON_MS) return false;
        _currentProfile.timeOnMs = onTime;
    }
    else if (strcmp(paramUpper, "OFF") == 0) {
        float offTime = static_cast<float>(atof(value));
        if (offTime < PARAM_MIN_TIME_OFF_MS || offTime > PARAM_MAX_TIME_OFF_MS) return false;
        _currentProfile.timeOffMs = offTime;
    }
    else if (strcmp(paramUpper, "SESSION") == 0) {
        int duration = atoi(value);
        if (duration < PARAM_MIN_SESSION_MIN || duration > PARAM_MAX_SESSION_MIN) return false;
        _currentProfile.sessionDurationMin = static_cast<uint16_t>(duration);
    }
    else if (strcmp(paramUpper, "AMPMIN") == 0) {
        int amp = atoi(value);
        if (amp < PARAM_MIN_AMPLITUDE_PCT || amp > MAX_AMPLITUDE) return false;
        if (static_cast<uint8_t>(amp) > _currentProfile.amplitudeMax) return false;
        _currentProfile.amplitudeMin = static_cast<uint8_t>(amp);
    }
    else if (strcmp(paramUpper, "AMPMAX") == 0) {
        int amp = atoi(value);
        if (amp < PARAM_MIN_AMPLITUDE_PCT || amp > MAX_AMPLITUDE) return false;
        if (static_cast<uint8_t>(amp) < _currentProfile.amplitudeMin) return false;
        _currentProfile.amplitudeMax = static_cast<uint8_t>(amp);
    }
    else if (strcmp(paramUpper, "PATTERN") == 0) {
        if (strcasecmp(value, "rndp") == 0 ||
            strcasecmp(value, "sequential") == 0 ||
            strcasecmp(value, "mirrored") == 0) {
            strncpy(_currentProfile.patternType, value, PATTERN_TYPE_MAX - 1);
            _currentProfile.patternType[PATTERN_TYPE_MAX - 1] = '\0';
            // Lowercase for consistency
            for (char* c = _currentProfile.patternType; *c; c++) {
                *c = static_cast<char>(tolower(*c));
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
        float jitter = static_cast<float>(atof(value));
        if (jitter < 0.0f || jitter > PARAM_MAX_JITTER_PCT) return false;
        _currentProfile.jitterPercent = jitter;
    }
    else if (strcmp(paramUpper, "FINGERS") == 0) {
        int fingers = atoi(value);
        if (fingers < 1 || fingers > MAX_ACTUATORS) return false;
        _currentProfile.numFingers = static_cast<uint8_t>(fingers);
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
        data.patternType[sizeof(data.patternType) - 1] = '\0';
        data.mirrorPattern = _currentProfile.mirrorPattern ? 1 : 0;
        data.numFingers = _currentProfile.numFingers;
    }

    // Therapy LED control
    data.therapyLedOff = _therapyLedOff ? 1 : 0;

    // Debug mode
    data.debugMode = _debugMode ? 1 : 0;

    // Write binary data (overwrites from the start of the file)
    if (!fsb::writeFile(SETTINGS_FILE, (const uint8_t*)&data, sizeof(data))) {
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

    if (!fsb::exists(SETTINGS_FILE)) {
        Serial.println(F("[SETTINGS] No settings file found"));
        return false;
    }

    SettingsData data;
    size_t bytesRead = 0;
    if (!fsb::readFile(SETTINGS_FILE, (uint8_t*)&data, sizeof(data), bytesRead)) {
        Serial.println(F("[SETTINGS] Failed to open file"));
        return false;
    }

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

        // Validate against the current parameter bounds - reject fields outside them.
        // This protects against corrupted or stale settings from old firmware versions.
        if (data.timeOnMs >= PARAM_MIN_TIME_ON_MS && data.timeOnMs <= PARAM_MAX_TIME_ON_MS) {
            _currentProfile.timeOnMs = data.timeOnMs;
        } else {
            Serial.printf("[SETTINGS] WARNING: Invalid timeOnMs %.1f, keeping default %.1f\n",
                          data.timeOnMs, _currentProfile.timeOnMs);
        }
        if (data.timeOffMs >= PARAM_MIN_TIME_OFF_MS && data.timeOffMs <= PARAM_MAX_TIME_OFF_MS) {
            _currentProfile.timeOffMs = data.timeOffMs;
        } else {
            Serial.printf("[SETTINGS] WARNING: Invalid timeOffMs %.1f, keeping default %.1f\n",
                          data.timeOffMs, _currentProfile.timeOffMs);
        }
        if (data.jitterPercent >= 0.0f && data.jitterPercent <= PARAM_MAX_JITTER_PCT) {
            _currentProfile.jitterPercent = data.jitterPercent;
        } else {
            Serial.printf("[SETTINGS] WARNING: Invalid jitterPercent %.1f, keeping default %.1f\n",
                          data.jitterPercent, _currentProfile.jitterPercent);
        }
        if (data.frequencyHz >= PARAM_MIN_FREQUENCY_HZ && data.frequencyHz <= PARAM_MAX_FREQUENCY_HZ) {
            _currentProfile.frequencyHz = data.frequencyHz;
        } else {
            Serial.printf("[SETTINGS] WARNING: Invalid frequencyHz %d, keeping default %d\n",
                          data.frequencyHz, _currentProfile.frequencyHz);
        }
        if (data.amplitudeMin >= PARAM_MIN_AMPLITUDE_PCT &&
            data.amplitudeMax <= MAX_AMPLITUDE &&
            data.amplitudeMin <= data.amplitudeMax) {
            _currentProfile.amplitudeMin = data.amplitudeMin;
            _currentProfile.amplitudeMax = data.amplitudeMax;
        } else {
            Serial.println(F("[SETTINGS] WARNING: Invalid amplitude range, keeping defaults"));
        }
        if (data.sessionDurationMin >= PARAM_MIN_SESSION_MIN &&
            data.sessionDurationMin <= PARAM_MAX_SESSION_MIN) {
            _currentProfile.sessionDurationMin = data.sessionDurationMin;
        } else {
            Serial.printf("[SETTINGS] WARNING: Invalid sessionDurationMin %d, keeping default %d\n",
                          data.sessionDurationMin, _currentProfile.sessionDurationMin);
        }
        if (data.numFingers >= 1 && data.numFingers <= MAX_ACTUATORS) {
            _currentProfile.numFingers = data.numFingers;
        } else {
            Serial.printf("[SETTINGS] WARNING: Invalid numFingers %d, keeping default %d\n",
                          data.numFingers, _currentProfile.numFingers);
        }
        strncpy(_currentProfile.patternType, data.patternType, PATTERN_TYPE_MAX - 1);
        _currentProfile.patternType[PATTERN_TYPE_MAX - 1] = '\0';
        _currentProfile.mirrorPattern = (data.mirrorPattern != 0);

        _profileLoaded = true;

        // Log loaded timing for debugging
        Serial.printf("[SETTINGS] Timing: ON=%.1fms, OFF=%.1fms, Jitter=%.1f%%\n",
                      _currentProfile.timeOnMs, _currentProfile.timeOffMs, _currentProfile.jitterPercent);

        // custom.bin is authoritative for the Custom profile: settings.bin's copy of
        // the parameters is overwritten whenever another profile is loaded.
        if (_currentProfileId == CUSTOM_PROFILE_ID) {
            loadCustomOverride();
        }
    }

    // Load therapy LED control setting
    _therapyLedOff = (data.therapyLedOff != 0);
    Serial.printf("[SETTINGS] Therapy LED Off: %s\n", _therapyLedOff ? "true" : "false");

    // Load debug mode setting
    _debugMode = (data.debugMode != 0);
    Serial.printf("[SETTINGS] Debug Mode: %s\n", _debugMode ? "true" : "false");

    Serial.printf("[SETTINGS] Loaded profile: %s\n", _currentProfile.name);
    return true;
}

bool ProfileManager::saveCustomOverride() {
    if (!_storageAvailable) {
        return false;
    }

    CustomOverrideData data{};
    data.magic              = CUSTOM_OVERRIDE_MAGIC;
    data.version            = CUSTOM_OVERRIDE_VERSION;
    data.timeOnMs           = _currentProfile.timeOnMs;
    data.timeOffMs          = _currentProfile.timeOffMs;
    data.jitterPercent      = _currentProfile.jitterPercent;
    data.amplitudeMin       = _currentProfile.amplitudeMin;
    data.amplitudeMax       = _currentProfile.amplitudeMax;
    data.sessionDurationMin = _currentProfile.sessionDurationMin;
    data.numFingers         = _currentProfile.numFingers;
    data.mirrorPattern      = _currentProfile.mirrorPattern ? 1 : 0;

    if (!fsb::writeFile(CUSTOM_OVERRIDE_FILE,
                        reinterpret_cast<const uint8_t*>(&data), sizeof(data))) {
        Serial.println(F("[CUSTOM] Write failed"));
        return false;
    }

    Serial.println(F("[CUSTOM] Override saved"));
    return true;
}

bool ProfileManager::loadCustomOverride() {
    if (!_storageAvailable || !fsb::exists(CUSTOM_OVERRIDE_FILE)) {
        return false;
    }

    CustomOverrideData data{};
    size_t bytesRead = 0;
    if (!fsb::readFile(CUSTOM_OVERRIDE_FILE,
                       reinterpret_cast<uint8_t*>(&data), sizeof(data), bytesRead)) {
        return false;
    }
    if (bytesRead != sizeof(data) ||
        data.magic != CUSTOM_OVERRIDE_MAGIC ||
        data.version != CUSTOM_OVERRIDE_VERSION) {
        Serial.println(F("[CUSTOM] Override rejected (bad header)"));
        return false;
    }

    // Revalidate against current bounds -- an override written by older
    // firmware may fall outside them. Out-of-range fields keep the built-in value.
    if (data.timeOnMs >= PARAM_MIN_TIME_ON_MS && data.timeOnMs <= PARAM_MAX_TIME_ON_MS) {
        _currentProfile.timeOnMs = data.timeOnMs;
    }
    if (data.timeOffMs >= PARAM_MIN_TIME_OFF_MS && data.timeOffMs <= PARAM_MAX_TIME_OFF_MS) {
        _currentProfile.timeOffMs = data.timeOffMs;
    }
    if (data.jitterPercent >= 0.0f && data.jitterPercent <= PARAM_MAX_JITTER_PCT) {
        _currentProfile.jitterPercent = data.jitterPercent;
    }
    if (data.amplitudeMin >= PARAM_MIN_AMPLITUDE_PCT &&
        data.amplitudeMax <= MAX_AMPLITUDE &&
        data.amplitudeMin <= data.amplitudeMax) {
        _currentProfile.amplitudeMin = data.amplitudeMin;
        _currentProfile.amplitudeMax = data.amplitudeMax;
    }
    if (data.sessionDurationMin >= PARAM_MIN_SESSION_MIN &&
        data.sessionDurationMin <= PARAM_MAX_SESSION_MIN) {
        _currentProfile.sessionDurationMin = data.sessionDurationMin;
    }
    if (data.numFingers >= 1 && data.numFingers <= MAX_ACTUATORS) {
        _currentProfile.numFingers = data.numFingers;
    }
    _currentProfile.mirrorPattern = (data.mirrorPattern != 0);

    Serial.println(F("[CUSTOM] Override applied"));
    return true;
}

bool ProfileManager::clearCustomOverride() {
    if (!_storageAvailable) {
        return false;
    }
    if (!fsb::exists(CUSTOM_OVERRIDE_FILE)) {
        return true;  // Already absent; nothing to do.
    }
    return fsb::removeFile(CUSTOM_OVERRIDE_FILE);
}
