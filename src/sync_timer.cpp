/**
 * @file sync_timer.cpp
 * @brief Hardware timer implementation for microsecond-precision sync
 */

#include "sync_timer.h"
#include "sync_protocol.h"  // For getMicros()
#include "hardware.h"
#include "profile_manager.h"

// External reference to profile manager for debug mode check
extern ProfileManager profiles;

// Set to 1 to enable hardware timer, 0 for software fallback
#define USE_NRF52_TIMER_INTERRUPT 1

#if USE_NRF52_TIMER_INTERRUPT
// NRF52_TimerInterrupt library
// Uses hardware TIMER3 (TIMER0 reserved for SoftDevice, TIMER1 for PWM, TIMER2 conflicts with Adafruit core)
#include <NRF52TimerInterrupt.h>

// Hardware timer instance using TIMER3
// NOTE: TIMER2 conflicts with Adafruit nRF52 core (both define TIMER2_IRQHandler)
// This caused hard freezes on PRIMARY devices when advertising was active.
// TIMER0 = SoftDevice (BLE), TIMER1 = PWM, TIMER2 = Adafruit core, TIMER3/4 = available
static NRF52Timer ITimer3(NRF_TIMER_3);
#endif

// Singleton instance
SyncTimer* SyncTimer::_instance = nullptr;

// Global instance
SyncTimer syncTimer;

SyncTimer::SyncTimer()
    : _haptic(nullptr)
    , _activationPending(false)
    , _finger(0)
    , _amplitude(0)
    , _frequencyHz(250)
    , _initialized(false)
    , _onActivationComplete(nullptr)
    , _channelPreSelected(false)
    , _preSelectedFinger(0)
    , _motorTaskHandle(nullptr)
{
    _instance = this;
}

bool SyncTimer::begin(HapticController* haptic) {
    if (!haptic) {
        return false;
    }

    _haptic = haptic;
    _activationPending = false;
    _initialized = true;

    Serial.println(F("[SYNC_TIMER] Hardware timer initialized (TIMER3)"));
    return true;
}

