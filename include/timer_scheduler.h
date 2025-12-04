/**
 * @file timer_scheduler.h
 * @brief Millisecond-precision callback scheduler
 * @version 1.0.0
 *
 * Provides non-blocking timed callbacks for operations that don't require
 * microsecond precision (motor deactivation, heartbeat, battery checks).
 * Uses millis() for timing - suitable for 1ms+ delays.
 */

#ifndef TIMER_SCHEDULER_H
#define TIMER_SCHEDULER_H

#include <Arduino.h>

/**
 * @brief Callback function type
 * @param context User-provided context pointer
 */
typedef void (*SchedulerCallback)(void* context);

/**
 * @class TimerScheduler
 * @brief Millisecond-precision timed callback scheduler
 *
 * Schedules callbacks to fire after a specified delay. Callbacks are
 * processed in update() which should be called from the main loop.
 * All timing uses millis() for ~1ms precision.
 */
class TimerScheduler {
public:
    TimerScheduler();

    /**
     * @brief Schedule a callback after delay
     * @param delayMs Delay in milliseconds
     * @param callback Function to call when timer fires
     * @param context Optional context pointer passed to callback
     * @return Timer ID (0-MAX_TIMERS-1) or 255 if no slots available
     */
    uint8_t schedule(uint32_t delayMs, SchedulerCallback callback, void* context = nullptr);

    /**
     * @brief Cancel a scheduled callback
     * @param id Timer ID returned from schedule()
     */
    void cancel(uint8_t id);

    /**
     * @brief Cancel all pending callbacks
     */
    void cancelAll();

    /**
     * @brief Process due callbacks
     *
     * Call this from the main loop. Executes all callbacks whose
     * scheduled time has passed.
     */
    void update();

    /**
     * @brief Get number of pending callbacks
     * @return Count of active timers
     */
    uint8_t getPendingCount() const;

    /**
     * @brief Check if a timer is active
     * @param id Timer ID
     * @return true if timer is pending
     */
    bool isActive(uint8_t id) const;

private:
    static constexpr uint8_t MAX_TIMERS = 12;
    static constexpr uint8_t INVALID_ID = 255;

    struct Timer {
        uint32_t fireTimeMs;
        SchedulerCallback callback;
        void* context;
        bool active;
    };

    Timer _timers[MAX_TIMERS];
};

// Global instance
extern TimerScheduler scheduler;

#endif // TIMER_SCHEDULER_H
