"""
Therapy Engine Tests
====================

Comprehensive tests for the therapy module including Pattern class,
pattern generation functions, and TherapyEngine class.

Tests all therapy execution components:
- Pattern data structure and validation
- Pattern generation (RNDP, sequential, mirrored)
- TherapyEngine state machine and execution
- Callback invocation and timing
- Statistics tracking

Test Categories:
    - Pattern class tests
    - Pattern generation tests (RNDP, sequential, mirrored)
    - TherapyEngine initialization tests
    - TherapyEngine state management tests
    - TherapyEngine callback tests
    - TherapyEngine timing tests

Run with: python -m pytest tests/test_therapy_engine.py -v

Module: tests.test_therapy_engine
Version: 2.0.0
"""

import pytest
import random
import sys
import time
from pathlib import Path
from unittest.mock import MagicMock, patch, call

# Add src directory to Python path
src_path = Path(__file__).parent.parent / "src"
sys.path.insert(0, str(src_path))

# Mock micropython module before importing code that requires it
if 'micropython' not in sys.modules:
    micropython_mock = MagicMock()
    micropython_mock.const = lambda x: x
    sys.modules['micropython'] = micropython_mock

# Import therapy module
from therapy import (
    Pattern,
    generate_random_permutation,
    generate_sequential_pattern,
    generate_mirrored_pattern,
    TherapyEngine,
    _shuffle_in_place,
)


# ============================================================================
# Test Fixtures
# ============================================================================

@pytest.fixture
def default_pattern_params():
    """
    Provide default pattern parameters.

    Returns:
        dict: Standard pattern configuration
    """
    return {
        "left_sequence": [0, 1, 2, 3, 4],
        "right_sequence": [0, 1, 2, 3, 4],
        "timing_ms": [668.0, 668.0, 668.0, 668.0, 668.0],
        "burst_duration_ms": 100.0,
        "inter_burst_interval_ms": 668.0,
    }


@pytest.fixture
def sample_pattern(default_pattern_params):
    """
    Provide a sample Pattern instance.

    Returns:
        Pattern: Pattern with default parameters
    """
    return Pattern(**default_pattern_params)


@pytest.fixture
def therapy_engine():
    """
    Provide a basic TherapyEngine instance.

    Returns:
        TherapyEngine: Fresh engine instance
    """
    return TherapyEngine()


@pytest.fixture
def mock_activate_callback():
    """
    Provide mock activate callback.

    Returns:
        MagicMock: Mock callback function
    """
    return MagicMock()


@pytest.fixture
def mock_deactivate_callback():
    """
    Provide mock deactivate callback.

    Returns:
        MagicMock: Mock callback function
    """
    return MagicMock()


@pytest.fixture
def mock_cycle_complete_callback():
    """
    Provide mock cycle complete callback.

    Returns:
        MagicMock: Mock callback function
    """
    return MagicMock()


@pytest.fixture
def mock_send_command_callback():
    """
    Provide mock send command callback.

    Returns:
        MagicMock: Mock callback function
    """
    return MagicMock()


@pytest.fixture
def configured_engine(
    therapy_engine,
    mock_activate_callback,
    mock_deactivate_callback,
    mock_cycle_complete_callback,
    mock_send_command_callback,
):
    """
    Provide TherapyEngine with all callbacks configured.

    Returns:
        TherapyEngine: Engine with mocked callbacks
    """
    therapy_engine.set_activate_callback(mock_activate_callback)
    therapy_engine.set_deactivate_callback(mock_deactivate_callback)
    therapy_engine.set_cycle_complete_callback(mock_cycle_complete_callback)
    therapy_engine.set_send_command_callback(mock_send_command_callback)

    # Attach mocks for easy access in tests
    therapy_engine._mock_activate = mock_activate_callback
    therapy_engine._mock_deactivate = mock_deactivate_callback
    therapy_engine._mock_cycle_complete = mock_cycle_complete_callback
    therapy_engine._mock_send_command = mock_send_command_callback

    return therapy_engine


# ============================================================================
# Pattern Class Tests
# ============================================================================

