/**
 * @file state_machine.cpp
 * @brief BlueBuzzah therapy state machine - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "state_machine.h"

// =============================================================================
// CONSTRUCTOR
// =============================================================================

TherapyStateMachine::TherapyStateMachine() :
    _currentState(TherapyState::IDLE),  // std::atomic supports direct initialization
    _previousState(TherapyState::IDLE),
    _callbackCount(0)
{
    for (int i = 0; i < MAX_STATE_CALLBACKS; i++) {
        _callbacks[i] = nullptr;
    }
}

// =============================================================================
// INITIALIZATION
// =============================================================================

void TherapyStateMachine::begin(TherapyState initialState) {
    _currentState.store(initialState, std::memory_order_release);
    _previousState.store(initialState, std::memory_order_release);
    Serial.printf("[STATE] Initialized: %s\n", therapyStateToString(initialState));
}

// =============================================================================
// STATE TRANSITIONS
// =============================================================================

bool TherapyStateMachine::transition(StateTrigger trigger) {
    // TP-2: Thread-safe transition using atomic compare-and-swap
    // Load current state ONCE and pass to determineNextState to avoid TOCTOU race
    TherapyState expected = _currentState.load(std::memory_order_acquire);
    TherapyState newState = determineNextState(expected, trigger);

    if (newState == expected) {
        // No state change
        return false;
    }

    // Atomic compare-and-swap: only update if state hasn't changed
    // If another thread changed state, we fail and return false
    if (!_currentState.compare_exchange_strong(expected, newState,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
        // State was changed by another thread - transition failed
        return false;
    }

    // Update previous state (safe after successful CAS)
    _previousState.store(expected, std::memory_order_release);

    Serial.printf("[STATE] %s -> %s [%s]\n",
        therapyStateToString(expected),
        therapyStateToString(newState),
        stateTriggerToString(trigger));

    // Notify callbacks
    StateTransition trans(expected, newState, trigger);
    notifyCallbacks(trans);

    return true;
}

void TherapyStateMachine::forceState(TherapyState state, const char* reason) {
    // TP-2: Force is intentionally non-atomic (emergency override)
    // Capture previous before storing new
    TherapyState previous = _currentState.load(std::memory_order_acquire);
    _previousState.store(previous, std::memory_order_release);
    _currentState.store(state, std::memory_order_release);

    Serial.printf("[STATE] FORCED: %s -> %s",
        therapyStateToString(previous),
        therapyStateToString(state));

    if (reason) {
        Serial.printf(" (%s)", reason);
    }
    Serial.println();

    // Notify callbacks with special trigger
    StateTransition trans(previous, state, StateTrigger::FORCED_SHUTDOWN, reason);
    notifyCallbacks(trans);
}

void TherapyStateMachine::reset() {
    // TP-2: Capture previous before storing new
    TherapyState previous = _currentState.load(std::memory_order_acquire);
    _previousState.store(previous, std::memory_order_release);
    _currentState.store(TherapyState::IDLE, std::memory_order_release);

    Serial.println(F("[STATE] Reset to IDLE"));

    StateTransition trans(previous, TherapyState::IDLE, StateTrigger::RESET);
    notifyCallbacks(trans);
}

// =============================================================================
// CALLBACKS
// =============================================================================

bool TherapyStateMachine::onStateChange(StateChangeCallback callback) {
    if (_callbackCount >= MAX_STATE_CALLBACKS) {
        Serial.println(F("[STATE] WARNING: Max callbacks reached"));
        return false;
    }

    // Check if already registered
    for (int i = 0; i < _callbackCount; i++) {
        if (_callbacks[i] == callback) {
            return true;  // Already registered
        }
    }

    _callbacks[_callbackCount++] = callback;
    return true;
}

void TherapyStateMachine::clearCallbacks() {
    for (int i = 0; i < MAX_STATE_CALLBACKS; i++) {
        _callbacks[i] = nullptr;
    }
    _callbackCount = 0;
}

void TherapyStateMachine::notifyCallbacks(const StateTransition& transition) {
    for (int i = 0; i < _callbackCount; i++) {
        if (_callbacks[i]) {
            _callbacks[i](transition);
        }
    }
}

// =============================================================================
// STATE TRANSITION LOGIC
// =============================================================================

TherapyState TherapyStateMachine::determineNextState(TherapyState current, StateTrigger trigger) {
    // TP-2: State passed in from caller to avoid TOCTOU race
    // (caller already loaded it atomically)

    switch (trigger) {
        // =====================================================================
        // CONNECTION TRIGGERS
        // =====================================================================
        case StateTrigger::CONNECTED:
            if (current == TherapyState::IDLE || current == TherapyState::CONNECTING) {
                return TherapyState::READY;
            }
            if (current == TherapyState::CONNECTION_LOST) {
                return TherapyState::READY;
            }
            break;

        case StateTrigger::DISCONNECTED:
            // Any active state -> CONNECTION_LOST
            if (current == TherapyState::RUNNING ||
                current == TherapyState::PAUSED ||
                current == TherapyState::READY) {
                return TherapyState::CONNECTION_LOST;
            }
            break;

        case StateTrigger::RECONNECTED:
            if (current == TherapyState::CONNECTION_LOST) {
                return TherapyState::READY;
            }
            break;

        case StateTrigger::RECONNECT_FAILED:
            if (current == TherapyState::CONNECTION_LOST) {
                return TherapyState::IDLE;
            }
            break;

        // =====================================================================
        // SESSION TRIGGERS
        // =====================================================================
        case StateTrigger::START_SESSION:
            if (current == TherapyState::READY || current == TherapyState::IDLE) {
                return TherapyState::RUNNING;
            }
            break;

        case StateTrigger::PAUSE_SESSION:
            if (current == TherapyState::RUNNING) {
                return TherapyState::PAUSED;
            }
            break;

        case StateTrigger::RESUME_SESSION:
            if (current == TherapyState::PAUSED) {
                return TherapyState::RUNNING;
            }
            break;

        case StateTrigger::STOP_SESSION:
            if (current == TherapyState::RUNNING || current == TherapyState::PAUSED) {
                return TherapyState::STOPPING;
            }
            break;

        case StateTrigger::SESSION_COMPLETE:
        case StateTrigger::STOPPED:
            if (current == TherapyState::STOPPING) {
                return TherapyState::IDLE;
            }
            if (current == TherapyState::RUNNING) {
                return TherapyState::IDLE;
            }
            break;

        // =====================================================================
        // BATTERY TRIGGERS
        // =====================================================================
        case StateTrigger::BATTERY_WARNING:
            if (current == TherapyState::RUNNING) {
                return TherapyState::LOW_BATTERY;
            }
            break;

        case StateTrigger::BATTERY_CRITICAL:
            // Any state -> CRITICAL_BATTERY (emergency)
            return TherapyState::CRITICAL_BATTERY;

        case StateTrigger::BATTERY_OK:
            if (current == TherapyState::LOW_BATTERY) {
                return TherapyState::RUNNING;
            }
            break;

        // =====================================================================
        // PHONE TRIGGERS
        // =====================================================================
        case StateTrigger::PHONE_LOST:
            if (current == TherapyState::READY || current == TherapyState::RUNNING) {
                return TherapyState::PHONE_DISCONNECTED;
            }
            break;

        case StateTrigger::PHONE_RECONNECTED:
            if (current == TherapyState::PHONE_DISCONNECTED) {
                // TP-2: Use atomic load for thread-safe read of previous state
                return _previousState.load(std::memory_order_acquire);
            }
            break;

        case StateTrigger::PHONE_TIMEOUT:
            if (current == TherapyState::PHONE_DISCONNECTED) {
                // Timeout waiting for phone - continue without
                // TP-2: Use atomic load for thread-safe read of previous state
                return _previousState.load(std::memory_order_acquire);
            }
            break;

        // =====================================================================
        // ERROR TRIGGERS
        // =====================================================================
        case StateTrigger::ERROR_OCCURRED:
            return TherapyState::ERROR;

        case StateTrigger::EMERGENCY_STOP:
            // Emergency stop from any active state
            if (isActiveState(current)) {
                return TherapyState::ERROR;
            }
            break;

        // =====================================================================
        // RESET TRIGGERS
        // =====================================================================
        case StateTrigger::RESET:
            return TherapyState::IDLE;

        case StateTrigger::FORCED_SHUTDOWN:
            return TherapyState::IDLE;

        default:
            break;
    }

    // No valid transition - stay in current state
    return current;
}
