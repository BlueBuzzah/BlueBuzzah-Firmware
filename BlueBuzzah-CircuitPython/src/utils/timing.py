"""
Timing Utilities
================
Timing utilities for BlueBuzzah v2 firmware.

Provides timing helpers compatible with both CircuitPython and standard Python:
- monotonic_ms(): Monotonic time in milliseconds
- sleep_ms(): Sleep for milliseconds
- Timer: Context manager for timing code blocks
- Stopwatch: Start/stop timer for measuring intervals
- Timeout: Expiration timer for deadline checks

Module: utils.timing
Version: 2.0.0
"""

import time


def monotonic_ms():
    """
    Return monotonic time in milliseconds.

    Returns:
        float: Current monotonic time in milliseconds
    """
    return time.monotonic() * 1000


def sleep_ms(ms):
    """
    Sleep for given milliseconds.

    Args:
        ms: Duration to sleep in milliseconds
    """
    time.sleep(ms / 1000)


class Timer:
    """
    Context manager for timing code blocks.

    Usage:
        timer = Timer("operation", silent=True)
        with timer:
            do_something()
        print(f"Took {timer.elapsed_ms()}ms")
    """

    def __init__(self, name="timer", silent=False):
        """
        Initialize timer.

        Args:
            name: Name for logging output
            silent: If True, suppress automatic print on exit
        """
        self.name = name
        self.silent = silent
        self.elapsed_sec = None
        self._start = None

    def __enter__(self):
        self._start = time.monotonic()
        return self

    def __exit__(self, *args):
        self.elapsed_sec = time.monotonic() - self._start
        if not self.silent:
            print("[{}] {:.3f}s".format(self.name, self.elapsed_sec))

    def elapsed_ms(self):
        """
        Return elapsed time in milliseconds.

        Returns:
            float: Elapsed time in milliseconds, or 0 if not yet measured
        """
        return self.elapsed_sec * 1000 if self.elapsed_sec else 0


class Stopwatch:
    """
    Start/stop timer for measuring intervals.

    Usage:
        stopwatch = Stopwatch()
        stopwatch.start()
        do_something()
        print(f"Elapsed: {stopwatch.elapsed_ms()}ms")
        stopwatch.stop()
    """

    def __init__(self):
        """Initialize stopwatch in stopped state."""
        self.start_time = None
        self._running = False

    def start(self):
        """Start the stopwatch."""
        self.start_time = time.monotonic()
        self._running = True

    def stop(self):
        """Stop the stopwatch."""
        self._running = False

    def reset(self):
        """Reset the stopwatch to initial state."""
        self.start_time = None
        self._running = False

    def is_running(self):
        """
        Check if stopwatch is running.

        Returns:
            bool: True if running, False if stopped
        """
        return self._running

    def elapsed_ms(self):
        """
        Return elapsed time in milliseconds.

        Returns:
            float: Elapsed time since start, or 0 if never started
        """
        if self.start_time is None:
            return 0
        return (time.monotonic() - self.start_time) * 1000


class Timeout:
    """
    Expiration timer for deadline checks.

    Usage:
        timeout = Timeout(5.0)  # 5 second timeout
        while not timeout.expired():
            if try_operation():
                break
            print(f"Remaining: {timeout.remaining_ms()}ms")
    """

    def __init__(self, timeout_sec):
        """
        Initialize timeout.

        Args:
            timeout_sec: Timeout duration in seconds
        """
        self.timeout_sec = timeout_sec
        self._start = time.monotonic()

    def expired(self):
        """
        Check if timeout has expired.

        Returns:
            bool: True if timeout has elapsed
        """
        return (time.monotonic() - self._start) >= self.timeout_sec

    def remaining_ms(self):
        """
        Return remaining time in milliseconds.

        Returns:
            float: Remaining time, or 0 if expired
        """
        remaining = self.timeout_sec - (time.monotonic() - self._start)
        return max(0, remaining * 1000)

    def reset(self):
        """Reset the timeout to start counting again."""
        self._start = time.monotonic()