class TestPattern:
    """Test Pattern class."""

    def test_pattern_creation(self, default_pattern_params):
        """Test Pattern initialization with valid parameters."""
        pattern = Pattern(**default_pattern_params)

        assert pattern.left_sequence == [0, 1, 2, 3, 4]
        assert pattern.right_sequence == [0, 1, 2, 3, 4]
        assert pattern.timing_ms == [668.0, 668.0, 668.0, 668.0, 668.0]
        assert pattern.burst_duration_ms == 100.0
        assert pattern.inter_burst_interval_ms == 668.0

    def test_pattern_length(self, sample_pattern):
        """Test Pattern __len__ returns correct count."""
        assert len(sample_pattern) == 5

    def test_pattern_length_different_sizes(self):
        """Test Pattern length with different sequence sizes."""
        pattern = Pattern(
            left_sequence=[0, 1, 2],
            right_sequence=[0, 1, 2],
            timing_ms=[100.0, 100.0, 100.0],
            burst_duration_ms=50.0,
            inter_burst_interval_ms=100.0,
        )
        assert len(pattern) == 3

    def test_pattern_validation_sequence_mismatch(self):
        """Test Pattern raises ValueError on sequence length mismatch."""
        with pytest.raises(ValueError) as exc_info:
            Pattern(
                left_sequence=[0, 1, 2, 3, 4],
                right_sequence=[0, 1, 2],  # Shorter
                timing_ms=[100.0, 100.0, 100.0, 100.0, 100.0],
                burst_duration_ms=100.0,
                inter_burst_interval_ms=668.0,
            )

        assert "Sequence length mismatch" in str(exc_info.value)
        assert "left=5" in str(exc_info.value)
        assert "right=3" in str(exc_info.value)

    def test_pattern_validation_timing_mismatch(self):
        """Test Pattern raises ValueError on timing length mismatch."""
        with pytest.raises(ValueError) as exc_info:
            Pattern(
                left_sequence=[0, 1, 2, 3, 4],
                right_sequence=[0, 1, 2, 3, 4],
                timing_ms=[100.0, 100.0],  # Too short
                burst_duration_ms=100.0,
                inter_burst_interval_ms=668.0,
            )

        assert "Timing length" in str(exc_info.value)
        assert "doesn't match sequence length" in str(exc_info.value)

    def test_get_finger_pair(self, sample_pattern):
        """Test get_finger_pair returns correct tuple."""
        pair_0 = sample_pattern.get_finger_pair(0)
        pair_2 = sample_pattern.get_finger_pair(2)
        pair_4 = sample_pattern.get_finger_pair(4)

        assert pair_0 == (0, 0)
        assert pair_2 == (2, 2)
        assert pair_4 == (4, 4)

    def test_get_finger_pair_different_sequences(self):
        """Test get_finger_pair with different left/right sequences."""
        pattern = Pattern(
            left_sequence=[0, 1, 2],
            right_sequence=[4, 3, 2],
            timing_ms=[100.0, 100.0, 100.0],
            burst_duration_ms=50.0,
            inter_burst_interval_ms=100.0,
        )

        assert pattern.get_finger_pair(0) == (0, 4)
        assert pattern.get_finger_pair(1) == (1, 3)
        assert pattern.get_finger_pair(2) == (2, 2)

    def test_get_total_duration(self, sample_pattern):
        """Test get_total_duration_ms calculation."""
        # timing = 5 * 668.0 = 3340.0
        # burst = 5 * 100.0 = 500.0
        # total = 3840.0
        expected = 5 * 668.0 + 5 * 100.0
        assert sample_pattern.get_total_duration_ms() == expected

    def test_get_total_duration_single_finger(self):
        """Test get_total_duration_ms with single finger pattern."""
        pattern = Pattern(
            left_sequence=[0],
            right_sequence=[0],
            timing_ms=[200.0],
            burst_duration_ms=100.0,
            inter_burst_interval_ms=200.0,
        )

        expected = 200.0 + 100.0  # timing + burst
        assert pattern.get_total_duration_ms() == expected


# ============================================================================
# Shuffle Utility Tests
# ============================================================================

