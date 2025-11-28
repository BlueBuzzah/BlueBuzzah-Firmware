/**
 * @file therapy_engine.cpp
 * @brief BlueBuzzah therapy engine - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "therapy_engine.h"

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

void shuffleArray(uint8_t* arr, uint8_t n) {
    // Fisher-Yates shuffle
    for (int i = n - 1; i > 0; i--) {
        int j = random(0, i + 1);
        uint8_t temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}

// =============================================================================
// PATTERN GENERATION
// =============================================================================

Pattern generateRandomPermutation(
    uint8_t numFingers,
    float timeOnMs,
    float timeOffMs,
    float jitterPercent,
    bool mirrorPattern
) {
    Pattern pattern;
    pattern.numFingers = numFingers;
    pattern.burstDurationMs = timeOnMs;

    // Calculate inter-burst interval: 4 * (time_on + time_off)
    float cycleDurationMs = timeOnMs + timeOffMs;
    pattern.interBurstIntervalMs = 4.0f * cycleDurationMs;

    // Generate left hand sequence (random permutation)
    for (int i = 0; i < numFingers; i++) {
        pattern.leftSequence[i] = i;
    }
    shuffleArray(pattern.leftSequence, numFingers);

    // Generate right hand sequence based on mirror setting
    if (mirrorPattern) {
        // Mirrored: same finger on both hands (for noisy vCR)
        for (int i = 0; i < numFingers; i++) {
            pattern.rightSequence[i] = pattern.leftSequence[i];
        }
    } else {
        // Non-mirrored: independent random sequence
        for (int i = 0; i < numFingers; i++) {
            pattern.rightSequence[i] = i;
        }
        shuffleArray(pattern.rightSequence, numFingers);
    }

    // Generate timing with optional jitter
    float jitterAmount = cycleDurationMs * (jitterPercent / 100.0f) / 2.0f;

    for (int i = 0; i < numFingers; i++) {
        float interval = pattern.interBurstIntervalMs;
        if (jitterPercent > 0) {
            // Add random jitter
            float jitter = random(-1000, 1001) / 1000.0f * jitterAmount;
            interval += jitter;
            if (interval < 0) interval = 0;
        }
        pattern.timingMs[i] = interval;
    }

    return pattern;
}

Pattern generateSequentialPattern(
    uint8_t numFingers,
    float timeOnMs,
    float timeOffMs,
    float jitterPercent,
    bool mirrorPattern,
    bool reverse
) {
    Pattern pattern;
    pattern.numFingers = numFingers;
    pattern.burstDurationMs = timeOnMs;

    // Calculate inter-burst interval
    float cycleDurationMs = timeOnMs + timeOffMs;
    pattern.interBurstIntervalMs = 4.0f * cycleDurationMs;

    // Generate sequential list
    for (int i = 0; i < numFingers; i++) {
        if (reverse) {
            pattern.leftSequence[i] = numFingers - 1 - i;
        } else {
            pattern.leftSequence[i] = i;
        }
    }

    // Generate right sequence based on mirror setting
    if (mirrorPattern) {
        // Mirrored: same as left
        for (int i = 0; i < numFingers; i++) {
            pattern.rightSequence[i] = pattern.leftSequence[i];
        }
    } else {
        // Non-mirrored: opposite order
        for (int i = 0; i < numFingers; i++) {
            pattern.rightSequence[i] = pattern.leftSequence[numFingers - 1 - i];
        }
    }

    // Generate timing with optional jitter
    float jitterAmount = cycleDurationMs * (jitterPercent / 100.0f) / 2.0f;

    for (int i = 0; i < numFingers; i++) {
        float interval = pattern.interBurstIntervalMs;
        if (jitterPercent > 0) {
            float jitter = random(-1000, 1001) / 1000.0f * jitterAmount;
            interval += jitter;
            if (interval < 0) interval = 0;
        }
        pattern.timingMs[i] = interval;
    }

    return pattern;
}

Pattern generateMirroredPattern(
    uint8_t numFingers,
    float timeOnMs,
    float timeOffMs,
    float jitterPercent,
    bool randomize
) {
    Pattern pattern;
    pattern.numFingers = numFingers;
    pattern.burstDurationMs = timeOnMs;

    // Calculate inter-burst interval
    float cycleDurationMs = timeOnMs + timeOffMs;
    pattern.interBurstIntervalMs = 4.0f * cycleDurationMs;

    // Generate base sequence
    for (int i = 0; i < numFingers; i++) {
        pattern.leftSequence[i] = i;
    }

    if (randomize) {
        shuffleArray(pattern.leftSequence, numFingers);
    }

    // Mirror to both hands (identical sequences)
    for (int i = 0; i < numFingers; i++) {
        pattern.rightSequence[i] = pattern.leftSequence[i];
    }

    // Generate timing with optional jitter
    float jitterAmount = cycleDurationMs * (jitterPercent / 100.0f) / 2.0f;

    for (int i = 0; i < numFingers; i++) {
        float interval = pattern.interBurstIntervalMs;
        if (jitterPercent > 0) {
            float jitter = random(-1000, 1001) / 1000.0f * jitterAmount;
            interval += jitter;
            if (interval < 0) interval = 0;
        }
        pattern.timingMs[i] = interval;
    }

    return pattern;
}

// =============================================================================
// THERAPY ENGINE - CONSTRUCTOR
// =============================================================================

TherapyEngine::TherapyEngine() :
    _isRunning(false),
    _isPaused(false),
    _shouldStop(false),
    _sessionStartTime(0),
    _sessionDurationSec(0),
    _patternType(PATTERN_TYPE_RNDP),
    _timeOnMs(100.0f),
    _timeOffMs(67.0f),
    _jitterPercent(23.5f),
    _numFingers(5),
    _mirrorPattern(true),
    _patternIndex(0),
    _activationStartTime(0),
    _waitingForInterval(false),
    _intervalStartTime(0),
    _motorActive(false),
    _cyclesCompleted(0),
    _totalActivations(0),
    _buzzSequenceId(0),
    _sendCommandCallback(nullptr),
    _activateCallback(nullptr),
    _deactivateCallback(nullptr),
    _cycleCompleteCallback(nullptr)
{
}

// =============================================================================
// THERAPY ENGINE - CALLBACKS
// =============================================================================

void TherapyEngine::setSendCommandCallback(SendCommandCallback callback) {
    _sendCommandCallback = callback;
}

void TherapyEngine::setActivateCallback(ActivateCallback callback) {
    _activateCallback = callback;
}

void TherapyEngine::setDeactivateCallback(DeactivateCallback callback) {
    _deactivateCallback = callback;
}

void TherapyEngine::setCycleCompleteCallback(CycleCompleteCallback callback) {
    _cycleCompleteCallback = callback;
}

// =============================================================================
// THERAPY ENGINE - SESSION CONTROL
// =============================================================================

void TherapyEngine::startSession(
    uint32_t durationSec,
    uint8_t patternType,
    float timeOnMs,
    float timeOffMs,
    float jitterPercent,
    uint8_t numFingers,
    bool mirrorPattern
) {
    // Reset state
    _isRunning = true;
    _isPaused = false;
    _shouldStop = false;
    _cyclesCompleted = 0;
    _totalActivations = 0;
    _buzzSequenceId = 0;

    // Store session parameters
    _sessionStartTime = millis();
    _sessionDurationSec = durationSec;
    _patternType = patternType;
    _timeOnMs = timeOnMs;
    _timeOffMs = timeOffMs;
    _jitterPercent = jitterPercent;
    _numFingers = numFingers;
    _mirrorPattern = mirrorPattern;

    // Reset pattern execution state
    _patternIndex = 0;
    _activationStartTime = 0;
    _waitingForInterval = false;
    _intervalStartTime = 0;
    _motorActive = false;

    // Generate first pattern
    generateNextPattern();

    Serial.printf("[THERAPY] Session started: %lu sec, pattern=%d, jitter=%.1f%%\n",
                  durationSec, patternType, jitterPercent);
}

void TherapyEngine::update() {
    if (!_isRunning) {
        return;
    }

    // Check for pause
    if (_isPaused) {
        return;
    }

    // Check for stop request
    if (_shouldStop) {
        _isRunning = false;
        return;
    }

    // Check session timeout
    if (_sessionDurationSec > 0) {
        uint32_t elapsed = (millis() - _sessionStartTime) / 1000;
        if (elapsed >= _sessionDurationSec) {
            Serial.println(F("[THERAPY] Session duration reached"));
            stop();
            return;
        }
    }

    // Execute current pattern
    executePatternStep();
}

void TherapyEngine::pause() {
    _isPaused = true;

    // Deactivate any active motors
    if (_motorActive && _deactivateCallback) {
        uint8_t leftFinger, rightFinger;
        _currentPattern.getFingerPair(_patternIndex, leftFinger, rightFinger);
        _deactivateCallback(leftFinger);
        _deactivateCallback(rightFinger);
        _motorActive = false;
    }

    Serial.println(F("[THERAPY] Paused"));
}

void TherapyEngine::resume() {
    _isPaused = false;
    Serial.println(F("[THERAPY] Resumed"));
}

void TherapyEngine::stop() {
    _shouldStop = true;
    _isRunning = false;

    // Deactivate any active motors
    if (_motorActive && _deactivateCallback) {
        uint8_t leftFinger, rightFinger;
        _currentPattern.getFingerPair(_patternIndex, leftFinger, rightFinger);
        _deactivateCallback(leftFinger);
        _deactivateCallback(rightFinger);
        _motorActive = false;
    }

    Serial.printf("[THERAPY] Stopped - Cycles: %lu, Activations: %lu\n",
                  _cyclesCompleted, _totalActivations);
}

// =============================================================================
// THERAPY ENGINE - STATUS
// =============================================================================

uint32_t TherapyEngine::getElapsedSeconds() const {
    if (!_isRunning || _sessionStartTime == 0) {
        return 0;
    }
    return (millis() - _sessionStartTime) / 1000;
}

uint32_t TherapyEngine::getRemainingSeconds() const {
    if (!_isRunning || _sessionDurationSec == 0) {
        return 0;
    }
    uint32_t elapsed = getElapsedSeconds();
    if (elapsed >= _sessionDurationSec) {
        return 0;
    }
    return _sessionDurationSec - elapsed;
}

// =============================================================================
// THERAPY ENGINE - INTERNAL METHODS
// =============================================================================

void TherapyEngine::generateNextPattern() {
    switch (_patternType) {
        case PATTERN_TYPE_RNDP:
            _currentPattern = generateRandomPermutation(
                _numFingers,
                _timeOnMs,
                _timeOffMs,
                _jitterPercent,
                _mirrorPattern
            );
            break;

        case PATTERN_TYPE_SEQUENTIAL:
            _currentPattern = generateSequentialPattern(
                _numFingers,
                _timeOnMs,
                _timeOffMs,
                _jitterPercent,
                _mirrorPattern,
                false
            );
            break;

        case PATTERN_TYPE_MIRRORED:
            _currentPattern = generateMirroredPattern(
                _numFingers,
                _timeOnMs,
                _timeOffMs,
                _jitterPercent,
                true
            );
            break;

        default:
            // Default to RNDP
            _currentPattern = generateRandomPermutation(
                _numFingers,
                _timeOnMs,
                _timeOffMs,
                _jitterPercent,
                _mirrorPattern
            );
            break;
    }

    // Reset pattern execution state
    _patternIndex = 0;
    _activationStartTime = 0;
    _waitingForInterval = false;
    _intervalStartTime = 0;
    _motorActive = false;
}

void TherapyEngine::executePatternStep() {
    // Check if pattern is complete
    if (_patternIndex >= _currentPattern.numFingers) {
        // Cycle complete
        _cyclesCompleted++;

        if (_cycleCompleteCallback) {
            _cycleCompleteCallback(_cyclesCompleted);
        }

        // Generate next pattern
        generateNextPattern();
        return;
    }

    uint32_t now = millis();

    // Handle inter-burst interval waiting
    if (_waitingForInterval) {
        uint32_t elapsedMs = now - _intervalStartTime;
        float requiredInterval = _currentPattern.timingMs[_patternIndex - 1];

        if (elapsedMs >= (uint32_t)requiredInterval) {
            // Interval complete, ready for next activation
            _waitingForInterval = false;
            _intervalStartTime = 0;
        }
        return;
    }

    // Check if we need to deactivate motors after burst duration
    if (_motorActive) {
        uint32_t elapsedMs = now - _activationStartTime;

        if (elapsedMs >= (uint32_t)_currentPattern.burstDurationMs) {
            // Deactivate motors
            uint8_t leftFinger, rightFinger;
            _currentPattern.getFingerPair(_patternIndex, leftFinger, rightFinger);

            if (_deactivateCallback) {
                _deactivateCallback(leftFinger);
                _deactivateCallback(rightFinger);
            }

            _motorActive = false;
            _activationStartTime = 0;

            // Start inter-burst interval
            _waitingForInterval = true;
            _intervalStartTime = now;
            _patternIndex++;
        }
        return;
    }

    // Start new activation
    uint8_t leftFinger, rightFinger;
    _currentPattern.getFingerPair(_patternIndex, leftFinger, rightFinger);

    // Send sync command to SECONDARY (if PRIMARY)
    if (_sendCommandCallback) {
        _sendCommandCallback("EXECUTE_BUZZ", leftFinger, rightFinger, 100, _buzzSequenceId);
        _buzzSequenceId++;
    }

    // Activate local motors
    if (_activateCallback) {
        _activateCallback(leftFinger, 100);
        _activateCallback(rightFinger, 100);
    }

    _activationStartTime = now;
    _motorActive = true;
    _totalActivations += 2;  // Left + right
}
