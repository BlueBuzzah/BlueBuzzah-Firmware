"""
Simplified Configuration Module
================================
Dict-based configuration loading for BlueBuzzah v2.

This module provides simple configuration loading without heavy dataclass
infrastructure, using plain dictionaries for memory efficiency.

Functions:
    load_device_config: Load device role configuration from settings.json
    load_therapy_profile: Load therapy profile parameters
    validate_config: Basic configuration validation

Module: config
Version: 2.0.0
"""

import json
from micropython import const
from core.types import DeviceRole, ActuatorType


# Default values - use const() for integers to store in flash instead of RAM
DEFAULT_FIRMWARE_VERSION = "2.0.0"
DEFAULT_BLE_NAME = "BlueBuzzah"
DEFAULT_STARTUP_WINDOW_SEC = const(30)
DEFAULT_CONNECTION_TIMEOUT_SEC = const(30)
DEFAULT_BLE_INTERVAL_MS = 7.5  # Float, cannot use const()
DEFAULT_BLE_TIMEOUT_MS = const(100)

# Battery thresholds (volts) - floats cannot use const()
DEFAULT_LOW_VOLTAGE = 3.4
DEFAULT_CRITICAL_VOLTAGE = 3.3

# Therapy defaults
DEFAULT_ACTUATOR_TYPE = ActuatorType.LRA
DEFAULT_FREQUENCY_HZ = const(175)
DEFAULT_MAX_AMPLITUDE = const(100)


def load_device_config(path="settings.json"):
    """
    Load device role configuration from settings.json.

    Args:
        path: Path to settings.json file (default: "settings.json")

    Returns:
        Dictionary with device configuration:
            - role: DeviceRole (PRIMARY or SECONDARY)
            - ble_name: BLE advertising/scanning name
            - firmware_version: Firmware version string
            - startup_window_sec: Boot sequence timeout
            - connection_timeout_sec: Connection timeout
            - ble_interval_ms: BLE connection interval

    Raises:
        OSError: If settings.json doesn't exist
        ValueError: If configuration is invalid

    Example settings.json:
        {
            "deviceRole": "Primary"
        }
    """
    try:
        with open(path, 'r') as f:
            data = json.load(f)

        # Parse device role
        role_str = data.get("deviceRole", "Primary")
        if role_str == "Primary":
            role = DeviceRole.PRIMARY
        elif role_str == "Secondary":
            role = DeviceRole.SECONDARY
        else:
            raise ValueError("Invalid device role: {}".format(role_str))

        return {
            'role': role,
            'ble_name': data.get('bleName', DEFAULT_BLE_NAME),
            'firmware_version': data.get('firmwareVersion', DEFAULT_FIRMWARE_VERSION),
            'startup_window_sec': data.get('startupWindowSec', DEFAULT_STARTUP_WINDOW_SEC),
            'connection_timeout_sec': data.get('connectionTimeoutSec', DEFAULT_CONNECTION_TIMEOUT_SEC),
            'ble_interval_ms': data.get('bleIntervalMs', DEFAULT_BLE_INTERVAL_MS),
            'ble_timeout_ms': data.get('bleTimeoutMs', DEFAULT_BLE_TIMEOUT_MS),
        }

    except (OSError, IOError) as e:
        raise OSError("Could not load settings from {}: {}".format(path, e))
    except json.JSONDecodeError as e:
        raise ValueError("Invalid JSON in {}: {}".format(path, e))