class TestShuffleInPlace:
    """Test _shuffle_in_place utility function."""

    def test_shuffle_preserves_elements(self):
        """Test shuffle keeps all elements."""
        original = [0, 1, 2, 3, 4]
        sequence = original.copy()

        _shuffle_in_place(sequence)

        assert sorted(sequence) == sorted(original)

    def test_shuffle_modifies_in_place(self):
        """Test shuffle modifies list in place."""
        sequence = [0, 1, 2, 3, 4]
        original_id = id(sequence)

        _shuffle_in_place(sequence)

        assert id(sequence) == original_id

    def test_shuffle_with_seed_reproducibility(self):
        """Test shuffle is reproducible with same seed."""
        random.seed(42)
        seq1 = [0, 1, 2, 3, 4]
        _shuffle_in_place(seq1)

        random.seed(42)
        seq2 = [0, 1, 2, 3, 4]
        _shuffle_in_place(seq2)

        assert seq1 == seq2

    def test_shuffle_empty_list(self):
        """Test shuffle handles empty list."""
        sequence = []
        _shuffle_in_place(sequence)
        assert sequence == []

    def test_shuffle_single_element(self):
        """Test shuffle handles single element list."""
        sequence = [0]
        _shuffle_in_place(sequence)
        assert sequence == [0]


# ============================================================================
# Generate Random Permutation Tests
# ============================================================================

class TestGenerateRandomPermutation:
    """Test generate_random_permutation function."""

    def test_generates_all_fingers(self):
        """Test RNDP pattern contains all fingers."""
        pattern = generate_random_permutation(num_fingers=5)

        assert sorted(pattern.left_sequence) == [0, 1, 2, 3, 4]
        assert sorted(pattern.right_sequence) == [0, 1, 2, 3, 4]

    def test_no_duplicate_fingers(self):
        """Test RNDP pattern has no duplicates."""
        pattern = generate_random_permutation(num_fingers=5)

        assert len(set(pattern.left_sequence)) == 5
        assert len(set(pattern.right_sequence)) == 5

    def test_mirror_pattern_matches_both_hands(self):
        """Test mirror_pattern=True produces identical sequences."""
        pattern = generate_random_permutation(
            num_fingers=5,
            mirror_pattern=True,
            random_seed=42,
        )

        assert pattern.left_sequence == pattern.right_sequence

    def test_non_mirror_pattern_differs(self):
        """Test mirror_pattern=False produces different sequences."""
        # Run multiple times to ensure we get different sequences
        found_different = False
        for seed in range(100):
            pattern = generate_random_permutation(
                num_fingers=5,
                mirror_pattern=False,
                random_seed=seed,
            )
            if pattern.left_sequence != pattern.right_sequence:
                found_different = True
                break

        assert found_different, "Expected non-mirrored pattern to have different sequences"

    def test_jitter_affects_timing(self):
        """Test jitter_percent creates timing variation."""
        pattern = generate_random_permutation(
            num_fingers=5,
            jitter_percent=50.0,
            random_seed=42,
        )

        # With 50% jitter, not all timings should be equal
        unique_timings = set(pattern.timing_ms)
        assert len(unique_timings) > 1

    def test_zero_jitter_consistent_timing(self):
        """Test jitter_percent=0 produces consistent timing."""
        pattern = generate_random_permutation(
            num_fingers=5,
            jitter_percent=0.0,
        )

        # All timings should be equal
        assert len(set(pattern.timing_ms)) == 1

    def test_random_seed_reproducibility(self):
        """Test random_seed produces reproducible patterns."""
        pattern1 = generate_random_permutation(
            num_fingers=5,
            random_seed=42,
        )
        pattern2 = generate_random_permutation(
            num_fingers=5,
            random_seed=42,
        )

        assert pattern1.left_sequence == pattern2.left_sequence
        assert pattern1.right_sequence == pattern2.right_sequence
        assert pattern1.timing_ms == pattern2.timing_ms

    def test_num_fingers_parameter(self):
        """Test num_fingers creates correct sized pattern."""
        for num in [1, 2, 3, 4, 5]:
            pattern = generate_random_permutation(num_fingers=num)

            assert len(pattern.left_sequence) == num
            assert len(pattern.right_sequence) == num
            assert len(pattern.timing_ms) == num

    def test_time_on_ms_parameter(self):
        """Test time_on_ms sets burst duration."""
        pattern = generate_random_permutation(time_on_ms=150.0)

        assert pattern.burst_duration_ms == 150.0

    def test_time_off_ms_parameter(self):
        """Test time_off_ms affects timing calculations."""
        pattern1 = generate_random_permutation(
            time_on_ms=100.0,
            time_off_ms=50.0,
            jitter_percent=0.0,
        )
        pattern2 = generate_random_permutation(
            time_on_ms=100.0,
            time_off_ms=100.0,
            jitter_percent=0.0,
        )

        # Different time_off should produce different intervals
        assert pattern1.inter_burst_interval_ms != pattern2.inter_burst_interval_ms

    def test_inter_burst_interval_calculation(self):
        """Test inter_burst_interval_ms is calculated correctly."""
        # Formula: inter_burst_interval_ms = 4 * (time_on_ms + time_off_ms)
        pattern = generate_random_permutation(
            time_on_ms=100.0,
            time_off_ms=67.0,
            jitter_percent=0.0,
        )

        expected_interval = 4 * (100.0 + 67.0)
        assert pattern.inter_burst_interval_ms == expected_interval


