"""
Session Manager
===============

Manages the complete lifecycle of therapy sessions including start, pause,
resume, and stop operations. Coordinates with the state machine, therapy engine,
and event bus to ensure proper session flow.

The SessionManager acts as the central controller for all session-related
operations, ensuring proper state transitions and event notifications.
"""

import gc
import time

# TODO: These imports reference modules that don't exist yet
# from config.therapy import TherapyConfig
from core.types import SessionInfo, TherapyState
# from state.machine import StateTrigger, TherapyStateMachine

# Temporary stubs for missing classes
class StateTrigger:
    """Placeholder for state machine triggers."""
    START_SESSION = "START_SESSION"
    PAUSE_SESSION = "PAUSE_SESSION"
    RESUME_SESSION = "RESUME_SESSION"
    STOP_SESSION = "STOP_SESSION"
    STOPPED = "STOPPED"

class TherapyStateMachine:
    """Placeholder state machine."""
    def __init__(self):
        self._state = TherapyState.IDLE

    def transition(self, trigger, metadata=None):
        """Stub transition method."""
        return True

    def get_current_state(self):
        """Stub get state method."""
        return self._state

class TherapyConfig:
    """Placeholder therapy config."""
    def __init__(self, **kwargs):
        self.config = kwargs

    @classmethod
    def default_noisy_vcr(cls):
        """Return default config."""
        return cls()


class SessionContext:
    """
    Context information for an active session.

    Attributes:
        session_id: Unique session identifier
        profile: Therapy configuration being used
        start_time: When session started (monotonic time)
        pause_time: When session was paused (if paused)
        total_pause_duration: Total time spent paused
        cycles_completed: Number of therapy cycles completed
    """

    def __init__(self, session_id, profile, start_time, pause_time=None, total_pause_duration=0.0, cycles_completed=0):
        self.session_id = session_id
        self.profile = profile
        self.start_time = start_time
        self.pause_time = pause_time
        self.total_pause_duration = total_pause_duration
        self.cycles_completed = cycles_completed


# Consolidated from session/tracker.py
class SessionRecord:
    """
    Record of a completed therapy session.
    Consolidated from tracker.py - now part of SessionManager.
    """

    def __init__(self, session_id, profile_name, start_time, end_time, duration_sec, elapsed_sec,
                 pause_duration_sec, cycles_completed, completion_percentage, stop_reason):
        self.session_id = session_id
        self.profile_name = profile_name
        self.start_time = start_time
        self.end_time = end_time
        self.duration_sec = duration_sec
        self.elapsed_sec = elapsed_sec
        self.pause_duration_sec = pause_duration_sec
        self.cycles_completed = cycles_completed
        self.completion_percentage = completion_percentage
        self.stop_reason = stop_reason


class SessionStatistics:
    """
    Aggregate statistics for therapy sessions.
    Consolidated from tracker.py - now part of SessionManager.
    """

    total_sessions= 0
    completed_sessions= 0
    total_therapy_time_sec= 0.0
    total_cycles= 0
    average_completion_percentage= 0.0
    average_session_duration_sec= 0.0
    current_streak= 0