def load_therapy_profile(profile_id="default"):
    """
    Load therapy profile by ID.

    Args:
        profile_id: Profile identifier (default: "default")

    Returns:
        Dictionary with therapy parameters:
            - profile_id: Profile identifier
            - pattern_type: Pattern generation algorithm
            - actuator_type: LRA or ERM
            - frequency_hz: Default haptic frequency
            - amplitude: Default amplitude (0-100)
            - duration_sec: Session duration
            - macrocycle_ms: Macrocycle period
            - microcycle_ms: Microcycle period

    Example:
        profile = load_therapy_profile("noisy_vcr")
        print("Pattern: {}".format(profile['pattern_type']))
        print("Duration: {}s".format(profile['duration_sec']))
    """
    # Default "noisy_vcr" profile parameters
    profiles = {
        "default": {
            "profile_id": "default",
            "pattern_type": "noisy_vcr",
            "actuator_type": DEFAULT_ACTUATOR_TYPE,
            "frequency_hz": DEFAULT_FREQUENCY_HZ,
            "amplitude": 80,
            "duration_sec": 7200,  # 2 hours
            "macrocycle_ms": 3000,  # 3 seconds
            "microcycle_ms": 100,  # 100ms
            "battery_warning_voltage": DEFAULT_LOW_VOLTAGE,
            "battery_critical_voltage": DEFAULT_CRITICAL_VOLTAGE,
        },
        "noisy_vcr": {
            "profile_id": "noisy_vcr",
            "pattern_type": "noisy_vcr",
            "actuator_type": DEFAULT_ACTUATOR_TYPE,
            "frequency_hz": DEFAULT_FREQUENCY_HZ,
            "amplitude": 80,
            "duration_sec": 7200,
            "macrocycle_ms": 3000,
            "microcycle_ms": 100,
            "battery_warning_voltage": DEFAULT_LOW_VOLTAGE,
            "battery_critical_voltage": DEFAULT_CRITICAL_VOLTAGE,
        },
        "gentle": {
            "profile_id": "gentle",
            "pattern_type": "noisy_vcr",
            "actuator_type": DEFAULT_ACTUATOR_TYPE,
            "frequency_hz": DEFAULT_FREQUENCY_HZ,
            "amplitude": 60,  # Reduced amplitude
            "duration_sec": 3600,  # 1 hour
            "macrocycle_ms": 4000,  # Slower macrocycle
            "microcycle_ms": 120,
            "battery_warning_voltage": DEFAULT_LOW_VOLTAGE,
            "battery_critical_voltage": DEFAULT_CRITICAL_VOLTAGE,
        },
    }

    profile = profiles.get(profile_id)
    if not profile:
        # Return default profile if not found
        print("[WARNING] Profile '{}' not found, using default".format(profile_id))
        profile = profiles["default"]

    return profile


def validate_config(config):
    """
    Validate configuration dictionary.

    Args:
        config: Configuration dictionary to validate

    Returns:
        True if valid, False otherwise

    Raises:
        ValueError: If critical configuration is invalid
    """
    # Check required fields
    if 'role' not in config:
        raise ValueError("Missing required field: 'role'")

    # Validate role
    if not isinstance(config['role'], DeviceRole):
        raise ValueError("Invalid role type: {}".format(type(config['role'])))

    # Validate numeric ranges
    if 'startup_window_sec' in config:
        if not (0 < config['startup_window_sec'] <= 300):
            raise ValueError("startup_window_sec must be between 0 and 300")

    if 'ble_interval_ms' in config:
        if not (7.5 <= config['ble_interval_ms'] <= 4000):
            raise ValueError("ble_interval_ms must be between 7.5 and 4000")

    return True


def get_device_tag(config):
    """
    Get device tag for logging.

    Args:
        config: Device configuration dictionary

    Returns:
        Device tag string like "[PRIMARY]" or "[SECONDARY]"
    """
    role = config.get('role')
    if role == DeviceRole.PRIMARY:
        return "[PRIMARY]"
    elif role == DeviceRole.SECONDARY:
        return "[SECONDARY]"
    else:
        return "[UNKNOWN]"


def is_primary(config):
    """
    Check if device is PRIMARY.

    Args:
        config: Device configuration dictionary

    Returns:
        True if PRIMARY, False otherwise
    """
    return config.get('role') == DeviceRole.PRIMARY


def is_secondary(config):
    """
    Check if device is SECONDARY.

    Args:
        config: Device configuration dictionary

    Returns:
        True if SECONDARY, False otherwise
    """
    return config.get('role') == DeviceRole.SECONDARY


def merge_configs(device_config, therapy_config):
    """
    Merge device and therapy configurations.

    Args:
        device_config: Device configuration dictionary
        therapy_config: Therapy profile dictionary

    Returns:
        Merged configuration dictionary
    """
    merged = {}
    merged.update(device_config)
    merged.update(therapy_config)
    return merged
