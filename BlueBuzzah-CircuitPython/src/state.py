"""
Therapy State Machine
=====================
Simplified state machine for therapy session management in CircuitPython.

This module provides a minimal state machine implementation optimized for
memory-constrained environments. It tracks therapy states and triggers
callbacks on state transitions.

Module: state
Version: 2.0.0
"""

from core.types import TherapyState


class StateTrigger:
    """
    State machine transition triggers.

    These represent events that cause state transitions.
    """
    # Connection triggers
    CONNECTED = "CONNECTED"
    DISCONNECTED = "DISCONNECTED"

    # Session triggers
    START_SESSION = "START_SESSION"
    PAUSE_SESSION = "PAUSE_SESSION"
    RESUME_SESSION = "RESUME_SESSION"
    STOP_SESSION = "STOP_SESSION"
    STOPPED = "STOPPED"

    # Error triggers
    ERROR = "ERROR"
    EMERGENCY_STOP = "EMERGENCY_STOP"


class StateTransition:
    """
    Represents a state transition event.

    Attributes:
        from_state: Previous state
        to_state: New state
        trigger: Event that caused the transition
        metadata: Optional transition metadata
    """

    def __init__(self, from_state, to_state, trigger, metadata=None):
        self.from_state = from_state
        self.to_state = to_state
        self.trigger = trigger
        self.metadata = metadata or {}


class TherapyStateMachine:
    """
    Simplified therapy session state machine.

    This class manages therapy state transitions and notifies callbacks
    when states change. It's optimized for memory-constrained CircuitPython.

    States (from core.types.TherapyState):
        - IDLE: No active session
        - READY: System ready to start
        - RUNNING: Therapy in progress
        - PAUSED: Therapy paused
        - STOPPING: Graceful shutdown
        - ERROR: Error state
        - LOW_BATTERY: Battery warning
        - CRITICAL_BATTERY: Battery critical
        - CONNECTION_LOST: Lost connection

    Usage:
        >>> machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        >>>
        >>> def on_change(transition):
        ...     print(f"State: {transition.from_state} → {transition.to_state}")
        >>>
        >>> machine.on_state_change(on_change)
        >>> machine.transition(StateTrigger.CONNECTED)
    """

    def __init__(self, initial_state=TherapyState.IDLE):
        """
        Initialize state machine.

        Args:
            initial_state: Starting state (default: TherapyState.IDLE)
        """
        self._state = initial_state
        self._callbacks = []

    def get_current_state(self):
        """
        Get current therapy state.

        Returns:
            Current TherapyState
        """
        return self._state

    def on_state_change(self, callback):
        """
        Register callback for state change events.

        The callback will be called with a StateTransition object whenever
        the state changes.

        Args:
            callback: Function(transition: StateTransition) to call on changes
        """
        if callback not in self._callbacks:
            self._callbacks.append(callback)

    def transition(self, trigger, metadata=None):
        """
        Trigger a state transition.

        This method applies the trigger and transitions to the appropriate
        new state based on the current state and trigger type.

        Args:
            trigger: StateTrigger that causes the transition
            metadata: Optional metadata dict for the transition

        Returns:
            bool: True if transition succeeded, False otherwise
        """
        old_state = self._state
        new_state = self._determine_next_state(trigger)

        if new_state == old_state:
            # No state change
            return True

        # Update state
        self._state = new_state

        # Notify callbacks
        transition = StateTransition(
            from_state=old_state,
            to_state=new_state,
            trigger=trigger,
            metadata=metadata
        )

        for callback in self._callbacks:
            try:
                callback(transition)
            except Exception as e:
                print("[ERROR] State callback failed: {}".format(e))

        return True

    def force_state(self, state, reason=None):
        """
        Force the state machine to a specific state.

        This bypasses normal transition logic and is used for error
        conditions and emergency situations.

        Args:
            state: TherapyState to force
            reason: Optional reason for forcing the state
        """
        old_state = self._state
        self._state = state

        # Notify callbacks
        transition = StateTransition(
            from_state=old_state,
            to_state=state,
            trigger="FORCE",
            metadata={"reason": reason} if reason else {}
        )

        for callback in self._callbacks:
            try:
                callback(transition)
            except Exception as e:
                print("[ERROR] State callback failed: {}".format(e))

    def _determine_next_state(self, trigger):
        """
        Determine the next state based on current state and trigger.

        Args:
            trigger: StateTrigger causing the transition

        Returns:
            New TherapyState
        """
        current = self._state

        # Connection triggers
        if trigger == StateTrigger.CONNECTED:
            if current == TherapyState.IDLE:
                return TherapyState.READY

        elif trigger == StateTrigger.DISCONNECTED:
            return TherapyState.CONNECTION_LOST

        # Session triggers
        elif trigger == StateTrigger.START_SESSION:
            if current in (TherapyState.READY, TherapyState.IDLE):
                return TherapyState.RUNNING

        elif trigger == StateTrigger.PAUSE_SESSION:
            if current == TherapyState.RUNNING:
                return TherapyState.PAUSED

        elif trigger == StateTrigger.RESUME_SESSION:
            if current == TherapyState.PAUSED:
                return TherapyState.RUNNING

        elif trigger == StateTrigger.STOP_SESSION:
            if current in (TherapyState.RUNNING, TherapyState.PAUSED):
                return TherapyState.STOPPING

        elif trigger == StateTrigger.STOPPED:
            if current == TherapyState.STOPPING:
                return TherapyState.IDLE

        # Error triggers
        elif trigger == StateTrigger.ERROR:
            return TherapyState.ERROR

        elif trigger == StateTrigger.EMERGENCY_STOP:
            return TherapyState.ERROR

        # No valid transition - stay in current state
        return current
