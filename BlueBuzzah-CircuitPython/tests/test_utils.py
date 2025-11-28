"""
Tests for Utils Module
======================
Unit tests for utilities in utils/ module.

Tests all utilities:
- Logging: Logger, get_logger
- Timing: Timer, Stopwatch, Timeout
- Memory: get_memory_info, MemoryMonitor, gc_collect
- Validation: All validation functions

Run with: python -m pytest tests/test_utils.py -v

Module: tests.test_utils
Version: 2.0.0
"""

import pytest
import time
import sys
from pathlib import Path
from io import StringIO

# Add src directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

# Import utilities
from utils.timing import Timer, Stopwatch, Timeout, sleep_ms, monotonic_ms
from utils.memory import get_memory_info, MemoryMonitor, gc_collect
from utils.validation import (
    validate_range,
    validate_voltage,
    validate_pin,
    validate_i2c_address,
    validate_actuator_index,
    validate_amplitude,
    validate_frequency,
)


# ============================================================================
# Timing Tests
# ============================================================================

class TestTiming:
    """Test timing utilities."""

    def test_monotonic_ms(self):
        """Test monotonic millisecond timer."""
        start = monotonic_ms()
        time.sleep(0.05)  # 50ms
        end = monotonic_ms()

        elapsed = end - start
        assert 40 <= elapsed <= 70  # Allow some tolerance

    def test_sleep_ms(self):
        """Test millisecond sleep."""
        start = time.monotonic()
        sleep_ms(50)
        end = time.monotonic()

        elapsed = (end - start) * 1000
        assert 40 <= elapsed <= 70

    def test_timer_context(self):
        """Test timer context manager."""
        timer = Timer("test", silent=True)

        with timer:
            time.sleep(0.05)

        assert timer.elapsed_sec is not None
        assert 0.04 <= timer.elapsed_sec <= 0.07

    def test_timer_elapsed_ms(self):
        """Test timer millisecond elapsed time."""
        timer = Timer("test", silent=True)

        with timer:
            time.sleep(0.05)

        elapsed_ms = timer.elapsed_ms()
        assert 40 <= elapsed_ms <= 70

    def test_stopwatch(self):
        """Test stopwatch functionality."""
        stopwatch = Stopwatch()
        assert not stopwatch.is_running()

        stopwatch.start()
        assert stopwatch.is_running()

        time.sleep(0.05)
        elapsed = stopwatch.elapsed_ms()
        assert 40 <= elapsed <= 70

        stopwatch.stop()
        assert not stopwatch.is_running()

    def test_stopwatch_reset(self):
        """Test stopwatch reset."""
        stopwatch = Stopwatch()
        stopwatch.start()
        time.sleep(0.02)
        stopwatch.reset()

        assert not stopwatch.is_running()
        assert stopwatch.start_time is None

    def test_timeout(self):
        """Test timeout functionality."""
        timeout = Timeout(0.1)  # 100ms timeout

        assert not timeout.expired()
        time.sleep(0.05)
        assert not timeout.expired()

        time.sleep(0.06)
        assert timeout.expired()

    def test_timeout_remaining(self):
        """Test timeout remaining time."""
        timeout = Timeout(0.1)

        remaining = timeout.remaining_ms()
        assert 90 <= remaining <= 110

        time.sleep(0.05)
        remaining = timeout.remaining_ms()
        assert 40 <= remaining <= 60

    def test_timeout_reset(self):
        """Test timeout reset."""
        timeout = Timeout(0.1)
        time.sleep(0.05)

        timeout.reset()
        remaining = timeout.remaining_ms()
        assert 90 <= remaining <= 110


# ============================================================================
# Memory Tests
# ============================================================================

class TestMemory:
    """Test memory utilities."""

    def test_get_memory_info(self):
        """Test memory info retrieval."""
        mem_info = get_memory_info()

        assert "free" in mem_info
        assert "allocated" in mem_info
        assert mem_info["free"] > 0
        assert mem_info["allocated"] >= 0

    def test_gc_collect(self):
        """Test garbage collection."""
        result = gc_collect()
        assert result is True  # Always collects without threshold

    def test_gc_collect_with_threshold(self):
        """Test threshold-based garbage collection."""
        # High threshold - should collect
        result = gc_collect(threshold=1000000)
        assert result is True

    def test_memory_monitor(self):
        """Test memory monitor."""
        monitor = MemoryMonitor(max_samples=5, auto_gc=True)

        for i in range(5):
            monitor.record()

        assert monitor.sample_count() == 5

        stats = monitor.stats()
        assert "min_free" in stats
        assert "max_free" in stats
        assert "avg_free" in stats

    def test_memory_monitor_clear(self):
        """Test memory monitor clear."""
        monitor = MemoryMonitor()
        monitor.record()
        monitor.record()

        assert monitor.sample_count() == 2

        monitor.clear()
        assert monitor.sample_count() == 0

    def test_memory_monitor_peak(self):
        """Test memory monitor peak allocated."""
        monitor = MemoryMonitor()

        for i in range(3):
            monitor.record()

        peak = monitor.peak_allocated()
        assert peak > 0


