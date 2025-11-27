"""
Memory Utilities
================
Memory management utilities for BlueBuzzah v2 firmware.

Provides memory helpers compatible with both CircuitPython and standard Python:
- gc_collect(): Garbage collection with optional threshold
- get_memory_info(): Get current memory statistics
- MemoryMonitor: Track memory usage over time

Note: CircuitPython provides gc.mem_free() and gc.mem_alloc().
Standard Python does not have these, so fallbacks are provided.

Module: utils.memory
Version: 2.0.0
"""

import gc


def _get_mem_free():
    """Get free memory, with fallback for standard Python."""
    if hasattr(gc, 'mem_free'):
        return gc.mem_free()
    # Fallback for standard Python - return a reasonable default
    try:
        import psutil
        return psutil.virtual_memory().available
    except ImportError:
        # No psutil, return a placeholder value
        return 1000000  # 1MB placeholder


def _get_mem_alloc():
    """Get allocated memory, with fallback for standard Python."""
    if hasattr(gc, 'mem_alloc'):
        return gc.mem_alloc()
    # Fallback for standard Python
    try:
        import psutil
        import os
        process = psutil.Process(os.getpid())
        return process.memory_info().rss
    except ImportError:
        # No psutil, return a placeholder value
        return 50000  # 50KB placeholder


def gc_collect(threshold=None):
    """
    Run garbage collection.

    Args:
        threshold: Optional minimum free bytes to trigger collection.
                   If None, always collects.

    Returns:
        bool: True if collection was performed
    """
    if threshold is None:
        gc.collect()
        return True

    # Check if free memory is below threshold
    if _get_mem_free() < threshold:
        gc.collect()
    return True


def get_memory_info():
    """
    Get current memory information.

    Returns:
        dict: Dictionary with 'free' and 'allocated' keys (in bytes)
    """
    gc.collect()
    return {
        "free": _get_mem_free(),
        "allocated": _get_mem_alloc()
    }


class MemoryMonitor:
    """
    Track memory usage over time.

    Usage:
        monitor = MemoryMonitor(max_samples=100, auto_gc=True)

        for i in range(10):
            do_work()
            monitor.record()

        stats = monitor.stats()
        print(f"Min free: {stats['min_free']}")
        print(f"Max free: {stats['max_free']}")
        print(f"Avg free: {stats['avg_free']}")
    """

    def __init__(self, max_samples=100, auto_gc=False):
        """
        Initialize memory monitor.

        Args:
            max_samples: Maximum number of samples to retain
            auto_gc: If True, run gc.collect() before each sample
        """
        self.max_samples = max_samples
        self.auto_gc = auto_gc
        self._samples = []

    def record(self):
        """Record current memory state."""
        if self.auto_gc:
            gc.collect()

        info = get_memory_info()
        self._samples.append(info)

        # Limit samples to max_samples
        if len(self._samples) > self.max_samples:
            self._samples = self._samples[-self.max_samples:]

    def sample_count(self):
        """
        Return number of recorded samples.

        Returns:
            int: Number of samples recorded
        """
        return len(self._samples)

    def clear(self):
        """Clear all recorded samples."""
        self._samples = []

    def stats(self):
        """
        Return min/max/avg statistics for free memory.

        Returns:
            dict: Dictionary with 'min_free', 'max_free', 'avg_free' keys
        """
        if not self._samples:
            return {"min_free": 0, "max_free": 0, "avg_free": 0}

        free_values = [s["free"] for s in self._samples]
        return {
            "min_free": min(free_values),
            "max_free": max(free_values),
            "avg_free": sum(free_values) // len(free_values)
        }

    def peak_allocated(self):
        """
        Return peak allocated memory across all samples.

        Returns:
            int: Maximum allocated memory in bytes
        """
        if not self._samples:
            return 0
        return max(s["allocated"] for s in self._samples)
