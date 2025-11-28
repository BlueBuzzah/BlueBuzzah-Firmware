"""
SESSION Commands Test for BlueBuzzah v2.0
==========================================
Tests all 5 SESSION commands functionality.

CRITICAL-007: SESSION Commands Testing Requirements
----------------------------------------------------
Test all 5 commands:
1. SESSION_START - Start therapy session
2. SESSION_PAUSE - Pause ongoing session
3. SESSION_RESUME - Resume paused session
4. SESSION_STOP - Stop session and return stats
5. SESSION_STATUS - Get current session info

Usage:
------
1. Connect to PRIMARY device via BLE
2. Run: import tests.session_commands_test
3. Run: tests.session_commands_test.run_all_tests(ble_connection)

Expected Results:
-----------------
All 5 commands should work correctly with SessionManager enabled.
If SessionManager disabled, commands should return appropriate errors.
"""

import time


class SessionCommandsTester:
    """Tests SESSION_* commands on BlueBuzzah PRIMARY device."""

    def __init__(self, ble_conn):
        """
        Initialize tester.

        Args:
            ble_conn: BLE UART connection to PRIMARY device
        """
        self.ble = ble_conn
        self.test_results = {}

    def send_command(self, command):
        """
        Send command with EOT terminator and wait for response.

        Args:
            command: Command string (without EOT)

        Returns:
            Response string (without EOT), or None if timeout
        """
        try:
            # Send command with EOT
            message = command + '\x04'
            self.ble.write(message.encode('utf-8'))

            # Wait for response (max 5 seconds)
            start_time = time.monotonic()
            buffer = ""

            while time.monotonic() - start_time < 5.0:
                if self.ble.in_waiting > 0:
                    data = self.ble.read()
                    if data:
                        buffer += data.decode('utf-8')
                        if '\x04' in buffer:
                            eot_index = buffer.index('\x04')
                            response = buffer[:eot_index]
                            return response

                time.sleep(0.01)

            return None  # Timeout

        except Exception as e:
            print(f"    [ERROR] Command failed: {e}")
            return None

    def test_session_start(self):
        """Test SESSION_START command."""
        print("\nTest 1: SESSION_START")
        print("-" * 40)

        response = self.send_command("SESSION_START")

        if response:
            print(f"    Response: {response}")

            if "SESSION_STARTED" in response or "STARTED" in response:
                print("    ✓ PASS: Session started successfully")
                self.test_results['SESSION_START'] = 'PASS'
                return True
            elif "ERROR" in response or "FAIL" in response:
                print(f"    ✗ FAIL: Command returned error")
                self.test_results['SESSION_START'] = 'FAIL'
                return False
            else:
                print(f"    ? UNKNOWN: Unexpected response format")
                self.test_results['SESSION_START'] = 'UNKNOWN'
                return False
        else:
            print("    ✗ FAIL: No response (timeout)")
            self.test_results['SESSION_START'] = 'TIMEOUT'
            return False

    def test_session_pause(self):
        """Test SESSION_PAUSE command."""
        print("\nTest 2: SESSION_PAUSE")
        print("-" * 40)

        # Wait a bit for session to be running
        print("    Waiting 2 seconds for session to start...")
        time.sleep(2)

        response = self.send_command("SESSION_PAUSE")

        if response:
            print(f"    Response: {response}")

            if "PAUSED" in response:
                print("    ✓ PASS: Session paused successfully")
                self.test_results['SESSION_PAUSE'] = 'PASS'
                return True
            elif "ERROR" in response or "FAIL" in response:
                print(f"    ✗ FAIL: Command returned error")
                self.test_results['SESSION_PAUSE'] = 'FAIL'
                return False
            else:
                print(f"    ? UNKNOWN: Unexpected response format")
                self.test_results['SESSION_PAUSE'] = 'UNKNOWN'
                return False
        else:
            print("    ✗ FAIL: No response (timeout)")
            self.test_results['SESSION_PAUSE'] = 'TIMEOUT'
            return False

    def test_session_resume(self):
        """Test SESSION_RESUME command."""
        print("\nTest 3: SESSION_RESUME")
        print("-" * 40)

        # Wait a bit while paused
        print("    Waiting 2 seconds while paused...")
        time.sleep(2)

        response = self.send_command("SESSION_RESUME")

        if response:
            print(f"    Response: {response}")

            if "RESUMED" in response:
                print("    ✓ PASS: Session resumed successfully")
                self.test_results['SESSION_RESUME'] = 'PASS'
                return True
            elif "ERROR" in response or "FAIL" in response:
                print(f"    ✗ FAIL: Command returned error")
                self.test_results['SESSION_RESUME'] = 'FAIL'
                return False
            else:
                print(f"    ? UNKNOWN: Unexpected response format")
                self.test_results['SESSION_RESUME'] = 'UNKNOWN'
                return False
        else:
            print("    ✗ FAIL: No response (timeout)")
            self.test_results['SESSION_RESUME'] = 'TIMEOUT'
            return False

    def test_session_status(self):
        """Test SESSION_STATUS command."""
        print("\nTest 4: SESSION_STATUS")
        print("-" * 40)

        response = self.send_command("SESSION_STATUS")

        if response:
            print(f"    Response: {response}")

            # Check for expected status fields
            has_status = any(keyword in response for keyword in [
                "STATUS", "RUNNING", "PAUSED", "elapsed", "cycles"
            ])

            if has_status:
                print("    ✓ PASS: Session status received")
                self.test_results['SESSION_STATUS'] = 'PASS'
                return True
            elif "ERROR" in response or "FAIL" in response:
                print(f"    ✗ FAIL: Command returned error")
                self.test_results['SESSION_STATUS'] = 'FAIL'
                return False
            else:
                print(f"    ? UNKNOWN: Unexpected response format")
                self.test_results['SESSION_STATUS'] = 'UNKNOWN'
                return False
        else:
            print("    ✗ FAIL: No response (timeout)")
            self.test_results['SESSION_STATUS'] = 'TIMEOUT'
            return False

    def test_session_stop(self):
        """Test SESSION_STOP command."""
        print("\nTest 5: SESSION_STOP")
        print("-" * 40)

        # Wait a bit for more session activity
        print("    Waiting 3 seconds for session activity...")
        time.sleep(3)

        response = self.send_command("SESSION_STOP")

        if response:
            print(f"    Response: {response}")

            if "STOPPED" in response or "COMPLETE" in response:
                print("    ✓ PASS: Session stopped successfully")
                self.test_results['SESSION_STOP'] = 'PASS'
                return True
            elif "ERROR" in response or "FAIL" in response:
                print(f"    ✗ FAIL: Command returned error")
                self.test_results['SESSION_STOP'] = 'FAIL'
                return False
            else:
                print(f"    ? UNKNOWN: Unexpected response format")
                self.test_results['SESSION_STOP'] = 'UNKNOWN'
                return False
        else:
            print("    ✗ FAIL: No response (timeout)")
            self.test_results['SESSION_STOP'] = 'TIMEOUT'
            return False

    def generate_report(self):
        """Generate final test report."""
        print("\n" + "=" * 60)
        print("SESSION COMMANDS TEST REPORT")
        print("=" * 60)

        passed = sum(1 for result in self.test_results.values() if result == 'PASS')
        total = len(self.test_results)

        print(f"\nResults: {passed}/{total} tests passed\n")

        for command, result in self.test_results.items():
            status_symbol = "✓" if result == "PASS" else "✗"
            print(f"  {status_symbol} {command:20s} : {result}")

        print("\n" + "=" * 60)

        if passed == total:
            print("✓ ALL TESTS PASSED")
            print("SessionManager is functional and all SESSION commands work correctly.")
        elif passed == 0:
            print("✗ ALL TESTS FAILED")
            print("SessionManager may be disabled or commands not implemented.")
            print("Check APPLICATION_LAYER_AVAILABLE flag in app.py")
        else:
            print(f"⚠ PARTIAL PASS: {passed}/{total} commands working")
            print("Some SESSION commands functional, others may need fixes.")

        return passed == total