# ============================================================================
# Validation Tests
# ============================================================================

class TestValidation:
    """Test validation utilities."""

    def test_validate_range_valid(self):
        """Test range validation with valid value."""
        result = validate_range(50, 0, 100, "test")
        assert result == 50

    def test_validate_range_invalid(self):
        """Test range validation with invalid value."""
        with pytest.raises(ValueError) as exc_info:
            validate_range(150, 0, 100, "test")

        assert "test must be between 0 and 100" in str(exc_info.value)

    def test_validate_voltage_valid(self):
        """Test voltage validation with valid value."""
        result = validate_voltage(3.7)
        assert result == 3.7

    def test_validate_voltage_invalid_high(self):
        """Test voltage validation with too high value."""
        with pytest.raises(ValueError) as exc_info:
            validate_voltage(5.0)

        assert "voltage must be between" in str(exc_info.value)

    def test_validate_voltage_invalid_low(self):
        """Test voltage validation with too low value."""
        with pytest.raises(ValueError) as exc_info:
            validate_voltage(2.5)

        assert "voltage must be between" in str(exc_info.value)

    def test_validate_pin_number(self):
        """Test pin validation with number."""
        result = validate_pin(13, "LED pin")
        assert result == 13

    def test_validate_pin_string(self):
        """Test pin validation with string."""
        result = validate_pin("D13", "LED pin")
        assert result == "D13"

    def test_validate_pin_negative(self):
        """Test pin validation with negative number."""
        with pytest.raises(ValueError) as exc_info:
            validate_pin(-1, "test pin")

        assert "must be non-negative" in str(exc_info.value)

    def test_validate_pin_with_whitelist(self):
        """Test pin validation with whitelist."""
        result = validate_pin("A6", "battery", valid_pins=["A6", "A7"])
        assert result == "A6"

        with pytest.raises(ValueError) as exc_info:
            validate_pin("A5", "battery", valid_pins=["A6", "A7"])

        assert "must be one of" in str(exc_info.value)

    def test_validate_i2c_address_valid(self):
        """Test I2C address validation with valid address."""
        result = validate_i2c_address(0x5A, "DRV2605")
        assert result == 0x5A

    def test_validate_i2c_address_invalid_low(self):
        """Test I2C address validation with reserved address."""
        with pytest.raises(ValueError) as exc_info:
            validate_i2c_address(0x05, "device")

        assert "must be between 0x08 and 0x77" in str(exc_info.value)

    def test_validate_i2c_address_invalid_high(self):
        """Test I2C address validation with reserved address."""
        with pytest.raises(ValueError) as exc_info:
            validate_i2c_address(0x78, "device")

        assert "must be between 0x08 and 0x77" in str(exc_info.value)

    def test_validate_actuator_index_valid(self):
        """Test actuator index validation with valid index."""
        result = validate_actuator_index(2)
        assert result == 2

    def test_validate_actuator_index_invalid(self):
        """Test actuator index validation with invalid index."""
        with pytest.raises(ValueError) as exc_info:
            validate_actuator_index(5)

        assert "must be between 0 and 4" in str(exc_info.value)

    def test_validate_amplitude_valid(self):
        """Test amplitude validation with valid value."""
        result = validate_amplitude(75)
        assert result == 75

    def test_validate_amplitude_invalid(self):
        """Test amplitude validation with invalid value."""
        with pytest.raises(ValueError) as exc_info:
            validate_amplitude(150)

        assert "amplitude must be between" in str(exc_info.value)

    def test_validate_frequency_valid(self):
        """Test frequency validation with valid value."""
        result = validate_frequency(175)
        assert result == 175

    def test_validate_frequency_invalid(self):
        """Test frequency validation with invalid value."""
        with pytest.raises(ValueError) as exc_info:
            validate_frequency(300)

        assert "frequency must be between" in str(exc_info.value)
