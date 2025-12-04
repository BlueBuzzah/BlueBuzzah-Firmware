/**
 * @file timer_scheduler.cpp
 * @brief Millisecond-precision callback scheduler implementation
 */

#include "timer_scheduler.h"

// Global instance
TimerScheduler scheduler;

TimerScheduler::TimerScheduler() {
    for (uint8_t i = 0; i < MAX_TIMERS; i++) {
        _timers[i].active = false;
        _timers[i].callback = nullptr;
        _timers[i].context = nullptr;
        _timers[i].fireTimeMs = 0;
    }
}

uint8_t TimerScheduler::schedule(uint32_t delayMs, SchedulerCallback callback, void* context) {
    if (!callback) {
        return INVALID_ID;
    }

    // Find free slot
    for (uint8_t i = 0; i < MAX_TIMERS; i++) {
        if (!_timers[i].active) {
            _timers[i].fireTimeMs = millis() + delayMs;
            _timers[i].callback = callback;
            _timers[i].context = context;
            _timers[i].active = true;
            return i;
        }
    }

    // No free slots
    return INVALID_ID;
}

void TimerScheduler::cancel(uint8_t id) {
    if (id < MAX_TIMERS) {
        _timers[id].active = false;
        _timers[id].callback = nullptr;
        _timers[id].context = nullptr;
    }
}

void TimerScheduler::cancelAll() {
    for (uint8_t i = 0; i < MAX_TIMERS; i++) {
        _timers[i].active = false;
        _timers[i].callback = nullptr;
        _timers[i].context = nullptr;
    }
}

void TimerScheduler::update() {
    uint32_t now = millis();

    for (uint8_t i = 0; i < MAX_TIMERS; i++) {
        if (_timers[i].active && now >= _timers[i].fireTimeMs) {
            // Mark inactive before callback (allows re-scheduling)
            _timers[i].active = false;

            // Store callback info (callback might modify timer)
            SchedulerCallback cb = _timers[i].callback;
            void* ctx = _timers[i].context;

            // Clear slot
            _timers[i].callback = nullptr;
            _timers[i].context = nullptr;

            // Execute callback
            if (cb) {
                cb(ctx);
            }
        }
    }
}

uint8_t TimerScheduler::getPendingCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_TIMERS; i++) {
        if (_timers[i].active) {
            count++;
        }
    }
    return count;
}

bool TimerScheduler::isActive(uint8_t id) const {
    if (id >= MAX_TIMERS) {
        return false;
    }
    return _timers[id].active;
}