def run_all_tests(ble_connection):
    """
    Run complete SESSION commands test suite.

    Args:
        ble_connection: BLE UART connection to PRIMARY device

    Returns:
        True if all tests passed, False otherwise
    """
    print("\n" + "=" * 60)
    print("BlueBuzzah v2.0 - SESSION Commands Test Suite")
    print("CRITICAL-007: SESSION Commands Functionality")
    print("=" * 60)

    print("\nNOTE: This test will:")
    print("  1. Start a therapy session")
    print("  2. Pause the session")
    print("  3. Resume the session")
    print("  4. Check session status")
    print("  5. Stop the session")
    print("\nTotal test duration: ~10 seconds\n")

    input("Press Enter to begin testing...")

    tester = SessionCommandsTester(ble_connection)

    # Run tests in sequence
    tester.test_session_start()
    tester.test_session_pause()
    tester.test_session_resume()
    tester.test_session_status()
    tester.test_session_stop()

    # Generate report
    all_passed = tester.generate_report()

    return all_passed


def run_quick_check(ble_connection):
    """
    Quick check to see if SessionManager is available.

    Args:
        ble_connection: BLE UART connection to PRIMARY device

    Returns:
        True if SessionManager appears to be available
    """
    print("\nQuick SessionManager availability check...")

    tester = SessionCommandsTester(ble_connection)
    response = tester.send_command("SESSION_STATUS")

    if response and "ERROR" not in response and "Unknown command" not in response:
        print("✓ SessionManager appears to be available")
        return True
    else:
        print("✗ SessionManager may be disabled")
        print(f"Response: {response}")
        return False


# Example usage (uncomment when running on hardware):
"""
# After connecting to PRIMARY device via BLE:
import tests.session_commands_test

# Quick check first
tests.session_commands_test.run_quick_check(ble_uart_connection)

# Full test suite
tests.session_commands_test.run_all_tests(ble_uart_connection)
"""
