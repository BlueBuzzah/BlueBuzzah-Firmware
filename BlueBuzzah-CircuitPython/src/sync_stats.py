"""
Sync Statistics Collection
===========================
Collects and analyzes synchronization timing statistics for validation.

This module tracks timing metrics for bilateral synchronization to validate
that the SimpleSyncProtocol meets the <10ms latency requirement for vCR therapy.

Metrics tracked:
- Network latency (PRIMARY timestamp → SECONDARY receive)
- Execution time (SECONDARY receive → motor activation complete)
- Total latency (PRIMARY timestamp → SECONDARY execution complete)

Module: sync_stats
Version: 2.0.0
"""

import time


class SyncStats:
    """
    Collect synchronization timing statistics for validation.

    Tracks timing metrics across the bilateral sync flow:
    1. Network Latency: BLE transmission time from PRIMARY to SECONDARY
    2. Execution Time: Motor activation processing time on SECONDARY
    3. Total Latency: End-to-end synchronization accuracy

    Uses circular buffers to prevent unbounded memory growth.
    """

    def __init__(self, max_samples=100):
        """
        Initialize statistics collector with circular buffer.

        Args:
            max_samples: Maximum number of samples to retain (default 100)
        """
        self.max_samples = max_samples

        # Circular buffers (lists for CircuitPython compatibility)
        self.network_latency_samples = []
        self.execution_time_samples = []
        self.total_latency_samples = []

        # Statistics
        self.sample_count = 0
        self.last_sample_time = None

    def add_sample(self, network_latency_us, execution_time_us, total_latency_us):
        """
        Add timing sample to statistics.

        Args:
            network_latency_us: Network latency in microseconds
            execution_time_us: Execution time in microseconds
            total_latency_us: Total latency in microseconds
        """
        # Add to buffers
        self.network_latency_samples.append(network_latency_us)
        self.execution_time_samples.append(execution_time_us)
        self.total_latency_samples.append(total_latency_us)

        # Maintain max size (simple circular buffer)
        if len(self.network_latency_samples) > self.max_samples:
            self.network_latency_samples.pop(0)
            self.execution_time_samples.pop(0)
            self.total_latency_samples.pop(0)

        self.sample_count += 1
        self.last_sample_time = time.monotonic()

    def get_statistics(self):
        """
        Calculate statistics for all timing measurements.

        Returns:
            dict with mean, median, min, max, p95, p99 for each metric,
            or None if no samples collected
        """
        if not self.network_latency_samples:
            return None

        stats = {}

        # Calculate for each metric
        for name, samples in [
            ("network_latency", self.network_latency_samples),
            ("execution_time", self.execution_time_samples),
            ("total_latency", self.total_latency_samples),
        ]:
            sorted_samples = sorted(samples)
            n = len(sorted_samples)

            stats[name] = {
                "mean": sum(sorted_samples) / n,
                "median": sorted_samples[n // 2],
                "min": sorted_samples[0],
                "max": sorted_samples[-1],
                "p95": sorted_samples[int(n * 0.95)] if n > 20 else sorted_samples[-1],
                "p99": sorted_samples[int(n * 0.99)] if n > 100 else sorted_samples[-1],
                "sample_count": n,
            }

        return stats

    def print_report(self):
        """Print formatted statistics report to console."""
        stats = self.get_statistics()

        if not stats:
            print("[SYNC_STATS] No samples collected yet")
            return

        print("\n" + "="*60)
        print("SYNCHRONIZATION TIMING STATISTICS")
        print("="*60)
        print("Total samples: {}".format(self.sample_count))
        print("Buffer size: {}/{}".format(len(self.network_latency_samples), self.max_samples))

        if self.last_sample_time:
            elapsed = time.monotonic() - self.last_sample_time
            print("Last sample: {:.1f}s ago".format(elapsed))

        print()

        for metric_name, metric_stats in stats.items():
            # Format metric name
            display_name = metric_name.upper().replace('_', ' ')
            print("{}:".format(display_name))
            print("  Mean:   {:8.2f} us ({:6.3f} ms)".format(metric_stats['mean'], metric_stats['mean']/1000))
            print("  Median: {:8.2f} us ({:6.3f} ms)".format(metric_stats['median'], metric_stats['median']/1000))
            print("  Min:    {:8.2f} us ({:6.3f} ms)".format(metric_stats['min'], metric_stats['min']/1000))
            print("  Max:    {:8.2f} us ({:6.3f} ms)".format(metric_stats['max'], metric_stats['max']/1000))
            print("  P95:    {:8.2f} us ({:6.3f} ms)".format(metric_stats['p95'], metric_stats['p95']/1000))
            print("  P99:    {:8.2f} us ({:6.3f} ms)".format(metric_stats['p99'], metric_stats['p99']/1000))
            print()

        # Check against vCR therapy requirements
        total_mean_ms = stats['total_latency']['mean'] / 1000
        total_p95_ms = stats['total_latency']['p95'] / 1000
        total_p99_ms = stats['total_latency']['p99'] / 1000

        print("vCR THERAPY REQUIREMENTS (< 10ms total latency):")
        mean_status = "PASS" if total_mean_ms < 10 else "FAIL"
        p95_status = "PASS" if total_p95_ms < 10 else "FAIL"
        p99_status = "PASS" if total_p99_ms < 10 else "FAIL"
        print("  Mean:   {} ({:.3f} ms)".format(mean_status, total_mean_ms))
        print("  P95:    {} ({:.3f} ms)".format(p95_status, total_p95_ms))
        print("  P99:    {} ({:.3f} ms)".format(p99_status, total_p99_ms))

        print("="*60 + "\n")

    def reset(self):
        """Reset all statistics."""
        self.network_latency_samples.clear()
        self.execution_time_samples.clear()
        self.total_latency_samples.clear()
        self.sample_count = 0
        self.last_sample_time = None
        print("[SYNC_STATS] Statistics reset")
