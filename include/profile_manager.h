/**
 * @file profile_manager.h
 * @brief BlueBuzzah therapy profile manager - Profile storage and management
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * Manages therapy profiles including:
 * - Built-in profiles (stored in flash)
 * - Custom profile parameters
 * - Profile loading and validation
 * - LittleFS storage for settings
 */

#ifndef PROFILE_MANAGER_H
#define PROFILE_MANAGER_H

#include <Arduino.h>
#include "types.h"
#include "config.h"

// =============================================================================
// CONSTANTS
// =============================================================================

// Maximum number of profiles
#define MAX_PROFILES 5

// Profile name max length
#define PROFILE_NAME_MAX 32

// Profile description max length
#define PROFILE_DESC_MAX 64

// Pattern type string max length
#define PATTERN_TYPE_MAX 16

// Settings file path and format
#define SETTINGS_FILE "/settings.bin"
#define SETTINGS_MAGIC 0xBB
#define SETTINGS_VERSION 1

// =============================================================================
// BINARY SETTINGS STRUCTURE (for persistent storage)
// =============================================================================

/**
 * @brief Packed binary settings structure for InternalFS storage
 *
 * This compact struct replaces JSON serialization, saving ~10-15KB flash
 * by eliminating the ArduinoJson dependency.
 */
struct __attribute__((packed)) SettingsData {
    uint8_t magic;               // 0xBB to validate file
    uint8_t version;             // Format version for future compatibility
    uint8_t role;                // 0=PRIMARY, 1=SECONDARY
    uint8_t profileId;           // 1-4 (built-in profile)
    uint8_t actuatorType;        // 0=LRA, 1=ERM
    uint16_t frequencyHz;        // 50-300
    float timeOnMs;              // Burst duration
    float timeOffMs;             // Inter-burst interval
    float jitterPercent;         // 0-100
    uint8_t amplitudeMin;        // 0-100
    uint8_t amplitudeMax;        // 0-100
    uint16_t sessionDurationMin; // Minutes
    char patternType[16];        // "rndp", "sequential", "mirrored"
    uint8_t mirrorPattern;       // 0 or 1
    uint8_t numFingers;          // 1-5
    uint8_t reserved[4];         // Future use, padding
};

// =============================================================================
// THERAPY PROFILE STRUCTURE
// =============================================================================

/**
 * @brief Therapy profile configuration
 *
 * Contains all parameters for a therapy session:
 * - Actuator settings (type, frequency)
 * - Timing parameters (on/off times, jitter)
 * - Session settings (duration, pattern)
 */
struct TherapyProfile {
    char name[PROFILE_NAME_MAX];
    char description[PROFILE_DESC_MAX];

    // Actuator settings
    ActuatorType actuatorType;
    uint16_t frequencyHz;

    // Timing parameters
    float timeOnMs;
    float timeOffMs;
    float jitterPercent;

    // Amplitude settings
    uint8_t amplitudeMin;
    uint8_t amplitudeMax;

    // Session settings
    uint16_t sessionDurationMin;  // Duration in minutes
    char patternType[PATTERN_TYPE_MAX];
    bool mirrorPattern;
    uint8_t numFingers;

    // Metadata
    bool isDefault;

    /**
     * @brief Default constructor with noisy vCR defaults
     */
    TherapyProfile() :
        actuatorType(ActuatorType::LRA),
        frequencyHz(175),
        timeOnMs(100.0f),
        timeOffMs(67.0f),
        jitterPercent(23.5f),
        amplitudeMin(50),
        amplitudeMax(100),
        sessionDurationMin(120),  // 2 hours
        mirrorPattern(true),
        numFingers(5),
        isDefault(false)
    {
        strcpy(name, "default");
        strcpy(description, "Default profile");
        strcpy(patternType, "rndp");
    }

    /**
     * @brief Copy constructor
     */
    TherapyProfile(const TherapyProfile& other) {
        strcpy(name, other.name);
        strcpy(description, other.description);
        strcpy(patternType, other.patternType);
        actuatorType = other.actuatorType;
        frequencyHz = other.frequencyHz;
        timeOnMs = other.timeOnMs;
        timeOffMs = other.timeOffMs;
        jitterPercent = other.jitterPercent;
        amplitudeMin = other.amplitudeMin;
        amplitudeMax = other.amplitudeMax;
        sessionDurationMin = other.sessionDurationMin;
        mirrorPattern = other.mirrorPattern;
        numFingers = other.numFingers;
        isDefault = other.isDefault;
    }

    /**
     * @brief Assignment operator
     */
    TherapyProfile& operator=(const TherapyProfile& other) {
        if (this != &other) {
            strcpy(name, other.name);
            strcpy(description, other.description);
            strcpy(patternType, other.patternType);
            actuatorType = other.actuatorType;
            frequencyHz = other.frequencyHz;
            timeOnMs = other.timeOnMs;
            timeOffMs = other.timeOffMs;
            jitterPercent = other.jitterPercent;
            amplitudeMin = other.amplitudeMin;
            amplitudeMax = other.amplitudeMax;
            sessionDurationMin = other.sessionDurationMin;
            mirrorPattern = other.mirrorPattern;
            numFingers = other.numFingers;
            isDefault = other.isDefault;
        }
        return *this;
    }
};

