/**
 * @file activation_queue.cpp
 * @brief Unified motor event queue for FreeRTOS-based motor control
 * @version 3.1.0
 *
 * Pure FreeRTOS architecture - motor task handles both activations
 * and deactivations with unified timing.
 *
 * Thread Safety: All public methods are protected by FreeRTOS mutex.
 * Queue is accessed from main loop, BLE callbacks, and motor task.
 */

#include "activation_queue.h"
#include "hardware.h"
#include "profile_manager.h"

// External reference for debug mode
extern ProfileManager profiles;

// Global instance
ActivationQueue activationQueue;

// =============================================================================
// RAII Mutex Lock Helper (same pattern as HapticController)
// =============================================================================

/**
 * @brief RAII wrapper for FreeRTOS mutex
 * Automatically releases mutex when going out of scope.
 */
class QueueMutexLock {
public:
    explicit QueueMutexLock(SemaphoreHandle_t mutex, TickType_t timeout = pdMS_TO_TICKS(50))
        : _mutex(mutex), _acquired(false)
    {
        if (_mutex != nullptr) {
            _acquired = (xSemaphoreTake(_mutex, timeout) == pdTRUE);
        }
    }

    ~QueueMutexLock() {
        if (_acquired && _mutex != nullptr) {
            xSemaphoreGive(_mutex);
        }
    }

    // Prevent copying
    QueueMutexLock(const QueueMutexLock&) = delete;
    QueueMutexLock& operator=(const QueueMutexLock&) = delete;

    bool acquired() const { return _acquired; }

private:
    SemaphoreHandle_t _mutex;
    bool _acquired;
};

// =============================================================================
// ActivationQueue Implementation
// =============================================================================

ActivationQueue::ActivationQueue()
    : _haptic(nullptr)
    , _motorTaskHandle(nullptr)
    , _queueMutex(nullptr)
    , _initialized(false)
{
    for (uint8_t i = 0; i < MAX_EVENTS; i++) {
        _events[i].clear();
    }
}

void ActivationQueue::begin(HapticController* haptic, TaskHandle_t motorTaskHandle) {
    _haptic = haptic;
    _motorTaskHandle = motorTaskHandle;

    // Create mutex for thread-safe queue access
    if (_queueMutex == nullptr) {
        _queueMutex = xSemaphoreCreateMutex();
        if (_queueMutex == nullptr) {
            Serial.println(F("[QUEUE] ERROR: Failed to create mutex!"));
        }
    }

    _initialized = (haptic != nullptr && _queueMutex != nullptr);

    if (_initialized) {
        Serial.println(F("[QUEUE] Activation queue initialized (FreeRTOS mode, mutex protected)"));
    }

    clear();
}

void ActivationQueue::clear() {
    QueueMutexLock lock(_queueMutex);
    // Proceed even if lock not acquired - safety operation

    for (uint8_t i = 0; i < MAX_EVENTS; i++) {
        _events[i].clear();
    }
}

