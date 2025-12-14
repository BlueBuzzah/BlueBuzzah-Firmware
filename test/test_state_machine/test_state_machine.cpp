/**
 * @file test_state_machine.cpp
 * @brief Unit tests for state_machine - FSM transitions and callbacks
 */

#include <unity.h>
#include "state_machine.h"

// Include source file directly for native testing
// (excluded from build_src_filter to avoid conflicts with other tests)
#include "../../src/state_machine.cpp"

// =============================================================================
// TEST STATE FOR CALLBACKS
// =============================================================================

static bool g_callbackCalled = false;
static StateTransition g_lastTransition;
static int g_callbackCount = 0;

void testCallback(const StateTransition& transition) {
    g_callbackCalled = true;
    g_lastTransition = transition;
    g_callbackCount++;
}

void testCallback2(const StateTransition& transition) {
    // Second callback for testing multiple callbacks
    g_callbackCount++;
}

// =============================================================================
// TEST FIXTURES
// =============================================================================

void setUp(void) {
    // Reset callback state before each test
    g_callbackCalled = false;
    g_callbackCount = 0;
    g_lastTransition = StateTransition();
}

void tearDown(void) {
    // Clean up after each test
}

// =============================================================================
// CONSTRUCTOR AND INITIALIZATION TESTS
// =============================================================================

void test_StateMachine_default_state(void) {
    TherapyStateMachine sm;
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

void test_StateMachine_begin_default(void) {
    TherapyStateMachine sm;
    sm.begin();
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

void test_StateMachine_begin_custom_state(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::CONNECTING);
    TEST_ASSERT_EQUAL(TherapyState::CONNECTING, sm.getCurrentState());
}

void test_StateMachine_getPreviousState_initial(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getPreviousState());
}

// =============================================================================
// CONNECTION TRANSITION TESTS
// =============================================================================

void test_StateMachine_idle_to_ready_on_connected(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::CONNECTED));
    TEST_ASSERT_EQUAL(TherapyState::READY, sm.getCurrentState());
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getPreviousState());
}

void test_StateMachine_connecting_to_ready_on_connected(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::CONNECTING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::CONNECTED));
    TEST_ASSERT_EQUAL(TherapyState::READY, sm.getCurrentState());
}

void test_StateMachine_ready_to_connectionLost_on_disconnected(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::READY);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::DISCONNECTED));
    TEST_ASSERT_EQUAL(TherapyState::CONNECTION_LOST, sm.getCurrentState());
}

void test_StateMachine_running_to_connectionLost_on_disconnected(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::DISCONNECTED));
    TEST_ASSERT_EQUAL(TherapyState::CONNECTION_LOST, sm.getCurrentState());
}

void test_StateMachine_connectionLost_to_ready_on_reconnected(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::CONNECTION_LOST);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::RECONNECTED));
    TEST_ASSERT_EQUAL(TherapyState::READY, sm.getCurrentState());
}

void test_StateMachine_connectionLost_to_idle_on_reconnectFailed(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::CONNECTION_LOST);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::RECONNECT_FAILED));
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

// =============================================================================
// SESSION TRANSITION TESTS
// =============================================================================

void test_StateMachine_ready_to_running_on_startSession(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::READY);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::START_SESSION));
    TEST_ASSERT_EQUAL(TherapyState::RUNNING, sm.getCurrentState());
}

void test_StateMachine_idle_to_running_on_startSession(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::START_SESSION));
    TEST_ASSERT_EQUAL(TherapyState::RUNNING, sm.getCurrentState());
}

void test_StateMachine_running_to_paused_on_pauseSession(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::PAUSE_SESSION));
    TEST_ASSERT_EQUAL(TherapyState::PAUSED, sm.getCurrentState());
}

void test_StateMachine_paused_to_running_on_resumeSession(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::PAUSED);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::RESUME_SESSION));
    TEST_ASSERT_EQUAL(TherapyState::RUNNING, sm.getCurrentState());
}

void test_StateMachine_running_to_stopping_on_stopSession(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::STOP_SESSION));
    TEST_ASSERT_EQUAL(TherapyState::STOPPING, sm.getCurrentState());
}

