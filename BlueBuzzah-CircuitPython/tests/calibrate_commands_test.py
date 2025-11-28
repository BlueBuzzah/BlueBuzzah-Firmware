"""
CALIBRATE Commands Test for BlueBuzzah v2.0
===========================================
Tests all 3 CALIBRATE commands and 8 motors functionality.

CRITICAL-008: CALIBRATE Commands Testing Requirements
------------------------------------------------------
Test all 3 commands:
1. CALIBRATE_START - Enter calibration mode
2. CALIBRATE_BUZZ - Test individual motor (motors 0-7)
3. CALIBRATE_STOP - Exit calibration mode

Test all 8 motors:
- Motor 0-7 response quality
- Intensity scaling (25%, 50%, 75%, 100%)
- Consistency across multiple activations

Usage:
------
1. Connect to device (PRIMARY or SECONDARY) via BLE
2. Run: import tests.calibrate_commands_test
3. Run: tests.calibrate_commands_test.run_all_tests(ble_connection)

Expected Results:
-----------------
All motors should respond consistently with appropriate intensity scaling.
Calibration workflow should be smooth for clinical use.
"""

import time


class CalibrateCommandsTester:
    """Tests CALIBRATE_* commands on BlueBuzzah device."""

    def __init__(self, ble_conn):
        """
        Initialize tester.

        Args:
            ble_conn: BLE UART connection to device
        """
        self.ble = ble_conn
        self.test_results = {}
        self.motor_test_results = {}

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

            # Wait for response (max 2 seconds)
            start_time = time.monotonic()
            buffer = ""

            while time.monotonic() - start_time < 2.0:
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

    def test_calibrate_start(self):
        """Test CALIBRATE_START command."""
        print("\nTest 1: CALIBRATE_START")
        print("-" * 40)

        response = self.send_command("CALIBRATE_START")

        if response:
            print(f"    Response: {response}")

            if "CALIBRATION" in response or "STARTED" in response or "OK" in response:
                print("    ✓ PASS: Entered calibration mode")
                self.test_results['CALIBRATE_START'] = 'PASS'
                return True
            elif "ERROR" in response or "FAIL" in response:
                print(f"    ✗ FAIL: Command returned error")
                self.test_results['CALIBRATE_START'] = 'FAIL'
                return False
            else:
                print(f"    ? UNKNOWN: Unexpected response format")
                self.test_results['CALIBRATE_START'] = 'UNKNOWN'
                return False
        else:
            print("    ✗ FAIL: No response (timeout)")
            self.test_results['CALIBRATE_START'] = 'TIMEOUT'
            return False

    def test_calibrate_buzz_single(self, motor_index, intensity, duration_ms):
        """
        Test CALIBRATE_BUZZ command for a single motor.

        Args:
            motor_index: Motor index (0-7)
            intensity: Vibration intensity (0-100)
            duration_ms: Duration in milliseconds

        Returns:
            True if motor responded correctly
        """
        command = f"CALIBRATE_BUZZ:{motor_index}:{intensity}:{duration_ms}"
        response = self.send_command(command)

        if response:
            if "OK" in response or "BUZZ" in response or "COMPLETE" in response:
                return True
            elif "ERROR" in response:
                print(f"        ✗ ERROR: {response}")
                return False
        return False

    def test_all_motors(self):
        """Test all 8 motors at standard intensity."""
        print("\nTest 2: CALIBRATE_BUZZ - All 8 Motors")
        print("-" * 40)

        all_passed = True

        for motor in range(8):
            print(f"    Testing motor {motor}...")

            # Test at 50% intensity, 500ms duration
            success = self.test_calibrate_buzz_single(motor, 50, 500)

            if success:
                print(f"        ✓ Motor {motor} responded")
                self.motor_test_results[f'motor_{motor}'] = 'PASS'
            else:
                print(f"        ✗ Motor {motor} failed or no response")
                self.motor_test_results[f'motor_{motor}'] = 'FAIL'
                all_passed = False

            # Wait between motor tests
            time.sleep(0.8)

        if all_passed:
            print("    ✓ PASS: All 8 motors responded correctly")
            self.test_results['ALL_MOTORS'] = 'PASS'
        else:
            print("    ✗ FAIL: Some motors did not respond")
            self.test_results['ALL_MOTORS'] = 'FAIL'

        return all_passed

    def test_intensity_scaling(self):
        """Test intensity scaling on motor 0."""
        print("\nTest 3: Intensity Scaling (Motor 0)")
        print("-" * 40)

        intensities = [25, 50, 75, 100]
        all_passed = True

        for intensity in intensities:
            print(f"    Testing intensity {intensity}%...")

            success = self.test_calibrate_buzz_single(0, intensity, 300)

            if success:
                print(f"        ✓ {intensity}% intensity OK")
            else:
                print(f"        ✗ {intensity}% intensity failed")
                all_passed = False

            time.sleep(0.5)

        if all_passed:
            print("    ✓ PASS: Intensity scaling working correctly")
            self.test_results['INTENSITY_SCALING'] = 'PASS'
        else:
            print("    ✗ FAIL: Intensity scaling has issues")
            self.test_results['INTENSITY_SCALING'] = 'FAIL'

        return all_passed

    def test_consistency(self):
        """Test motor consistency (motor 0, 10 repetitions)."""
        print("\nTest 4: Motor Consistency (Motor 0, 10x)")
        print("-" * 40)

        success_count = 0

        for i in range(10):
            success = self.test_calibrate_buzz_single(0, 50, 200)
            if success:
                success_count += 1
            time.sleep(0.3)

        consistency_rate = (success_count / 10) * 100

        print(f"    Success rate: {success_count}/10 ({consistency_rate:.0f}%)")

        if consistency_rate >= 90:
            print("    ✓ PASS: Motor consistency excellent (≥90%)")
            self.test_results['CONSISTENCY'] = 'PASS'
        elif consistency_rate >= 70:
            print("    ⚠ WARN: Motor consistency acceptable (≥70%)")
            self.test_results['CONSISTENCY'] = 'WARN'
        else:
            print("    ✗ FAIL: Motor consistency poor (<70%)")
            self.test_results['CONSISTENCY'] = 'FAIL'

        return consistency_rate >= 70

    def test_error_handling(self):
        """Test error handling with invalid parameters."""
        print("\nTest 5: Error Handling")
        print("-" * 40)

        test_cases = [
            ("CALIBRATE_BUZZ:99:50:500", "Invalid motor index"),
            ("CALIBRATE_BUZZ:0:150:500", "Invalid intensity"),
            ("CALIBRATE_BUZZ:0:-10:500", "Negative intensity"),
        ]

        all_handled = True

        for command, description in test_cases:
            print(f"    Testing: {description}")
            response = self.send_command(command)

            if response and "ERROR" in response:
                print(f"        ✓ Error properly handled")
            else:
                print(f"        ✗ Error not handled (response: {response})")
                all_handled = False

            time.sleep(0.3)

        if all_handled:
            print("    ✓ PASS: Error handling working correctly")
            self.test_results['ERROR_HANDLING'] = 'PASS'
        else:
            print("    ⚠ WARN: Some errors not properly handled")
            self.test_results['ERROR_HANDLING'] = 'WARN'

        return all_handled

    def test_calibrate_stop(self):
        """Test CALIBRATE_STOP command."""
        print("\nTest 6: CALIBRATE_STOP")
        print("-" * 40)

        response = self.send_command("CALIBRATE_STOP")

        if response:
            print(f"    Response: {response}")

            if "STOPPED" in response or "EXIT" in response or "OK" in response:
                print("    ✓ PASS: Exited calibration mode")
                self.test_results['CALIBRATE_STOP'] = 'PASS'
                return True
            elif "ERROR" in response or "FAIL" in response:
                print(f"    ✗ FAIL: Command returned error")
                self.test_results['CALIBRATE_STOP'] = 'FAIL'
                return False
            else:
                print(f"    ? UNKNOWN: Unexpected response format")
                self.test_results['CALIBRATE_STOP'] = 'UNKNOWN'
                return False
        else:
            print("    ✗ FAIL: No response (timeout)")
            self.test_results['CALIBRATE_STOP'] = 'TIMEOUT'
            return False

    def generate_report(self):
        """Generate final test report."""
        print("\n" + "=" * 60)
        print("CALIBRATE COMMANDS TEST REPORT")
        print("=" * 60)

        # Overall results
        passed = sum(1 for result in self.test_results.values() if result == 'PASS')
        warned = sum(1 for result in self.test_results.values() if result == 'WARN')
        total = len(self.test_results)

        print(f"\nOverall Results: {passed}/{total} tests passed")
        if warned > 0:
            print(f"                 {warned} warnings")

        print("\nCommand Tests:")
        for command, result in self.test_results.items():
            if command in ['CALIBRATE_START', 'CALIBRATE_STOP', 'ALL_MOTORS',
                          'INTENSITY_SCALING', 'CONSISTENCY', 'ERROR_HANDLING']:
                status_symbol = "✓" if result == "PASS" else ("⚠" if result == "WARN" else "✗")
                print(f"  {status_symbol} {command:20s} : {result}")

        # Motor results
        if self.motor_test_results:
            print("\nIndividual Motor Results:")
            motor_passed = sum(1 for result in self.motor_test_results.values() if result == 'PASS')
            motor_total = len(self.motor_test_results)
            print(f"  {motor_passed}/{motor_total} motors working")

            for motor_id, result in sorted(self.motor_test_results.items()):
                status_symbol = "✓" if result == "PASS" else "✗"
                print(f"    {status_symbol} {motor_id}")

        print("\n" + "=" * 60)

        if passed == total:
            print("✓ ALL TESTS PASSED")
            print("CalibrationController is functional and all motors respond correctly.")
        elif passed == 0:
            print("✗ ALL TESTS FAILED")
            print("CalibrationController may be disabled or not implemented.")
            print("Check APPLICATION_LAYER_AVAILABLE flag in app.py")
        else:
            print(f"⚠ PARTIAL PASS: {passed}/{total} tests passed")
            print("Some calibration functionality working, review failures above.")

        # Clinical usability assessment
        print("\nClinical Usability Assessment:")
        if passed >= total - 1 and motor_passed == motor_total:
            print("  ✓ System ready for clinical calibration procedures")
        else:
            print("  ✗ System needs fixes before clinical use")
            print("  Recommendation: Address failed tests before deployment")

        return passed == total and motor_passed == motor_total