class SessionManager:
    """
    Manages therapy session lifecycle and coordinates with system components.

    The SessionManager is responsible for:
        - Starting therapy sessions with validated profiles
        - Pausing and resuming active sessions
        - Stopping sessions gracefully
        - Tracking session state and progress
        - Publishing session lifecycle events
        - Coordinating with state machine and therapy engine

    Example:
        >>> state_machine = TherapyStateMachine()
        >>>
        >>> # Define callbacks
        >>> def on_started(session_id, profile_name):
        ...     print(f"Session {session_id} started with {profile_name}")
        >>>
        >>> session_mgr = SessionManager(
        ...     state_machine,
        ...     on_session_started=on_started
        ... )
        >>>
        >>> # Start a session
        >>> profile = TherapyConfig.default_noisy_vcr()
        >>> if session_mgr.start_session(profile):
        ...     print("Session started successfully")
        >>>
        >>> # Pause session
        >>> session_mgr.pause_session()
        >>>
        >>> # Resume session
        >>> session_mgr.resume_session()
        >>>
        >>> # Stop session
        >>> session_mgr.stop_session()
    """

    def __init__(
        self,
        state_machine,
        on_session_started=None,
        on_session_paused=None,
        on_session_resumed=None,
        on_session_stopped=None,
        therapy_engine=None,
        max_history=100,
        send_sync_callback=None,
    ):
        """
        Initialize the session manager.

        Args:
            state_machine: State machine for managing session states
            on_session_started: Callback when session starts (session_id, profile_name)
            on_session_paused: Callback when session pauses (session_id)
            on_session_resumed: Callback when session resumes (session_id)
            on_session_stopped: Callback when session stops (session_id, reason)
            therapy_engine: Optional therapy engine for executing therapy
            max_history: Maximum number of session records to retain (default: 100)
            send_sync_callback: Optional callback for sending sync commands to SECONDARY
        """
        self._state_machine = state_machine
        self._on_session_started = on_session_started
        self._on_session_paused = on_session_paused
        self._on_session_resumed = on_session_resumed
        self._on_session_stopped = on_session_stopped
        self._therapy_engine = therapy_engine
        self._send_sync_callback = send_sync_callback
        self._current_session= None
        self._session_counter = 0

        # Consolidated tracking functionality from tracker.py
        self._max_history = max_history
        self._session_history = []  # CircuitPython compatible - use list instead of deque

    def start_session(self, profile):
        """
        Start a new therapy session with the specified profile.

        This method:
            1. Validates the profile configuration
            2. Checks state machine allows starting
            3. Creates session context
            4. Transitions state machine to RUNNING
            5. Publishes SessionStartedEvent
            6. Initiates therapy engine (if available)

        Args:
            profile: Therapy configuration to use for this session

        Returns:
            True if session started successfully, False otherwise

        Raises:
            ValueError: If profile is invalid
            RuntimeError: If session cannot be started in current state

        Example:
            >>> profile = TherapyConfig.default_noisy_vcr()
            >>> if session_mgr.start_session(profile):
            ...     print("Session started")
        """
        # Validate profile
        if not profile:
            raise ValueError("Profile cannot be None")

        # Check if we can start a session
        current_state = self._state_machine.get_current_state()
        if not current_state.can_start_therapy():
            raise RuntimeError(
                "Cannot start session in state {}. Must be in IDLE or READY state.".format(current_state)
            )

        # Check if there's already an active session
        if self._current_session is not None:
            return False

        # Create session context
        self._session_counter += 1
        session_id = "session_{:04d}".format(self._session_counter)

        self._current_session = SessionContext(
            session_id=session_id,
            profile=profile,
            start_time=time.monotonic(),
        )

        # Transition state machine to RUNNING
        success = self._state_machine.transition(
            StateTrigger.START_SESSION,
            metadata={
                "session_id": session_id,
                "profile_name": "Custom Profile",  # Could be enhanced with profile names
            },
        )

        if not success:
            self._current_session = None
            return False

        # Call session started callback
        if self._on_session_started:
            self._on_session_started(session_id, "Custom Profile")

        # Start therapy engine if available
        if self._therapy_engine:
            try:
                # Start therapy with profile configuration
                # Extract therapy parameters from profile config
                config = profile.config if hasattr(profile, 'config') else profile

                # CRITICAL FIX: Send START_SESSION sync command to SECONDARY BEFORE starting local engine
                if self._send_sync_callback:
                    print("[SessionManager] _send_sync_callback IS SET - preparing to send START_SESSION")
                    # Send session parameters to SECONDARY device
                    sync_data = {
                        'duration_sec': config.get('session_duration_min', 120) * 60,
                        'pattern_type': config.get('pattern_type', 'rndp'),
                        'time_on_ms': int(config.get('time_on_ms', 100.0)),
                        'time_off_ms': int(config.get('time_off_ms', 67.0)),
                        'jitter_percent': int(config.get('jitter_percent', 23.5) * 10),  # Send as int (235 = 23.5%)
                        'num_fingers': config.get('num_fingers', 5),
                        'mirror_pattern': 1 if config.get('mirror_pattern', True) else 0
                    }
                    print("[SessionManager] CALLING _send_sync_callback('START_SESSION', {})".format(sync_data))
                    self._send_sync_callback('START_SESSION', sync_data)
                    print("[SessionManager] _send_sync_callback RETURNED - START_SESSION sent to SECONDARY")
                else:
                    print("[SessionManager] WARNING: _send_sync_callback is NOT SET - cannot send to SECONDARY")

                # Reclaim memory before pattern allocation
                gc.collect()

                # Start session with parameters from profile
                self._therapy_engine.start_session(
                    duration_sec=config.get('session_duration_min', 120) * 60,  # Convert minutes to seconds
                    pattern_type=config.get('pattern_type', 'rndp'),
                    time_on_ms=config.get('time_on_ms', 100.0),
                    time_off_ms=config.get('time_off_ms', 67.0),
                    jitter_percent=config.get('jitter_percent', 23.5),
                    num_fingers=config.get('num_fingers', 5),
                    mirror_pattern=config.get('mirror_pattern', True)
                )
                print("[SessionManager] Therapy engine started with {} pattern".format(config.get('pattern_type', 'rndp')))
            except Exception as e:
                print("[SessionManager] Error starting therapy engine: {}".format(e))

        return True

    def pause_session(self):
        """
        Pause the currently active session.

        This method:
            1. Verifies a session is running
            2. Records pause time
            3. Transitions state machine to PAUSED
            4. Publishes SessionPausedEvent
            5. Pauses therapy engine (if available)

        Returns:
            True if session paused successfully, False otherwise

        Example:
            >>> if session_mgr.pause_session():
            ...     print("Session paused")
        """
        # Check if we can pause
        current_state = self._state_machine.get_current_state()
        if not current_state.can_pause():
            return False

        if self._current_session is None:
            return False

        # Record pause time
        self._current_session.pause_time = time.monotonic()

        # Transition state machine
        success = self._state_machine.transition(
            StateTrigger.PAUSE_SESSION,
            metadata={"session_id": self._current_session.session_id},
        )

        if not success:
            self._current_session.pause_time = None
            return False

        # Call session paused callback
        if self._on_session_paused:
            self._on_session_paused(self._current_session.session_id)

        # CRITICAL FIX: Send PAUSE_SESSION sync command to SECONDARY
        if self._send_sync_callback:
            self._send_sync_callback('PAUSE_SESSION', {})
            print("[SessionManager] Sent PAUSE_SESSION sync command to SECONDARY")

        # Pause therapy engine
        if self._therapy_engine:
            try:
                self._therapy_engine.pause()
                print("[SessionManager] Therapy engine paused")
            except Exception as e:
                print("[SessionManager] Error pausing therapy engine: {}".format(e))

        return True

    def resume_session(self):
        """
        Resume a paused session.

        This method:
            1. Verifies session is paused
            2. Calculates pause duration
            3. Transitions state machine to RUNNING
            4. Publishes SessionResumedEvent
            5. Resumes therapy engine (if available)

        Returns:
            True if session resumed successfully, False otherwise

        Example:
            >>> if session_mgr.resume_session():
            ...     print("Session resumed")
        """
        # Check if we can resume
        current_state = self._state_machine.get_current_state()
        if not current_state.can_resume():
            return False

        if self._current_session is None or self._current_session.pause_time is None:
            return False

        # Calculate pause duration
        pause_duration = time.monotonic() - self._current_session.pause_time
        self._current_session.total_pause_duration += pause_duration
        self._current_session.pause_time = None

        # Transition state machine
        success = self._state_machine.transition(
            StateTrigger.RESUME_SESSION,
            metadata={"session_id": self._current_session.session_id},
        )

        if not success:
            return False

        # Call session resumed callback
        if self._on_session_resumed:
            self._on_session_resumed(self._current_session.session_id)

        # CRITICAL FIX: Send RESUME_SESSION sync command to SECONDARY
        if self._send_sync_callback:
            self._send_sync_callback('RESUME_SESSION', {})
            print("[SessionManager] Sent RESUME_SESSION sync command to SECONDARY")

        # Resume therapy engine
        if self._therapy_engine:
            try:
                self._therapy_engine.resume()
                print("[SessionManager] Therapy engine resumed")
            except Exception as e:
                print("[SessionManager] Error resuming therapy engine: {}".format(e))

        return True

    def stop_session(self, reason= "USER"):
        """
        Stop the currently active session.

        This method:
            1. Verifies a session exists
            2. Calculates completion percentage
            3. Transitions state machine to STOPPING then READY
            4. Publishes SessionStoppedEvent
            5. Stops therapy engine (if available)
            6. Clears session context

        Args:
            reason: Reason for stopping (USER, COMPLETED, ERROR, etc.)

        Returns:
            True if session stopped successfully, False otherwise

        Example:
            >>> if session_mgr.stop_session():
            ...     print("Session stopped")
        """
        if self._current_session is None:
            return False

        # Transition to STOPPING
        success = self._state_machine.transition(
            StateTrigger.STOP_SESSION,
            metadata={"session_id": self._current_session.session_id, "reason": reason},
        )

        if not success:
            return False

        # Store session ID before clearing
        session_id = self._current_session.session_id

        # Consolidated: Record session in history before clearing
        self._record_session(stop_reason=reason)

        # Call session stopped callback
        if self._on_session_stopped:
            self._on_session_stopped(session_id, reason)

        # CRITICAL FIX: Send STOP_SESSION sync command to SECONDARY
        if self._send_sync_callback:
            self._send_sync_callback('STOP_SESSION', {'reason': reason})
            print("[SessionManager] Sent STOP_SESSION sync command to SECONDARY")

        # Stop therapy engine
        if self._therapy_engine:
            try:
                self._therapy_engine.stop()
                print("[SessionManager] Therapy engine stopped")
            except Exception as e:
                print("[SessionManager] Error stopping therapy engine: {}".format(e))

        # Clear session context
        self._current_session = None

        # Transition to READY (stopped)
        self._state_machine.transition(
            StateTrigger.STOPPED, metadata={"reason": reason}
        )

        return True

    def emergency_stop(self):
        """
        Immediately stop the session in an emergency condition.

        This method differs from stop_session() by:
            1. Transitioning directly to ERROR state (not STOPPING/READY)
            2. NOT recording the session in history (emergency stops are not tracked)
            3. Using "EMERGENCY" as the stop reason
            4. Sending STOP_SESSION sync command to SECONDARY immediately

        This is intended for critical failures, safety issues, or system errors
        that require immediate session termination.

        Returns:
            True if emergency stop succeeded, False otherwise

        Example:
            >>> if session_mgr.emergency_stop():
            ...     print("Session emergency stopped")
        """
        if self._current_session is None:
            return False

        # Store session ID before clearing
        session_id = self._current_session.session_id

        # CRITICAL: Send STOP_SESSION sync command to SECONDARY immediately
        if self._send_sync_callback:
            self._send_sync_callback('STOP_SESSION', {'reason': 'EMERGENCY'})
            print("[SessionManager] Sent EMERGENCY STOP_SESSION sync command to SECONDARY")

        # Stop therapy engine immediately
        if self._therapy_engine:
            try:
                self._therapy_engine.stop()
                print("[SessionManager] Therapy engine emergency stopped")
            except Exception as e:
                print("Error stopping therapy engine during emergency: {}".format(e))

        # Call session stopped callback
        if self._on_session_stopped:
            self._on_session_stopped(session_id, "EMERGENCY")

        # Clear session context immediately (no history recording)
        self._current_session = None

        # Transition to ERROR state (not STOPPING/READY like normal stop)
        # Note: This assumes StateTrigger has an ERROR trigger or we force state
        # If no ERROR trigger exists, we transition to STOPPING then set error state
        try:
            # Attempt direct ERROR transition if available
            self._state_machine.transition(
                StateTrigger.STOP_SESSION,
                metadata={"session_id": session_id, "reason": "EMERGENCY"}
            )
            # Force ERROR state after stopping
            # This may need adjustment based on actual state machine implementation
            if hasattr(self._state_machine, '_state'):
                self._state_machine._state = TherapyState.ERROR
        except Exception as e:
            print("Error during emergency state transition: {}".format(e))

        return True

    def get_session_info(self):
        """
        Get information about the current session.

        Returns:
            SessionInfo object with current session details, or None if no session

        Example:
            >>> info = session_mgr.get_session_info()
            >>> if info:
            ...     print(f"Progress: {info.progress_percentage()}%")
        """
        if self._current_session is None:
            return None

        elapsed = self._get_elapsed_time()
        duration = self._current_session.profile.session_duration_min * 60

        return SessionInfo(
            session_id=self._current_session.session_id,
            start_time=self._current_session.start_time,
            duration_sec=duration,
            elapsed_sec=int(elapsed),
            profile_name="Custom Profile",
            state=self._state_machine.get_current_state(),
        )

    def _get_elapsed_time(self):
        """
        Calculate elapsed session time excluding pauses.

        Returns:
            Elapsed time in seconds
        """
        if self._current_session is None:
            return 0.0

        current_time = time.monotonic()

        # If currently paused, use pause time as end time
        if self._current_session.pause_time is not None:
            total_time = (
                self._current_session.pause_time - self._current_session.start_time
            )
        else:
            total_time = current_time - self._current_session.start_time

        # Subtract total pause duration
        elapsed = total_time - self._current_session.total_pause_duration

        return max(0.0, elapsed)

    def is_session_active(self):
        """
        Check if there is an active session.

        Returns:
            True if session is active (running or paused), False otherwise
        """
        return self._current_session is not None

    def get_current_profile(self):
        """
        Get the therapy profile for the current session.

        Returns:
            TherapyConfig for active session, or None if no session
        """
        if self._current_session is None:
            return None
        return self._current_session.profile

    def on_cycle_complete(self):
        """
        Callback to be called when a therapy cycle completes.

        This should be called by the therapy engine to update session statistics.
        """
        if self._current_session is not None:
            self._current_session.cycles_completed += 1

    # ============================================================================
    # Session tracking methods (consolidated from tracker.py)
    # ============================================================================

    def _record_session(
        self, stop_reason= "COMPLETED"
    ):
        """
        Create a session record for the current session.
        Consolidated from tracker.py.

        Args:
            stop_reason: Reason session stopped

        Returns:
            SessionRecord for completed session, or None if no current session
        """
        if self._current_session is None:
            return None

        end_time = time.monotonic()
        elapsed = self._get_elapsed_time()
        duration = self._current_session.profile.session_duration_min * 60

        # Calculate completion percentage
        completion = (elapsed / duration * 100) if duration > 0 else 0.0
        completion = min(100.0, max(0.0, completion))

        # Create session record
        record = SessionRecord(
            session_id=self._current_session.session_id,
            profile_name="Custom Profile",  # Could enhance with profile names
            start_time=self._current_session.start_time,
            end_time=end_time,
            duration_sec=duration,
            elapsed_sec=int(elapsed),
            pause_duration_sec=self._current_session.total_pause_duration,
            cycles_completed=self._current_session.cycles_completed,
            completion_percentage=completion,
            stop_reason=stop_reason,
        )

        # Add to history (CircuitPython compatible - maintain max size)
        self._session_history.append(record)
        if len(self._session_history) > self._max_history:
            self._session_history.pop(0)

        return record

    def get_session_history(self, limit= None):
        """
        Get historical session records.
        Consolidated from tracker.py.

        Args:
            limit: Maximum number of records to return (most recent first)

        Returns:
            List of SessionRecord objects
        """
        records = list(self._session_history)
        records.reverse()  # Most recent first

        if limit is not None:
            records = records[:limit]

        return records

    def get_statistics(self):
        """
        Calculate aggregate statistics across all recorded sessions.
        Consolidated from tracker.py.

        Returns:
            SessionStatistics with aggregate metrics
        """
        if not self._session_history:
            return SessionStatistics()

        total_sessions = len(self._session_history)
        completed_sessions = sum(
            1 for record in self._session_history if record.stop_reason == "COMPLETED"
        )

        total_therapy_time = sum(record.elapsed_sec for record in self._session_history)

        total_cycles = sum(record.cycles_completed for record in self._session_history)

        avg_completion = (
            sum(record.completion_percentage for record in self._session_history)
            / total_sessions
        )

        avg_duration = total_therapy_time / total_sessions

        # Calculate current streak (consecutive completed sessions from end)
        streak = 0
        for record in reversed(self._session_history):
            if record.stop_reason == "COMPLETED":
                streak += 1
            else:
                break

        return SessionStatistics(
            total_sessions=total_sessions,
            completed_sessions=completed_sessions,
            total_therapy_time_sec=total_therapy_time,
            total_cycles=total_cycles,
            average_completion_percentage=avg_completion,
            average_session_duration_sec=avg_duration,
            current_streak=streak,
        )

    def clear_history(self):
        """
        Clear all session history records.
        Consolidated from tracker.py.
        """
        self._session_history.clear()

    def export_history(self):
        """
        Export session history as a list of dictionaries.
        Consolidated from tracker.py.

        Returns:
            List of session records as dictionaries
        """
        return [
            {
                "session_id": record.session_id,
                "profile_name": record.profile_name,
                "start_time": record.start_time,
                "end_time": record.end_time,
                "duration_sec": record.duration_sec,
                "elapsed_sec": record.elapsed_sec,
                "pause_duration_sec": record.pause_duration_sec,
                "cycles_completed": record.cycles_completed,
                "completion_percentage": record.completion_percentage,
                "stop_reason": record.stop_reason,
            }
            for record in self._session_history
        ]
