/**
 * @file sync_timer.h
 * @brief Hardware timer for microsecond-precision sync compensation
 * @version 2.0.0
 *
 * Uses nRF52840 hardware TIMER3 via NRF52_TimerInterrupt library
 * to achieve <100us sync precision between PRIMARY and SECONDARY devices.
 *
 * Architecture (FreeRTOS):
 * - scheduleActivation() arms hardware timer for future activation
 * - Timer ISR notifies high-priority motor task via vTaskNotifyGiveFromISR()
 * - Motor task (Priority 4/HIGHEST) preempts main loop and executes I2C activation
 * - Chain scheduling: callback after activation enables scheduling next event
 *
 * This architecture ensures motor activations are never blocked by Serial
 * logging (~100-500μs) or BLE operations (~1-5ms) in the main loop.
 */

#ifndef SYNC_TIMER_H
#define SYNC_TIMER_H

#include <Arduino.h>
#include "rtos.h"  // FreeRTOS task notification

// Forward declaration for HapticController
class HapticController;

// Callback type for chain scheduling - called after activation completes
typedef void (*ActivationCompleteCallback)();

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
     * Arms hardware TIMER3 to fire after delayUs microseconds.
     * When timer fires, ISR sets flag. Call processPendingActivation()
     * to execute the actual motor activation (I2C-safe context).
     *
     * @param delayUs Delay in microseconds before activation
     * @param finger Finger index (0-3)
     * @param amplitude Activation amplitude (0-100)
     * @param frequencyHz Motor frequency in Hz (set before activation)
     */
    void scheduleActivation(uint32_t delayUs, uint8_t finger, uint8_t amplitude, uint16_t frequencyHz);

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
     * @param frequencyHz Motor frequency in Hz (set before activation)
     * @return true if scheduled for future, false if immediate activation
     */
    bool scheduleAbsoluteActivation(uint64_t absoluteTimeUs, uint8_t finger, uint8_t amplitude, uint16_t frequencyHz);

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

    /**
     * @brief Set callback for chain scheduling
     *
     * Called after processPendingActivation() completes motor activation,
     * allowing the caller to schedule the next activation in a chain.
     * This enables hardware timer precision for multiple sequential events.
     *
     * @param callback Function to call after each activation completes
     */
    void setActivationCompleteCallback(ActivationCompleteCallback callback);

    // =========================================================================
    // FREERTOS MOTOR TASK INTEGRATION
    // =========================================================================

    /**
     * @brief Set the FreeRTOS task handle for motor activations
     *
     * When set, the timer ISR will notify this task using vTaskNotifyGiveFromISR()
     * instead of relying on main loop polling. This enables preemptive activation
     * that cannot be blocked by Serial/BLE operations.
     *
     * @param handle Task handle created by xTaskCreate() in main.cpp
     */
    void setMotorTaskHandle(TaskHandle_t handle);

    /**
     * @brief Process pending activation from motor task context
     *
     * Called by the high-priority motor task after receiving notification
     * from the timer ISR. Executes the I2C motor activation sequence.
     * This method should NOT be called from main loop when task is active.
     */
    void processPendingActivationFromTask();

    // =========================================================================
    // PRE-SELECTION OPTIMIZATION
    // =========================================================================

    /**
     * @brief Mark that channel has been pre-selected for next activation
     * @param finger Finger index that was pre-selected
     *
     * When set, processPendingActivation() will use the optimized path
     * (activatePreSelected) instead of the full activation path.
     */
    void setPreSelected(uint8_t finger);

    /**
     * @brief Check if pre-selection is active
     * @return true if channel is pre-selected for next activation
     */
    bool isPreSelected() const { return _channelPreSelected; }

    /**
     * @brief Clear pre-selection state (e.g., on cancel or fallback)
     */
    void clearPreSelection();

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
    volatile uint16_t _frequencyHz;

    // Timer initialized flag
    bool _initialized;

    // Chain scheduling callback
    ActivationCompleteCallback _onActivationComplete;

    // Pre-selection optimization state
    bool _channelPreSelected;
    uint8_t _preSelectedFinger;

    // FreeRTOS motor task handle (NULL = fallback to main loop polling)
    TaskHandle_t _motorTaskHandle;
};

// Global instance
extern SyncTimer syncTimer;

#endif // SYNC_TIMER_H
