/**
 * @file sync_timer.cpp
 * @brief Hardware timer implementation for microsecond-precision sync
 */

#include "sync_timer.h"
#include "hardware.h"

// NRF52_TimerInterrupt library
// Uses hardware TIMER2 (TIMER0 reserved for SoftDevice, TIMER1 for PWM)
#include <NRF52TimerInterrupt.h>

// Hardware timer instance using TIMER2
static NRF52Timer ITimer2(NRF_TIMER_2);

// Singleton instance
SyncTimer* SyncTimer::_instance = nullptr;

// Global instance
SyncTimer syncTimer;

SyncTimer::SyncTimer()
    : _haptic(nullptr)
    , _activationPending(false)
    , _finger(0)
    , _amplitude(0)
    , _initialized(false)
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

    Serial.println(F("[SYNC_TIMER] Hardware timer initialized (TIMER2)"));
    return true;
}

void SyncTimer::timerISR() {
    // ISR-safe: only set flag, no I2C or Serial
    // NOTE: Do NOT call stopTimer() here - it's not ISR-safe on nRF52
    // The timer will be stopped in processPendingActivation()
    if (_instance && !_instance->_activationPending) {
        _instance->_activationPending = true;
    }
}

void SyncTimer::scheduleActivation(uint32_t delayUs, uint8_t finger, uint8_t amplitude) {
    if (!_initialized) {
        return;
    }

    // Cancel any pending activation first
    ITimer2.stopTimer();

    // Store parameters for ISR
    _finger = finger;
    _amplitude = amplitude;
    _activationPending = false;

    // Minimum delay to avoid immediate fire (timer setup overhead ~10us)
    if (delayUs < 50) {
        delayUs = 50;
    }

    // Configure timer with interval in microseconds
    // Timer will fire repeatedly, but we stop it in ISR for one-shot behavior
    if (ITimer2.attachInterruptInterval(delayUs, timerISR)) {
        // Timer armed successfully
    } else {
        // Fallback: activate immediately if timer fails
        _activationPending = true;
    }
}

bool SyncTimer::processPendingActivation() {
    if (!_activationPending) {
        return false;
    }

    // CRITICAL: Stop timer FIRST from main loop context (ISR-safe)
    // This prevents the repeating timer from setting the flag again
    ITimer2.stopTimer();

    // Clear flag after stopping timer
    _activationPending = false;

    // Execute motor activation (I2C-safe context)
    if (_haptic && _haptic->isEnabled(_finger)) {
        Serial.printf("[SYNC_TIMER] Firing F%d A%d\n", _finger, _amplitude);
        _haptic->activate(_finger, _amplitude);
    }

    return true;
}

void SyncTimer::cancel() {
    ITimer2.stopTimer();
    _activationPending = false;
}