void SyncTimer::timerISR() {
    // ISR-safe: only set flag and notify task, no I2C or Serial
    // NOTE: Do NOT call stopTimer() here - it's not ISR-safe on nRF52
    // The timer will be stopped in processPendingActivation()
    if (_instance && !_instance->_activationPending) {
        _instance->_activationPending = true;

        // FreeRTOS task notification: wake high-priority motor task
        // Task will preempt main loop immediately due to Priority 4 (HIGHEST)
        if (_instance->_motorTaskHandle != nullptr) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(_instance->_motorTaskHandle, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
        // If no task handle, main loop will poll via processPendingActivation()
    }
}

void SyncTimer::setMotorTaskHandle(TaskHandle_t handle) {
    _motorTaskHandle = handle;
    if (handle != nullptr) {
        Serial.println(F("[SYNC_TIMER] Motor task registered - ISR will notify task"));
    }
}

void SyncTimer::scheduleActivation(uint32_t delayUs, uint8_t finger, uint8_t amplitude, uint16_t frequencyHz) {
    if (!_initialized) {
        return;
    }

#if USE_NRF52_TIMER_INTERRUPT
    // Cancel any pending activation first
    ITimer3.stopTimer();

    // Store parameters for ISR
    _finger = finger;
    _amplitude = amplitude;
    _frequencyHz = frequencyHz;
    _activationPending = false;

    // Minimum delay to avoid immediate fire (timer setup overhead ~10us)
    if (delayUs < 50) {
        delayUs = 50;
    }

    // Configure timer with interval in microseconds
    // Timer will fire repeatedly, but we stop it in ISR for one-shot behavior
    if (ITimer3.attachInterruptInterval(delayUs, timerISR)) {
        // Timer armed successfully
    } else {
        // Fallback: activate immediately if timer fails
        _activationPending = true;
    }
#else
    // Timer disabled - just set pending for immediate activation
    _finger = finger;
    _amplitude = amplitude;
    _frequencyHz = frequencyHz;
    _activationPending = true;
#endif
}

bool SyncTimer::scheduleAbsoluteActivation(uint64_t absoluteTimeUs, uint8_t finger, uint8_t amplitude, uint16_t frequencyHz) {
    if (!_initialized) {
        return false;
    }

    uint64_t now = getMicros();

    // Check if target time has already passed
    if (absoluteTimeUs <= now) {
        // Already past - set pending for immediate activation
#if USE_NRF52_TIMER_INTERRUPT
        // Cancel any existing timer first
        ITimer3.stopTimer();
#endif

        _finger = finger;
        _amplitude = amplitude;
        _frequencyHz = frequencyHz;
        _activationPending = true;

        if (profiles.getDebugMode()) {
            Serial.printf("[SYNC_TIMER] Immediate (late by %lu us)\n",
                          (unsigned long)(now - absoluteTimeUs));
        }
        return false;  // Indicate immediate, not scheduled
    }

    // Calculate delay from now to target time
    uint64_t delayUs64 = absoluteTimeUs - now;

    // Clamp to 32-bit max (hardware timer limit ~71 minutes)
    uint32_t delayUs;
    if (delayUs64 > UINT32_MAX) {
        delayUs = UINT32_MAX;
    } else {
        delayUs = (uint32_t)delayUs64;
    }

    if (profiles.getDebugMode()) {
        Serial.printf("[SYNC_TIMER] Scheduled in %lu us\n", (unsigned long)delayUs);
    }

    // Use existing scheduling method
    scheduleActivation(delayUs, finger, amplitude, frequencyHz);
    return true;  // Indicate scheduled for future
}

bool SyncTimer::processPendingActivation() {
    if (!_activationPending) {
        return false;
    }

#if USE_NRF52_TIMER_INTERRUPT
    // CRITICAL: Stop timer FIRST from main loop context (ISR-safe)
    // This prevents the repeating timer from setting the flag again
    ITimer3.stopTimer();
#endif

    // Clear flag after stopping timer
    _activationPending = false;

    // Execute motor activation (I2C-safe context)
    if (_haptic && _haptic->isEnabled(_finger)) {
        if (profiles.getDebugMode()) {
            Serial.printf("[SYNC_TIMER] Firing F%d A%d @%dHz%s\n",
                          _finger, _amplitude, _frequencyHz,
                          _channelPreSelected ? " (pre-sel)" : "");
        }

        // Use optimized path if channel was pre-selected for this finger
        if (_channelPreSelected && _finger == _preSelectedFinger) {
            // FAST PATH: Channel already selected, frequency already set
            // Only write RTP value (~100-150μs instead of ~400-600μs)
            _haptic->activatePreSelected(_finger, _amplitude);
            // Close channel after activation (deferred from pre-selection)
            _haptic->closeAllChannels();
        } else {
            // FALLBACK PATH: Full activation sequence
            // Used when pre-selection failed or finger mismatch
            _haptic->setFrequency(_finger, _frequencyHz);
            _haptic->activate(_finger, _amplitude);
        }
    }

    // Clear pre-selection state after use
    _channelPreSelected = false;

    // Chain scheduling callback - allows scheduling next event
    if (_onActivationComplete) {
        _onActivationComplete();
    }

    return true;
}

void SyncTimer::setActivationCompleteCallback(ActivationCompleteCallback callback) {
    _onActivationComplete = callback;
}

void SyncTimer::processPendingActivationFromTask() {
    // Called from high-priority motor task after ISR notification
    // This method executes in task context, safe for I2C operations

    if (!_activationPending) {
        return;
    }

#if USE_NRF52_TIMER_INTERRUPT
    // CRITICAL: Stop timer FIRST (prevents repeated ISR fires)
    ITimer3.stopTimer();
#endif

    // Clear flag after stopping timer
    _activationPending = false;

    // Execute motor activation (I2C-safe context)
    if (_haptic && _haptic->isEnabled(_finger)) {
        if (profiles.getDebugMode()) {
            Serial.printf("[MOTOR_TASK] Firing F%d A%d @%dHz%s\n",
                          _finger, _amplitude, _frequencyHz,
                          _channelPreSelected ? " (pre-sel)" : "");
        }

        // Use optimized path if channel was pre-selected for this finger
        if (_channelPreSelected && _finger == _preSelectedFinger) {
            // FAST PATH: Channel already selected, frequency already set
            // Only write RTP value (~100-150μs instead of ~400-600μs)
            _haptic->activatePreSelected(_finger, _amplitude);
            // Close channel after activation (deferred from pre-selection)
            _haptic->closeAllChannels();
        } else {
            // FALLBACK PATH: Full activation sequence
            _haptic->setFrequency(_finger, _frequencyHz);
            _haptic->activate(_finger, _amplitude);
        }
    }

    // Clear pre-selection state after use
    _channelPreSelected = false;

    // Chain scheduling callback - allows scheduling next event
    // Note: This may call scheduleNext() which does I2C pre-selection
    if (_onActivationComplete) {
        _onActivationComplete();
    }
}

void SyncTimer::cancel() {
#if USE_NRF52_TIMER_INTERRUPT
    ITimer3.stopTimer();
#endif
    _activationPending = false;
    clearPreSelection();
}

void SyncTimer::setPreSelected(uint8_t finger) {
    _channelPreSelected = true;
    _preSelectedFinger = finger;
}

void SyncTimer::clearPreSelection() {
    if (_channelPreSelected && _haptic) {
        // Close the pre-selected channel if it was left open
        _haptic->closeAllChannels();
    }
    _channelPreSelected = false;
}
