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

    // TIME_RELAX = 4 * (time_on + time_off) - fixed interval between pattern cycles
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

    // Calculate jitter amount per v1 formula: (TIME_ON + TIME_OFF) * jitter% / 100 / 2
    // With 23.5% jitter: 167ms * 0.235 / 2 = 19.6ms
    float jitterAmount = cycleDurationMs * (jitterPercent / 100.0f) / 2.0f;

    // Apply jitter to TIME_OFF (67ms), NOT the inter-burst interval
    // v1 behavior: TIME_OFF_actual = TIME_OFF ± jitter (range: 47-87ms with 23.5% jitter)
    for (int i = 0; i < numFingers; i++) {
        float offTime = timeOffMs;
        if (jitterPercent > 0) {
            // Add random jitter to TIME_OFF
            float jitter = random(-1000, 1001) / 1000.0f * jitterAmount;
            offTime += jitter;
            if (offTime < 0) offTime = 0;
        }
        pattern.timeOffMs[i] = offTime;
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

    // TIME_RELAX = 4 * (time_on + time_off) - fixed interval between pattern cycles
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

    // Calculate jitter amount per v1 formula: (TIME_ON + TIME_OFF) * jitter% / 100 / 2
    float jitterAmount = cycleDurationMs * (jitterPercent / 100.0f) / 2.0f;

    // Apply jitter to TIME_OFF, NOT the inter-burst interval
    for (int i = 0; i < numFingers; i++) {
        float offTime = timeOffMs;
        if (jitterPercent > 0) {
            float jitter = random(-1000, 1001) / 1000.0f * jitterAmount;
            offTime += jitter;
            if (offTime < 0) offTime = 0;
        }
        pattern.timeOffMs[i] = offTime;
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

    // TIME_RELAX = 4 * (time_on + time_off) - fixed interval between pattern cycles
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

    // Calculate jitter amount per v1 formula: (TIME_ON + TIME_OFF) * jitter% / 100 / 2
    float jitterAmount = cycleDurationMs * (jitterPercent / 100.0f) / 2.0f;

    // Apply jitter to TIME_OFF, NOT the inter-burst interval
    for (int i = 0; i < numFingers; i++) {
        float offTime = timeOffMs;
        if (jitterPercent > 0) {
            float jitter = random(-1000, 1001) / 1000.0f * jitterAmount;
            offTime += jitter;
            if (offTime < 0) offTime = 0;
        }
        pattern.timeOffMs[i] = offTime;
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
    _jitterPercent(0.0f),
    _numFingers(4),
    _mirrorPattern(false),
    _amplitudeMin(100),
    _amplitudeMax(100),
    _frequencyRandomization(false),
    _frequencyMin(210),
    _frequencyMax(260),
    _patternIndex(0),
    _activationStartTime(0),
    _waitingForInterval(false),
    _intervalStartTime(0),
    _motorActive(false),
    _cyclesCompleted(0),
    _totalActivations(0),
    _buzzSequenceId(0),
    _buzzFlowState(BuzzFlowState::IDLE),
    _buzzSendTime(0),
    _sendCommandCallback(nullptr),
    _activateCallback(nullptr),
    _deactivateCallback(nullptr),
    _cycleCompleteCallback(nullptr),
    _setFrequencyCallback(nullptr)
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

void TherapyEngine::setSetFrequencyCallback(SetFrequencyCallback callback) {
    _setFrequencyCallback = callback;
}

void TherapyEngine::setFrequencyRandomization(bool enabled, uint16_t minHz, uint16_t maxHz) {
    _frequencyRandomization = enabled;
    _frequencyMin = minHz;
    _frequencyMax = maxHz;
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
    bool mirrorPattern,
    uint8_t amplitudeMin,
    uint8_t amplitudeMax
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
    _amplitudeMin = amplitudeMin;
    _amplitudeMax = amplitudeMax;

    // Reset pattern execution state
    _patternIndex = 0;
    _activationStartTime = 0;
    _waitingForInterval = false;
    _intervalStartTime = 0;
    _motorActive = false;

    // Reset flow control state
    _buzzFlowState = BuzzFlowState::IDLE;
    _buzzSendTime = 0;

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

    // Reset flow control state for new pattern
    _buzzFlowState = BuzzFlowState::IDLE;
    _buzzSendTime = millis();  // Allow immediate first buzz

    // Apply frequency randomization if enabled (Custom vCR feature)
    // v1 reconfigures frequency at the start of each pattern cycle
    applyFrequencyRandomization();
}

void TherapyEngine::executePatternStep() {
    uint32_t now = millis();

    // State machine for flow control - matches v1 timing model
    switch (_buzzFlowState) {
        case BuzzFlowState::IDLE: {
            // Ready to send BUZZ - no wait state, immediately activate
            uint8_t leftFinger, rightFinger;
            _currentPattern.getFingerPair(_patternIndex, leftFinger, rightFinger);

            // Calculate random amplitude within configured range
            uint8_t amplitude = (_amplitudeMin == _amplitudeMax)
                ? _amplitudeMin
                : (uint8_t)random(_amplitudeMin, _amplitudeMax + 1);

            // Send sync command to SECONDARY (if PRIMARY with callback)
            if (_sendCommandCallback) {
                _sendCommandCallback("BUZZ", leftFinger, rightFinger, amplitude, _buzzSequenceId);
                _buzzSequenceId++;
            }

            // Activate local motors
            if (_activateCallback) {
                _activateCallback(leftFinger, amplitude);
            }

            _activationStartTime = now;
            _motorActive = true;
            _totalActivations++;

            // Transition to ACTIVE (waiting for TIME_ON to elapse)
            _buzzFlowState = BuzzFlowState::ACTIVE;
            break;
        }

        case BuzzFlowState::ACTIVE: {
            // Motor is ON - wait for TIME_ON (burstDurationMs = 100ms)
            if ((now - _activationStartTime) >= (uint32_t)_currentPattern.burstDurationMs) {
                // TIME_ON elapsed - deactivate motor
                uint8_t leftFinger, rightFinger;
                _currentPattern.getFingerPair(_patternIndex, leftFinger, rightFinger);

                if (_deactivateCallback) {
                    _deactivateCallback(leftFinger);
                }

                _motorActive = false;
                _buzzSendTime = now;  // Record time for TIME_OFF wait

                // Transition to WAITING_OFF (waiting for TIME_OFF + jitter)
                _buzzFlowState = BuzzFlowState::WAITING_OFF;
            }
            break;
        }

        case BuzzFlowState::WAITING_OFF: {
            // Motor is OFF - wait for TIME_OFF + jitter (timeOffMs[i])
            float timeOffWithJitter = _currentPattern.timeOffMs[_patternIndex];

            if ((now - _buzzSendTime) >= (uint32_t)timeOffWithJitter) {
                // TIME_OFF elapsed - advance to next finger
                _patternIndex++;

                if (_patternIndex >= _currentPattern.numFingers) {
                    // Pattern cycle complete - wait TIME_RELAX before next cycle
                    _cyclesCompleted++;

                    if (_cycleCompleteCallback) {
                        _cycleCompleteCallback(_cyclesCompleted);
                    }

                    _buzzSendTime = now;  // Record time for TIME_RELAX wait
                    _buzzFlowState = BuzzFlowState::WAITING_RELAX;
                } else {
                    // More fingers to go - immediately start next buzz
                    _buzzFlowState = BuzzFlowState::IDLE;
                }
            }
            break;
        }

        case BuzzFlowState::WAITING_RELAX: {
            // Pattern complete - wait for TIME_RELAX (interBurstIntervalMs = 668ms)
            if ((now - _buzzSendTime) >= (uint32_t)_currentPattern.interBurstIntervalMs) {
                // TIME_RELAX elapsed - generate new pattern and start next cycle
                generateNextPattern();
            }
            break;
        }
    }
}

void TherapyEngine::applyFrequencyRandomization() {
    // Custom vCR feature: randomize motor frequency at start of each pattern cycle
    // v1 behavior from defaults_CustomVCR.py:
    //   ACTUATOR_FREQL = 210 Hz
    //   ACTUATOR_FREQH = 260 Hz
    //   ACTUATOR_FREQUENCY = random.randrange(FREQL, FREQH, 5)
    // Generates: 210, 215, 220, 225, 230, 235, 240, 245, 250, 255, 260 Hz

    if (!_frequencyRandomization || !_setFrequencyCallback) {
        return;
    }

    // Calculate number of 5Hz steps between min and max
    uint16_t range = _frequencyMax - _frequencyMin;
    uint16_t steps = range / 5;

    // Apply randomized frequency to each finger's motor
    for (int finger = 0; finger < _numFingers; finger++) {
        // Generate random frequency in 5 Hz steps (matching v1 behavior)
        uint16_t freq = _frequencyMin + (random(0, steps + 1) * 5);
        _setFrequencyCallback(finger, freq);
    }
}
