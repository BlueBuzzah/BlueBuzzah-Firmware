/**
 * @file therapy_engine.h
 * @brief BlueBuzzah therapy engine - Pattern generation and execution
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * Implements therapy pattern generation and execution:
 * - Random permutation (RNDP) patterns for noisy vCR
 * - Sequential patterns
 * - Mirrored bilateral patterns
 * - Timing with jitter support
 * - Callback-driven motor control
 */

#ifndef THERAPY_ENGINE_H
#define THERAPY_ENGINE_H

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include <vector>
#include <ranges>
#include <cassert>

// =============================================================================
// BUZZ FLOW STATE
// =============================================================================

/**
 * @brief State machine for buzz execution flow control
 *
 * v1 macrocycle timing model:
 *   A macrocycle consists of 3 patterns followed by double TIME_RELAX:
 *
 *   [Pattern 1] -> [Pattern 2] -> [Pattern 3] -> [Relax] -> [Relax]
 *      668ms        668ms         668ms       668ms     668ms   = 3340ms
 *
 *   Within each pattern (4 fingers):
 *     IDLE -> Send BUZZ, activate motor -> ACTIVE
 *     ACTIVE -> Wait TIME_ON (100ms) -> deactivate motor -> WAITING_OFF
 *     WAITING_OFF -> Wait TIME_OFF + jitter (67ms +/- jitter) -> IDLE (next finger)
 *
 *   After pattern completes:
 *     If patterns < 3: -> generate new pattern -> IDLE (NO relaxation)
 *     If patterns == 3: -> WAITING_RELAX -> Wait 2x TIME_RELAX (1336ms) -> IDLE (new macrocycle)
 */
enum class BuzzFlowState : uint8_t {
    IDLE = 0,           // Ready to send next BUZZ
    ACTIVE,             // Motor running, waiting for TIME_ON (burst duration)
    WAITING_OFF,        // Motor off, waiting for TIME_OFF + jitter before next finger
    WAITING_RELAX       // Pattern complete, waiting TIME_RELAX before next cycle
};

// =============================================================================
// PATTERN CONSTANTS
// =============================================================================

constexpr const static size_t PATTERN_MAX_FINGERS = 5; // v1 uses 4 fingers per hand (no pinky)
constexpr const static size_t DEFAULT_NUM_FINGERS = 4;
#define PATTERN_TYPE_RNDP 0
#define PATTERN_TYPE_SEQUENTIAL 1
#define PATTERN_TYPE_MIRRORED 2

// =============================================================================
// PATTERN STRUCTURE
// =============================================================================

/**
 * @brief Generated therapy pattern
 *
 * Contains finger sequences for both hands with timing information.
 *
 * Timing model (matching v1 original):
 *   For each finger: MOTOR_ON(burstDurationMs) → MOTOR_OFF(timeOffMs[i] with jitter)
 *   After all fingers: Wait interBurstIntervalMs (TIME_RELAX = 668ms)
 */
struct [[nodiscard]] Pattern {
    std::vector<uint8_t> primarySequence;
    std::vector<uint8_t> secondarySequence;
    std::vector<float> timeOffMs;   // TIME_OFF + jitter for each finger (v1: 67ms ± jitter)
    uint8_t numFingers;
    float burstDurationMs;                  // TIME_ON (v1: 100ms)
    float interBurstIntervalMs;             // TIME_RELAX after pattern cycle (v1: 668ms fixed)

    Pattern(uint8_t _numFingers = DEFAULT_NUM_FINGERS) :
        primarySequence(std::vector<uint8_t>(_numFingers)),
        secondarySequence(std::vector<uint8_t>(_numFingers)),
        timeOffMs(std::vector<float>(_numFingers, 67.0f)),
        numFingers(_numFingers),
        burstDurationMs(100.0f),
        interBurstIntervalMs(668.0f)
    {
        assert(primarySequence.size() == primarySequence.size() && primarySequence.size() == timeOffMs.size());
        for (uint8_t i = 0; i < primarySequence.size(); i++) {
            primarySequence[i] = i;
            secondarySequence[i] = i;
        }
    }

    /**
     * @brief Get total pattern duration in milliseconds
     */
    float getTotalDurationMs() const {
        float total = 0;
        for (int i = 0; i < numFingers; i++) {
            total += burstDurationMs + timeOffMs[i];
        }
        return total + interBurstIntervalMs;  // Include TIME_RELAX at end
    }

    /**
     * @brief Get finger pair at specified index
     * @param index Pattern step index
     * @param primaryFinger Output PRIMARY device finger index
     * @param secondaryFinger Output SECONDARY device finger index
     */
    void getFingerPair(uint8_t index, uint8_t& primaryFinger, uint8_t& secondaryFinger) const {
        if (index < numFingers) {
            primaryFinger = primarySequence[index];
            secondaryFinger = secondarySequence[index];
        }
    }
};

// =============================================================================
// PATTERN GENERATION FUNCTIONS
// =============================================================================

