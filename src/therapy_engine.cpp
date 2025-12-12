/**
 * @file therapy_engine.cpp
 * @brief BlueBuzzah therapy engine - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "therapy_engine.h"
#include "sync_protocol.h"  // For getMicros() - overflow-safe 64-bit timestamp
#include <span>

using namespace std::literals;

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

constexpr void shuffleArray(std::span<uint8_t> arr) {
    // Fisher-Yates shuffle
    for (size_t i = arr.size() - 1; i > 0; i--) {
        size_t const j = static_cast<size_t>(random(0, static_cast<long>(i + 1)));
        std::swap(arr[i], arr[j]);
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
    Pattern pattern = Pattern(numFingers);
    pattern.numFingers = numFingers;
    pattern.burstDurationMs = timeOnMs;

    // TIME_RELAX = 4 * (time_on + time_off) - fixed interval between pattern cycles
    float cycleDurationMs = timeOnMs + timeOffMs;
    pattern.interBurstIntervalMs = 4.0f * cycleDurationMs;

    // Generate PRIMARY device sequence (random permutation)
    for (uint8_t i = 0; i < numFingers; i++) {
        pattern.primarySequence[i] = i;
    }
    shuffleArray(pattern.primarySequence);

    // Generate SECONDARY device sequence based on mirror setting
    if (mirrorPattern) {
        // Mirrored: same finger on both devices (for noisy vCR)
        for (uint8_t i = 0; i < numFingers; i++) {
            pattern.secondarySequence[i] = pattern.primarySequence[i];
        }
    } else {
        // Non-mirrored: independent random sequence
        for (uint8_t i = 0; i < numFingers; i++) {
            pattern.secondarySequence[i] = i;
        }
        shuffleArray(pattern.secondarySequence);
    }

    // Calculate jitter amount per v1 formula: (TIME_ON + TIME_OFF) * jitter% / 100 / 2
    // With 23.5% jitter: 167ms * 0.235 / 2 = 19.6ms
    float jitterAmount = cycleDurationMs * (jitterPercent / 100.0f) / 2.0f;

    // Apply jitter to TIME_OFF (67ms), NOT the inter-burst interval
    // v1 behavior: TIME_OFF_actual = TIME_OFF ± jitter (range: 47-87ms with 23.5% jitter)
    for (uint8_t i = 0; i < numFingers; i++) {
        float offTime = timeOffMs;
        if (jitterPercent > 0) {
            // Add random jitter to TIME_OFF
            float jitter = static_cast<float>(random(-1000, 1001)) / 1000.0f * jitterAmount;
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
    Pattern pattern = Pattern(numFingers);
    pattern.numFingers = numFingers;
    pattern.burstDurationMs = timeOnMs;

    // TIME_RELAX = 4 * (time_on + time_off) - fixed interval between pattern cycles
    float cycleDurationMs = timeOnMs + timeOffMs;
    pattern.interBurstIntervalMs = 4.0f * cycleDurationMs;

    // Generate sequential list
    for (uint8_t i = 0; i < numFingers; i++) {
        if (reverse) {
            pattern.primarySequence[i] = static_cast<uint8_t>(numFingers - 1 - i);
        } else {
            pattern.primarySequence[i] = i;
        }
    }

    // Generate SECONDARY sequence based on mirror setting
    if (mirrorPattern) {
        // Mirrored: same as PRIMARY
        for (uint8_t i = 0; i < numFingers; i++) {
            pattern.secondarySequence[i] = pattern.primarySequence[i];
        }
    } else {
        // Non-mirrored: opposite order
        for (uint8_t i = 0; i < numFingers; i++) {
            pattern.secondarySequence[i] = pattern.primarySequence[numFingers - 1 - i];
        }
    }

    // Calculate jitter amount per v1 formula: (TIME_ON + TIME_OFF) * jitter% / 100 / 2
    float jitterAmount = cycleDurationMs * (jitterPercent / 100.0f) / 2.0f;

    // Apply jitter to TIME_OFF, NOT the inter-burst interval
    for (uint8_t i = 0; i < numFingers; i++) {
        float offTime = timeOffMs;
        if (jitterPercent > 0) {
            float jitter = static_cast<float>(random(-1000, 1001)) / 1000.0f * jitterAmount;
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
    Pattern pattern = Pattern(numFingers);
    pattern.numFingers = numFingers;
    pattern.burstDurationMs = timeOnMs;

    // TIME_RELAX = 4 * (time_on + time_off) - fixed interval between pattern cycles
    float cycleDurationMs = timeOnMs + timeOffMs;
    pattern.interBurstIntervalMs = 4.0f * cycleDurationMs;

    // Generate base sequence
    for (uint8_t i = 0; i < numFingers; i++) {
        pattern.primarySequence[i] = i;
    }

    if (randomize) {
        shuffleArray(pattern.primarySequence);
    }

    // Mirror to both devices (identical sequences)
    for (uint8_t i = 0; i < numFingers; i++) {
        pattern.secondarySequence[i] = pattern.primarySequence[i];
    }

    // Calculate jitter amount per v1 formula: (TIME_ON + TIME_OFF) * jitter% / 100 / 2
    float jitterAmount = cycleDurationMs * (jitterPercent / 100.0f) / 2.0f;

    // Apply jitter to TIME_OFF, NOT the inter-burst interval
    for (uint8_t i = 0; i < numFingers; i++) {
        float offTime = timeOffMs;
        if (jitterPercent > 0) {
            float jitter = static_cast<float>(random(-1000, 1001)) / 1000.0f * jitterAmount;
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
    _patternType(PatternType::RNDP),
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
    _patternsInMacrocycle(0),
    _buzzSequenceId(0),
    _buzzFlowState(BuzzFlowState::IDLE),
    _buzzSendTime(0),
    _sendCommandCallback(nullptr),
    _activateCallback(nullptr),
    _deactivateCallback(nullptr),
    _cycleCompleteCallback(nullptr),
    _setFrequencyCallback(nullptr),
    _macrocycleStartCallback(nullptr),
    _sendMacrocycleCallback(nullptr),
    _scheduleActivationCallback(nullptr),
    _startSchedulingCallback(nullptr),
    _isSchedulingCompleteCallback(nullptr),
    _getLeadTimeCallback(nullptr),
    _macrocycleBatching(false),
    _macrocycleSequenceId(0),
    _macrocycleEventIndex(0),
    _macrocycleBaseTime(0)
{
    // Initialize frequencies to default (250 Hz per v1 ACTUATOR_FREQUENCY)
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        _currentFrequency[i] = 250;
    }
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

void TherapyEngine::setMacrocycleStartCallback(MacrocycleStartCallback callback) {
    _macrocycleStartCallback = callback;
}

void TherapyEngine::setSendMacrocycleCallback(SendMacrocycleCallback callback) {
    _sendMacrocycleCallback = callback;
}

void TherapyEngine::setSchedulingCallbacks(ScheduleActivationCallback scheduleCallback,
                                           StartSchedulingCallback startCallback,
                                           IsSchedulingCompleteCallback isCompleteCallback) {
    _scheduleActivationCallback = scheduleCallback;
    _startSchedulingCallback = startCallback;
    _isSchedulingCompleteCallback = isCompleteCallback;
}

void TherapyEngine::setGetLeadTimeCallback(GetLeadTimeCallback callback) {
    _getLeadTimeCallback = callback;
}

void TherapyEngine::setMacrocycleBatching(bool enabled) {
    _macrocycleBatching = enabled;
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
    PatternType patternType,
    float timeOnMs,
    float timeOffMs,
    float jitterPercent,
    uint8_t numFingers,
    bool mirrorPattern,
    uint8_t amplitudeMin,
    uint8_t amplitudeMax,
    bool isTestMode
) {
    // Reset state
    _isRunning = true;
    _isPaused = false;
    _shouldStop = false;
    _isTestMode = isTestMode;
    _cyclesCompleted = 0;
    _totalActivations = 0;
    _patternsInMacrocycle = 0;
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

    // Notify macrocycle start (first macrocycle)
    if (_macrocycleStartCallback) {
        _macrocycleStartCallback(_cyclesCompleted);
    }

    Serial.printf("[THERAPY] Session started: %lu sec, pattern=%d\n", durationSec, patternType);
    Serial.printf("[THERAPY] Timing: ON=%.1fms, OFF=%.1fms, Jitter=%.1f%%\n",
                  timeOnMs, timeOffMs, jitterPercent);
    Serial.printf("[THERAPY] Pattern duration: %.1fms, Relax: %.1fms\n",
                  _currentPattern.burstDurationMs, _currentPattern.interBurstIntervalMs);
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
        auto elapsed = uint32_t((millis() - _sessionStartTime) / 1000);
        if (elapsed >= _sessionDurationSec) {
            Serial.println(F("[THERAPY] Session duration reached"));
            stop();
            return;
        }
    }

    // Execute based on mode
    if (_macrocycleBatching) {
        executeMacrocycleStep();
    } else {
        executePatternStep();
    }
}

void TherapyEngine::pause() {
    _isPaused = true;

    // Deactivate any active motors
    if (_motorActive && _deactivateCallback) {
        uint8_t primaryFinger, secondaryFinger;
        _currentPattern.getFingerPair(_patternIndex, primaryFinger, secondaryFinger);
        _deactivateCallback(primaryFinger);
        _deactivateCallback(secondaryFinger);
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
        uint8_t primaryFinger, secondaryFinger;
        _currentPattern.getFingerPair(_patternIndex, primaryFinger, secondaryFinger);
        _deactivateCallback(primaryFinger);
        _deactivateCallback(secondaryFinger);
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
    return uint32_t((millis() - _sessionStartTime) / 1000);
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
        case PatternType::RNDP:
            _currentPattern = generateRandomPermutation(
                _numFingers,
                _timeOnMs,
                _timeOffMs,
                _jitterPercent,
                _mirrorPattern
            );
            break;

        case PatternType::SEQUENTIAL:
            _currentPattern = generateSequentialPattern(
                _numFingers,
                _timeOnMs,
                _timeOffMs,
                _jitterPercent,
                _mirrorPattern,
                false
            );
            break;

        case PatternType::MIRRORED:
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
            uint8_t primaryFinger, secondaryFinger;
            _currentPattern.getFingerPair(_patternIndex, primaryFinger, secondaryFinger);

            // Calculate random amplitude within configured range
            uint8_t amplitude = (_amplitudeMin == _amplitudeMax)
                ? _amplitudeMin
                : (uint8_t)random(_amplitudeMin, _amplitudeMax + 1);

            // Send sync command to SECONDARY (if PRIMARY with callback)
            if (_sendCommandCallback) {
                uint32_t durationMs = (uint32_t)_currentPattern.burstDurationMs;
                uint16_t freq = _currentFrequency[primaryFinger];
                _sendCommandCallback("BUZZ", primaryFinger, secondaryFinger, amplitude, durationMs, _buzzSequenceId, freq);
                _buzzSequenceId++;
            }

            // Activate local motors
            if (_activateCallback) {
                _activateCallback(primaryFinger, amplitude);
            }

            // Use fresh timestamp AFTER callbacks complete - BLE send in
            // _sendCommandCallback can take 10-15ms, and that time should
            // NOT count against TIME_ON
            _activationStartTime = millis();
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
                uint8_t primaryFinger, secondaryFinger;
                _currentPattern.getFingerPair(_patternIndex, primaryFinger, secondaryFinger);

                if (_deactivateCallback) {
                    _deactivateCallback(primaryFinger);
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
                    // One pattern complete - check if macrocycle is done
                    _patternsInMacrocycle++;

                    if (_patternsInMacrocycle < PATTERNS_PER_MACROCYCLE) {
                        // More patterns to execute in this macrocycle (3 patterns back-to-back)
                        // NO TIME_RELAX wait between patterns within a macrocycle
                        generateNextPattern();
                    } else {
                        // Macrocycle complete (3 patterns done) - now wait double TIME_RELAX
                        _buzzSendTime = now;
                        _buzzFlowState = BuzzFlowState::WAITING_RELAX;
                    }
                } else {
                    // More fingers to go in current pattern
                    _buzzFlowState = BuzzFlowState::IDLE;
                }
            }
            break;
        }

        case BuzzFlowState::WAITING_RELAX: {
            // Macrocycle complete (3 patterns done) - wait for 2x TIME_RELAX (1336ms total)
            float doubleRelaxMs = 2.0f * _currentPattern.interBurstIntervalMs;

            if ((now - _buzzSendTime) >= (uint32_t)doubleRelaxMs) {
                // Double TIME_RELAX elapsed - macrocycle complete
                _cyclesCompleted++;

                if (_cycleCompleteCallback) {
                    _cycleCompleteCallback(_cyclesCompleted);
                }

                // Reset pattern counter for next macrocycle
                _patternsInMacrocycle = 0;

                // Generate new pattern and start next macrocycle
                generateNextPattern();

                // Notify macrocycle start (for PING/PONG latency measurement)
                if (_macrocycleStartCallback) {
                    _macrocycleStartCallback(_cyclesCompleted);
                }
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
    // Generates: 210, 215, 220, 225, 230, 235, 240, 245, 250, 255 Hz (randrange excludes upper bound)

    if (!_frequencyRandomization) {
        return;
    }

    // Calculate number of 5Hz steps between min and max
    uint16_t range = _frequencyMax - _frequencyMin;
    uint16_t steps = range / 5;

    // Apply randomized frequency to each finger's motor
    for (uint8_t finger = 0; finger < _numFingers; finger++) {
        // Generate random frequency in 5 Hz steps (matching v1 behavior)
        uint16_t freq = _frequencyMin + static_cast<uint16_t>(random(0, steps + 1) * 5);
        _currentFrequency[finger] = freq;

        // Apply locally if callback registered
        if (_setFrequencyCallback) {
            _setFrequencyCallback(finger, freq);
        }
    }
}

// =============================================================================
// THERAPY ENGINE - MACROCYCLE BATCHING
// =============================================================================

Macrocycle TherapyEngine::generateMacrocycle() {
    // Generate 3 patterns × 4 fingers = 12 events
    // Each event has a delta time relative to baseTime

    Macrocycle mc;
    mc.sequenceId = _macrocycleSequenceId++;
    mc.durationMs = (uint8_t)_timeOnMs;  // Common duration for all events (V2 format)
    mc.clockOffset = 0;  // Will be set by PRIMARY before sending (V2 format)
    mc.eventCount = 0;

    uint16_t cumulativeTimeMs = 0;  // Running time offset from base

    // Generate 3 patterns
    for (uint8_t patternNum = 0; patternNum < PATTERNS_PER_MACROCYCLE; patternNum++) {
        // Generate pattern based on type
        Pattern pattern;
        switch (_patternType) {
            case PATTERN_TYPE_RNDP:
                pattern = generateRandomPermutation(_numFingers, _timeOnMs, _timeOffMs, _jitterPercent, _mirrorPattern);
                break;
            case PATTERN_TYPE_SEQUENTIAL:
                pattern = generateSequentialPattern(_numFingers, _timeOnMs, _timeOffMs, _jitterPercent, _mirrorPattern, false);
                break;
            case PATTERN_TYPE_MIRRORED:
                pattern = generateMirroredPattern(_numFingers, _timeOnMs, _timeOffMs, _jitterPercent, true);
                break;
            default:
                pattern = generateRandomPermutation(_numFingers, _timeOnMs, _timeOffMs, _jitterPercent, _mirrorPattern);
                break;
        }

        // Apply frequency randomization if enabled
        if (_frequencyRandomization) {
            uint16_t range = _frequencyMax - _frequencyMin;
            uint16_t steps = range / 5;
            for (uint8_t finger = 0; finger < _numFingers; finger++) {
                _currentFrequency[finger] = _frequencyMin + static_cast<uint16_t>(random(0, steps + 1) * 5);
            }
        }

        // Add events for each finger in this pattern
        for (uint8_t fingerIdx = 0; fingerIdx < pattern.numFingers; fingerIdx++) {
            if (mc.eventCount >= MACROCYCLE_MAX_EVENTS) break;

            uint8_t primaryFinger = pattern.primarySequence[fingerIdx];
            uint8_t secondaryFinger = pattern.secondarySequence[fingerIdx];
            uint8_t amplitude = (_amplitudeMin == _amplitudeMax)
                ? _amplitudeMin
                : (uint8_t)random(_amplitudeMin, _amplitudeMax + 1);

            // Create event with both finger indices:
            // - secondaryFinger: transmitted over BLE to SECONDARY device
            // - primaryFinger: used locally on PRIMARY device
            // In mirrored mode these are identical; in non-mirrored mode they differ
            MacrocycleEvent evt(
                cumulativeTimeMs,
                secondaryFinger,   // For SECONDARY (BLE transmission)
                primaryFinger,     // For PRIMARY (local scheduling)
                amplitude,
                (uint8_t)pattern.burstDurationMs,
                _currentFrequency[primaryFinger]  // Use PRIMARY finger for frequency lookup
            );

            mc.events[mc.eventCount++] = evt;

            // Advance time: TIME_ON + TIME_OFF (with jitter)
            cumulativeTimeMs += (uint16_t)pattern.burstDurationMs;
            cumulativeTimeMs += (uint16_t)pattern.timeOffMs[fingerIdx];
        }

        // NO extra time between patterns within a macrocycle
        // (v1 behavior: patterns are back-to-back)
    }

    return mc;
}

void TherapyEngine::executeMacrocycleStep() {
    uint32_t now = millis();
    uint64_t nowUs = getMicros();  // Use overflow-safe 64-bit micros (raw micros() wraps at 71 min)

    // State machine for macrocycle batching with hardware timer scheduling
    switch (_buzzFlowState) {
        case BuzzFlowState::IDLE: {
            // Generate and send a new macrocycle
            _currentMacrocycle = generateMacrocycle();
            _macrocycleEventIndex = 0;

            // Notify macrocycle start
            if (_macrocycleStartCallback) {
                _macrocycleStartCallback(_cyclesCompleted);
            }

            // Calculate lead time for scheduling using adaptive RTT-based calculation
            // Falls back to 50ms if no callback registered
            uint32_t leadTimeUs = _getLeadTimeCallback ? _getLeadTimeCallback() : 50000;
            _macrocycleBaseTime = nowUs + leadTimeUs;
            _currentMacrocycle.baseTime = _macrocycleBaseTime;

            // Send macrocycle to SECONDARY
            if (_sendMacrocycleCallback) {
                _sendMacrocycleCallback(_currentMacrocycle);
            }

            // Schedule all PRIMARY activations locally via ActivationQueue
            // This uses hardware timer chain scheduling for <100us precision
            // Note: Use primaryFinger for local PRIMARY scheduling (finger is for SECONDARY)
            if (_scheduleActivationCallback) {
                for (uint8_t i = 0; i < _currentMacrocycle.eventCount; i++) {
                    const MacrocycleEvent& evt = _currentMacrocycle.events[i];
                    uint64_t activateTime = _macrocycleBaseTime + (evt.deltaTimeMs * 1000ULL);

                    // Enqueue to ActivationQueue using PRIMARY's finger index
                    // Frequency is set by SyncTimer just before activation for precise timing
                    _scheduleActivationCallback(activateTime, evt.primaryFinger, evt.amplitude,
                                                evt.durationMs, evt.getFrequencyHz());
                    _totalActivations++;
                }

                // Start chain scheduling - arms hardware timer for first event
                if (_startSchedulingCallback) {
                    _startSchedulingCallback();
                }
            }

            // Record send time for tracking
            _buzzSendTime = now;

            // Transition to ACTIVE - waiting for all events to complete
            _buzzFlowState = BuzzFlowState::ACTIVE;
            break;
        }

        case BuzzFlowState::ACTIVE: {
            // Check if all activations have completed via ActivationQueue
            // The queue handles both activations (hardware timer) and deactivations (software polling)
            bool queueComplete = true;
            if (_isSchedulingCompleteCallback) {
                queueComplete = _isSchedulingCompleteCallback();
            }

            if (queueComplete) {
                // Macrocycle execution complete on PRIMARY side
                // Wait for 2x TIME_RELAX before starting next macrocycle
                _buzzSendTime = now;
                _buzzFlowState = BuzzFlowState::WAITING_RELAX;
            }
            break;
        }

        case BuzzFlowState::WAITING_OFF:
            // Not used in macrocycle batching mode
            break;

        case BuzzFlowState::WAITING_RELAX: {
            // Wait for 2x TIME_RELAX (1336ms with default timing)
            float doubleRelaxMs = 2.0f * 4.0f * (_timeOnMs + _timeOffMs);  // TIME_RELAX = 4 * (ON + OFF)

            if ((now - _buzzSendTime) >= (uint32_t)doubleRelaxMs) {
                // Double TIME_RELAX elapsed - macrocycle complete
                _cyclesCompleted++;

                if (_cycleCompleteCallback) {
                    _cycleCompleteCallback(_cyclesCompleted);
                }

                // Ready for next macrocycle
                _buzzFlowState = BuzzFlowState::IDLE;
            }
            break;
        }
    }
}
