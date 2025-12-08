/**
 * @file sync_timer.h
 * @brief Hardware timer for microsecond-precision sync compensation
 * @version 1.0.0
 *
 * Uses nRF52840 hardware TIMER2 via NRF52_TimerInterrupt library
 * to achieve <100us sync precision between PRIMARY and SECONDARY devices.
 *
 * Architecture:
 * - scheduleActivation() arms hardware timer for future activation
 * - Timer ISR sets flag (ISR-safe, no I2C)
 * - processPendingActivation() called at TOP of loop() executes motor activation
 */

#ifndef SYNC_TIMER_H
#define SYNC_TIMER_H

#include <Arduino.h>

// Forward declaration for HapticController
class HapticController;

/**
 * @class SyncTimer
 * @brief Hardware timer-based motor activation scheduler
 *
 * Provides microsecond-precision scheduling for bilateral sync compensation.
 * When PRIMARY sends BUZZ to SECONDARY, it schedules local activation
 * after measured latency using this hardware timer.
 */
class SyncTimer {
public:
    SyncTimer();

    /**
     * @brief Initialize hardware timer
     * @param haptic Pointer to HapticController for motor activation
     * @return true if initialization successful
     */
    bool begin(HapticController* haptic);

    /**
     * @brief Schedule motor activation after specified delay
     *
     * Arms hardware TIMER2 to fire after delayUs microseconds.
     * When timer fires, ISR sets flag. Call processPendingActivation()
     * to execute the actual motor activation (I2C-safe context).
     *
     * @param delayUs Delay in microseconds before activation
     * @param finger Finger index (0-3)
     * @param amplitude Activation amplitude (0-100)
     */
    void scheduleActivation(uint32_t delayUs, uint8_t finger, uint8_t amplitude);

    /**
     * @brief Schedule motor activation at an absolute time
     *
     * Calculates delay from current time to target time and schedules
     * using hardware timer. If target time has already passed, activates
     * immediately (sets pending flag for next processPendingActivation call).
     *
     * @param absoluteTimeUs Target activation time in microseconds (local clock)
     * @param finger Finger index (0-3)
     * @param amplitude Activation amplitude (0-100)
     * @return true if scheduled for future, false if immediate activation
     */
    bool scheduleAbsoluteActivation(uint64_t absoluteTimeUs, uint8_t finger, uint8_t amplitude);

    /**
     * @brief Check and execute pending activation
     *
     * Call this at the TOP of loop() for minimum latency.
     * If timer ISR has set the pending flag, executes motor activation.
     *
     * @return true if activation was executed
     */
    bool processPendingActivation();

    /**
     * @brief Cancel any pending activation
     */
    void cancel();

    /**
     * @brief Check if activation is pending
     * @return true if activation scheduled and not yet executed
     */
    bool isPending() const { return _activationPending; }

    /**
     * @brief Get scheduled finger (for diagnostics)
     */
    uint8_t getScheduledFinger() const { return _finger; }

    /**
     * @brief Get scheduled amplitude (for diagnostics)
     */
    uint8_t getScheduledAmplitude() const { return _amplitude; }

private:
    // Timer ISR callback (static for C linkage)
    static void timerISR();

    // Singleton instance for ISR access
    static SyncTimer* _instance;

    // Haptic controller reference
    HapticController* _haptic;

    // Volatile for ISR-safe access
    volatile bool _activationPending;
    volatile uint8_t _finger;
    volatile uint8_t _amplitude;

    // Timer initialized flag
    bool _initialized;
};

// Global instance
extern SyncTimer syncTimer;

#endif // SYNC_TIMER_H
