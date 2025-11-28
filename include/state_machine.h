/**
 * @file state_machine.h
 * @brief BlueBuzzah therapy state machine - State management and transitions
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * Implements a state machine for therapy session management:
 * - State tracking (IDLE, READY, RUNNING, PAUSED, etc.)
 * - Event-driven transitions via triggers
 * - Callback notification on state changes
 * - Force state for emergency conditions
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>
#include "types.h"

// =============================================================================
// CONSTANTS
// =============================================================================

#define MAX_STATE_CALLBACKS 4

// =============================================================================
// STATE TRANSITION
// =============================================================================

/**
 * @brief Represents a state transition event
 */
struct StateTransition {
    TherapyState fromState;
    TherapyState toState;
    StateTrigger trigger;
    const char* reason;  // Optional reason for forced transitions

    StateTransition() :
        fromState(TherapyState::IDLE),
        toState(TherapyState::IDLE),
        trigger(StateTrigger::RESET),
        reason(nullptr) {}

    StateTransition(TherapyState from, TherapyState to, StateTrigger trig, const char* rsn = nullptr) :
        fromState(from),
        toState(to),
        trigger(trig),
        reason(rsn) {}
};

// =============================================================================
// CALLBACK TYPES
// =============================================================================

/**
 * @brief Callback function type for state change notifications
 */
typedef void (*StateChangeCallback)(const StateTransition& transition);

// =============================================================================
// THERAPY STATE MACHINE
// =============================================================================

/**
 * @brief Manages therapy session state transitions
 *
 * The state machine tracks the current therapy state and handles
 * transitions based on trigger events. It notifies registered
 * callbacks whenever the state changes.
 *
 * States (from types.h):
 *   - IDLE: No active session
 *   - CONNECTING: Establishing BLE connection during boot
 *   - READY: Connected, ready for therapy
 *   - RUNNING: Active therapy session
 *   - PAUSED: Session paused, can resume
 *   - STOPPING: Session ending, cleanup in progress
 *   - ERROR: Error condition
 *   - LOW_BATTERY: Battery warning
 *   - CRITICAL_BATTERY: Battery critical
 *   - CONNECTION_LOST: BLE connection lost
 *   - PHONE_DISCONNECTED: Phone disconnected (informational)
 *
 * Usage:
 *   TherapyStateMachine machine;
 *   machine.begin(TherapyState::IDLE);
 *
 *   machine.onStateChange([](const StateTransition& t) {
 *       Serial.printf("State: %s -> %s\n",
 *           therapyStateToString(t.fromState),
 *           therapyStateToString(t.toState));
 *   });
 *
 *   machine.transition(StateTrigger::CONNECTED);
 *   // State changes: IDLE -> READY
 */
class TherapyStateMachine {
public:
    TherapyStateMachine();

    /**
     * @brief Initialize state machine with starting state
     * @param initialState Starting state (default: IDLE)
     */
    void begin(TherapyState initialState = TherapyState::IDLE);

    /**
     * @brief Get current therapy state
     */
    TherapyState getCurrentState() const { return _currentState; }

    /**
     * @brief Get previous therapy state
     */
    TherapyState getPreviousState() const { return _previousState; }

    /**
     * @brief Trigger a state transition
     *
     * Applies the trigger and transitions to the appropriate new state
     * based on the current state and transition rules.
     *
     * @param trigger Event that causes the transition
     * @return true if transition occurred, false if no valid transition
     */
    bool transition(StateTrigger trigger);

    /**
     * @brief Force the state machine to a specific state
     *
     * Bypasses normal transition logic. Used for error conditions
     * and emergency situations.
     *
     * @param state State to force
     * @param reason Optional reason for the forced transition
     */
    void forceState(TherapyState state, const char* reason = nullptr);

    /**
     * @brief Reset state machine to IDLE
     */
    void reset();

    // =========================================================================
    // CALLBACKS
    // =========================================================================

    /**
     * @brief Register callback for state change events
     *
     * The callback will be called with a StateTransition object
     * whenever the state changes.
     *
     * @param callback Function to call on state changes
     * @return true if callback registered, false if max callbacks reached
     */
    bool onStateChange(StateChangeCallback callback);

    /**
     * @brief Clear all registered callbacks
     */
    void clearCallbacks();

    // =========================================================================
    // STATE CHECKS
    // =========================================================================

    /**
     * @brief Check if currently in an active therapy session
     */
    bool isActive() const { return isActiveState(_currentState); }

    /**
     * @brief Check if currently in an error state
     */
    bool isError() const { return isErrorState(_currentState); }

    /**
     * @brief Check if therapy is running
     */
    bool isRunning() const { return _currentState == TherapyState::RUNNING; }

    /**
     * @brief Check if therapy is paused
     */
    bool isPaused() const { return _currentState == TherapyState::PAUSED; }

    /**
     * @brief Check if ready for therapy
     */
    bool isReady() const { return _currentState == TherapyState::READY; }

    /**
     * @brief Check if idle (no session)
     */
    bool isIdle() const { return _currentState == TherapyState::IDLE; }

private:
    TherapyState _currentState;
    TherapyState _previousState;

    StateChangeCallback _callbacks[MAX_STATE_CALLBACKS];
    uint8_t _callbackCount;

    /**
     * @brief Determine next state based on current state and trigger
     * @param trigger Event causing the transition
     * @return New state (may be same as current if no valid transition)
     */
    TherapyState determineNextState(StateTrigger trigger);

    /**
     * @brief Notify all registered callbacks of state change
     */
    void notifyCallbacks(const StateTransition& transition);
};

#endif // STATE_MACHINE_H
