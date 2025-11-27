"""
BlueBuzzah v2 - CircuitPython Entry Point
==========================================

Main entry point for BlueBuzzah v2 bilateral haptic therapy system.

This module:
- Loads device configuration from settings.json
- Creates BlueBuzzahApplication instance
- Executes boot sequence based on device role
- Runs main application loop
- Handles top-level errors and recovery
- Monitors memory usage

The main.py file is automatically executed by CircuitPython on device boot.

Module: main
Version: 2.0.0
Author: BlueBuzzah Team
"""

import gc
import time
import traceback
import sys

# Memory optimization - collect garbage early
gc.collect()

print("=" * 60)
print("BlueBuzzah v2 - Bilateral Haptic Therapy System")
print("=" * 60)
print(f"Starting up... [Free memory: {gc.mem_free()} bytes]")
print()

# Import configuration loaders
try:
    from config import load_device_config, load_therapy_profile
    from core.constants import FIRMWARE_VERSION
    from core.types import DeviceRole
except ImportError as e:
    print(f"FATAL: Failed to import configuration modules: {e}")
    print("Ensure src/ directory structure is correct")
    sys.exit(1)

# Import main application
try:
    from app import BlueBuzzahApplication
except ImportError as e:
    print(f"FATAL: Failed to import BlueBuzzahApplication: {e}")
    print("Ensure src/app.py exists and is correct")
    traceback.print_exception(type(e), e, e.__traceback__)
    sys.exit(1)


def load_device_configuration():
    """
    Load device configuration from settings.json.

    Returns:
        Tuple of (device_config dict, therapy_config dict)

    Raises:
        RuntimeError: If configuration loading fails
    """
    print("Loading configuration...")

    try:
        # Load device configuration
        device_config = load_device_config("settings.json")
        print(f"  Device role: {device_config['role']}")

        # Display BLE name based on device role
        base_name = device_config.get('ble_name', 'BlueBuzzah')
        if device_config['role'] == DeviceRole.SECONDARY:
            print(f"  BLE identity name: {base_name}-Secondary")
            print(f"  BLE scan target: {base_name}")
        else:
            print(f"  BLE name: {base_name}")

        print(f"  Firmware: {device_config.get('firmware_version', '2.0.0')}")

        # Load therapy configuration
        therapy_config = load_therapy_profile("default")
        print(f"  Actuator type: {therapy_config['actuator_type']}")
        print(f"  Frequency: {therapy_config['frequency_hz']}Hz")
        print(f"  Amplitude: {therapy_config['amplitude']}%")

        print("Configuration loaded successfully")
        print()

        return device_config, therapy_config

    except OSError as e:
        print(f"ERROR: Configuration file not found: {e}")
        print("Ensure settings.json exists on device")
        raise RuntimeError(f"Configuration file not found: {e}")

    except Exception as e:
        print(f"ERROR: Failed to load configuration: {e}")
        traceback.print_exception(type(e), e, e.__traceback__)
        raise RuntimeError(f"Configuration loading failed: {e}")


def run_application(
    device_config,
    therapy_config
):
    """
    Run the main BlueBuzzah application.

    Args:
        device_config: Device configuration dict
        therapy_config: Therapy configuration dict

    Raises:
        Exception unhandled exception from application
    """
    app = None

    try:
        print("Creating BlueBuzzahApplication instance...")

        # Create application
        app = BlueBuzzahApplication(
            device_config=device_config,
            therapy_config=therapy_config
        )

        print()

        print("Starting application...")
        print("=" * 60)
        print()

        # Run application (main loop)
        app.run()

    except KeyboardInterrupt:
        print()
        print("=" * 60)
        print("Keyboard interrupt received - shutting down")
        print("=" * 60)

    except Exception as e:
        print()
        print("=" * 60)
        print(f"FATAL ERROR: {e}")
        print("=" * 60)
        traceback.print_exception(type(e), e, e.__traceback__)

        # Try to get application status for debugging
        if app:
            try:
                status = app.get_status()
                print()
                print("Application status at time of crash:")
                for key, value in status.items():
                    print(f"  {key}: {value}")
            except:
                pass

        raise

    finally:
        pass


def run_main():
    """
    Main entry point.

    This function:
    1. Loads configuration
    2. Creates and runs application
    3. Handles errors and recovery
    """
    try:
        # Load configuration
        device_config, therapy_config = load_device_configuration()

        print()

        # Run application
        run_application(device_config, therapy_config)

    except RuntimeError as e:
        print()
        print("=" * 60)
        print(f"STARTUP FAILED: {e}")
        print("=" * 60)
        print()
        print("Device requires restart")

    except Exception as e:
        print()
        print("=" * 60)
        print(f"UNHANDLED EXCEPTION: {e}")
        print("=" * 60)
        traceback.print_exception(type(e), e, e.__traceback__)
        print()
        print("Device requires restart")

    finally:
        # Final cleanup
        print()
        print("=" * 60)
        print("BlueBuzzah v2 - Shutdown complete")
        print("=" * 60)


def main():
    """
    Application entry point.

    Called when CircuitPython executes main.py on boot.
    """
    try:
        # Run main logic
        run_main()

    except Exception as e:
        print()
        print("=" * 60)
        print(f"CRITICAL ERROR in main(): {e}")
        print("=" * 60)
        traceback.print_exception(type(e), e, e.__traceback__)


# Entry point - automatically executed by CircuitPython
if __name__ == "__main__":
    main()
