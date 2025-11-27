"""
Consolidated Therapy Engine
============================
Simplified therapy execution and pattern generation for BlueBuzzah v2.

This module consolidates therapy engine and pattern generation into a single file:
- Pattern generation (RNDP, sequential, mirrored)
- Therapy execution with precise timing
- Cycle management
- Simple state tracking (no async overhead)

Module: therapy
Version: 2.0.0
"""

import random
import time

# ============================================================================
# Utility Functions
# ============================================================================


def _shuffle_in_place(sequence):
    """
    In-place Fisher-Yates shuffle for CircuitPython.

    CircuitPython's random module does not include shuffle(), so we implement
    the Fisher-Yates algorithm manually. This operates in-place to avoid
    memory allocation.

    Args:
        sequence: List to shuffle in-place

    Memory: Zero allocations (modifies list in-place)
    """
    n = len(sequence)
    for i in range(n - 1, 0, -1):
        # random.randint(a, b) returns random integer N such that a <= N <= b
        j = random.randint(0, i)
        # Swap elements at i and j
        sequence[i], sequence[j] = sequence[j], sequence[i]


# ============================================================================
# Pattern Data Structures
# ============================================================================


class Pattern:
    """
    Generated therapy pattern.

    Attributes:
        left_sequence: List of finger indices for left hand (0-4)
        right_sequence: List of finger indices for right hand (0-4)
        timing_ms: List of timing values for each activation (milliseconds)
        burst_duration_ms: Duration of each burst (milliseconds)
        inter_burst_interval_ms: Time between bursts (milliseconds)
    """

    def __init__(
        self,
        left_sequence,
        right_sequence,
        timing_ms,
        burst_duration_ms,
        inter_burst_interval_ms,
    ):
        """Initialize pattern with validation."""
        # Validate sequence lengths match
        if len(left_sequence) != len(right_sequence):
            raise ValueError(
                f"Sequence length mismatch: left={len(left_sequence)}, right={len(right_sequence)}"
            )

        # Validate timing matches sequence length
        if len(timing_ms) != len(left_sequence):
            raise ValueError(
                f"Timing length {len(timing_ms)} doesn't match sequence length {len(left_sequence)}"
            )

        self.left_sequence = left_sequence
        self.right_sequence = right_sequence
        self.timing_ms = timing_ms
        self.burst_duration_ms = burst_duration_ms
        self.inter_burst_interval_ms = inter_burst_interval_ms

    def __len__(self):
        """Return number of finger activations in the pattern."""
        return len(self.left_sequence)

    def get_total_duration_ms(self):
        """Calculate total pattern duration in milliseconds."""
        total = sum(self.timing_ms)
        total += len(self.timing_ms) * self.burst_duration_ms
        return total

    def get_finger_pair(self, index):
        """Get the left/right finger pair at the specified index."""
        return (self.left_sequence[index], self.right_sequence[index])


# ============================================================================
# Pattern Generation Functions
# ============================================================================