/**
 * @brief Fisher-Yates shuffle for array
 * @param arr Array to shuffle
 */
constexpr void shuffleArray(std::span<uint8_t> arr);

/**
 * @brief Generate random permutation (RNDP) pattern
 *
 * Each finger activated exactly once per cycle in randomized order.
 * Used for noisy vCR therapy.
 *
 * @param numFingers Number of fingers per hand (1-4)
 * @param timeOnMs Vibration burst duration
 * @param timeOffMs Time between bursts
 * @param jitterPercent Timing jitter percentage (0-100)
 * @param mirrorPattern If true, same finger on both hands (noisy vCR)
 * @return Generated pattern
 */
Pattern generateRandomPermutation(
    uint8_t numFingers = 4,
    float timeOnMs = 100.0f,
    float timeOffMs = 67.0f,
    float jitterPercent = 0.0f,
    bool mirrorPattern = false
);

/**
 * @brief Generate sequential pattern
 *
 * Fingers activated in order: 0->1->2->3 (or reverse)
 *
 * @param numFingers Number of fingers per hand (1-4)
 * @param timeOnMs Vibration burst duration
 * @param timeOffMs Time between bursts
 * @param jitterPercent Timing jitter percentage (0-100)
 * @param mirrorPattern If true, same sequence for both hands
 * @param reverse If true, reverse order (3->0)
 * @return Generated pattern
 */
Pattern generateSequentialPattern(
    uint8_t numFingers = 4,
    float timeOnMs = 100.0f,
    float timeOffMs = 67.0f,
    float jitterPercent = 0.0f,
    bool mirrorPattern = false,
    bool reverse = false
);

/**
 * @brief Generate mirrored bilateral pattern
 *
 * Both hands use identical finger sequences.
 *
 * @param numFingers Number of fingers per hand (1-4)
 * @param timeOnMs Vibration burst duration
 * @param timeOffMs Time between bursts
 * @param jitterPercent Timing jitter percentage (0-100)
 * @param randomize If true, randomize sequence
 * @return Generated pattern
 */
Pattern generateMirroredPattern(
    uint8_t numFingers = 4,
    float timeOnMs = 100.0f,
    float timeOffMs = 67.0f,
    float jitterPercent = 0.0f,
    bool randomize = true
);

// =============================================================================
// CALLBACK TYPES
// =============================================================================

// Callback for sending sync commands to SECONDARY
typedef void (*SendCommandCallback)(const char* commandType, uint8_t primaryFinger, uint8_t secondaryFinger, uint8_t amplitude, uint32_t durationMs, uint32_t seq, uint16_t frequencyHz);

// Callback for activating haptic motor
typedef void (*ActivateCallback)(uint8_t finger, uint8_t amplitude);

// Callback for deactivating haptic motor
typedef void (*DeactivateCallback)(uint8_t finger);

// Callback for cycle completion
typedef void (*CycleCompleteCallback)(uint32_t cycleCount);

// Callback for setting motor frequency (for Custom vCR frequency randomization)
// Called at start of each pattern cycle when frequencyRandomization is enabled
typedef void (*SetFrequencyCallback)(uint8_t finger, uint16_t frequencyHz);

// Callback for macrocycle start (for PING/PONG latency measurement)
// Called at the start of each macrocycle before the first pattern
typedef void (*MacrocycleStartCallback)(uint32_t macrocycleCount);

// =============================================================================
// THERAPY ENGINE CLASS
// =============================================================================

/**
 * @brief Simplified therapy execution engine
 *
 * Executes therapy patterns with precise timing and bilateral synchronization.
 *
 * Usage:
 *   TherapyEngine engine;
 *   engine.setActivateCallback(onActivate);
 *   engine.setDeactivateCallback(onDeactivate);
 *   engine.startSession(7200, PATTERN_TYPE_RNDP, 100.0f, 67.0f, 23.5f);
 *
 *   // In loop:
 *   engine.update();
 */
class TherapyEngine {
public:
    TherapyEngine();

    // =========================================================================
    // CALLBACKS
    // =========================================================================

    /**
     * @brief Set callback for sending sync commands
     */
    void setSendCommandCallback(SendCommandCallback callback);

    /**
     * @brief Set callback for activating haptic motor
     */
    void setActivateCallback(ActivateCallback callback);

    /**
     * @brief Set callback for deactivating haptic motor
     */
    void setDeactivateCallback(DeactivateCallback callback);

    /**
     * @brief Set callback for cycle completion
     */
    void setCycleCompleteCallback(CycleCompleteCallback callback);

    /**
     * @brief Set callback for frequency changes (Custom vCR frequency randomization)
     */
    void setSetFrequencyCallback(SetFrequencyCallback callback);

    /**
     * @brief Set callback for macrocycle start (PING/PONG latency measurement)
     */
    void setMacrocycleStartCallback(MacrocycleStartCallback callback);

