"""
Utils Module
============
Common utility functions and classes for BlueBuzzah v2.

This module provides reusable utilities for:
- Validation helpers for common operations
- Timing utilities (Timer, Stopwatch, Timeout)
- Memory utilities (MemoryMonitor, gc helpers)

All utilities are CircuitPython-compatible and avoid standard library
dependencies that aren't available in the CircuitPython environment.

Modules:
    validation: Common validation functions for pins, voltages, etc.
    timing: Timing utilities (Timer, Stopwatch, Timeout)
    memory: Memory monitoring and garbage collection utilities

Example:
    from utils.validation import validate_range
    from utils.timing import Timer, Stopwatch
    from utils.memory import MemoryMonitor

Module: utils
Version: 2.0.0
"""

from utils.validation import (
    validate_range,
    validate_voltage,
    validate_pin,
    validate_i2c_address
)

from utils.timing import (
    Timer,
    Stopwatch,
    Timeout,
    monotonic_ms,
    sleep_ms
)

from utils.memory import (
    gc_collect,
    get_memory_info,
    MemoryMonitor
)

__all__ = [
    # Validation
    "validate_range",
    "validate_voltage",
    "validate_pin",
    "validate_i2c_address",
    # Timing
    "Timer",
    "Stopwatch",
    "Timeout",
    "monotonic_ms",
    "sleep_ms",
    # Memory
    "gc_collect",
    "get_memory_info",
    "MemoryMonitor",
]

__version__ = "2.0.0"