def generate_random_permutation(
    num_fingers=5,
    time_on_ms=100.0,
    time_off_ms=67.0,
    jitter_percent=0.0,
    mirror_pattern=True,
    random_seed=None,
):
    """
    Generate random permutation (RNDP) pattern.

    Each finger is activated exactly once per cycle in randomized order.

    Args:
        num_fingers: Number of fingers per hand (1-5)
        time_on_ms: Duration of each vibration burst
        time_off_ms: Duration between bursts
        jitter_percent: Timing jitter percentage for noisy vCR (0-100)
        mirror_pattern: If True, both hands use same sequence (mirrored bilateral).
                        If False, each hand gets independent random sequence.
                        Research: Use True for noisy vCR, False for regular vCR.
        random_seed: Optional random seed for reproducibility

    Returns:
        Pattern with randomized finger sequences

    Usage:
        # Noisy vCR (mirrored - same finger on both hands)
        pattern = generate_random_permutation(
            jitter_percent=23.5,
            mirror_pattern=True
        )

        # Regular vCR (non-mirrored - independent sequences per hand)
        pattern = generate_random_permutation(
            jitter_percent=0.0,
            mirror_pattern=False
        )
    """
    # CircuitPython: Use module-level random.seed() instead of Random class
    if random_seed is not None:
        random.seed(random_seed)

    # Calculate cycle parameters
    cycle_duration_ms = time_on_ms + time_off_ms
    inter_burst_interval_ms = 4 * cycle_duration_ms

    # Generate left hand sequence (random permutation)
    left_sequence = list(range(num_fingers))
    _shuffle_in_place(left_sequence)

    # Generate right hand sequence based on mirror setting
    if mirror_pattern:
        # Mirrored: same finger on both hands (for noisy vCR)
        # Avoids bilateral masking interference
        right_sequence = left_sequence.copy()
    else:
        # Non-mirrored: independent random sequence (for regular vCR)
        # Increases spatial randomization for synaptic decoupling
        right_sequence = list(range(num_fingers))
        _shuffle_in_place(right_sequence)

    # Generate timing with optional jitter
    timing_ms = []
    jitter_amount = cycle_duration_ms * (jitter_percent / 100.0) / 2.0

    for _ in range(num_fingers):
        if jitter_percent > 0:
            # CircuitPython random.uniform() takes (a, b) and returns float in [a, b]
            jitter = random.uniform(-jitter_amount, jitter_amount)
            interval = inter_burst_interval_ms + jitter
            interval = max(0.0, interval)
        else:
            interval = inter_burst_interval_ms
        timing_ms.append(interval)

    return Pattern(
        left_sequence=left_sequence,
        right_sequence=right_sequence,
        timing_ms=timing_ms,
        burst_duration_ms=time_on_ms,
        inter_burst_interval_ms=inter_burst_interval_ms,
    )


def generate_sequential_pattern(
    num_fingers=5,
    time_on_ms=100.0,
    time_off_ms=67.0,
    jitter_percent=0.0,
    mirror_pattern=True,
    reverse=False,
):
    """
    Generate sequential pattern.

    Fingers are activated in order: 0 → 1 → 2 → 3 → 4 (or reverse).

    Args:
        num_fingers: Number of fingers per hand (1-5)
        time_on_ms: Duration of each vibration burst
        time_off_ms: Duration between bursts
        jitter_percent: Timing jitter percentage (0-100)
        mirror_pattern: If True, both hands use same sequence (mirrored bilateral).
                        If False, right hand uses reverse of left sequence.
                        Research: Use True for noisy vCR, False for regular vCR.
        reverse: If True, generate reverse order (4 → 0)

    Returns:
        Pattern with sequential finger order

    Usage:
        pattern = generate_sequential_pattern(
            num_fingers=5,
            mirror_pattern=True,
            reverse=False  # 0 → 1 → 2 → 3 → 4
        )
    """
    # Calculate cycle parameters
    cycle_duration_ms = time_on_ms + time_off_ms
    inter_burst_interval_ms = 4 * cycle_duration_ms

    # Generate sequential list
    sequence = list(range(num_fingers))
    if reverse:
        sequence.reverse()

    # Generate sequences based on mirror setting
    left_sequence = sequence.copy()
    if mirror_pattern:
        # Mirrored: same finger on both hands
        right_sequence = sequence.copy()
    else:
        # Non-mirrored: right hand uses opposite order
        right_sequence = sequence.copy()
        right_sequence.reverse()

    # Generate timing with optional jitter
    timing_ms = []
    jitter_amount = cycle_duration_ms * (jitter_percent / 100.0) / 2.0

    for _ in range(num_fingers):
        if jitter_percent > 0:
            # CircuitPython: Use module-level random.uniform()
            jitter = random.uniform(-jitter_amount, jitter_amount)
            interval = inter_burst_interval_ms + jitter
            interval = max(0.0, interval)
        else:
            interval = inter_burst_interval_ms
        timing_ms.append(interval)

    return Pattern(
        left_sequence=left_sequence,
        right_sequence=right_sequence,
        timing_ms=timing_ms,
        burst_duration_ms=time_on_ms,
        inter_burst_interval_ms=inter_burst_interval_ms,
    )