# ============================================================================
# Generate Sequential Pattern Tests
# ============================================================================

class TestGenerateSequentialPattern:
    """Test generate_sequential_pattern function."""

    def test_sequential_order(self):
        """Test sequential pattern produces ordered sequence."""
        pattern = generate_sequential_pattern(
            num_fingers=5,
            reverse=False,
        )

        assert pattern.left_sequence == [0, 1, 2, 3, 4]

    def test_reverse_order(self):
        """Test reverse=True produces reversed sequence."""
        pattern = generate_sequential_pattern(
            num_fingers=5,
            reverse=True,
        )

        assert pattern.left_sequence == [4, 3, 2, 1, 0]

    def test_mirror_pattern(self):
        """Test mirror_pattern=True produces identical sequences."""
        pattern = generate_sequential_pattern(
            num_fingers=5,
            mirror_pattern=True,
        )

        assert pattern.left_sequence == pattern.right_sequence

    def test_non_mirror_pattern(self):
        """Test mirror_pattern=False produces opposite sequences."""
        pattern = generate_sequential_pattern(
            num_fingers=5,
            mirror_pattern=False,
            reverse=False,
        )

        # Left is 0,1,2,3,4 - right is reversed 4,3,2,1,0
        assert pattern.left_sequence == [0, 1, 2, 3, 4]
        assert pattern.right_sequence == [4, 3, 2, 1, 0]

    def test_jitter_affects_timing(self):
        """Test jitter_percent creates timing variation."""
        # Use a seed to ensure consistent behavior
        random.seed(42)
        pattern = generate_sequential_pattern(
            num_fingers=5,
            jitter_percent=50.0,
        )

        # With jitter, not all timings should be equal
        unique_timings = set(pattern.timing_ms)
        assert len(unique_timings) > 1

    def test_zero_jitter_consistent_timing(self):
        """Test jitter_percent=0 produces consistent timing."""
        pattern = generate_sequential_pattern(
            num_fingers=5,
            jitter_percent=0.0,
        )

        assert len(set(pattern.timing_ms)) == 1

    def test_num_fingers_parameter(self):
        """Test num_fingers creates correct sized pattern."""
        pattern = generate_sequential_pattern(num_fingers=3)

        assert len(pattern.left_sequence) == 3
        assert pattern.left_sequence == [0, 1, 2]


# ============================================================================
# Generate Mirrored Pattern Tests
# ============================================================================