int8_t ActivationQueue::findEmptySlot() const {
    // NOTE: Caller must hold mutex
    for (uint8_t i = 0; i < MAX_EVENTS; i++) {
        if (!_events[i].active) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

int8_t ActivationQueue::findNextEvent() const {
    // NOTE: Caller must hold mutex
    int8_t earliestIdx = -1;
    uint64_t earliestTime = UINT64_MAX;

    for (uint8_t i = 0; i < MAX_EVENTS; i++) {
        if (_events[i].active) {
            if (_events[i].timeUs < earliestTime) {
                earliestTime = _events[i].timeUs;
                earliestIdx = static_cast<int8_t>(i);
            }
        }
    }

    return earliestIdx;
}

int8_t ActivationQueue::addEvent(const MotorEvent& event) {
    // NOTE: Caller must hold mutex
    int8_t slot = findEmptySlot();
    if (slot < 0) {
        // Verbose logging for queue full (M5 fix)
        Serial.printf("[QUEUE] FULL! Cannot add F%d %s event at T=%lu ms (count=%d/%d)\n",
                      event.finger,
                      (event.type == MotorEventType::ACTIVATE) ? "ACTIVATE" : "DEACTIVATE",
                      static_cast<unsigned long>(event.timeUs / 1000),
                      eventCount(), MAX_EVENTS);
        return -1;
    }

    _events[slot] = event;
    _events[slot].active = true;
    return slot;
}

bool ActivationQueue::enqueue(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
                              uint16_t durationMs, uint16_t frequencyHz) {
    QueueMutexLock lock(_queueMutex);
    if (!lock.acquired()) {
        Serial.println(F("[QUEUE] ERROR: Failed to acquire mutex for enqueue"));
        return false;
    }

    // Create activation event
    MotorEvent actEvent;
    actEvent.timeUs = activateTimeUs;
    actEvent.finger = finger;
    actEvent.amplitude = amplitude;
    actEvent.frequencyHz = frequencyHz;
    actEvent.type = MotorEventType::ACTIVATE;
    actEvent.active = true;

    // H4 fix: Store slot index for proper rollback
    int8_t actSlot = addEvent(actEvent);
    if (actSlot < 0) {
        return false;
    }

    // Create corresponding deactivation event
    MotorEvent deactEvent;
    deactEvent.timeUs = activateTimeUs + (static_cast<uint64_t>(durationMs) * 1000ULL);
    deactEvent.finger = finger;
    deactEvent.amplitude = 0;
    deactEvent.frequencyHz = 0;
    deactEvent.type = MotorEventType::DEACTIVATE;
    deactEvent.active = true;

    int8_t deactSlot = addEvent(deactEvent);
    if (deactSlot < 0) {
        // H4 fix: Rollback by known index, not by searching
        _events[actSlot].clear();
        return false;
    }

    if (profiles.getDebugMode()) {
        Serial.printf("[QUEUE] Enqueued F%d A%d @%dHz (ON at T+%lums, OFF at T+%lums)\n",
                      finger, amplitude, frequencyHz,
                      static_cast<unsigned long>(activateTimeUs / 1000),
                      static_cast<unsigned long>((activateTimeUs + durationMs * 1000ULL) / 1000));
    }

    // Notify motor task that new events are available
    // NOTE: This is outside mutex because xTaskNotifyGive is safe from any context
    // and we want to release mutex before potentially waking higher-priority task
    notifyMotorTask();

    return true;
}

bool ActivationQueue::peekNextEvent(MotorEvent& event) const {
    QueueMutexLock lock(_queueMutex);
    if (!lock.acquired()) {
        return false;
    }

    int8_t idx = findNextEvent();
    if (idx < 0) {
        return false;
    }

    // Copy entire event while holding mutex (H5 fix - atomic 64-bit copy)
    event = _events[idx];
    return true;
}

bool ActivationQueue::dequeueNextEvent(MotorEvent& event) {
    QueueMutexLock lock(_queueMutex);
    if (!lock.acquired()) {
        return false;
    }

    int8_t idx = findNextEvent();
    if (idx < 0) {
        return false;
    }

    // Copy and clear while holding mutex (C2 fix - atomic peek+dequeue)
    event = _events[idx];
    _events[idx].clear();
    return true;
}

uint64_t ActivationQueue::getNextEventTime() const {
    QueueMutexLock lock(_queueMutex);
    if (!lock.acquired()) {
        return UINT64_MAX;
    }

    int8_t idx = findNextEvent();
    if (idx < 0) {
        return UINT64_MAX;
    }
    return _events[idx].timeUs;
}

uint8_t ActivationQueue::eventCount() const {
    // NOTE: Can be called without mutex for approximate count (used in logging)
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_EVENTS; i++) {
        if (_events[i].active) {
            count++;
        }
    }
    return count;
}

bool ActivationQueue::isEmpty() const {
    QueueMutexLock lock(_queueMutex);
    if (!lock.acquired()) {
        return true;  // Assume empty on failure (safe default)
    }

    for (uint8_t i = 0; i < MAX_EVENTS; i++) {
        if (_events[i].active) {
            return false;
        }
    }
    return true;
}

void ActivationQueue::notifyMotorTask() {
    // xTaskNotifyGive is ISR-safe, no mutex needed
    if (_motorTaskHandle != nullptr) {
        xTaskNotifyGive(_motorTaskHandle);
    }
}