def generate_mirrored_pattern(
    num_fingers= 5,
    time_on_ms= 100.0,
    time_off_ms= 67.0,
    jitter_percent= 0.0,
    randomize= True,
    random_seed= None,
):
    """
    Generate mirrored bilateral pattern.

    Both hands use identical finger sequences for perfect bilateral symmetry.

    Args:
        num_fingers: Number of fingers per hand (1-5)
        time_on_ms: Duration of each vibration burst
        time_off_ms: Duration between bursts
        jitter_percent: Timing jitter percentage (0-100)
        randomize: If True, use random sequence. If False, use sequential.
        random_seed: Optional random seed for reproducibility

    Returns:
        Pattern with identical left and right sequences

    Usage:
        pattern = generate_mirrored_pattern(
            randomize=True,  # Random mirrored
            jitter_percent=23.5
        )
    """
    # CircuitPython: Use module-level random.seed() instead of Random class
    if random_seed is not None:
        random.seed(random_seed)

    # Calculate cycle parameters
    cycle_duration_ms = time_on_ms + time_off_ms
    inter_burst_interval_ms = 4 * cycle_duration_ms

    # Generate base sequence
    sequence = list(range(num_fingers))
    if randomize:
        _shuffle_in_place(sequence)

    # Mirror to both hands
    left_sequence = sequence.copy()
    right_sequence = sequence.copy()

    # Generate timing with optional jitter
    timing_ms = []
    jitter_amount = cycle_duration_ms * (jitter_percent / 100.0) / 2.0

    for _ in range(num_fingers):
        if jitter_percent > 0:
            # CircuitPython: Use module-level random.uniform()
            jitter = random.uniform(-jitter_amount, jitter_amount)
            interval = inter_burst_interval_ms + jitter
            interval = max(0.0, interval)
        else:
            interval = inter_burst_interval_ms
        timing_ms.append(interval)

    return Pattern(
        left_sequence=left_sequence,
        right_sequence=right_sequence,
        timing_ms=timing_ms,
        burst_duration_ms=time_on_ms,
        inter_burst_interval_ms=inter_burst_interval_ms,
    )


# ============================================================================
# Therapy Engine
# ============================================================================