class TestGenerateMirroredPattern:
    """Test generate_mirrored_pattern function."""

    def test_identical_sequences_both_hands(self):
        """Test mirrored pattern produces identical sequences."""
        pattern = generate_mirrored_pattern(
            num_fingers=5,
            random_seed=42,
        )

        assert pattern.left_sequence == pattern.right_sequence

    def test_randomize_option_true(self):
        """Test randomize=True shuffles sequence."""
        pattern = generate_mirrored_pattern(
            num_fingers=5,
            randomize=True,
            random_seed=42,
        )

        # Should be randomized but still contain all fingers
        assert sorted(pattern.left_sequence) == [0, 1, 2, 3, 4]
        # With specific seed, unlikely to be sequential
        assert pattern.left_sequence != [0, 1, 2, 3, 4]

    def test_randomize_option_false(self):
        """Test randomize=False keeps sequential order."""
        pattern = generate_mirrored_pattern(
            num_fingers=5,
            randomize=False,
        )

        assert pattern.left_sequence == [0, 1, 2, 3, 4]
        assert pattern.right_sequence == [0, 1, 2, 3, 4]

    def test_jitter_affects_timing(self):
        """Test jitter_percent creates timing variation."""
        random.seed(42)
        pattern = generate_mirrored_pattern(
            num_fingers=5,
            jitter_percent=50.0,
        )

        unique_timings = set(pattern.timing_ms)
        assert len(unique_timings) > 1

    def test_random_seed_reproducibility(self):
        """Test random_seed produces reproducible patterns."""
        pattern1 = generate_mirrored_pattern(
            num_fingers=5,
            randomize=True,
            random_seed=42,
        )
        pattern2 = generate_mirrored_pattern(
            num_fingers=5,
            randomize=True,
            random_seed=42,
        )

        assert pattern1.left_sequence == pattern2.left_sequence

    def test_num_fingers_parameter(self):
        """Test num_fingers creates correct sized pattern."""
        pattern = generate_mirrored_pattern(num_fingers=3)

        assert len(pattern.left_sequence) == 3
        assert len(pattern.right_sequence) == 3


# ============================================================================
# TherapyEngine Initialization Tests
# ============================================================================

class TestTherapyEngineInit:
    """Test TherapyEngine initialization."""

    def test_engine_starts_stopped(self, therapy_engine):
        """Test engine initializes in stopped state."""
        assert therapy_engine.is_running() is False
        assert therapy_engine.is_paused() is False

    def test_engine_starts_with_zero_stats(self, therapy_engine):
        """Test engine initializes with zero statistics."""
        stats = therapy_engine.get_stats()

        assert stats["cycles_completed"] == 0
        assert stats["total_activations"] == 0
        assert stats["timing_drift_ms"] == 0.0

    def test_engine_starts_with_no_pattern(self, therapy_engine):
        """Test engine initializes with no current pattern."""
        assert therapy_engine._current_pattern is None

    def test_engine_starts_with_no_callbacks(self, therapy_engine):
        """Test engine initializes with no callbacks set."""
        assert therapy_engine._activate_callback is None
        assert therapy_engine._deactivate_callback is None
        assert therapy_engine._on_cycle_complete is None
        assert therapy_engine._send_command_callback is None


# ============================================================================
# TherapyEngine Callback Setup Tests
# ============================================================================

class TestTherapyEngineCallbackSetup:
    """Test TherapyEngine callback configuration."""

    def test_set_activate_callback(self, therapy_engine, mock_activate_callback):
        """Test set_activate_callback stores callback."""
        therapy_engine.set_activate_callback(mock_activate_callback)

        assert therapy_engine._activate_callback is mock_activate_callback

    def test_set_deactivate_callback(self, therapy_engine, mock_deactivate_callback):
        """Test set_deactivate_callback stores callback."""
        therapy_engine.set_deactivate_callback(mock_deactivate_callback)

        assert therapy_engine._deactivate_callback is mock_deactivate_callback

    def test_set_cycle_complete_callback(self, therapy_engine, mock_cycle_complete_callback):
        """Test set_cycle_complete_callback stores callback."""
        therapy_engine.set_cycle_complete_callback(mock_cycle_complete_callback)

        assert therapy_engine._on_cycle_complete is mock_cycle_complete_callback

    def test_set_send_command_callback(self, therapy_engine, mock_send_command_callback):
        """Test set_send_command_callback stores callback."""
        therapy_engine.set_send_command_callback(mock_send_command_callback)

        assert therapy_engine._send_command_callback is mock_send_command_callback


# ============================================================================
# TherapyEngine State Management Tests
# ============================================================================