void test_StateMachine_paused_to_stopping_on_stopSession(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::PAUSED);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::STOP_SESSION));
    TEST_ASSERT_EQUAL(TherapyState::STOPPING, sm.getCurrentState());
}

void test_StateMachine_stopping_to_idle_on_sessionComplete(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::STOPPING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::SESSION_COMPLETE));
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

void test_StateMachine_stopping_to_idle_on_stopped(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::STOPPING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::STOPPED));
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

void test_StateMachine_running_to_idle_on_sessionComplete(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::SESSION_COMPLETE));
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

// =============================================================================
// BATTERY TRANSITION TESTS
// =============================================================================

void test_StateMachine_running_to_lowBattery_on_batteryWarning(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::BATTERY_WARNING));
    TEST_ASSERT_EQUAL(TherapyState::LOW_BATTERY, sm.getCurrentState());
}

void test_StateMachine_lowBattery_to_running_on_batteryOk(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::LOW_BATTERY);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::BATTERY_OK));
    TEST_ASSERT_EQUAL(TherapyState::RUNNING, sm.getCurrentState());
}

void test_StateMachine_any_to_criticalBattery_on_batteryCritical(void) {
    // From IDLE
    TherapyStateMachine sm1;
    sm1.begin(TherapyState::IDLE);
    TEST_ASSERT_TRUE(sm1.transition(StateTrigger::BATTERY_CRITICAL));
    TEST_ASSERT_EQUAL(TherapyState::CRITICAL_BATTERY, sm1.getCurrentState());

    // From RUNNING
    TherapyStateMachine sm2;
    sm2.begin(TherapyState::RUNNING);
    TEST_ASSERT_TRUE(sm2.transition(StateTrigger::BATTERY_CRITICAL));
    TEST_ASSERT_EQUAL(TherapyState::CRITICAL_BATTERY, sm2.getCurrentState());

    // From READY
    TherapyStateMachine sm3;
    sm3.begin(TherapyState::READY);
    TEST_ASSERT_TRUE(sm3.transition(StateTrigger::BATTERY_CRITICAL));
    TEST_ASSERT_EQUAL(TherapyState::CRITICAL_BATTERY, sm3.getCurrentState());
}

// =============================================================================
// PHONE TRANSITION TESTS
// =============================================================================

void test_StateMachine_ready_to_phoneDisconnected_on_phoneLost(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::READY);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::PHONE_LOST));
    TEST_ASSERT_EQUAL(TherapyState::PHONE_DISCONNECTED, sm.getCurrentState());
}

void test_StateMachine_running_to_phoneDisconnected_on_phoneLost(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::PHONE_LOST));
    TEST_ASSERT_EQUAL(TherapyState::PHONE_DISCONNECTED, sm.getCurrentState());
}

// =============================================================================
// ERROR TRANSITION TESTS
// =============================================================================

void test_StateMachine_any_to_error_on_errorOccurred(void) {
    TherapyStateMachine sm1;
    sm1.begin(TherapyState::RUNNING);
    TEST_ASSERT_TRUE(sm1.transition(StateTrigger::ERROR_OCCURRED));
    TEST_ASSERT_EQUAL(TherapyState::ERROR, sm1.getCurrentState());

    TherapyStateMachine sm2;
    sm2.begin(TherapyState::IDLE);
    TEST_ASSERT_TRUE(sm2.transition(StateTrigger::ERROR_OCCURRED));
    TEST_ASSERT_EQUAL(TherapyState::ERROR, sm2.getCurrentState());
}