class TherapyEngine:
    """
    Simplified therapy execution engine.

    This class executes therapy patterns with precise timing and bilateral
    synchronization. It uses direct function calls instead of async overhead
    for memory efficiency.

    Features:
        - Pattern execution with precise timing
        - Bilateral synchronization (< 10ms drift)
        - Pause/resume functionality
        - Cycle counting and statistics
        - Simple state tracking

    Usage:
        # Initialize
        engine = TherapyEngine()

        # Set callbacks for hardware control
        engine.set_send_command_callback(send_sync_command)
        engine.set_activate_callback(activate_haptic)

        # Execute therapy
        engine.start_session(
            duration_sec=7200,
            pattern_type='rndp',
            time_on_ms=100,
            time_off_ms=67,
            jitter_percent=23.5
        )

        # In main loop
        while engine.is_running():
            engine.update()
            time.sleep(0.001)  # 1ms update rate
    """

    def __init__(self):
        """Initialize therapy engine."""
        # State tracking
        self._is_running = False
        self._is_paused = False
        self._should_stop = False

        # Session parameters
        self._session_start_time= None
        self._session_duration_sec= None
        self._pattern_config= {}

        # Current pattern execution
        self._current_pattern= None
        self._pattern_index= 0
        self._activation_start_time= None
        self._waiting_for_interval = False
        self._interval_start_time= None

        # Statistics
        self.cycles_completed= 0
        self.total_activations= 0
        self.timing_drift_ms= 0.0

        # Sequence tracking for command loss detection
        self._buzz_sequence_id = 0

        # Callbacks
        self._send_command_callback= None
        self._activate_callback= None
        self._deactivate_callback= None
        self._on_cycle_complete= None

    def set_send_command_callback(self, callback):
        """
        Set callback for sending sync commands.

        Args:
            callback: Function(command_type, data) for sending BLE commands
        """
        self._send_command_callback = callback

    def set_activate_callback(self, callback):
        """
        Set callback for activating haptic motor.

        Args:
            callback: Function(finger_index, amplitude) for activating motor
        """
        self._activate_callback = callback

    def set_deactivate_callback(self, callback):
        """
        Set callback for deactivating haptic motor.

        Args:
            callback: Function(finger_index) for deactivating motor
        """
        self._deactivate_callback = callback

    def set_cycle_complete_callback(self, callback):
        """
        Set callback for cycle completion events.

        Args:
            callback: Function(cycle_count) called when each cycle completes
        """
        self._on_cycle_complete = callback

    def start_session(
        self,
        duration_sec,
        pattern_type="rndp",
        time_on_ms=100.0,
        time_off_ms=67.0,
        jitter_percent=23.5,
        num_fingers=5,
        mirror_pattern=True,
    ):
        """
        Start therapy session.

        Args:
            duration_sec: Total session duration in seconds
            pattern_type: Pattern type ('rndp', 'sequential', 'mirrored')
            time_on_ms: Vibration burst duration
            time_off_ms: Time between bursts
            jitter_percent: Timing jitter for noisy vCR (0-100)
            num_fingers: Number of fingers per hand (1-5)
            mirror_pattern: If True, same finger on both hands (noisy vCR).
                            If False, independent sequences (regular vCR).

        Usage:
            # Noisy vCR (mirrored bilateral)
            engine.start_session(
                duration_sec=7200,
                pattern_type='rndp',
                jitter_percent=23.5,
                mirror_pattern=True
            )

            # Regular vCR (non-mirrored bilateral)
            engine.start_session(
                duration_sec=7200,
                pattern_type='rndp',
                jitter_percent=0.0,
                mirror_pattern=False
            )
        """
        # Reset state
        self._is_running = True
        self._is_paused = False
        self._should_stop = False
        self.cycles_completed = 0
        self.total_activations = 0
        self.timing_drift_ms = 0.0

        # Store session parameters
        self._session_start_time = time.monotonic()
        self._session_duration_sec = duration_sec
        self._pattern_config = {
            "pattern_type": pattern_type,
            "time_on_ms": time_on_ms,
            "time_off_ms": time_off_ms,
            "jitter_percent": jitter_percent,
            "num_fingers": num_fingers,
            "mirror_pattern": mirror_pattern,
        }

        # Generate first pattern
        self._generate_next_pattern()

    def update(self):
        """
        Update therapy engine state.

        This method should be called frequently (e.g., 1ms interval) to handle
        pattern execution timing, motor activation/deactivation, and cycle
        management.

        Usage:
            while engine.is_running():
                engine.update()
                time.sleep(0.001)
        """
        if not self._is_running:
            return

        # Check for pause
        if self._is_paused:
            return

        # Check for stop request
        if self._should_stop:
            self._is_running = False
            return

        # Check session timeout
        if self._session_start_time and self._session_duration_sec:
            elapsed = time.monotonic() - self._session_start_time
            if elapsed >= self._session_duration_sec:
                self.stop()
                return

        # Execute current pattern
        if self._current_pattern:
            self._execute_pattern_step()

    def _execute_pattern_step(self):
        """Execute one step of the current pattern."""
        pattern = self._current_pattern

        # Check if pattern is complete
        if self._pattern_index >= len(pattern):
            # Cycle complete
            self.cycles_completed += 1
            if self._on_cycle_complete:
                self._on_cycle_complete(self.cycles_completed)

            # Generate next pattern
            self._generate_next_pattern()
            return

        # Handle inter-burst interval waiting
        if self._waiting_for_interval:
            if self._interval_start_time:
                elapsed_ms = (time.monotonic() - self._interval_start_time) * 1000
                required_interval = pattern.timing_ms[self._pattern_index - 1]

                if elapsed_ms >= required_interval:
                    # Interval complete, move to next activation
                    self._waiting_for_interval = False
                    self._interval_start_time = None
            return

        # Start new activation
        if self._activation_start_time is None:
            left_finger, right_finger = pattern.get_finger_pair(self._pattern_index)

            # Send sync command to SECONDARY (if PRIMARY)
            if self._send_command_callback:
                # Include sequence number and timestamp for command loss detection
                self._send_command_callback(
                    "EXECUTE_BUZZ",
                    {
                        "left_finger": left_finger,
                        "right_finger": right_finger,
                        "amplitude": 100,
                        "seq": self._buzz_sequence_id,
                        "timestamp": int(time.monotonic_ns() // 1000),  # Microseconds
                    },
                )
                self._buzz_sequence_id += 1

            # Activate local motors
            if self._activate_callback:
                self._activate_callback(left_finger, 100)
                self._activate_callback(right_finger, 100)

            self._activation_start_time = time.monotonic()
            self.total_activations += 2  # Left + right
            return

        # Check if burst duration complete
        elapsed_ms = (time.monotonic() - self._activation_start_time) * 1000
        if elapsed_ms >= pattern.burst_duration_ms:
            # Deactivate motors
            left_finger, right_finger = pattern.get_finger_pair(self._pattern_index)
            if self._deactivate_callback:
                self._deactivate_callback(left_finger)
                self._deactivate_callback(right_finger)

            # Start inter-burst interval
            self._activation_start_time = None
            self._waiting_for_interval = True
            self._interval_start_time = time.monotonic()
            self._pattern_index += 1

    def _generate_next_pattern(self):
        """Generate the next pattern based on configuration.

        Bilateral mirroring is controlled by the mirror_pattern config:
        - True: Same finger on both hands (noisy vCR)
        - False: Independent sequences per hand (regular vCR)
        """
        config = self._pattern_config
        pattern_type = config.get("pattern_type", "rndp")
        mirror = config.get("mirror_pattern", True)

        if pattern_type == "rndp":
            self._current_pattern = generate_random_permutation(
                num_fingers=config.get("num_fingers", 5),
                time_on_ms=config.get("time_on_ms", 100.0),
                time_off_ms=config.get("time_off_ms", 67.0),
                jitter_percent=config.get("jitter_percent", 23.5),
                mirror_pattern=mirror,
            )
        elif pattern_type == "sequential":
            self._current_pattern = generate_sequential_pattern(
                num_fingers=config.get("num_fingers", 5),
                time_on_ms=config.get("time_on_ms", 100.0),
                time_off_ms=config.get("time_off_ms", 67.0),
                jitter_percent=config.get("jitter_percent", 0.0),
                mirror_pattern=mirror,
            )
        elif pattern_type == "mirrored":
            self._current_pattern = generate_mirrored_pattern(
                num_fingers=config.get("num_fingers", 5),
                time_on_ms=config.get("time_on_ms", 100.0),
                time_off_ms=config.get("time_off_ms", 67.0),
                jitter_percent=config.get("jitter_percent", 23.5),
                randomize=True,
            )
        else:
            raise ValueError(f"Unknown pattern type: {pattern_type}")

        # Reset pattern execution state
        self._pattern_index = 0
        self._activation_start_time = None
        self._waiting_for_interval = False
        self._interval_start_time = None

    def pause(self):
        """Pause therapy execution."""
        self._is_paused = True

    def resume(self):
        """Resume paused therapy execution."""
        self._is_paused = False

    def stop(self):
        """Stop therapy execution."""
        self._should_stop = True
        self._is_running = False

    def is_running(self):
        """Check if therapy is currently running."""
        return self._is_running

    def is_paused(self):
        """Check if therapy is currently paused."""
        return self._is_paused

    def get_stats(self):
        """
        Get current execution statistics.

        Returns:
            Dictionary with cycles_completed, total_activations, timing_drift_ms
        """
        return {
            "cycles_completed": self.cycles_completed,
            "total_activations": self.total_activations,
            "timing_drift_ms": self.timing_drift_ms,
        }
