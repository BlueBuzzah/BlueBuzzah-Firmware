"""
State Machine Tests
===================

Comprehensive tests for the TherapyStateMachine class.

Tests all state transitions, callbacks, force state functionality,
and complete therapy session lifecycle scenarios.

Test Categories:
    - Initial state tests
    - Connection transition tests
    - Session lifecycle transition tests
    - Invalid transition tests (should stay in current state)
    - Error trigger tests
    - Callback tests
    - Force state tests
    - Full lifecycle integration tests

Run with: python -m pytest tests/test_state_machine.py -v

Module: tests.test_state_machine
Version: 2.0.0
"""

import pytest
import sys
from pathlib import Path
from unittest.mock import MagicMock

# Add src directory to Python path
src_path = Path(__file__).parent.parent / "src"
sys.path.insert(0, str(src_path))

# Mock micropython module before importing code that requires it
micropython_mock = MagicMock()
micropython_mock.const = lambda x: x
sys.modules['micropython'] = micropython_mock

from state import TherapyStateMachine, StateTrigger, StateTransition
from core.types import TherapyState


# ============================================================================
# Initial State Tests
# ============================================================================

class TestInitialState:
    """Test initial state configuration."""

    def test_initial_state_is_idle(self):
        """Test that default initial state is IDLE."""
        machine = TherapyStateMachine()
        assert machine.get_current_state() == TherapyState.IDLE

    def test_initial_state_can_be_overridden(self):
        """Test that initial state can be specified at creation."""
        machine = TherapyStateMachine(initial_state=TherapyState.READY)
        assert machine.get_current_state() == TherapyState.READY

    def test_initial_state_can_be_error(self):
        """Test that initial state can be set to ERROR."""
        machine = TherapyStateMachine(initial_state=TherapyState.ERROR)
        assert machine.get_current_state() == TherapyState.ERROR

    def test_initial_state_can_be_running(self):
        """Test that initial state can be set to RUNNING."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        assert machine.get_current_state() == TherapyState.RUNNING


# ============================================================================
# Connection Transition Tests
# ============================================================================

class TestConnectionTransitions:
    """Test connection-related state transitions."""

    def test_connected_from_idle_transitions_to_ready(self):
        """Test CONNECTED trigger from IDLE transitions to READY."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        result = machine.transition(StateTrigger.CONNECTED)

        assert result is True
        assert machine.get_current_state() == TherapyState.READY

    def test_connected_from_ready_stays_ready(self):
        """Test CONNECTED trigger from READY stays in READY."""
        machine = TherapyStateMachine(initial_state=TherapyState.READY)
        result = machine.transition(StateTrigger.CONNECTED)

        assert result is True
        assert machine.get_current_state() == TherapyState.READY

    def test_disconnected_from_idle_goes_to_connection_lost(self):
        """Test DISCONNECTED from IDLE goes to CONNECTION_LOST."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        machine.transition(StateTrigger.DISCONNECTED)

        assert machine.get_current_state() == TherapyState.CONNECTION_LOST

    def test_disconnected_from_ready_goes_to_connection_lost(self):
        """Test DISCONNECTED from READY goes to CONNECTION_LOST."""
        machine = TherapyStateMachine(initial_state=TherapyState.READY)
        machine.transition(StateTrigger.DISCONNECTED)

        assert machine.get_current_state() == TherapyState.CONNECTION_LOST

    def test_disconnected_from_running_goes_to_connection_lost(self):
        """Test DISCONNECTED from RUNNING goes to CONNECTION_LOST."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        machine.transition(StateTrigger.DISCONNECTED)

        assert machine.get_current_state() == TherapyState.CONNECTION_LOST

    def test_disconnected_from_paused_goes_to_connection_lost(self):
        """Test DISCONNECTED from PAUSED goes to CONNECTION_LOST."""
        machine = TherapyStateMachine(initial_state=TherapyState.PAUSED)
        machine.transition(StateTrigger.DISCONNECTED)

        assert machine.get_current_state() == TherapyState.CONNECTION_LOST

    def test_disconnected_from_stopping_goes_to_connection_lost(self):
        """Test DISCONNECTED from STOPPING goes to CONNECTION_LOST."""
        machine = TherapyStateMachine(initial_state=TherapyState.STOPPING)
        machine.transition(StateTrigger.DISCONNECTED)

        assert machine.get_current_state() == TherapyState.CONNECTION_LOST