void test_StateMachine_activeState_to_error_on_emergencyStop(void) {
    // From RUNNING (active)
    TherapyStateMachine sm1;
    sm1.begin(TherapyState::RUNNING);
    TEST_ASSERT_TRUE(sm1.transition(StateTrigger::EMERGENCY_STOP));
    TEST_ASSERT_EQUAL(TherapyState::ERROR, sm1.getCurrentState());

    // From PAUSED (active)
    TherapyStateMachine sm2;
    sm2.begin(TherapyState::PAUSED);
    TEST_ASSERT_TRUE(sm2.transition(StateTrigger::EMERGENCY_STOP));
    TEST_ASSERT_EQUAL(TherapyState::ERROR, sm2.getCurrentState());

    // From LOW_BATTERY (active)
    TherapyStateMachine sm3;
    sm3.begin(TherapyState::LOW_BATTERY);
    TEST_ASSERT_TRUE(sm3.transition(StateTrigger::EMERGENCY_STOP));
    TEST_ASSERT_EQUAL(TherapyState::ERROR, sm3.getCurrentState());
}

void test_StateMachine_idle_no_change_on_emergencyStop(void) {
    // From IDLE (not active) - should not change
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);
    TEST_ASSERT_FALSE(sm.transition(StateTrigger::EMERGENCY_STOP));
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

// =============================================================================
// RESET TRANSITION TESTS
// =============================================================================

void test_StateMachine_any_to_idle_on_reset(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::RESET));
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

void test_StateMachine_any_to_idle_on_forcedShutdown(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::FORCED_SHUTDOWN));
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

// =============================================================================
// INVALID TRANSITION TESTS
// =============================================================================

void test_StateMachine_invalid_transition_returns_false(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);

    // Can't pause when not running
    TEST_ASSERT_FALSE(sm.transition(StateTrigger::PAUSE_SESSION));
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

void test_StateMachine_resume_from_nonPaused_stays_same(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);

    TEST_ASSERT_FALSE(sm.transition(StateTrigger::RESUME_SESSION));
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
}

void test_StateMachine_batteryOk_from_running_stays_same(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    // Battery OK only transitions from LOW_BATTERY
    TEST_ASSERT_FALSE(sm.transition(StateTrigger::BATTERY_OK));
    TEST_ASSERT_EQUAL(TherapyState::RUNNING, sm.getCurrentState());
}

// =============================================================================
// FORCE STATE TESTS
// =============================================================================

void test_StateMachine_forceState(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);

    sm.forceState(TherapyState::ERROR, "Test error");

    TEST_ASSERT_EQUAL(TherapyState::ERROR, sm.getCurrentState());
    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getPreviousState());
}

void test_StateMachine_forceState_notifies_callbacks(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);
    sm.onStateChange(testCallback);

    sm.forceState(TherapyState::CRITICAL_BATTERY, "Battery dead");

    TEST_ASSERT_TRUE(g_callbackCalled);
    TEST_ASSERT_EQUAL(TherapyState::IDLE, g_lastTransition.fromState);
    TEST_ASSERT_EQUAL(TherapyState::CRITICAL_BATTERY, g_lastTransition.toState);
}

// =============================================================================
// RESET METHOD TESTS
// =============================================================================

void test_StateMachine_reset(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    sm.reset();

    TEST_ASSERT_EQUAL(TherapyState::IDLE, sm.getCurrentState());
    TEST_ASSERT_EQUAL(TherapyState::RUNNING, sm.getPreviousState());
}

void test_StateMachine_reset_notifies_callbacks(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);
    sm.onStateChange(testCallback);

    sm.reset();

    TEST_ASSERT_TRUE(g_callbackCalled);
    TEST_ASSERT_EQUAL(StateTrigger::RESET, g_lastTransition.trigger);
}

// =============================================================================
// CALLBACK TESTS
// =============================================================================

void test_StateMachine_callback_on_transition(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);
    sm.onStateChange(testCallback);

    sm.transition(StateTrigger::CONNECTED);

    TEST_ASSERT_TRUE(g_callbackCalled);
    TEST_ASSERT_EQUAL(TherapyState::IDLE, g_lastTransition.fromState);
    TEST_ASSERT_EQUAL(TherapyState::READY, g_lastTransition.toState);
    TEST_ASSERT_EQUAL(StateTrigger::CONNECTED, g_lastTransition.trigger);
}

void test_StateMachine_no_callback_on_invalid_transition(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);
    sm.onStateChange(testCallback);

    sm.transition(StateTrigger::PAUSE_SESSION);  // Invalid from IDLE

    TEST_ASSERT_FALSE(g_callbackCalled);
}