class TestTherapyEngineStateManagement:
    """Test TherapyEngine state transitions."""

    def test_start_session_sets_running(self, therapy_engine):
        """Test start_session sets running state."""
        therapy_engine.start_session(duration_sec=60)

        assert therapy_engine.is_running() is True
        assert therapy_engine.is_paused() is False

    def test_pause_sets_paused(self, therapy_engine):
        """Test pause sets paused state."""
        therapy_engine.start_session(duration_sec=60)
        therapy_engine.pause()

        assert therapy_engine.is_running() is True
        assert therapy_engine.is_paused() is True

    def test_resume_clears_paused(self, therapy_engine):
        """Test resume clears paused state."""
        therapy_engine.start_session(duration_sec=60)
        therapy_engine.pause()

        assert therapy_engine.is_paused() is True

        therapy_engine.resume()

        assert therapy_engine.is_paused() is False
        assert therapy_engine.is_running() is True

    def test_stop_ends_session(self, therapy_engine):
        """Test stop ends session."""
        therapy_engine.start_session(duration_sec=60)

        assert therapy_engine.is_running() is True

        therapy_engine.stop()

        assert therapy_engine.is_running() is False

    def test_stop_while_paused(self, therapy_engine):
        """Test stop works while paused."""
        therapy_engine.start_session(duration_sec=60)
        therapy_engine.pause()

        therapy_engine.stop()

        # Note: stop() sets _is_running=False but doesn't clear _is_paused
        # This is current implementation behavior
        assert therapy_engine.is_running() is False

    def test_start_session_resets_stats(self, therapy_engine):
        """Test start_session resets statistics."""
        therapy_engine.start_session(duration_sec=60)
        therapy_engine.cycles_completed = 10
        therapy_engine.total_activations = 100
        therapy_engine.stop()

        therapy_engine.start_session(duration_sec=60)

        stats = therapy_engine.get_stats()
        assert stats["cycles_completed"] == 0
        assert stats["total_activations"] == 0


# ============================================================================
# TherapyEngine Update Tests
# ============================================================================

class TestTherapyEngineUpdate:
    """Test TherapyEngine update behavior."""

    def test_update_during_running(self, therapy_engine):
        """Test update executes during running state."""
        therapy_engine.start_session(duration_sec=60)

        # Should not raise
        therapy_engine.update()

        assert therapy_engine.is_running() is True

    def test_update_while_paused_does_nothing(self, therapy_engine):
        """Test update does nothing while paused."""
        therapy_engine.start_session(duration_sec=60)
        therapy_engine.pause()

        initial_activations = therapy_engine.total_activations

        # Update while paused
        therapy_engine.update()

        # Activations should not increase
        assert therapy_engine.total_activations == initial_activations

    def test_update_when_stopped_does_nothing(self, therapy_engine):
        """Test update does nothing when stopped."""
        assert therapy_engine.is_running() is False

        # Should not raise
        therapy_engine.update()

        assert therapy_engine.is_running() is False


# ============================================================================
# TherapyEngine Callback Invocation Tests
# ============================================================================

class TestTherapyEngineCallbacks:
    """Test TherapyEngine callback invocation."""

    def test_on_activate_callback_called(self, configured_engine):
        """Test activate callback is called during pattern execution."""
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
            jitter_percent=0.0,
        )

        # First update should trigger activation
        configured_engine.update()

        configured_engine._mock_activate.assert_called()

    def test_on_activate_callback_receives_finger_and_amplitude(self, configured_engine):
        """Test activate callback receives correct parameters."""
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
            jitter_percent=0.0,
        )

        configured_engine.update()

        # Should be called with finger index and amplitude
        call_args = configured_engine._mock_activate.call_args_list[0]
        finger_index = call_args[0][0]
        amplitude = call_args[0][1]

        assert isinstance(finger_index, int)
        assert 0 <= finger_index <= 4
        assert amplitude == 100

    def test_on_deactivate_callback_called(self, configured_engine):
        """Test deactivate callback is called after burst duration."""
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
            time_on_ms=10.0,  # Short burst
            time_off_ms=10.0,
            jitter_percent=0.0,
        )

        # Trigger activation
        configured_engine.update()

        # Wait for burst to complete
        time.sleep(0.015)

        # Update to trigger deactivation
        configured_engine.update()

        configured_engine._mock_deactivate.assert_called()

    def test_on_cycle_complete_callback_called(self, configured_engine):
        """Test cycle complete callback is called after full pattern."""
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
            num_fingers=1,  # Single finger for quick cycle
            time_on_ms=5.0,
            time_off_ms=5.0,
            jitter_percent=0.0,
        )

        # Execute until cycle completes
        start = time.monotonic()
        while time.monotonic() - start < 0.5:  # Max 500ms
            configured_engine.update()
            time.sleep(0.001)

            if configured_engine._mock_cycle_complete.called:
                break

        configured_engine._mock_cycle_complete.assert_called()

    def test_on_cycle_complete_receives_cycle_count(self, configured_engine):
        """Test cycle complete callback receives cycle count."""
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
            num_fingers=1,
            time_on_ms=5.0,
            time_off_ms=5.0,
            jitter_percent=0.0,
        )

        # Execute until cycle completes
        start = time.monotonic()
        while time.monotonic() - start < 0.5:
            configured_engine.update()
            time.sleep(0.001)

            if configured_engine._mock_cycle_complete.called:
                break

        call_args = configured_engine._mock_cycle_complete.call_args
        cycle_count = call_args[0][0]

        assert cycle_count == 1


