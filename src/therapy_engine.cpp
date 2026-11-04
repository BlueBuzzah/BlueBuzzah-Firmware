/**
 * @file therapy_engine.cpp
 * @brief BlueBuzzah therapy engine - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "therapy_engine.h"
#include "sync_protocol.h"  // For getMicros() - overflow-safe 64-bit timestamp
#include "fs_backend.h"
#include "platform.h"
#include <span>
#include <cmath>
#if defined(NRF52840_XXAA) && !defined(NATIVE_TEST_BUILD)
#include <nrf_soc.h>
#endif

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

float onsetJitterLimitMs(float timeOnMs, float timeOffMs, float jitterPercent) {
    const float limit = (timeOnMs + timeOffMs) * (jitterPercent / 100.0f) / 2.0f;
    const float cap = (timeOffMs - MIN_INTER_BURST_GAP_MS - ONSET_ROUNDING_RESERVE_MS) / 2.0f;
    const float capped = (limit < cap) ? limit : cap;
    return (capped > 0.0f) ? capped : 0.0f;
}

// =============================================================================
// PATTERN GENERATION
// =============================================================================

Pattern generateRandomPermutation(
    uint8_t numFingers,
    float timeOnMs,
    float timeOffMs,
    bool mirrorPattern
) {
    Pattern pattern = Pattern(numFingers);
    pattern.numFingers = numFingers;
    pattern.burstDurationMs = timeOnMs;

    // TIME_RELAX = numFingers * (time_on + time_off) - fixed interval between pattern cycles
    float cycleDurationMs = timeOnMs + timeOffMs;
    pattern.interBurstIntervalMs = static_cast<float>(numFingers) * cycleDurationMs;

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

    // OFF time is nominal; onset jitter is applied when the macrocycle is built
    for (uint8_t i = 0; i < numFingers; i++) {
        pattern.timeOffMs[i] = timeOffMs;
    }

    return pattern;
}

Pattern generateSequentialPattern(
    uint8_t numFingers,
    float timeOnMs,
    float timeOffMs,
    bool mirrorPattern,
    bool reverse
) {
    Pattern pattern = Pattern(numFingers);
    pattern.numFingers = numFingers;
    pattern.burstDurationMs = timeOnMs;

    // TIME_RELAX = numFingers * (time_on + time_off) - fixed interval between pattern cycles
    float cycleDurationMs = timeOnMs + timeOffMs;
    pattern.interBurstIntervalMs = static_cast<float>(numFingers) * cycleDurationMs;

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

    // OFF time is nominal; onset jitter is applied when the macrocycle is built
    for (uint8_t i = 0; i < numFingers; i++) {
        pattern.timeOffMs[i] = timeOffMs;
    }

    return pattern;
}

Pattern generateMirroredPattern(
    uint8_t numFingers,
    float timeOnMs,
    float timeOffMs,
    bool randomize
) {
    Pattern pattern = Pattern(numFingers);
    pattern.numFingers = numFingers;
    pattern.burstDurationMs = timeOnMs;

    // TIME_RELAX = numFingers * (time_on + time_off) - fixed interval between pattern cycles
    float cycleDurationMs = timeOnMs + timeOffMs;
    pattern.interBurstIntervalMs = static_cast<float>(numFingers) * cycleDurationMs;

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

    // OFF time is nominal; onset jitter is applied when the macrocycle is built
    for (uint8_t i = 0; i < numFingers; i++) {
        pattern.timeOffMs[i] = timeOffMs;
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
    _numFingers(MAX_ACTUATORS),
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
    _buzzFlowState(BuzzFlowState::IDLE),
    _activateCallback(nullptr),
    _deactivateCallback(nullptr),
    _cycleCompleteCallback(nullptr),
    _macrocycleStartCallback(nullptr),
    _sendMacrocycleCallback(nullptr),
    _scheduleActivationCallback(nullptr),
    _startSchedulingCallback(nullptr),
    _isSchedulingCompleteCallback(nullptr),
    _getLeadTimeCallback(nullptr),
    _macrocycleSequenceId(0),
    _macrocycleEventIndex(0),
    _macrocycleBaseTime(0)
{
    // Initialize frequencies to default (250 Hz per v1 ACTUATOR_FREQUENCY)
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        _currentFrequency[i] = 250;
    }
    // Identity finger map: patterns use all fingers until told otherwise
    _fingerMapCount = MAX_ACTUATORS;
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        _fingerMap[i] = i;
    }
}

void TherapyEngine::setActiveFingers(const uint8_t* fingers, uint8_t count) {
    bool valid = (fingers != nullptr) && (count >= 1) && (count <= MAX_ACTUATORS);
    if (valid) {
        uint8_t seen = 0;
        for (uint8_t i = 0; i < count; i++) {
            if (fingers[i] >= MAX_ACTUATORS || (seen & (1u << fingers[i]))) {
                valid = false;  // out of range or duplicate
                break;
            }
            seen |= static_cast<uint8_t>(1u << fingers[i]);
        }
    }
    if (!valid) {
        _fingerMapCount = MAX_ACTUATORS;
        for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
            _fingerMap[i] = i;
        }
        return;
    }
    _fingerMapCount = count;
    for (uint8_t i = 0; i < count; i++) {
        _fingerMap[i] = fingers[i];
    }
}

void TherapyEngine::remapPatternFingers(Pattern& pattern) {
    // Pattern sequences are permutations of 0.._numFingers-1 (slot indices);
    // map them onto physical fingers so a missing motor is skipped entirely.
    // _numFingers <= _fingerMapCount is enforced by startSession.
    for (uint8_t i = 0; i < pattern.numFingers; i++) {
        if (pattern.primarySequence[i] < _fingerMapCount) {
            pattern.primarySequence[i] = _fingerMap[pattern.primarySequence[i]];
        }
        if (pattern.secondarySequence[i] < _fingerMapCount) {
            pattern.secondarySequence[i] = _fingerMap[pattern.secondarySequence[i]];
        }
    }
}

// =============================================================================
// THERAPY ENGINE - CALLBACKS
// =============================================================================

void TherapyEngine::setActivateCallback(ActivateCallback callback) {
    _activateCallback = callback;
}

void TherapyEngine::setDeactivateCallback(DeactivateCallback callback) {
    _deactivateCallback = callback;
}

void TherapyEngine::setCycleCompleteCallback(CycleCompleteCallback callback) {
    _cycleCompleteCallback = callback;
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

void TherapyEngine::setFrequencyRandomization(bool enabled, uint16_t minHz, uint16_t maxHz) {
    _frequencyRandomization = enabled;
    _frequencyMin = minHz;
    _frequencyMax = maxHz;
    if (!enabled) {
        // A previous randomized session left its last draws here; events
        // read them, so a fixed-frequency session must start from 250 Hz
        for (uint8_t finger = 0; finger < MAX_ACTUATORS; finger++) {
            _currentFrequency[finger] = DEFAULT_FREQUENCY_HZ;
        }
    }
}

// =============================================================================
// THERAPY ENGINE - SESSION CONTROL
// =============================================================================

// Hardware entropy for session ids, which must not repeat across reboots, and the nRF52 RNG seed
static uint32_t sessionEntropy() {
#if defined(BOARD_PENTABUZZER_ESP32S3) && !defined(NATIVE_TEST_BUILD)
    return esp_random();  // true RNG while the radio is on
#elif defined(NRF52840_XXAA) && !defined(NATIVE_TEST_BUILD)
    uint32_t value = 0;
    uint32_t err;
    // The SoftDevice pool refills within microseconds; only "empty" is worth a retry
    do {
        err = sd_rand_application_vector_get(reinterpret_cast<uint8_t*>(&value), sizeof(value));
    } while (err == NRF_ERROR_SOC_RAND_NOT_ENOUGH_VALUES);
    if (err != NRF_SUCCESS) {
        // Not random, but the start tap time varies run to run, unlike a fixed fallback
        Serial.printf("[THERAPY] RNG error 0x%lX, falling back to micros()\n", static_cast<unsigned long>(err));
        return micros();
    }
    return value;
#else
    return static_cast<uint32_t>(random(0, 0x7FFFFFFF));
#endif
}

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
    // Reset state. _isRunning is set last: this can run on the BLE core, and
    // update() must never see a running session with the last session's state.
    _isPaused = false;
    _shouldStop = false;
    _isTestMode = isTestMode;
    _cyclesCompleted = 0;
    _totalActivations = 0;
    _patternsInMacrocycle = 0;

    // Store session parameters
#if defined(NRF52840_XXAA) && !defined(NATIVE_TEST_BUILD)
    // nRF52 rand() repeats after every boot. Never on ESP32: there, calling
    // randomSeed() swaps the hardware RNG for a software one.
    randomSeed(sessionEntropy());
#endif
    _sessionStartTime = millis();
    // Test sessions are never reported, so they keep the last real id.
    // 31 bits so it prints as a positive int32 response value.
    if (!isTestMode) {
        _sessionId = sessionEntropy() & 0x7FFFFFFF;
        if (_sessionId == 0) {
            _sessionId = 1;  // 0 means "no session"
        }
    }
    _pauseStartTime = 0;
    _pausedMs = 0;
    _sessionDurationSec = durationSec;
    _patternType = patternType;
    _timeOnMs = timeOnMs;
    _timeOffMs = timeOffMs;
    // Never more fingers than the active (physically present) set
    _numFingers = (numFingers < _fingerMapCount) ? numFingers : _fingerMapCount;
    _jitterLimitMs = onsetJitterLimitMs(timeOnMs, timeOffMs, jitterPercent);
    _jitterGuardMs = static_cast<uint16_t>(std::ceil(_jitterLimitMs));
    _gridPeriodUs = double(CR_CYCLES_PER_MACROCYCLE) * _numFingers * (double(_timeOnMs) + _timeOffMs) * 1000.0;
    _gridOriginUs = 0;
    _gridMacroIndex = 0;
    _appliedPauseMs = 0;
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

    // Generate first pattern
    generateNextPattern();

    {
        PLATFORM_CRITICAL_ENTER();
        _isRunning = true;
        PLATFORM_CRITICAL_EXIT();
    }

    // Notify macrocycle start (first macrocycle)
    if (_macrocycleStartCallback) {
        _macrocycleStartCallback(_cyclesCompleted);
    }

    Serial.printf("[THERAPY] Session started: %lu sec, pattern=%d\n", durationSec, static_cast<int>(patternType));
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

    // Check session timeout (paused time doesn't count toward the length)
    if (_sessionDurationSec > 0) {
        if (getElapsedSeconds() >= _sessionDurationSec) {
            Serial.println(F("[THERAPY] Session duration reached"));
            stop(SessionEndReason::COMPLETED);
            return;
        }
    }

    // Account for deliberate pauses once, in the scheduler thread. Events already
    // dispatched to the hardware queue keep the existing queue/pause semantics.
    if (_gridOriginUs != 0) {
        PLATFORM_CRITICAL_ENTER();
        const uint32_t paused = _pausedMs;
        PLATFORM_CRITICAL_EXIT();
        _gridOriginUs += uint64_t(paused - _appliedPauseMs) * 1000ULL;
        _appliedPauseMs = paused;
    }

    // Execute macrocycle batching (MACROCYCLE-only architecture)
    executeMacrocycleStep();
}

void TherapyEngine::pause() {
    // Menu commands run on the NimBLE core on ESP32 while update() reads these
    // on the loop core: set the pair as one step
    {
        PLATFORM_CRITICAL_ENTER();
        if (!_isPaused) {
            _pauseStartTime = millis();
        }
        _isPaused = true;
        PLATFORM_CRITICAL_EXIT();
    }

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
    {
        PLATFORM_CRITICAL_ENTER();
        if (_isPaused) {
            _pausedMs += millis() - _pauseStartTime;
        }
        _isPaused = false;
        PLATFORM_CRITICAL_EXIT();
    }
    Serial.println(F("[THERAPY] Resumed"));
}

void TherapyEngine::stop(SessionEndReason reason) {
    // stop() runs on the BLE task and in loop(): the running check, the record
    // and the clear must be one step, or two stops can both record
    bool recorded = false;
    {
        PLATFORM_CRITICAL_ENTER();
        uint32_t now = millis();
        if (_isRunning && !_isTestMode) {
            _lastSession = {
                LAST_SESSION_MAGIC,
                _sessionId,
                (now - _sessionStartTime) / 1000,
                pausedMsAt(now) / 1000,
                _sessionDurationSec,
                reason
            };
            _lastSessionDirty = true;
            recorded = true;
        }
        _shouldStop = true;
        _isRunning = false;
        _isPaused = false;  // a later SESSION_RESUME must not find a dead session paused
        PLATFORM_CRITICAL_EXIT();
    }
    if (recorded) {
        Serial.printf("[THERAPY] Session %lu ended: %s\n",
                      static_cast<unsigned long>(_sessionId), sessionEndReasonToString(reason));
    }

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

void TherapyEngine::loadLastSession() {
    LastSessionSummary loaded = {};
    size_t len = 0;
    if (fsb::readFile(LAST_SESSION_FILE, reinterpret_cast<uint8_t*>(&loaded), sizeof(loaded), len)
        && len == sizeof(loaded) && loaded.magic == LAST_SESSION_MAGIC) {
        _lastSession = loaded;
    }
}

void TherapyEngine::persistLastSession() {
    if (!_lastSessionDirty
        || (_persistFailedAt != 0 && millis() - _persistFailedAt < LAST_SESSION_RETRY_MS)) {
        return;
    }
    LastSessionSummary copy;
    {
        PLATFORM_CRITICAL_ENTER();
        copy = _lastSession;
        _lastSessionDirty = false;
        PLATFORM_CRITICAL_EXIT();
    }
    if (fsb::writeFile(LAST_SESSION_FILE, reinterpret_cast<const uint8_t*>(&copy), sizeof(copy))) {
        _persistFailedAt = 0;
    } else {
        // Retry later; a newer stop in the meantime has already set the flag
        _lastSessionDirty = true;
        _persistFailedAt = millis();
        Serial.println(F("[THERAPY] Could not save last session"));
    }
}

void TherapyEngine::clearLastSession() {
    _lastSession = {};
    _lastSessionDirty = false;
    fsb::removeFile(LAST_SESSION_FILE);
}

// =============================================================================
// THERAPY ENGINE - STATUS
// =============================================================================

uint32_t TherapyEngine::getElapsedSeconds() const {
    if (!_isRunning) {
        return 0;
    }
    // Sample the clock under the lock: a pause() after it would start later than now
    uint32_t now;
    uint32_t pausedMs;
    {
        PLATFORM_CRITICAL_ENTER();
        now = millis();
        pausedMs = pausedMsAt(now);
        PLATFORM_CRITICAL_EXIT();
    }
    return (now - _sessionStartTime - pausedMs) / 1000;
}

uint32_t TherapyEngine::getPausedSeconds() const {
    if (!_isRunning) {
        return 0;
    }
    PLATFORM_CRITICAL_ENTER();
    uint32_t pausedMs = pausedMsAt(millis());
    PLATFORM_CRITICAL_EXIT();
    return pausedMs / 1000;
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
                _mirrorPattern
            );
            break;

        case PatternType::SEQUENTIAL:
            _currentPattern = generateSequentialPattern(
                _numFingers,
                _timeOnMs,
                _timeOffMs,
                _mirrorPattern,
                false
            );
            break;

        case PatternType::MIRRORED:
            _currentPattern = generateMirroredPattern(
                _numFingers,
                _timeOnMs,
                _timeOffMs,
                true
            );
            break;

        default:
            // Default to RNDP
            _currentPattern = generateRandomPermutation(
                _numFingers,
                _timeOnMs,
                _timeOffMs,
                _mirrorPattern
            );
            break;
    }

    remapPatternFingers(_currentPattern);

    // Reset pattern execution state
    _patternIndex = 0;
    _activationStartTime = 0;
    _waitingForInterval = false;
    _intervalStartTime = 0;
    _motorActive = false;

    // Reset flow control state for new pattern
    _buzzFlowState = BuzzFlowState::IDLE;
}

// =============================================================================
// THERAPY ENGINE - MACROCYCLE BATCHING
// =============================================================================

Macrocycle TherapyEngine::generateMacrocycle() {
    // Generate 3 patterns × numFingers events (12 at 4 fingers, 15 at 5)
    // Each event has a delta time relative to baseTime

    Macrocycle mc;
    mc.sequenceId = _macrocycleSequenceId++;
    mc.durationMs = (uint8_t)_timeOnMs;  // Common duration for all events (V2 format)
    mc.clockOffset = 0;  // Will be set by PRIMARY before sending (V2 format)
    mc.eventCount = 0;

    // The session's very first onset stays on its slot
    const bool firstMacrocycle = (_gridOriginUs == 0);

    // Generate 3 patterns
    for (uint8_t patternNum = 0; patternNum < PATTERNS_PER_MACROCYCLE; patternNum++) {
        // Generate pattern based on type
        Pattern pattern;
        switch (_patternType) {
            case PatternType::RNDP:
                pattern = generateRandomPermutation(_numFingers, _timeOnMs, _timeOffMs, _mirrorPattern);
                break;
            case PatternType::SEQUENTIAL:
                pattern = generateSequentialPattern(_numFingers, _timeOnMs, _timeOffMs, _mirrorPattern, false);
                break;
            case PatternType::MIRRORED:
                pattern = generateMirroredPattern(_numFingers, _timeOnMs, _timeOffMs, true);
                break;
            default:
                pattern = generateRandomPermutation(_numFingers, _timeOnMs, _timeOffMs, _mirrorPattern);
                break;
        }

        remapPatternFingers(pattern);

        // Apply frequency randomization if enabled. Covers all MAX_ACTUATORS
        // slots (not just _numFingers): events look frequency up by PHYSICAL
        // finger, which can exceed _numFingers when the finger map skips a
        // missing motor.
        if (_frequencyRandomization) {
            uint16_t range = _frequencyMax - _frequencyMin;
            uint16_t steps = range / 5;
            for (uint8_t finger = 0; finger < MAX_ACTUATORS; finger++) {
                _currentFrequency[finger] = _frequencyMin + static_cast<uint16_t>(random(0, steps) * 5);
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
            // The guard moves the wire base earlier so negative onset offsets fit the
            // unsigned deltaTimeMs field. Both gloves use the same base and deltas.
            const double onsetOffsetMs = (firstMacrocycle && mc.eventCount == 0) ? 0.0 :
                (double(random(0, 1000001)) / 500000.0 - 1.0) * _jitterLimitMs;
            const uint16_t eventDelta = static_cast<uint16_t>(std::lround(
                _jitterGuardMs + double(mc.eventCount) * (double(_timeOnMs) + _timeOffMs) + onsetOffsetMs));
            MacrocycleEvent evt(
                eventDelta,
                secondaryFinger,   // For SECONDARY (BLE transmission)
                primaryFinger,     // For PRIMARY (local scheduling)
                amplitude,
                (uint8_t)pattern.burstDurationMs,
                _currentFrequency[primaryFinger]  // Use PRIMARY finger for frequency lookup
            );

            mc.events[mc.eventCount++] = evt;
        }

        // NO extra time between patterns within a macrocycle
        // (v1 behavior: patterns are back-to-back)
    }

    return mc;
}

uint32_t TherapyEngine::currentLeadTimeUs() const {
    return _getLeadTimeCallback ? _getLeadTimeCallback() : SYNC_MIN_LEAD_TIME_US;
}

uint64_t TherapyEngine::gridSlotUs(uint64_t macroIndex) const {
    return _gridOriginUs + static_cast<uint64_t>(std::llround(double(macroIndex) * _gridPeriodUs));
}

bool TherapyEngine::isPreparePointReached(uint64_t macroIndex) const {
    // The max lead plus the prepare margin ahead of the slot's earliest possible onset
    const uint64_t earliest = gridSlotUs(macroIndex) - uint64_t(_jitterGuardMs) * 1000ULL;
    return getMicros() + SYNC_MAX_LEAD_TIME_US + MACROCYCLE_PREPARE_MARGIN_US >= earliest;
}

void TherapyEngine::executeMacrocycleStep() {
    // State machine for macrocycle batching with FreeRTOS motor task scheduling
    switch (_buzzFlowState) {
        case BuzzFlowState::IDLE: {
            const uint64_t guardUs = uint64_t(_jitterGuardMs) * 1000ULL;
            const bool gridStarted = (_gridOriginUs != 0);

            if (gridStarted) {
                // Every macrocycle goes out at its prepare point, including the
                // slot after a skip, so baseTime is never a whole period away
                if (!isPreparePointReached(_gridMacroIndex)) {
                    break;
                }

                // Read the clock after the lead time callback, which can take 50-200µs
                const uint32_t leadTimeUs = currentLeadTimeUs();
                const uint64_t earliestNominal = getMicros() + leadTimeUs + guardUs;
                const uint64_t nominal = gridSlotUs(_gridMacroIndex);

                // Never compress late events into a catch-up burst. Skip whole
                // grid periods if the transport lead cannot be satisfied.
                if (nominal < earliestNominal) {
                    const double late = double(earliestNominal - nominal);
                    const uint64_t skipped = static_cast<uint64_t>(std::ceil(late / _gridPeriodUs));
                    _gridMacroIndex += skipped;
                    Serial.printf("[THERAPY] Skipped %lu late macrocycles\n",
                                  static_cast<unsigned long>(skipped));
                    break;
                }
            }

            // Generate and send a new macrocycle
            _currentMacrocycle = generateMacrocycle();
            _macrocycleEventIndex = 0;

            // Notify macrocycle start
            if (_macrocycleStartCallback) {
                _macrocycleStartCallback(_cyclesCompleted);
            }

            if (!gridStarted) {
                // Calculate lead time FIRST (callback takes 50-200µs)
                // Falls back to the 70ms minimum if no callback registered
                const uint32_t leadTimeUs = currentLeadTimeUs();

                // CRITICAL: Capture timestamp AFTER lead time calculation to minimize staleness
                // Lead time callback can take 50-200µs; capturing before causes systematic lateness
                _gridOriginUs = getMicros() + leadTimeUs + guardUs;
                PLATFORM_CRITICAL_ENTER();
                _appliedPauseMs = _pausedMs;
                PLATFORM_CRITICAL_EXIT();
            }
            _macrocycleBaseTime = gridSlotUs(_gridMacroIndex) - guardUs;
            _currentMacrocycle.baseTime = _macrocycleBaseTime;

            // Send macrocycle to SECONDARY
            if (_sendMacrocycleCallback) {
                _sendMacrocycleCallback(_currentMacrocycle);
            }

            // Schedule all PRIMARY activations locally via ActivationQueue
            // FreeRTOS motor task handles timing with ~1ms precision
            // Note: Use primaryFinger for local PRIMARY scheduling (finger is for SECONDARY)
            if (_scheduleActivationCallback) {
                for (uint8_t i = 0; i < _currentMacrocycle.eventCount; i++) {
                    const MacrocycleEvent& evt = _currentMacrocycle.events[i];
                    uint64_t activateTime = _macrocycleBaseTime + (evt.deltaTimeMs * 1000ULL);

                    // Enqueue to ActivationQueue using PRIMARY's finger index
                    // Motor task handles timing and frequency via FreeRTOS
                    _scheduleActivationCallback(activateTime, evt.primaryFinger, evt.amplitude,
                                                evt.durationMs, evt.getFrequencyHz());
                    _totalActivations++;
                }

                // Signal motor task that events are ready for processing
                if (_startSchedulingCallback) {
                    _startSchedulingCallback();
                }
            }

            // Transition to ACTIVE - waiting for all events to complete
            _buzzFlowState = BuzzFlowState::ACTIVE;
            break;
        }

        case BuzzFlowState::ACTIVE: {
            // Check if all activations have completed via ActivationQueue
            // FreeRTOS motor task handles both activations and deactivations
            bool queueComplete = true;
            if (_isSchedulingCompleteCallback) {
                queueComplete = _isSchedulingCompleteCallback();
            }

            if (queueComplete) {
                // Macrocycle execution complete on PRIMARY side
                // Wait until it is time to prepare the next grid slot
                _buzzFlowState = BuzzFlowState::WAITING_RELAX;
            }
            break;
        }

        case BuzzFlowState::WAITING_OFF:
            // Not used in macrocycle batching mode
            break;

        case BuzzFlowState::WAITING_RELAX: {
            // IDLE recalculates the transport lead and detects a missed deadline
            if (isPreparePointReached(_gridMacroIndex + 1)) {
                ++_gridMacroIndex;
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