void test_StateMachine_multiple_callbacks(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);
    sm.onStateChange(testCallback);
    sm.onStateChange(testCallback2);

    sm.transition(StateTrigger::CONNECTED);

    TEST_ASSERT_EQUAL(2, g_callbackCount);
}

void test_StateMachine_clearCallbacks(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);
    sm.onStateChange(testCallback);
    sm.clearCallbacks();

    sm.transition(StateTrigger::CONNECTED);

    TEST_ASSERT_FALSE(g_callbackCalled);
}

void test_StateMachine_duplicate_callback_not_registered(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);

    // Register same callback twice
    TEST_ASSERT_TRUE(sm.onStateChange(testCallback));
    TEST_ASSERT_TRUE(sm.onStateChange(testCallback));  // Should succeed but not add duplicate

    sm.transition(StateTrigger::CONNECTED);

    // Should only be called once
    TEST_ASSERT_EQUAL(1, g_callbackCount);
}

// =============================================================================
// STATE QUERY TESTS
// =============================================================================

void test_StateMachine_isActive_when_running(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);
    TEST_ASSERT_TRUE(sm.isActive());
}

void test_StateMachine_isActive_when_paused(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::PAUSED);
    TEST_ASSERT_TRUE(sm.isActive());
}

void test_StateMachine_isActive_when_lowBattery(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::LOW_BATTERY);
    TEST_ASSERT_TRUE(sm.isActive());
}

void test_StateMachine_isActive_when_idle(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);
    TEST_ASSERT_FALSE(sm.isActive());
}

void test_StateMachine_isError_when_error(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::ERROR);
    TEST_ASSERT_TRUE(sm.isError());
}

void test_StateMachine_isError_when_criticalBattery(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::CRITICAL_BATTERY);
    TEST_ASSERT_TRUE(sm.isError());
}

void test_StateMachine_isError_when_connectionLost(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::CONNECTION_LOST);
    TEST_ASSERT_TRUE(sm.isError());
}

void test_StateMachine_isError_when_running(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);
    TEST_ASSERT_FALSE(sm.isError());
}

void test_StateMachine_isRunning(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);
    TEST_ASSERT_TRUE(sm.isRunning());

    sm.begin(TherapyState::PAUSED);
    TEST_ASSERT_FALSE(sm.isRunning());
}

void test_StateMachine_isPaused(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::PAUSED);
    TEST_ASSERT_TRUE(sm.isPaused());

    sm.begin(TherapyState::RUNNING);
    TEST_ASSERT_FALSE(sm.isPaused());
}

void test_StateMachine_isReady(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::READY);
    TEST_ASSERT_TRUE(sm.isReady());

    sm.begin(TherapyState::IDLE);
    TEST_ASSERT_FALSE(sm.isReady());
}

void test_StateMachine_isIdle(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);
    TEST_ASSERT_TRUE(sm.isIdle());

    sm.begin(TherapyState::RUNNING);
    TEST_ASSERT_FALSE(sm.isIdle());
}

// =============================================================================
// PHONE RECONNECT/TIMEOUT TESTS
// =============================================================================

void test_StateMachine_phoneDisconnected_to_previous_on_phoneReconnected(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::RUNNING);

    // First, go to PHONE_DISCONNECTED
    TEST_ASSERT_TRUE(sm.transition(StateTrigger::PHONE_LOST));
    TEST_ASSERT_EQUAL(TherapyState::PHONE_DISCONNECTED, sm.getCurrentState());
    TEST_ASSERT_EQUAL(TherapyState::RUNNING, sm.getPreviousState());

    // Then reconnect - should return to previous state (RUNNING)
    TEST_ASSERT_TRUE(sm.transition(StateTrigger::PHONE_RECONNECTED));
    TEST_ASSERT_EQUAL(TherapyState::RUNNING, sm.getCurrentState());
}

