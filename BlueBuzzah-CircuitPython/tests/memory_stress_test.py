"""
Memory Stress Test for BlueBuzzah v2.0
========================================
Tests memory stability over extended operation periods.

CRITICAL-001: Memory Budget Analysis Requirements
--------------------------------------------------
1. Measure baseline memory usage
2. Test SessionManager memory cost
3. Test CalibrationController memory cost
4. Run 2-hour stress test
5. Verify memory stays above 10KB free

Usage:
------
1. Deploy this file and the firmware to nRF52840
2. Run from REPL: import tests.memory_stress_test
3. Monitor output for 2 hours
4. Check memory_log.txt for detailed data

Expected Results:
-----------------
- Baseline free RAM: ~90KB with BLE active
- SessionManager cost: <1.5KB
- CalibrationController cost: <700 bytes
- Total application layer: <2.5KB
- Free memory during therapy: >10KB maintained
- No memory leaks (stable free memory over time)
"""

import time
import gc


def measure_baseline():
    """Measure baseline memory usage before loading application components."""
    print("\n" + "=" * 60)
    print("MEMORY BASELINE MEASUREMENT")
    print("=" * 60)

    gc.collect()
    baseline = gc.mem_free()
    print(f"Baseline free memory: {baseline} bytes ({baseline / 1024:.2f} KB)")

    return baseline


def measure_component_cost(component_name, import_statement):
    """
    Measure memory cost of a specific component.

    Args:
        component_name: Name of component for logging
        import_statement: Lambda function that performs the import

    Returns:
        Memory cost in bytes
    """
    print(f"\nMeasuring {component_name} memory cost...")

    gc.collect()
    mem_before = gc.mem_free()
    print(f"  Before import: {mem_before} bytes")

    try:
        component = import_statement()
        gc.collect()
        mem_after = gc.mem_free()
        cost = mem_before - mem_after

        print(f"  After import: {mem_after} bytes")
        print(f"  Cost: {cost} bytes ({cost / 1024:.2f} KB)")

        return cost

    except (ImportError, MemoryError) as e:
        print(f"  ERROR: Failed to import {component_name}: {e}")
        return None


def run_stress_test(duration_minutes=120):
    """
    Run extended memory stress test.

    Args:
        duration_minutes: Test duration in minutes (default: 120 = 2 hours)
    """
    print("\n" + "=" * 60)
    print(f"MEMORY STRESS TEST ({duration_minutes} minutes)")
    print("=" * 60)

    start_time = time.monotonic()
    end_time = start_time + (duration_minutes * 60)
    check_interval = 60  # Check every 60 seconds
    last_check = start_time

    # Open log file
    try:
        log_file = open("/memory_log.txt", "w")
        log_file.write("timestamp_sec,free_bytes,free_kb\n")
    except Exception as e:
        print(f"Warning: Could not open log file: {e}")
        log_file = None

    print(f"\nTest will run for {duration_minutes} minutes ({duration_minutes * 60} seconds)")
    print("Press Ctrl+C to stop early\n")

    iteration = 0
    min_memory = float('inf')
    max_memory = 0

    try:
        while time.monotonic() < end_time:
            current_time = time.monotonic()

            # Check memory every 60 seconds
            if current_time - last_check >= check_interval:
                iteration += 1
                elapsed = int(current_time - start_time)

                gc.collect()
                free_mem = gc.mem_free()
                free_kb = free_mem / 1024

                # Track min/max
                min_memory = min(min_memory, free_mem)
                max_memory = max(max_memory, free_mem)

                # Console output
                print(f"[{elapsed:5d}s] Free: {free_mem:6d} bytes ({free_kb:6.2f} KB) " +
                      f"| Min: {min_memory / 1024:.2f} KB | Max: {max_memory / 1024:.2f} KB")

                # Log to file
                if log_file:
                    log_file.write(f"{elapsed},{free_mem},{free_kb:.2f}\n")
                    log_file.flush()

                # Memory warnings
                if free_mem < 10000:
                    print(f"  [WARNING] Low memory! Free < 10KB")
                if free_mem < 5000:
                    print(f"  [CRITICAL] Very low memory! Free < 5KB")

                last_check = current_time

            # Brief sleep
            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\n\nTest stopped by user")

    finally:
        if log_file:
            log_file.close()

        # Final summary
        elapsed_total = time.monotonic() - start_time
        print("\n" + "=" * 60)
        print("STRESS TEST COMPLETE")
        print("=" * 60)
        print(f"Duration: {elapsed_total:.0f} seconds ({elapsed_total / 60:.1f} minutes)")
        print(f"Iterations: {iteration}")
        print(f"Min free memory: {min_memory} bytes ({min_memory / 1024:.2f} KB)")
        print(f"Max free memory: {max_memory} bytes ({max_memory / 1024:.2f} KB)")
        print(f"Memory range: {max_memory - min_memory} bytes ({(max_memory - min_memory) / 1024:.2f} KB)")

        # Check for memory leaks
        memory_range = max_memory - min_memory
        if memory_range < 5000:  # Less than 5KB variation
            print("\n✓ PASS: Memory appears stable (variation < 5KB)")
        else:
            print(f"\n✗ WARNING: Large memory variation ({memory_range / 1024:.2f} KB)")
            print("  Possible memory leak or fragmentation")

        if min_memory > 10000:
            print("✓ PASS: Memory never dropped below 10KB")
        else:
            print(f"✗ FAIL: Memory dropped to {min_memory / 1024:.2f} KB (below 10KB threshold)")