def run_all_tests(ble_connection):
    """
    Run complete CALIBRATE commands test suite.

    Args:
        ble_connection: BLE UART connection to device

    Returns:
        True if all tests passed, False otherwise
    """
    print("\n" + "=" * 60)
    print("BlueBuzzah v2.0 - CALIBRATE Commands Test Suite")
    print("CRITICAL-008: CALIBRATE Commands and Motor Testing")
    print("=" * 60)

    print("\nNOTE: This test will:")
    print("  1. Enter calibration mode")
    print("  2. Test all 8 motors (one at a time)")
    print("  3. Test intensity scaling (25%, 50%, 75%, 100%)")
    print("  4. Test motor consistency (10 repetitions)")
    print("  5. Test error handling")
    print("  6. Exit calibration mode")
    print("\nYou should FEEL and HEAR each motor vibrate during the test.")
    print("Total test duration: ~30-40 seconds\n")

    input("Press Enter to begin testing...")

    tester = CalibrateCommandsTester(ble_connection)

    # Run tests in sequence
    tester.test_calibrate_start()
    tester.test_all_motors()
    tester.test_intensity_scaling()
    tester.test_consistency()
    tester.test_error_handling()
    tester.test_calibrate_stop()

    # Generate report
    all_passed = tester.generate_report()

    return all_passed


def run_quick_motor_check(ble_connection):
    """
    Quick check of all 8 motors.

    Args:
        ble_connection: BLE UART connection to device

    Returns:
        Number of working motors (0-8)
    """
    print("\nQuick motor check (all 8 motors at 50% intensity)...")

    tester = CalibrateCommandsTester(ble_connection)

    # Enter calibration mode
    tester.send_command("CALIBRATE_START")
    time.sleep(0.5)

    working_motors = 0

    for motor in range(8):
        print(f"  Testing motor {motor}...", end='')
        success = tester.test_calibrate_buzz_single(motor, 50, 300)
        if success:
            print(" ✓")
            working_motors += 1
        else:
            print(" ✗")
        time.sleep(0.5)

    # Exit calibration mode
    tester.send_command("CALIBRATE_STOP")

    print(f"\nResult: {working_motors}/8 motors working")
    return working_motors


# Example usage (uncomment when running on hardware):
"""
# After connecting to device via BLE:
import tests.calibrate_commands_test

# Quick motor check first
working = tests.calibrate_commands_test.run_quick_motor_check(ble_uart_connection)

# Full test suite
tests.calibrate_commands_test.run_all_tests(ble_uart_connection)
"""