    /**
     * @brief Enable/disable frequency randomization (Custom vCR feature)
     * @param enabled Enable frequency randomization
     * @param minHz Minimum frequency (default 210 Hz)
     * @param maxHz Maximum frequency (default 260 Hz)
     */
    void setFrequencyRandomization(bool enabled, uint16_t minHz = 210, uint16_t maxHz = 260);

    // =========================================================================
    // SESSION CONTROL
    // =========================================================================

    /**
     * @brief Start therapy session
     * @param durationSec Total session duration in seconds
     * @param patternType Pattern type (PATTERN_TYPE_RNDP, etc.)
     * @param timeOnMs Vibration burst duration
     * @param timeOffMs Time between bursts
     * @param jitterPercent Timing jitter percentage
     * @param numFingers Number of fingers per hand
     * @param mirrorPattern If true, same finger on both hands
     * @param amplitudeMin Minimum motor amplitude (0-100)
     * @param amplitudeMax Maximum motor amplitude (0-100)
     */
    void startSession(
        uint32_t durationSec,
        uint8_t patternType = PATTERN_TYPE_RNDP,
        float timeOnMs = 100.0f,
        float timeOffMs = 67.0f,
        float jitterPercent = 0.0f,
        uint8_t numFingers = 4,
        bool mirrorPattern = false,
        uint8_t amplitudeMin = 100,
        uint8_t amplitudeMax = 100
    );

    /**
     * @brief Update therapy engine (call frequently in loop)
     */
    void update();

    /**
     * @brief Pause therapy execution
     */
    void pause();

    /**
     * @brief Resume paused therapy
     */
    void resume();

    /**
     * @brief Stop therapy execution
     */
    void stop();

    // =========================================================================
    // STATUS
    // =========================================================================

    /**
     * @brief Check if therapy is running
     */
    bool isRunning() const { return _isRunning; }

    /**
     * @brief Check if therapy is paused
     */
    bool isPaused() const { return _isPaused; }

    /**
     * @brief Get cycles completed
     */
    uint32_t getCyclesCompleted() const { return _cyclesCompleted; }

    /**
     * @brief Get total activations
     */
    uint32_t getTotalActivations() const { return _totalActivations; }

    /**
     * @brief Get elapsed session time in seconds
     */
    uint32_t getElapsedSeconds() const;

    /**
     * @brief Get remaining session time in seconds
     */
    uint32_t getRemainingSeconds() const;

    /**
     * @brief Get total session duration in seconds
     */
    uint32_t getDurationSeconds() const { return _sessionDurationSec; }

    /**
     * @brief Get current frequency for a finger
     * @param finger Finger index (0-3)
     * @return Current frequency in Hz
     */
    uint16_t getFrequency(uint8_t finger) const {
        return (finger < MAX_ACTUATORS) ? _currentFrequency[finger] : 235;
    }

private:
    // State
    bool _isRunning;
    bool _isPaused;
    bool _shouldStop;

    // Session parameters
    uint32_t _sessionStartTime;
    uint32_t _sessionDurationSec;
    uint8_t _patternType;
    float _timeOnMs;
    float _timeOffMs;
    float _jitterPercent;
    uint8_t _numFingers;
    bool _mirrorPattern;
    uint8_t _amplitudeMin;
    uint8_t _amplitudeMax;

    // Frequency randomization (Custom vCR feature - v1 defaults_CustomVCR.py)
    bool _frequencyRandomization;
    uint16_t _frequencyMin;     // 210 Hz (v1 ACTUATOR_FREQL)
    uint16_t _frequencyMax;     // 260 Hz (v1 ACTUATOR_FREQH)
    uint16_t _currentFrequency[MAX_ACTUATORS];  // Current frequency per finger (Hz)

    // Current pattern execution
    Pattern _currentPattern;
    uint8_t _patternIndex;
    uint32_t _activationStartTime;
    bool _waitingForInterval;
    uint32_t _intervalStartTime;
    bool _motorActive;

    // Statistics
    uint32_t _cyclesCompleted;
    uint32_t _totalActivations;

    // Macrocycle tracking (v1 parity: 3 patterns per macrocycle)
    uint8_t _patternsInMacrocycle;      // Count of patterns executed in current macrocycle (0-2)
    static const uint8_t PATTERNS_PER_MACROCYCLE = 3;  // v1: 3 patterns per macrocycle

    // Sequence tracking
    uint32_t _buzzSequenceId;

    // Flow control state machine (PRIMARY only)
    BuzzFlowState _buzzFlowState;
    uint32_t _buzzSendTime;          // Time when BUZZ was sent

    // Callbacks
    SendCommandCallback _sendCommandCallback;
    ActivateCallback _activateCallback;
    DeactivateCallback _deactivateCallback;
    CycleCompleteCallback _cycleCompleteCallback;
    SetFrequencyCallback _setFrequencyCallback;
    MacrocycleStartCallback _macrocycleStartCallback;

    // Internal methods
    void generateNextPattern();
    void executePatternStep();
    void applyFrequencyRandomization();  // Called at start of each pattern cycle
};

#endif // THERAPY_ENGINE_H