// =============================================================================
// PROFILE MANAGER CLASS
// =============================================================================

/**
 * @brief Therapy profile manager
 *
 * Manages therapy profiles including:
 * - Built-in profiles (Noisy vCR, Standard vCR, Gentle)
 * - Profile loading by ID or name
 * - Parameter modification
 * - Settings persistence via LittleFS
 *
 * Usage:
 *   ProfileManager profiles;
 *   profiles.begin();
 *
 *   // List available profiles
 *   uint8_t count;
 *   const char** names = profiles.getProfileNames(&count);
 *
 *   // Load profile by ID
 *   profiles.loadProfile(1);
 *
 *   // Get current profile
 *   const TherapyProfile* profile = profiles.getCurrentProfile();
 *
 *   // Modify parameters
 *   profiles.setParameter("JITTER", "30.0");
 */
class ProfileManager {
public:
    ProfileManager();

    /**
     * @brief Initialize profile manager
     * @param loadFromStorage Load settings from LittleFS if true
     * @return true if initialization successful
     */
    bool begin(bool loadFromStorage = true);

    // =========================================================================
    // PROFILE ACCESS
    // =========================================================================

    /**
     * @brief Get array of profile names
     * @param count Output parameter for number of profiles
     * @return Array of profile name strings
     */
    const char** getProfileNames(uint8_t* count);

    /**
     * @brief Load profile by ID (1-based)
     * @param profileId Profile ID (1, 2, 3, etc.)
     * @return true if profile loaded successfully
     */
    bool loadProfile(uint8_t profileId);

    /**
     * @brief Load profile by name
     * @param name Profile name
     * @return true if profile found and loaded
     */
    bool loadProfileByName(const char* name);

    /**
     * @brief Get current profile
     * @return Pointer to current profile, or nullptr if none loaded
     */
    const TherapyProfile* getCurrentProfile() const;

    /**
     * @brief Get current profile name
     * @return Profile name string
     */
    const char* getCurrentProfileName() const;

    /**
     * @brief Get number of available profiles
     */
    uint8_t getProfileCount() const { return _profileCount; }

    // =========================================================================
    // PARAMETER MODIFICATION
    // =========================================================================

    /**
     * @brief Set individual parameter by name
     * @param paramName Parameter name (case insensitive)
     * @param value Value string
     * @return true if parameter set successfully
     *
     * Valid parameters:
     * - TYPE: LRA or ERM
     * - FREQ: Frequency in Hz (50-300)
     * - ON: On time in ms (10-1000)
     * - OFF: Off time in ms (10-1000)
     * - SESSION: Session duration in minutes (1-240)
     * - AMPMIN: Min amplitude 0-100
     * - AMPMAX: Max amplitude 0-100
     * - PATTERN: rndp, sequential, or mirrored
     * - MIRROR: 0 or 1
     * - JITTER: Jitter percent (0-100)
     * - FINGERS: Number of fingers (1-5)
     */
    bool setParameter(const char* paramName, const char* value);

    /**
     * @brief Reset current profile to defaults
     */
    void resetToDefaults();

    // =========================================================================
    // SETTINGS PERSISTENCE
    // =========================================================================

    /**
     * @brief Save current profile settings to LittleFS
     * @return true if saved successfully
     */
    bool saveSettings();

    /**
     * @brief Load profile settings from LittleFS
     * @return true if loaded successfully
     */
    bool loadSettings();

    /**
     * @brief Check if LittleFS is available
     */
    bool isStorageAvailable() const { return _storageAvailable; }

    // =========================================================================
    // DEVICE ROLE
    // =========================================================================

    /**
     * @brief Get configured device role
     * @return DeviceRole from settings (defaults to PRIMARY if not set)
     */
    DeviceRole getDeviceRole() const { return _deviceRole; }

    /**
     * @brief Set device role (does not persist until saveSettings called)
     * @param role DeviceRole to set
     */
    void setDeviceRole(DeviceRole role) { _deviceRole = role; }

    /**
     * @brief Check if device role was loaded from settings
     * @return true if role was found in settings file
     */
    bool hasStoredRole() const { return _roleFromSettings; }

private:
    // Built-in profiles
    TherapyProfile _builtInProfiles[MAX_PROFILES];
    uint8_t _profileCount;

    // Profile name pointers for getProfileNames()
    const char* _profileNames[MAX_PROFILES];

    // Current profile (working copy)
    TherapyProfile _currentProfile;
    uint8_t _currentProfileId;
    bool _profileLoaded;

    // Storage state
    bool _storageAvailable;

    // Device role
    DeviceRole _deviceRole;
    bool _roleFromSettings;

    /**
     * @brief Initialize built-in profiles
     */
    void initBuiltInProfiles();

    /**
     * @brief Validate parameter value
     */
    bool validateParameter(const char* paramName, const char* value);
};

#endif // PROFILE_MANAGER_H
