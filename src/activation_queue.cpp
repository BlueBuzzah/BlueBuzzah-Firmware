/**
 * @file activation_queue.cpp
 * @brief Hardware timer-based queue for scheduling motor activations
 * @version 2.0.0
 *
 * Uses chain scheduling via SyncTimer for microsecond-precision activations.
 * Deactivations use software polling (less time-critical).
 */

#include "activation_queue.h"
#include "sync_timer.h"
#include "hardware.h"
#include "profile_manager.h"

// External reference for debug mode
extern ProfileManager profiles;

// Global instance
ActivationQueue activationQueue;

ActivationQueue::ActivationQueue()
    : _haptic(nullptr)
    , _syncTimer(nullptr)
    , _currentEventIndex(-1)
    , _initialized(false)
{
    for (uint8_t i = 0; i < MAX_QUEUED; i++) {
        _queue[i].clear();
    }
}

void ActivationQueue::begin(HapticController* haptic, SyncTimer* syncTimer) {
    _haptic = haptic;
    _syncTimer = syncTimer;
    _currentEventIndex = -1;
    _initialized = (haptic != nullptr && syncTimer != nullptr);

    // Register chain scheduling callback with SyncTimer
    if (_initialized) {
        syncTimer->setActivationCompleteCallback([]() {
            activationQueue.onActivationComplete();
        });
        Serial.println(F("[QUEUE] Activation queue initialized with hardware timer"));
    }

    clear();
}

void ActivationQueue::clear() {
    // Cancel any pending hardware timer
    if (_syncTimer) {
        _syncTimer->cancel();
    }

    _currentEventIndex = -1;

    for (uint8_t i = 0; i < MAX_QUEUED; i++) {
        _queue[i].clear();
    }
}

int8_t ActivationQueue::findEmptySlot() const {
    for (uint8_t i = 0; i < MAX_QUEUED; i++) {
        if (!_queue[i].active) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

int8_t ActivationQueue::findNextPendingEvent() const {
    int8_t earliestIdx = -1;
    uint64_t earliestTime = UINT64_MAX;

    for (uint8_t i = 0; i < MAX_QUEUED; i++) {
        if (_queue[i].active && !_queue[i].activated) {
            if (_queue[i].activateTimeUs < earliestTime) {
                earliestTime = _queue[i].activateTimeUs;
                earliestIdx = static_cast<int8_t>(i);
            }
        }
    }

    return earliestIdx;
}

bool ActivationQueue::enqueue(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
                              uint16_t durationMs, uint16_t frequencyHz) {
    int8_t slot = findEmptySlot();
    if (slot < 0) {
        Serial.println(F("[QUEUE] ERROR: Queue full, cannot enqueue"));
        return false;
    }

    _queue[slot].activateTimeUs = activateTimeUs;
    _queue[slot].deactivateTimeUs = activateTimeUs + (durationMs * 1000ULL);
    _queue[slot].finger = finger;
    _queue[slot].amplitude = amplitude;
    _queue[slot].durationMs = durationMs;
    _queue[slot].frequencyHz = frequencyHz;
    _queue[slot].active = true;
    _queue[slot].activated = false;

    if (profiles.getDebugMode()) {
        Serial.printf("[QUEUE] Enqueued F%d A%d at T+%lums\n",
                      finger, amplitude,
                      (unsigned long)(activateTimeUs / 1000));
    }

    return true;
}

void ActivationQueue::scheduleNext() {
    if (!_initialized || !_syncTimer || !_haptic) {
        return;
    }

    int8_t nextIdx = findNextPendingEvent();
    if (nextIdx < 0) {
        // No more pending events - chain complete
        if (profiles.getDebugMode()) {
            Serial.println(F("[QUEUE] All activations scheduled"));
        }
        return;
    }

    _currentEventIndex = nextIdx;

    // PRE-SELECTION OPTIMIZATION:
    // Select mux channel and set frequency NOW (before timer fires)
    // This reduces activation latency from ~400-600μs to ~100-150μs
    uint8_t finger = _queue[nextIdx].finger;
    uint16_t frequencyHz = _queue[nextIdx].frequencyHz;

    if (_haptic->isEnabled(finger)) {
        // Pre-select: Open mux channel and set frequency
        _haptic->selectChannelPersistent(finger);
        _haptic->setFrequencyDirect(finger, frequencyHz);
        _syncTimer->setPreSelected(finger);

        if (profiles.getDebugMode()) {
            Serial.printf("[QUEUE] Pre-selected F%d @%dHz\n", finger, frequencyHz);
        }
    }

    // Arm hardware timer for next activation
    // When timer fires, SyncTimer will use the pre-selected fast path
    _syncTimer->scheduleAbsoluteActivation(
        _queue[nextIdx].activateTimeUs,
        finger,
        _queue[nextIdx].amplitude,
        frequencyHz
    );

    if (profiles.getDebugMode()) {
        Serial.printf("[QUEUE] Armed timer for F%d (idx %d)\n", finger, nextIdx);
    }
}

void ActivationQueue::onActivationComplete() {
    // Mark current event as activated (motor is now ON, waiting for deactivation)
    if (_currentEventIndex >= 0 && _currentEventIndex < MAX_QUEUED) {
        _queue[_currentEventIndex].activated = true;

        if (profiles.getDebugMode()) {
            Serial.printf("[QUEUE] Activation complete F%d (idx %d)\n",
                          _queue[_currentEventIndex].finger, _currentEventIndex);
        }
    }

    _currentEventIndex = -1;

    // Chain to next event
    scheduleNext();
}

uint8_t ActivationQueue::processDeactivations(uint64_t nowUs) {
    if (!_initialized || !_haptic) {
        return 0;
    }

    uint8_t processed = 0;

    for (uint8_t i = 0; i < MAX_QUEUED; i++) {
        if (!_queue[i].active || !_queue[i].activated) {
            continue;
        }

        // Check if deactivation time has passed
        if (nowUs >= _queue[i].deactivateTimeUs) {
            _haptic->deactivate(_queue[i].finger);

            if (profiles.getDebugMode()) {
                int64_t lateness = static_cast<int64_t>(nowUs) -
                                   static_cast<int64_t>(_queue[i].deactivateTimeUs);
                Serial.printf("[QUEUE] Deactivated F%d (lateness: %ldus)\n",
                              _queue[i].finger, static_cast<long>(lateness));
            }

            // Clear slot
            _queue[i].clear();
            processed++;
        }
    }

    return processed;
}

uint8_t ActivationQueue::pendingCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_QUEUED; i++) {
        if (_queue[i].active && !_queue[i].activated) {
            count++;
        }
    }
    return count;
}

uint8_t ActivationQueue::activeCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_QUEUED; i++) {
        if (_queue[i].active && _queue[i].activated) {
            count++;
        }
    }
    return count;
}

bool ActivationQueue::isEmpty() const {
    for (uint8_t i = 0; i < MAX_QUEUED; i++) {
        if (_queue[i].active) {
            return false;
        }
    }
    return true;
}

bool ActivationQueue::isComplete() const {
    return isEmpty();
}

uint64_t ActivationQueue::getNextActivationTime() const {
    uint64_t earliest = UINT64_MAX;
    for (uint8_t i = 0; i < MAX_QUEUED; i++) {
        if (_queue[i].active && !_queue[i].activated) {
            if (_queue[i].activateTimeUs < earliest) {
                earliest = _queue[i].activateTimeUs;
            }
        }
    }
    return earliest;
}