# ============================================================================
# Session Lifecycle Transition Tests
# ============================================================================

class TestSessionLifecycleTransitions:
    """Test session lifecycle state transitions."""

    def test_start_session_from_ready_transitions_to_running(self):
        """Test START_SESSION from READY transitions to RUNNING."""
        machine = TherapyStateMachine(initial_state=TherapyState.READY)
        result = machine.transition(StateTrigger.START_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.RUNNING

    def test_start_session_from_idle_transitions_to_running(self):
        """Test START_SESSION from IDLE transitions to RUNNING."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        result = machine.transition(StateTrigger.START_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.RUNNING

    def test_pause_session_from_running_transitions_to_paused(self):
        """Test PAUSE_SESSION from RUNNING transitions to PAUSED."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        result = machine.transition(StateTrigger.PAUSE_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.PAUSED

    def test_resume_session_from_paused_transitions_to_running(self):
        """Test RESUME_SESSION from PAUSED transitions to RUNNING."""
        machine = TherapyStateMachine(initial_state=TherapyState.PAUSED)
        result = machine.transition(StateTrigger.RESUME_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.RUNNING

    def test_stop_session_from_running_transitions_to_stopping(self):
        """Test STOP_SESSION from RUNNING transitions to STOPPING."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        result = machine.transition(StateTrigger.STOP_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.STOPPING

    def test_stop_session_from_paused_transitions_to_stopping(self):
        """Test STOP_SESSION from PAUSED transitions to STOPPING."""
        machine = TherapyStateMachine(initial_state=TherapyState.PAUSED)
        result = machine.transition(StateTrigger.STOP_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.STOPPING

    def test_stopped_from_stopping_transitions_to_idle(self):
        """Test STOPPED from STOPPING transitions to IDLE."""
        machine = TherapyStateMachine(initial_state=TherapyState.STOPPING)
        result = machine.transition(StateTrigger.STOPPED)

        assert result is True
        assert machine.get_current_state() == TherapyState.IDLE


# ============================================================================
# Invalid Transition Tests (Should Stay in Current State)
# ============================================================================

class TestInvalidTransitions:
    """Test invalid transitions that should stay in current state."""

    def test_pause_from_idle_stays_idle(self):
        """Test PAUSE_SESSION from IDLE stays in IDLE."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        result = machine.transition(StateTrigger.PAUSE_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.IDLE

    def test_resume_from_running_stays_running(self):
        """Test RESUME_SESSION from RUNNING stays in RUNNING."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        result = machine.transition(StateTrigger.RESUME_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.RUNNING

    def test_start_session_from_running_stays_running(self):
        """Test START_SESSION from RUNNING stays in RUNNING."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        result = machine.transition(StateTrigger.START_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.RUNNING

    def test_pause_from_ready_stays_ready(self):
        """Test PAUSE_SESSION from READY stays in READY."""
        machine = TherapyStateMachine(initial_state=TherapyState.READY)
        result = machine.transition(StateTrigger.PAUSE_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.READY

    def test_resume_from_idle_stays_idle(self):
        """Test RESUME_SESSION from IDLE stays in IDLE."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        result = machine.transition(StateTrigger.RESUME_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.IDLE

    def test_stop_from_idle_stays_idle(self):
        """Test STOP_SESSION from IDLE stays in IDLE."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        result = machine.transition(StateTrigger.STOP_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.IDLE

    def test_stopped_from_running_stays_running(self):
        """Test STOPPED from RUNNING stays in RUNNING."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        result = machine.transition(StateTrigger.STOPPED)

        assert result is True
        assert machine.get_current_state() == TherapyState.RUNNING

    def test_connected_from_running_stays_running(self):
        """Test CONNECTED from RUNNING stays in RUNNING."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        result = machine.transition(StateTrigger.CONNECTED)

        assert result is True
        assert machine.get_current_state() == TherapyState.RUNNING


# ============================================================================
# Error Trigger Tests
# ============================================================================

class TestErrorTriggers:
    """Test error-related state transitions."""

    def test_error_from_idle_goes_to_error(self):
        """Test ERROR from IDLE goes to ERROR state."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        machine.transition(StateTrigger.ERROR)

        assert machine.get_current_state() == TherapyState.ERROR

    def test_error_from_ready_goes_to_error(self):
        """Test ERROR from READY goes to ERROR state."""
        machine = TherapyStateMachine(initial_state=TherapyState.READY)
        machine.transition(StateTrigger.ERROR)

        assert machine.get_current_state() == TherapyState.ERROR

    def test_error_from_running_goes_to_error(self):
        """Test ERROR from RUNNING goes to ERROR state."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        machine.transition(StateTrigger.ERROR)

        assert machine.get_current_state() == TherapyState.ERROR

    def test_error_from_paused_goes_to_error(self):
        """Test ERROR from PAUSED goes to ERROR state."""
        machine = TherapyStateMachine(initial_state=TherapyState.PAUSED)
        machine.transition(StateTrigger.ERROR)

        assert machine.get_current_state() == TherapyState.ERROR

    def test_error_from_stopping_goes_to_error(self):
        """Test ERROR from STOPPING goes to ERROR state."""
        machine = TherapyStateMachine(initial_state=TherapyState.STOPPING)
        machine.transition(StateTrigger.ERROR)

        assert machine.get_current_state() == TherapyState.ERROR

    def test_emergency_stop_from_idle_goes_to_error(self):
        """Test EMERGENCY_STOP from IDLE goes to ERROR state."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        machine.transition(StateTrigger.EMERGENCY_STOP)

        assert machine.get_current_state() == TherapyState.ERROR

    def test_emergency_stop_from_running_goes_to_error(self):
        """Test EMERGENCY_STOP from RUNNING goes to ERROR state."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        machine.transition(StateTrigger.EMERGENCY_STOP)

        assert machine.get_current_state() == TherapyState.ERROR

    def test_emergency_stop_from_paused_goes_to_error(self):
        """Test EMERGENCY_STOP from PAUSED goes to ERROR state."""
        machine = TherapyStateMachine(initial_state=TherapyState.PAUSED)
        machine.transition(StateTrigger.EMERGENCY_STOP)

        assert machine.get_current_state() == TherapyState.ERROR

    def test_emergency_stop_from_stopping_goes_to_error(self):
        """Test EMERGENCY_STOP from STOPPING goes to ERROR state."""
        machine = TherapyStateMachine(initial_state=TherapyState.STOPPING)
        machine.transition(StateTrigger.EMERGENCY_STOP)

        assert machine.get_current_state() == TherapyState.ERROR

    def test_emergency_stop_from_ready_goes_to_error(self):
        """Test EMERGENCY_STOP from READY goes to ERROR state."""
        machine = TherapyStateMachine(initial_state=TherapyState.READY)
        machine.transition(StateTrigger.EMERGENCY_STOP)

        assert machine.get_current_state() == TherapyState.ERROR


# ============================================================================
# Callback Tests
# ============================================================================

class TestCallbacks:
    """Test state change callback functionality."""

    def test_callback_invoked_on_transition(self):
        """Test that callback is invoked when state changes."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        callback_invoked = [False]

        def callback(transition):
            callback_invoked[0] = True

        machine.on_state_change(callback)
        machine.transition(StateTrigger.CONNECTED)

        assert callback_invoked[0] is True

    def test_callback_not_invoked_when_state_unchanged(self):
        """Test that callback is not invoked when state does not change."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        callback_invoked = [False]

        def callback(transition):
            callback_invoked[0] = True

        machine.on_state_change(callback)
        machine.transition(StateTrigger.PAUSE_SESSION)  # Invalid from IDLE

        assert callback_invoked[0] is False

    def test_multiple_callbacks_all_invoked(self):
        """Test that multiple callbacks are all invoked on transition."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        callbacks_invoked = [0]

        def callback1(transition):
            callbacks_invoked[0] += 1

        def callback2(transition):
            callbacks_invoked[0] += 1

        def callback3(transition):
            callbacks_invoked[0] += 1

        machine.on_state_change(callback1)
        machine.on_state_change(callback2)
        machine.on_state_change(callback3)
        machine.transition(StateTrigger.CONNECTED)

        assert callbacks_invoked[0] == 3

    def test_callback_receives_correct_transition_info(self):
        """Test that callback receives correct StateTransition info."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        received_transition = [None]

        def callback(transition):
            received_transition[0] = transition

        machine.on_state_change(callback)
        machine.transition(StateTrigger.CONNECTED, metadata={"test": "value"})

        transition = received_transition[0]
        assert transition is not None
        assert isinstance(transition, StateTransition)
        assert transition.from_state == TherapyState.IDLE
        assert transition.to_state == TherapyState.READY
        assert transition.trigger == StateTrigger.CONNECTED
        assert transition.metadata == {"test": "value"}

    def test_callback_exception_does_not_break_state_machine(self):
        """Test that exception in callback does not prevent state change."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        second_callback_invoked = [False]

        def bad_callback(transition):
            raise RuntimeError("Callback error")

        def good_callback(transition):
            second_callback_invoked[0] = True

        machine.on_state_change(bad_callback)
        machine.on_state_change(good_callback)

        # Transition should still succeed despite callback error
        machine.transition(StateTrigger.CONNECTED)

        assert machine.get_current_state() == TherapyState.READY
        assert second_callback_invoked[0] is True

    def test_same_callback_not_registered_twice(self):
        """Test that the same callback cannot be registered twice."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        invocation_count = [0]

        def callback(transition):
            invocation_count[0] += 1

        machine.on_state_change(callback)
        machine.on_state_change(callback)  # Should not add again
        machine.transition(StateTrigger.CONNECTED)

        assert invocation_count[0] == 1

    def test_callback_receives_empty_metadata_by_default(self):
        """Test that callback receives empty metadata dict when none provided."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        received_transition = [None]

        def callback(transition):
            received_transition[0] = transition

        machine.on_state_change(callback)
        machine.transition(StateTrigger.CONNECTED)

        assert received_transition[0].metadata == {}


# ============================================================================
# Force State Tests
# ============================================================================

class TestForceState:
    """Test force_state functionality."""

    def test_force_state_changes_regardless_of_current(self):
        """Test force_state changes state regardless of current state."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        machine.force_state(TherapyState.ERROR)

        assert machine.get_current_state() == TherapyState.ERROR

    def test_force_state_from_idle_to_running(self):
        """Test force_state from IDLE to RUNNING."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        machine.force_state(TherapyState.RUNNING)

        assert machine.get_current_state() == TherapyState.RUNNING

    def test_force_state_includes_reason_in_metadata(self):
        """Test that force_state includes reason in transition metadata."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        received_transition = [None]

        def callback(transition):
            received_transition[0] = transition

        machine.on_state_change(callback)
        machine.force_state(TherapyState.ERROR, reason="Battery critical")

        transition = received_transition[0]
        assert transition is not None
        assert transition.metadata.get("reason") == "Battery critical"

    def test_force_state_without_reason_has_empty_metadata(self):
        """Test that force_state without reason has empty metadata."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        received_transition = [None]

        def callback(transition):
            received_transition[0] = transition

        machine.on_state_change(callback)
        machine.force_state(TherapyState.ERROR)

        transition = received_transition[0]
        assert transition.metadata == {}

    def test_force_state_uses_force_trigger(self):
        """Test that force_state uses FORCE as trigger."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        received_transition = [None]

        def callback(transition):
            received_transition[0] = transition

        machine.on_state_change(callback)
        machine.force_state(TherapyState.IDLE)

        assert received_transition[0].trigger == "FORCE"

    def test_force_state_invokes_callbacks(self):
        """Test that force_state invokes registered callbacks."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        callback_invoked = [False]

        def callback(transition):
            callback_invoked[0] = True

        machine.on_state_change(callback)
        machine.force_state(TherapyState.ERROR)

        assert callback_invoked[0] is True

    def test_force_state_to_same_state_still_invokes_callback(self):
        """Test force_state to same state still invokes callbacks."""
        machine = TherapyStateMachine(initial_state=TherapyState.ERROR)
        callback_invoked = [False]

        def callback(transition):
            callback_invoked[0] = True

        machine.on_state_change(callback)
        machine.force_state(TherapyState.ERROR, reason="Different error")

        assert callback_invoked[0] is True
        assert machine.get_current_state() == TherapyState.ERROR


# ============================================================================
# Full Lifecycle Tests
# ============================================================================

class TestFullLifecycle:
    """Test complete therapy session lifecycle scenarios."""

    def test_complete_therapy_session_lifecycle(self):
        """Test a complete therapy session from start to finish."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        state_history = []

        def track_state(transition):
            state_history.append(transition.to_state)

        machine.on_state_change(track_state)

        # Connect
        assert machine.get_current_state() == TherapyState.IDLE
        machine.transition(StateTrigger.CONNECTED)
        assert machine.get_current_state() == TherapyState.READY

        # Start session
        machine.transition(StateTrigger.START_SESSION)
        assert machine.get_current_state() == TherapyState.RUNNING

        # Pause
        machine.transition(StateTrigger.PAUSE_SESSION)
        assert machine.get_current_state() == TherapyState.PAUSED

        # Resume
        machine.transition(StateTrigger.RESUME_SESSION)
        assert machine.get_current_state() == TherapyState.RUNNING

        # Stop
        machine.transition(StateTrigger.STOP_SESSION)
        assert machine.get_current_state() == TherapyState.STOPPING

        # Complete
        machine.transition(StateTrigger.STOPPED)
        assert machine.get_current_state() == TherapyState.IDLE

        # Verify state history
        expected_history = [
            TherapyState.READY,
            TherapyState.RUNNING,
            TherapyState.PAUSED,
            TherapyState.RUNNING,
            TherapyState.STOPPING,
            TherapyState.IDLE,
        ]
        assert state_history == expected_history

    def test_session_interrupted_by_disconnect(self):
        """Test session interrupted by connection loss."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)
        state_history = []

        def track_state(transition):
            state_history.append(transition.to_state)

        machine.on_state_change(track_state)

        # Simulate disconnection during running
        machine.transition(StateTrigger.DISCONNECTED)
        assert machine.get_current_state() == TherapyState.CONNECTION_LOST

        # Verify history
        assert state_history == [TherapyState.CONNECTION_LOST]

    def test_session_interrupted_by_error(self):
        """Test session interrupted by error condition."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)

        machine.transition(StateTrigger.ERROR)
        assert machine.get_current_state() == TherapyState.ERROR

    def test_session_interrupted_by_emergency_stop(self):
        """Test session interrupted by emergency stop."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)

        machine.transition(StateTrigger.EMERGENCY_STOP)
        assert machine.get_current_state() == TherapyState.ERROR

    def test_start_session_directly_from_idle(self):
        """Test starting session directly from IDLE (no CONNECTED first)."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)

        # Start session directly
        machine.transition(StateTrigger.START_SESSION)
        assert machine.get_current_state() == TherapyState.RUNNING

        # Stop session
        machine.transition(StateTrigger.STOP_SESSION)
        assert machine.get_current_state() == TherapyState.STOPPING

        machine.transition(StateTrigger.STOPPED)
        assert machine.get_current_state() == TherapyState.IDLE

    def test_multiple_pause_resume_cycles(self):
        """Test multiple pause/resume cycles in a session."""
        machine = TherapyStateMachine(initial_state=TherapyState.RUNNING)

        for _ in range(3):
            machine.transition(StateTrigger.PAUSE_SESSION)
            assert machine.get_current_state() == TherapyState.PAUSED

            machine.transition(StateTrigger.RESUME_SESSION)
            assert machine.get_current_state() == TherapyState.RUNNING

    def test_force_recovery_from_error(self):
        """Test using force_state to recover from ERROR."""
        machine = TherapyStateMachine(initial_state=TherapyState.ERROR)

        # Cannot use normal transitions from ERROR
        machine.transition(StateTrigger.CONNECTED)
        assert machine.get_current_state() == TherapyState.ERROR

        # Use force_state to recover
        machine.force_state(TherapyState.IDLE, reason="Error cleared")
        assert machine.get_current_state() == TherapyState.IDLE


# ============================================================================
# StateTransition Object Tests
# ============================================================================

class TestStateTransition:
    """Test StateTransition class functionality."""

    def test_state_transition_initialization(self):
        """Test StateTransition object initialization."""
        transition = StateTransition(
            from_state=TherapyState.IDLE,
            to_state=TherapyState.READY,
            trigger=StateTrigger.CONNECTED,
            metadata={"key": "value"}
        )

        assert transition.from_state == TherapyState.IDLE
        assert transition.to_state == TherapyState.READY
        assert transition.trigger == StateTrigger.CONNECTED
        assert transition.metadata == {"key": "value"}

    def test_state_transition_default_metadata(self):
        """Test StateTransition default metadata is empty dict."""
        transition = StateTransition(
            from_state=TherapyState.IDLE,
            to_state=TherapyState.READY,
            trigger=StateTrigger.CONNECTED
        )

        assert transition.metadata == {}

    def test_state_transition_none_metadata_becomes_empty_dict(self):
        """Test StateTransition with None metadata becomes empty dict."""
        transition = StateTransition(
            from_state=TherapyState.IDLE,
            to_state=TherapyState.READY,
            trigger=StateTrigger.CONNECTED,
            metadata=None
        )

        assert transition.metadata == {}


# ============================================================================
# StateTrigger Constants Tests
# ============================================================================

class TestStateTrigger:
    """Test StateTrigger constants."""

    def test_connection_triggers_exist(self):
        """Test connection trigger constants exist."""
        assert StateTrigger.CONNECTED == "CONNECTED"
        assert StateTrigger.DISCONNECTED == "DISCONNECTED"

    def test_session_triggers_exist(self):
        """Test session trigger constants exist."""
        assert StateTrigger.START_SESSION == "START_SESSION"
        assert StateTrigger.PAUSE_SESSION == "PAUSE_SESSION"
        assert StateTrigger.RESUME_SESSION == "RESUME_SESSION"
        assert StateTrigger.STOP_SESSION == "STOP_SESSION"
        assert StateTrigger.STOPPED == "STOPPED"

    def test_error_triggers_exist(self):
        """Test error trigger constants exist."""
        assert StateTrigger.ERROR == "ERROR"
        assert StateTrigger.EMERGENCY_STOP == "EMERGENCY_STOP"


# ============================================================================
# Edge Cases and Boundary Tests
# ============================================================================

class TestEdgeCases:
    """Test edge cases and boundary conditions."""

    def test_transition_returns_true_on_success(self):
        """Test that transition returns True on successful transition."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        result = machine.transition(StateTrigger.CONNECTED)

        assert result is True

    def test_transition_returns_true_even_when_state_unchanged(self):
        """Test that transition returns True even when state unchanged."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        result = machine.transition(StateTrigger.PAUSE_SESSION)

        assert result is True
        assert machine.get_current_state() == TherapyState.IDLE

    def test_transition_with_metadata(self):
        """Test transition with metadata is passed to callback."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        received_metadata = [None]

        def callback(transition):
            received_metadata[0] = transition.metadata

        machine.on_state_change(callback)
        machine.transition(
            StateTrigger.CONNECTED,
            metadata={"session_id": "abc123", "device": "primary"}
        )

        assert received_metadata[0] == {"session_id": "abc123", "device": "primary"}

    def test_rapid_state_transitions(self):
        """Test rapid sequential state transitions."""
        machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        transition_count = [0]

        def count_transitions(transition):
            transition_count[0] += 1

        machine.on_state_change(count_transitions)

        # Perform rapid transitions
        machine.transition(StateTrigger.START_SESSION)  # IDLE -> RUNNING
        machine.transition(StateTrigger.PAUSE_SESSION)  # RUNNING -> PAUSED
        machine.transition(StateTrigger.RESUME_SESSION)  # PAUSED -> RUNNING
        machine.transition(StateTrigger.STOP_SESSION)  # RUNNING -> STOPPING
        machine.transition(StateTrigger.STOPPED)  # STOPPING -> IDLE

        assert transition_count[0] == 5
        assert machine.get_current_state() == TherapyState.IDLE


# ============================================================================
# Integration with conftest.py Fixture
# ============================================================================

class TestWithFixture:
    """Test state machine using the conftest.py fixture."""

    def test_fixture_provides_fresh_machine(self, state_machine):
        """Test that fixture provides a fresh state machine."""
        assert state_machine.get_current_state() == TherapyState.IDLE

    def test_fixture_machine_can_transition(self, state_machine):
        """Test that fixture machine can perform transitions."""
        state_machine.transition(StateTrigger.CONNECTED)
        assert state_machine.get_current_state() == TherapyState.READY

    def test_fixture_machine_can_register_callbacks(self, state_machine):
        """Test that fixture machine can register callbacks."""
        callback_called = [False]

        def callback(transition):
            callback_called[0] = True

        state_machine.on_state_change(callback)
        state_machine.transition(StateTrigger.CONNECTED)

        assert callback_called[0] is True


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