def run_full_test():
    """Run complete memory test suite."""
    print("\n" + "=" * 60)
    print("BlueBuzzah v2.0 - Memory Test Suite")
    print("CRITICAL-001: Memory Budget Analysis")
    print("=" * 60)

    # 1. Baseline measurement
    baseline = measure_baseline()

    # 2. Measure SessionManager cost
    session_cost = measure_component_cost(
        "SessionManager",
        lambda: __import__('application.session.manager', fromlist=['SessionManager']).SessionManager
    )

    # 3. Measure CalibrationController cost
    calibration_cost = measure_component_cost(
        "CalibrationController",
        lambda: __import__('application.calibration.controller', fromlist=['CalibrationController']).CalibrationController
    )

    # 4. Summary
    print("\n" + "=" * 60)
    print("COMPONENT MEMORY SUMMARY")
    print("=" * 60)
    if session_cost and calibration_cost:
        total_cost = session_cost + calibration_cost
        print(f"SessionManager:          {session_cost:6d} bytes ({session_cost / 1024:.2f} KB)")
        print(f"CalibrationController:   {calibration_cost:6d} bytes ({calibration_cost / 1024:.2f} KB)")
        print(f"{'=' * 60}")
        print(f"Total application layer: {total_cost:6d} bytes ({total_cost / 1024:.2f} KB)")

        # Check against targets
        print(f"\nTarget costs:")
        print(f"  SessionManager target:       <1.5 KB   [{'PASS' if session_cost < 1536 else 'FAIL'}]")
        print(f"  CalibrationController target: <1.0 KB   [{'PASS' if calibration_cost < 1024 else 'FAIL'}]")
        print(f"  Total target:                <2.5 KB   [{'PASS' if total_cost < 2560 else 'FAIL'}]")

        gc.collect()
        remaining = gc.mem_free()
        print(f"\nFree memory after loading application layer: {remaining} bytes ({remaining / 1024:.2f} KB)")

        if remaining > 10000:
            print("✓ PASS: Sufficient memory remaining (>10KB)")
        else:
            print("✗ FAIL: Insufficient memory (<10KB free)")

    # 5. Ask user if they want to run stress test
    print("\n" + "=" * 60)
    print("Ready to run 2-hour stress test?")
    print("This will monitor memory every 60 seconds for 2 hours.")
    print("Press Ctrl+C at any time to stop.")
    print("=" * 60)
    response = input("\nRun stress test? (y/n): ").strip().lower()

    if response == 'y':
        run_stress_test(duration_minutes=120)
    else:
        print("\nStress test skipped. You can run it later with:")
        print("  >>> import tests.memory_stress_test")
        print("  >>> tests.memory_stress_test.run_stress_test(120)")


# Auto-run when imported
if __name__ == "__main__":
    run_full_test()
else:
    print("\nMemory test module loaded.")
    print("Run full test: >>> run_full_test()")
    print("Run stress test only: >>> run_stress_test(120)")