void test_StateMachine_phoneDisconnected_to_previous_on_phoneTimeout(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::READY);

    // First, go to PHONE_DISCONNECTED
    TEST_ASSERT_TRUE(sm.transition(StateTrigger::PHONE_LOST));
    TEST_ASSERT_EQUAL(TherapyState::PHONE_DISCONNECTED, sm.getCurrentState());
    TEST_ASSERT_EQUAL(TherapyState::READY, sm.getPreviousState());

    // Then timeout - should return to previous state (READY)
    TEST_ASSERT_TRUE(sm.transition(StateTrigger::PHONE_TIMEOUT));
    TEST_ASSERT_EQUAL(TherapyState::READY, sm.getCurrentState());
}

// =============================================================================
// MAX CALLBACKS TESTS
// =============================================================================

// Helper callbacks for max callbacks test
void helperCallback1(const StateTransition&) {}
void helperCallback2(const StateTransition&) {}
void helperCallback3(const StateTransition&) {}
void helperCallback4(const StateTransition&) {}

void test_StateMachine_onStateChange_max_callbacks_returns_false(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::IDLE);

    // Register max callbacks (MAX_STATE_CALLBACKS = 4)
    TEST_ASSERT_TRUE(sm.onStateChange(helperCallback1));
    TEST_ASSERT_TRUE(sm.onStateChange(helperCallback2));
    TEST_ASSERT_TRUE(sm.onStateChange(helperCallback3));
    TEST_ASSERT_TRUE(sm.onStateChange(helperCallback4));

    // Next one should fail
    TEST_ASSERT_FALSE(sm.onStateChange(testCallback));
}

// =============================================================================
// ADDITIONAL TRANSITION EDGE CASES
// =============================================================================

void test_StateMachine_paused_disconnected(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::PAUSED);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::DISCONNECTED));
    TEST_ASSERT_EQUAL(TherapyState::CONNECTION_LOST, sm.getCurrentState());
}

void test_StateMachine_connectionLost_connected(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::CONNECTION_LOST);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::CONNECTED));
    TEST_ASSERT_EQUAL(TherapyState::READY, sm.getCurrentState());
}

void test_StateMachine_lowBattery_stopSession(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::LOW_BATTERY);

    // LOW_BATTERY is an active state, can stop
    TEST_ASSERT_FALSE(sm.transition(StateTrigger::STOP_SESSION));  // Not running/paused
}

void test_StateMachine_stopping_emergencyStop(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::STOPPING);

    // STOPPING is not active, should not transition on emergency stop
    TEST_ASSERT_FALSE(sm.transition(StateTrigger::EMERGENCY_STOP));
    TEST_ASSERT_EQUAL(TherapyState::STOPPING, sm.getCurrentState());
}

void test_StateMachine_criticalBattery_from_paused(void) {
    TherapyStateMachine sm;
    sm.begin(TherapyState::PAUSED);

    TEST_ASSERT_TRUE(sm.transition(StateTrigger::BATTERY_CRITICAL));
    TEST_ASSERT_EQUAL(TherapyState::CRITICAL_BATTERY, sm.getCurrentState());
}