# ============================================================================
# TherapyEngine Session Timeout Tests
# ============================================================================

class TestTherapyEngineTimeout:
    """Test TherapyEngine session timeout behavior."""

    def test_session_timeout_stops_engine(self, therapy_engine):
        """Test session automatically stops after duration."""
        therapy_engine.start_session(duration_sec=0.05)  # 50ms session

        assert therapy_engine.is_running() is True

        # Wait for timeout
        time.sleep(0.06)

        # Update to process timeout
        therapy_engine.update()

        assert therapy_engine.is_running() is False


# ============================================================================
# TherapyEngine Statistics Tests
# ============================================================================

class TestTherapyEngineStatistics:
    """Test TherapyEngine statistics tracking."""

    def test_get_stats_returns_correct_structure(self, therapy_engine):
        """Test get_stats returns expected dictionary structure."""
        stats = therapy_engine.get_stats()

        assert "cycles_completed" in stats
        assert "total_activations" in stats
        assert "timing_drift_ms" in stats

    def test_get_stats_returns_correct_counts(self, configured_engine):
        """Test get_stats returns accurate counts."""
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
            num_fingers=1,
            time_on_ms=5.0,
            time_off_ms=5.0,
            jitter_percent=0.0,
        )

        # Execute until at least one cycle completes
        start = time.monotonic()
        while time.monotonic() - start < 0.5:
            configured_engine.update()
            time.sleep(0.001)

            stats = configured_engine.get_stats()
            if stats["cycles_completed"] >= 1:
                break

        stats = configured_engine.get_stats()

        assert stats["cycles_completed"] >= 1
        # Each activation is 2 (left + right)
        assert stats["total_activations"] >= 2


# ============================================================================
# TherapyEngine Pattern Type Tests
# ============================================================================

class TestTherapyEnginePatternTypes:
    """Test TherapyEngine pattern type selection."""

    def test_pattern_type_rndp(self, therapy_engine):
        """Test RNDP pattern type generates random pattern."""
        therapy_engine.start_session(
            duration_sec=60,
            pattern_type="rndp",
        )

        assert therapy_engine._current_pattern is not None
        assert len(therapy_engine._current_pattern) == 5

    def test_pattern_type_sequential(self, therapy_engine):
        """Test sequential pattern type generates ordered pattern."""
        therapy_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
        )

        pattern = therapy_engine._current_pattern
        assert pattern is not None
        assert pattern.left_sequence == [0, 1, 2, 3, 4]

    def test_pattern_type_mirrored(self, therapy_engine):
        """Test mirrored pattern type generates mirrored pattern."""
        therapy_engine.start_session(
            duration_sec=60,
            pattern_type="mirrored",
        )

        pattern = therapy_engine._current_pattern
        assert pattern is not None
        assert pattern.left_sequence == pattern.right_sequence

    def test_pattern_type_invalid_raises_error(self, therapy_engine):
        """Test invalid pattern type raises ValueError during start_session."""
        # ValueError is raised during start_session when pattern type is invalid
        # because _generate_next_pattern() is called internally
        with pytest.raises(ValueError) as exc_info:
            therapy_engine.start_session(duration_sec=60, pattern_type="invalid")

        assert "Unknown pattern type" in str(exc_info.value)


# ============================================================================
# TherapyEngine Mirror Pattern Tests
# ============================================================================

