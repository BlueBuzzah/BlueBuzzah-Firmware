"""
Sync Accuracy Test for BlueBuzzah v2.0
========================================
Validates SimpleSyncProtocol bilateral synchronization accuracy.

CRITICAL-006: Sync Accuracy Validation Requirements
----------------------------------------------------
- Measure actual sync accuracy over 2-hour session
- Calculate statistics: mean, median, P95, P99, max latency
- Document clinical acceptability (target: <50ms P99)
- Update documentation with realistic measured values

Usage:
------
1. Set up PRIMARY and SECONDARY devices
2. Establish BLE connection
3. Run this script on PRIMARY device
4. Monitor output and check sync_latency_log.txt

Expected Results:
-----------------
- Mean latency: ~24-25ms
- Median latency: ~23ms
- P95 latency: <35ms
- P99 latency: <50ms
- Maximum latency: <70ms

If P99 > 50ms: May need hardware GPIO sync instead of BLE
"""

import time
import gc


class SyncAccuracyValidator:
    """
    Validates bilateral synchronization accuracy by measuring
    PRIMARY→SECONDARY→PRIMARY round-trip latency.
    """

    def __init__(self):
        """Initialize validator with statistics tracking."""
        self.latencies = []
        self.log_file = None

    def measure_single_latency(self, primary_conn, secondary_conn):
        """
        Measure single round-trip latency for EXECUTE_BUZZ command.

        Args:
            primary_conn: BLE connection to SECONDARY
            secondary_conn: Not needed (receives on SECONDARY)

        Returns:
            Latency in milliseconds, or None if measurement failed
        """
        try:
            # Send EXECUTE_BUZZ with timestamp
            send_time = time.monotonic() * 1000  # Convert to milliseconds

            command = f"EXECUTE_BUZZ:{send_time}:0:50\x04"  # EOT terminated
            primary_conn.write(command.encode('utf-8'))

            # Wait for BUZZ_COMPLETE response (with timeout)
            start_wait = time.monotonic()
            timeout = 0.1  # 100ms timeout

            buffer = ""
            while time.monotonic() - start_wait < timeout:
                if primary_conn.in_waiting > 0:
                    data = primary_conn.read()
                    if data:
                        buffer += data.decode('utf-8')
                        if '\x04' in buffer:
                            # Got complete response
                            eot_index = buffer.index('\x04')
                            response = buffer[:eot_index]

                            # Parse BUZZ_COMPLETE response
                            if response.startswith("BUZZ_COMPLETE"):
                                receive_time = time.monotonic() * 1000
                                latency = receive_time - send_time
                                return latency

                time.sleep(0.001)  # 1ms sleep

            return None  # Timeout

        except Exception as e:
            print(f"[ERROR] Latency measurement failed: {e}")
            return None

    def run_validation_session(self, primary_conn, duration_minutes=120):
        """
        Run extended validation session measuring sync latency.

        Args:
            primary_conn: BLE UART connection to SECONDARY
            duration_minutes: Test duration (default: 120 = 2 hours)
        """
        print("\n" + "=" * 60)
        print(f"SYNC ACCURACY VALIDATION ({duration_minutes} minutes)")
        print("=" * 60)

        # Open log file
        try:
            self.log_file = open("/sync_latency_log.txt", "w")
            self.log_file.write("timestamp_sec,latency_ms\n")
        except Exception as e:
            print(f"Warning: Could not open log file: {e}")

        start_time = time.monotonic()
        end_time = start_time + (duration_minutes * 60)
        measurement_interval = 1.0  # Measure every 1 second
        last_measurement = start_time

        print(f"\nMeasuring sync latency every {measurement_interval}s for {duration_minutes} minutes")
        print("Press Ctrl+C to stop early\n")

        measurement_count = 0

        try:
            while time.monotonic() < end_time:
                current_time = time.monotonic()

                # Take measurement every interval
                if current_time - last_measurement >= measurement_interval:
                    latency = self.measure_single_latency(primary_conn, None)

                    if latency is not None:
                        self.latencies.append(latency)
                        measurement_count += 1
                        elapsed = int(current_time - start_time)

                        # Console output (every 60th measurement)
                        if measurement_count % 60 == 0:
                            print(f"[{elapsed:5d}s] Measurements: {measurement_count:4d} | " +
                                  f"Latest: {latency:5.1f}ms | " +
                                  f"Mean: {sum(self.latencies) / len(self.latencies):5.1f}ms")

                        # Log to file
                        if self.log_file:
                            self.log_file.write(f"{elapsed},{latency:.2f}\n")
                            if measurement_count % 10 == 0:
                                self.log_file.flush()

                    last_measurement = current_time

                time.sleep(0.01)

        except KeyboardInterrupt:
            print("\n\nValidation stopped by user")

        finally:
            if self.log_file:
                self.log_file.close()

            # Generate statistics report
            self._generate_report(time.monotonic() - start_time)

    def _generate_report(self, elapsed_seconds):
        """Generate final statistics report."""
        print("\n" + "=" * 60)
        print("SYNC ACCURACY VALIDATION COMPLETE")
        print("=" * 60)

        if len(self.latencies) == 0:
            print("ERROR: No measurements collected")
            return

        # Calculate statistics
        sorted_latencies = sorted(self.latencies)
        n = len(sorted_latencies)

        mean = sum(sorted_latencies) / n
        median = sorted_latencies[n // 2]
        p95 = sorted_latencies[int(n * 0.95)]
        p99 = sorted_latencies[int(n * 0.99)]
        minimum = sorted_latencies[0]
        maximum = sorted_latencies[-1]

        # Print report
        print(f"\nTest Duration: {elapsed_seconds:.0f} seconds ({elapsed_seconds / 60:.1f} minutes)")
        print(f"Measurements: {n}")
        print(f"\nLatency Statistics:")
        print(f"  Mean:    {mean:6.2f} ms")
        print(f"  Median:  {median:6.2f} ms")
        print(f"  P95:     {p95:6.2f} ms")
        print(f"  P99:     {p99:6.2f} ms")
        print(f"  Min:     {minimum:6.2f} ms")
        print(f"  Max:     {maximum:6.2f} ms")

        # Clinical acceptability check
        print(f"\nClinical Acceptability (target: P99 < 50ms):")
        if p99 < 50:
            print(f"  ✓ PASS: P99 latency ({p99:.2f}ms) < 50ms threshold")
        else:
            print(f"  ✗ FAIL: P99 latency ({p99:.2f}ms) >= 50ms threshold")
            print(f"  RECOMMENDATION: Consider hardware GPIO sync instead of BLE")

        # Documentation update reminder
        print(f"\nDocumentation Update Required:")
        print(f"  Update docs/SYNCHRONIZATION_PROTOCOL.md with:")
        print(f"  - Mean latency: {mean:.1f}ms")
        print(f"  - Median latency: {median:.1f}ms")
        print(f"  - P95 latency: {p95:.1f}ms")
        print(f"  - P99 latency: {p99:.1f}ms")
        print(f"  - Maximum latency: {maximum:.1f}ms")


def run_quick_test(ble_connection, samples=100):
    """
    Run quick 100-sample test for initial validation.

    Args:
        ble_connection: BLE UART connection to SECONDARY
        samples: Number of samples to collect (default: 100)
    """
    print("\n" + "=" * 60)
    print(f"QUICK SYNC ACCURACY TEST ({samples} samples)")
    print("=" * 60)

    validator = SyncAccuracyValidator()

    print(f"\nCollecting {samples} latency measurements...")

    for i in range(samples):
        latency = validator.measure_single_latency(ble_connection, None)
        if latency:
            validator.latencies.append(latency)

        if (i + 1) % 10 == 0:
            print(f"  Progress: {i + 1}/{samples}")

        time.sleep(0.1)  # 100ms between measurements

    validator._generate_report(samples * 0.1)


# Example usage (uncomment when running on hardware):
"""
# Connect to SECONDARY device first
import _bleio
from adafruit_ble.uart import UARTService

# ... establish BLE connection to SECONDARY ...

# Run quick test (100 samples, ~10 seconds)
run_quick_test(secondary_uart_connection, samples=100)

# Or run full 2-hour validation
validator = SyncAccuracyValidator()
validator.run_validation_session(secondary_uart_connection, duration_minutes=120)
"""