// =============================================================================
// TEST RUNNER
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Constructor and Initialization Tests
    RUN_TEST(test_StateMachine_default_state);
    RUN_TEST(test_StateMachine_begin_default);
    RUN_TEST(test_StateMachine_begin_custom_state);
    RUN_TEST(test_StateMachine_getPreviousState_initial);

    // Connection Transition Tests
    RUN_TEST(test_StateMachine_idle_to_ready_on_connected);
    RUN_TEST(test_StateMachine_connecting_to_ready_on_connected);
    RUN_TEST(test_StateMachine_ready_to_connectionLost_on_disconnected);
    RUN_TEST(test_StateMachine_running_to_connectionLost_on_disconnected);
    RUN_TEST(test_StateMachine_connectionLost_to_ready_on_reconnected);
    RUN_TEST(test_StateMachine_connectionLost_to_idle_on_reconnectFailed);

    // Session Transition Tests
    RUN_TEST(test_StateMachine_ready_to_running_on_startSession);
    RUN_TEST(test_StateMachine_idle_to_running_on_startSession);
    RUN_TEST(test_StateMachine_running_to_paused_on_pauseSession);
    RUN_TEST(test_StateMachine_paused_to_running_on_resumeSession);
    RUN_TEST(test_StateMachine_running_to_stopping_on_stopSession);
    RUN_TEST(test_StateMachine_paused_to_stopping_on_stopSession);
    RUN_TEST(test_StateMachine_stopping_to_idle_on_sessionComplete);
    RUN_TEST(test_StateMachine_stopping_to_idle_on_stopped);
    RUN_TEST(test_StateMachine_running_to_idle_on_sessionComplete);

    // Battery Transition Tests
    RUN_TEST(test_StateMachine_running_to_lowBattery_on_batteryWarning);
    RUN_TEST(test_StateMachine_lowBattery_to_running_on_batteryOk);
    RUN_TEST(test_StateMachine_any_to_criticalBattery_on_batteryCritical);

    // Phone Transition Tests
    RUN_TEST(test_StateMachine_ready_to_phoneDisconnected_on_phoneLost);
    RUN_TEST(test_StateMachine_running_to_phoneDisconnected_on_phoneLost);

    // Error Transition Tests
    RUN_TEST(test_StateMachine_any_to_error_on_errorOccurred);
    RUN_TEST(test_StateMachine_activeState_to_error_on_emergencyStop);
    RUN_TEST(test_StateMachine_idle_no_change_on_emergencyStop);

    // Reset Transition Tests
    RUN_TEST(test_StateMachine_any_to_idle_on_reset);
    RUN_TEST(test_StateMachine_any_to_idle_on_forcedShutdown);

    // Invalid Transition Tests
    RUN_TEST(test_StateMachine_invalid_transition_returns_false);
    RUN_TEST(test_StateMachine_resume_from_nonPaused_stays_same);
    RUN_TEST(test_StateMachine_batteryOk_from_running_stays_same);

    // Force State Tests
    RUN_TEST(test_StateMachine_forceState);
    RUN_TEST(test_StateMachine_forceState_notifies_callbacks);

    // Reset Method Tests
    RUN_TEST(test_StateMachine_reset);
    RUN_TEST(test_StateMachine_reset_notifies_callbacks);

    // Callback Tests
    RUN_TEST(test_StateMachine_callback_on_transition);
    RUN_TEST(test_StateMachine_no_callback_on_invalid_transition);
    RUN_TEST(test_StateMachine_multiple_callbacks);
    RUN_TEST(test_StateMachine_clearCallbacks);
    RUN_TEST(test_StateMachine_duplicate_callback_not_registered);

    // State Query Tests
    RUN_TEST(test_StateMachine_isActive_when_running);
    RUN_TEST(test_StateMachine_isActive_when_paused);
    RUN_TEST(test_StateMachine_isActive_when_lowBattery);
    RUN_TEST(test_StateMachine_isActive_when_idle);
    RUN_TEST(test_StateMachine_isError_when_error);
    RUN_TEST(test_StateMachine_isError_when_criticalBattery);
    RUN_TEST(test_StateMachine_isError_when_connectionLost);
    RUN_TEST(test_StateMachine_isError_when_running);
    RUN_TEST(test_StateMachine_isRunning);
    RUN_TEST(test_StateMachine_isPaused);
    RUN_TEST(test_StateMachine_isReady);
    RUN_TEST(test_StateMachine_isIdle);

    // Phone Reconnect/Timeout Tests
    RUN_TEST(test_StateMachine_phoneDisconnected_to_previous_on_phoneReconnected);
    RUN_TEST(test_StateMachine_phoneDisconnected_to_previous_on_phoneTimeout);

    // Max Callbacks Tests
    RUN_TEST(test_StateMachine_onStateChange_max_callbacks_returns_false);

    // Additional Transition Edge Cases
    RUN_TEST(test_StateMachine_paused_disconnected);
    RUN_TEST(test_StateMachine_connectionLost_connected);
    RUN_TEST(test_StateMachine_lowBattery_stopSession);
    RUN_TEST(test_StateMachine_stopping_emergencyStop);
    RUN_TEST(test_StateMachine_criticalBattery_from_paused);

    return UNITY_END();
}