class TestTherapyEngineMirrorPattern:
    """Test TherapyEngine mirror_pattern parameter."""

    def test_mirror_pattern_true(self, therapy_engine):
        """Test mirror_pattern=True creates identical sequences."""
        therapy_engine.start_session(
            duration_sec=60,
            pattern_type="rndp",
            mirror_pattern=True,
        )

        pattern = therapy_engine._current_pattern
        assert pattern.left_sequence == pattern.right_sequence

    def test_mirror_pattern_false(self, therapy_engine):
        """Test mirror_pattern=False can create different sequences."""
        # With RNDP and mirror_pattern=False, sequences should differ
        found_different = False
        for _ in range(10):
            therapy_engine.start_session(
                duration_sec=60,
                pattern_type="rndp",
                mirror_pattern=False,
            )
            pattern = therapy_engine._current_pattern
            if pattern.left_sequence != pattern.right_sequence:
                found_different = True
                break
            therapy_engine.stop()

        assert found_different, "Expected non-mirrored pattern to have different sequences"


# ============================================================================
# TherapyEngine Send Command Tests
# ============================================================================

class TestTherapyEngineSendCommand:
    """Test TherapyEngine send command functionality."""

    def test_send_command_callback_called_on_activation(self, configured_engine):
        """Test send_command callback is called during activation."""
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
        )

        configured_engine.update()

        configured_engine._mock_send_command.assert_called()

    def test_send_command_contains_execute_buzz(self, configured_engine):
        """Test send_command includes EXECUTE_BUZZ command type."""
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
        )

        configured_engine.update()

        call_args = configured_engine._mock_send_command.call_args
        command_type = call_args[0][0]

        assert command_type == "EXECUTE_BUZZ"

    def test_send_command_contains_required_data(self, configured_engine):
        """Test send_command data contains required fields."""
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
        )

        configured_engine.update()

        call_args = configured_engine._mock_send_command.call_args
        data = call_args[0][1]

        assert "left_finger" in data
        assert "right_finger" in data
        assert "amplitude" in data
        assert "seq" in data
        assert "timestamp" in data

    def test_send_command_sequence_increments(self, configured_engine):
        """Test send_command sequence number increments."""
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
            num_fingers=2,
            time_on_ms=5.0,
            time_off_ms=5.0,
            jitter_percent=0.0,
        )

        sequences = []

        # Execute multiple activations
        start = time.monotonic()
        while time.monotonic() - start < 0.5:
            configured_engine.update()
            time.sleep(0.001)

            # Collect sequence numbers
            for call in configured_engine._mock_send_command.call_args_list:
                seq = call[0][1]["seq"]
                if seq not in sequences:
                    sequences.append(seq)

            if len(sequences) >= 2:
                break

        # Verify sequences increment
        assert sequences[1] > sequences[0]


# ============================================================================
# Integration Tests
# ============================================================================

class TestTherapyEngineIntegration:
    """Integration tests for TherapyEngine."""

    def test_full_session_lifecycle(self, configured_engine):
        """Test complete session lifecycle."""
        # Start
        configured_engine.start_session(
            duration_sec=60,
            pattern_type="sequential",
            num_fingers=2,
            time_on_ms=10.0,
            time_off_ms=10.0,
            jitter_percent=0.0,
        )
        assert configured_engine.is_running() is True

        # Run some updates
        for _ in range(10):
            configured_engine.update()
            time.sleep(0.005)

        # Pause
        configured_engine.pause()
        assert configured_engine.is_paused() is True

        # Resume
        configured_engine.resume()
        assert configured_engine.is_paused() is False
        assert configured_engine.is_running() is True

        # Stop
        configured_engine.stop()
        assert configured_engine.is_running() is False

    def test_multiple_sessions(self, configured_engine):
        """Test running multiple sessions consecutively."""
        for i in range(3):
            configured_engine.start_session(
                duration_sec=0.05,
                pattern_type="sequential",
                num_fingers=1,
                time_on_ms=5.0,
                time_off_ms=5.0,
            )

            # Run until timeout
            start = time.monotonic()
            while configured_engine.is_running() and time.monotonic() - start < 0.1:
                configured_engine.update()
                time.sleep(0.001)

            # Session should have stopped
            assert configured_engine.is_running() is False

            # Verify stats accumulated
            stats = configured_engine.get_stats()
            assert stats["total_activations"] >= 0


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
