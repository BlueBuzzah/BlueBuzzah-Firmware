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

// =============================================================================
// PATTERN CONSTANTS
// =============================================================================

#define PATTERN_MAX_FINGERS 5
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
 */
struct Pattern {
    uint8_t leftSequence[PATTERN_MAX_FINGERS];
    uint8_t rightSequence[PATTERN_MAX_FINGERS];
    float timingMs[PATTERN_MAX_FINGERS];
    uint8_t numFingers;
    float burstDurationMs;
    float interBurstIntervalMs;

    Pattern() :
        numFingers(5),
        burstDurationMs(100.0f),
        interBurstIntervalMs(668.0f)
    {
        for (int i = 0; i < PATTERN_MAX_FINGERS; i++) {
            leftSequence[i] = i;
            rightSequence[i] = i;
            timingMs[i] = 668.0f;
        }
    }

    /**
     * @brief Get total pattern duration in milliseconds
     */
    float getTotalDurationMs() const {
        float total = 0;
        for (int i = 0; i < numFingers; i++) {
            total += timingMs[i] + burstDurationMs;
        }
        return total;
    }

    /**
     * @brief Get finger pair at specified index
     * @param index Pattern step index
     * @param leftFinger Output left finger index
     * @param rightFinger Output right finger index
     */
    void getFingerPair(uint8_t index, uint8_t& leftFinger, uint8_t& rightFinger) const {
        if (index < numFingers) {
            leftFinger = leftSequence[index];
            rightFinger = rightSequence[index];
        }
    }
};

// =============================================================================
// PATTERN GENERATION FUNCTIONS
// =============================================================================

/**
 * @brief Fisher-Yates shuffle for array
 * @param arr Array to shuffle
 * @param n Array length
 */
void shuffleArray(uint8_t* arr, uint8_t n);

/**
 * @brief Generate random permutation (RNDP) pattern
 *
 * Each finger activated exactly once per cycle in randomized order.
 * Used for noisy vCR therapy.
 *
 * @param numFingers Number of fingers per hand (1-5)
 * @param timeOnMs Vibration burst duration
 * @param timeOffMs Time between bursts
 * @param jitterPercent Timing jitter percentage (0-100)
 * @param mirrorPattern If true, same finger on both hands (noisy vCR)
 * @return Generated pattern
 */
Pattern generateRandomPermutation(
    uint8_t numFingers = 5,
    float timeOnMs = 100.0f,
    float timeOffMs = 67.0f,
    float jitterPercent = 23.5f,
    bool mirrorPattern = true
);

/**
 * @brief Generate sequential pattern
 *
 * Fingers activated in order: 0->1->2->3->4 (or reverse)
 *
 * @param numFingers Number of fingers per hand (1-5)
 * @param timeOnMs Vibration burst duration
 * @param timeOffMs Time between bursts
 * @param jitterPercent Timing jitter percentage (0-100)
 * @param mirrorPattern If true, same sequence for both hands
 * @param reverse If true, reverse order (4->0)
 * @return Generated pattern
 */
Pattern generateSequentialPattern(
    uint8_t numFingers = 5,
    float timeOnMs = 100.0f,
    float timeOffMs = 67.0f,
    float jitterPercent = 0.0f,
    bool mirrorPattern = true,
    bool reverse = false
);

/**
 * @brief Generate mirrored bilateral pattern
 *
 * Both hands use identical finger sequences.
 *
 * @param numFingers Number of fingers per hand (1-5)
 * @param timeOnMs Vibration burst duration
 * @param timeOffMs Time between bursts
 * @param jitterPercent Timing jitter percentage (0-100)
 * @param randomize If true, randomize sequence
 * @return Generated pattern
 */
Pattern generateMirroredPattern(
    uint8_t numFingers = 5,
    float timeOnMs = 100.0f,
    float timeOffMs = 67.0f,
    float jitterPercent = 23.5f,
    bool randomize = true
);

// =============================================================================
// CALLBACK TYPES
// =============================================================================

// Callback for sending sync commands to SECONDARY
typedef void (*SendCommandCallback)(const char* commandType, uint8_t leftFinger, uint8_t rightFinger, uint8_t amplitude, uint32_t seq);

// Callback for activating haptic motor
typedef void (*ActivateCallback)(uint8_t finger, uint8_t amplitude);

// Callback for deactivating haptic motor
typedef void (*DeactivateCallback)(uint8_t finger);

// Callback for cycle completion
typedef void (*CycleCompleteCallback)(uint32_t cycleCount);

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
     */
    void startSession(
        uint32_t durationSec,
        uint8_t patternType = PATTERN_TYPE_RNDP,
        float timeOnMs = 100.0f,
        float timeOffMs = 67.0f,
        float jitterPercent = 23.5f,
        uint8_t numFingers = 5,
        bool mirrorPattern = true
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

    // Sequence tracking
    uint32_t _buzzSequenceId;

    // Callbacks
    SendCommandCallback _sendCommandCallback;
    ActivateCallback _activateCallback;
    DeactivateCallback _deactivateCallback;
    CycleCompleteCallback _cycleCompleteCallback;

    // Internal methods
    void generateNextPattern();
    void executePatternStep();
};

#endif // THERAPY_ENGINE_H
