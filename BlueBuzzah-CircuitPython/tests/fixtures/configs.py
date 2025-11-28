"""
Test Configuration Fixtures
============================

Predefined test configurations for PRIMARY, SECONDARY devices and
various therapy profile configurations.

Functions:
    primary_config: PRIMARY device configuration
    secondary_config: SECONDARY device configuration
    test_therapy_config_noisy: Noisy vCR therapy configuration
    test_therapy_config_regular: Regular vCR therapy configuration
    test_therapy_config_hybrid: Hybrid vCR therapy configuration
    create_custom_base_config: Factory for custom base configs
    create_custom_therapy_config: Factory for custom therapy configs

Version: 2.0.0
"""

from config.base import BaseConfig, DeviceRole
from config.therapy import TherapyConfig, ActuatorType, PatternType
from typing import Optional


# ============================================================================
# Base Configuration Fixtures
# ============================================================================

def primary_config(
    startup_window_sec: int = 30,
    connection_timeout_sec: int = 30,
    firmware_version: str = "2.0.0-test"
) -> BaseConfig:
    """
    Create PRIMARY device configuration for testing.

    Args:
        startup_window_sec: Startup window duration (seconds)
        connection_timeout_sec: Connection timeout (seconds)
        firmware_version: Firmware version string

    Returns:
        BaseConfig: PRIMARY device configuration

    Usage:
        config = primary_config()
        assert config.is_primary()
        assert config.startup_window_sec == 30

        # Custom startup window
        config = primary_config(startup_window_sec=60)
        assert config.startup_window_sec == 60
    """
    return BaseConfig(
        device_role=DeviceRole.PRIMARY,
        firmware_version=firmware_version,
        startup_window_sec=startup_window_sec,
        connection_timeout_sec=connection_timeout_sec,
        ble_name="BlueBuzzah",
        ble_interval_ms=7.5,
        ble_latency=0,
        ble_timeout_ms=100,
        ble_adv_interval_ms=500,
        auth_token="bluebuzzah-test-token",
        chunk_size=100,
    )


def secondary_config(
    connection_timeout_sec: int = 30,
    firmware_version: str = "2.0.0-test"
) -> BaseConfig:
    """
    Create SECONDARY device configuration for testing.

    Args:
        connection_timeout_sec: Connection timeout (seconds)
        firmware_version: Firmware version string

    Returns:
        BaseConfig: SECONDARY device configuration

    Usage:
        config = secondary_config()
        assert config.is_secondary()
        assert config.device_role == DeviceRole.SECONDARY
    """
    return BaseConfig(
        device_role=DeviceRole.SECONDARY,
        firmware_version=firmware_version,
        startup_window_sec=30,  # SECONDARY also has startup window
        connection_timeout_sec=connection_timeout_sec,
        ble_name="BlueBuzzah",
        ble_interval_ms=7.5,
        ble_latency=0,
        ble_timeout_ms=100,
        ble_adv_interval_ms=500,
        auth_token="bluebuzzah-test-token",
        chunk_size=100,
    )


def create_custom_base_config(**kwargs) -> BaseConfig:
    """
    Factory function for creating custom base configurations.

    Args:
        **kwargs: BaseConfig parameters to override

    Returns:
        BaseConfig: Custom configuration

    Usage:
        config = create_custom_base_config(
            device_role=DeviceRole.PRIMARY,
            ble_interval_ms=15.0,
            startup_window_sec=60
        )
    """
    defaults = {
        "device_role": DeviceRole.PRIMARY,
        "firmware_version": "2.0.0-test",
        "startup_window_sec": 30,
        "connection_timeout_sec": 30,
    }
    defaults.update(kwargs)
    return BaseConfig(**defaults)


# ============================================================================
# Therapy Configuration Fixtures
# ============================================================================

def test_therapy_config_noisy(
    frequency_hz: int = 250,
    amplitude_percent: int = 100,
    jitter_percent: float = 23.5,
    session_duration_min: int = 120
) -> TherapyConfig:
    """
    Create Noisy vCR therapy configuration for testing.

    This is the primary therapeutic configuration with timing jitter
    for enhanced desynchronization effects.

    Args:
        frequency_hz: Vibration frequency (Hz)
        amplitude_percent: Vibration amplitude (0-100%)
        jitter_percent: Timing jitter percentage (0-100%)
        session_duration_min: Session duration (minutes)

    Returns:
        TherapyConfig: Noisy vCR configuration

    Usage:
        config = test_therapy_config_noisy()
        assert config.jitter_percent == 23.5
        assert config.is_noisy_vcr()

        # Custom jitter
        config = test_therapy_config_noisy(jitter_percent=30.0)
        assert config.jitter_percent == 30.0
    """
    return TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=frequency_hz,
        amplitude_percent=amplitude_percent,

        # Pattern settings
        pattern_type=PatternType.RNDP,
        time_on_sec=0.100,
        time_off_sec=0.067,
        jitter_percent=jitter_percent,
        mirror_pattern=True,
        random_frequency=False,
        frequency_low_hz=210,
        frequency_high_hz=260,

        # LED settings
        led_enabled=True,
        led_brightness_percent=50,
        led_breathing_rate_sec=4.0,

        # Battery settings
        battery_warning_enabled=True,
        battery_warning_voltage=3.4,
        battery_critical_voltage=3.3,

        # Session settings
        session_duration_min=session_duration_min,
    )


def test_therapy_config_regular(
    frequency_hz: int = 250,
    amplitude_percent: int = 100,
    session_duration_min: int = 120
) -> TherapyConfig:
    """
    Create Regular vCR therapy configuration for testing.

    This configuration uses precise, non-jittered timing for
    predictable stimulation patterns.

    Args:
        frequency_hz: Vibration frequency (Hz)
        amplitude_percent: Vibration amplitude (0-100%)
        session_duration_min: Session duration (minutes)

    Returns:
        TherapyConfig: Regular vCR configuration

    Usage:
        config = test_therapy_config_regular()
        assert config.jitter_percent == 0.0
        assert not config.is_noisy_vcr()
    """
    return TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=frequency_hz,
        amplitude_percent=amplitude_percent,

        # Pattern settings
        pattern_type=PatternType.RNDP,
        time_on_sec=0.100,
        time_off_sec=0.067,
        jitter_percent=0.0,  # No jitter for regular vCR
        mirror_pattern=True,
        random_frequency=False,

        # LED settings
        led_enabled=True,
        led_brightness_percent=50,
        led_breathing_rate_sec=4.0,

        # Battery settings
        battery_warning_enabled=True,
        battery_warning_voltage=3.4,
        battery_critical_voltage=3.3,

        # Session settings
        session_duration_min=session_duration_min,
    )


def test_therapy_config_hybrid(
    frequency_hz: int = 250,
    amplitude_percent: int = 100,
    jitter_percent: float = 15.0,
    random_frequency: bool = True,
    session_duration_min: int = 120
) -> TherapyConfig:
    """
    Create Hybrid vCR therapy configuration for testing.

    This configuration combines timing jitter with frequency variation
    for maximum stimulus variability.

    Args:
        frequency_hz: Base vibration frequency (Hz)
        amplitude_percent: Vibration amplitude (0-100%)
        jitter_percent: Timing jitter percentage (0-100%)
        random_frequency: Enable frequency randomization
        session_duration_min: Session duration (minutes)

    Returns:
        TherapyConfig: Hybrid vCR configuration

    Usage:
        config = test_therapy_config_hybrid()
        assert config.jitter_percent == 15.0
        assert config.random_frequency
    """
    return TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=frequency_hz,
        amplitude_percent=amplitude_percent,

        # Pattern settings
        pattern_type=PatternType.RNDP,
        time_on_sec=0.100,
        time_off_sec=0.067,
        jitter_percent=jitter_percent,
        mirror_pattern=True,
        random_frequency=random_frequency,
        frequency_low_hz=210,
        frequency_high_hz=260,
        amplitude_min_percent=80,
        amplitude_max_percent=100,

        # LED settings
        led_enabled=True,
        led_brightness_percent=50,
        led_breathing_rate_sec=4.0,

        # Battery settings
        battery_warning_enabled=True,
        battery_warning_voltage=3.4,
        battery_critical_voltage=3.3,

        # Session settings
        session_duration_min=session_duration_min,
    )


def test_therapy_config_sequential(
    frequency_hz: int = 250,
    amplitude_percent: int = 100,
    session_duration_min: int = 120
) -> TherapyConfig:
    """
    Create Sequential pattern therapy configuration for testing.

    This configuration uses predictable sequential finger activation
    rather than random permutations.

    Args:
        frequency_hz: Vibration frequency (Hz)
        amplitude_percent: Vibration amplitude (0-100%)
        session_duration_min: Session duration (minutes)

    Returns:
        TherapyConfig: Sequential pattern configuration

    Usage:
        config = test_therapy_config_sequential()
        assert config.pattern_type == PatternType.SEQUENTIAL
    """
    return TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=frequency_hz,
        amplitude_percent=amplitude_percent,

        # Pattern settings
        pattern_type=PatternType.SEQUENTIAL,
        time_on_sec=0.100,
        time_off_sec=0.067,
        jitter_percent=0.0,
        mirror_pattern=True,
        random_frequency=False,

        # LED settings
        led_enabled=True,
        led_brightness_percent=50,
        led_breathing_rate_sec=4.0,

        # Battery settings
        battery_warning_enabled=True,
        battery_warning_voltage=3.4,
        battery_critical_voltage=3.3,

        # Session settings
        session_duration_min=session_duration_min,
    )


def test_therapy_config_mirrored(
    frequency_hz: int = 250,
    amplitude_percent: int = 100,
    session_duration_min: int = 120
) -> TherapyConfig:
    """
    Create Mirrored bilateral pattern configuration for testing.

    This configuration ensures both hands receive identical finger
    patterns for synchronized bilateral stimulation.

    Args:
        frequency_hz: Vibration frequency (Hz)
        amplitude_percent: Vibration amplitude (0-100%)
        session_duration_min: Session duration (minutes)

    Returns:
        TherapyConfig: Mirrored pattern configuration

    Usage:
        config = test_therapy_config_mirrored()
        assert config.pattern_type == PatternType.MIRRORED
        assert config.mirror_pattern
    """
    return TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=frequency_hz,
        amplitude_percent=amplitude_percent,

        # Pattern settings
        pattern_type=PatternType.MIRRORED,
        time_on_sec=0.100,
        time_off_sec=0.067,
        jitter_percent=23.5,
        mirror_pattern=True,  # Explicitly mirrored
        random_frequency=False,

        # LED settings
        led_enabled=True,
        led_brightness_percent=50,
        led_breathing_rate_sec=4.0,

        # Battery settings
        battery_warning_enabled=True,
        battery_warning_voltage=3.4,
        battery_critical_voltage=3.3,

        # Session settings
        session_duration_min=session_duration_min,
    )


def create_custom_therapy_config(**kwargs) -> TherapyConfig:
    """
    Factory function for creating custom therapy configurations.

    Args:
        **kwargs: TherapyConfig parameters to override

    Returns:
        TherapyConfig: Custom configuration

    Usage:
        config = create_custom_therapy_config(
            frequency_hz=175,
            amplitude_percent=80,
            jitter_percent=30.0,
            led_enabled=False
        )
    """
    defaults = {
        "actuator_type": ActuatorType.LRA,
        "frequency_hz": 250,
        "amplitude_percent": 100,
        "pattern_type": PatternType.RNDP,
        "time_on_sec": 0.100,
        "time_off_sec": 0.067,
        "jitter_percent": 23.5,
    }
    defaults.update(kwargs)
    return TherapyConfig(**defaults)


# ============================================================================
# Edge Case Configurations
# ============================================================================

def test_therapy_config_minimum_values() -> TherapyConfig:
    """
    Create configuration with minimum valid values for edge case testing.

    Returns:
        TherapyConfig: Minimum values configuration

    Usage:
        config = test_therapy_config_minimum_values()
        assert config.frequency_hz == 50  # Minimum valid
        assert config.amplitude_percent == 0  # Minimum amplitude
    """
    return TherapyConfig(
        actuator_type=ActuatorType.LRA,
        frequency_hz=50,  # Minimum valid frequency
        amplitude_percent=0,  # Minimum amplitude
        time_on_sec=0.010,  # Very short burst
        time_off_sec=0.010,
        jitter_percent=0.0,
        session_duration_min=1,  # Very short session
    )


def test_therapy_config_maximum_values() -> TherapyConfig:
    """
    Create configuration with maximum valid values for edge case testing.

    Returns:
        TherapyConfig: Maximum values configuration

    Usage:
        config = test_therapy_config_maximum_values()
        assert config.frequency_hz == 500  # Maximum valid
        assert config.amplitude_percent == 100
    """
    return TherapyConfig(
        actuator_type=ActuatorType.LRA,
        frequency_hz=500,  # Maximum valid frequency
        amplitude_percent=100,  # Maximum amplitude
        time_on_sec=1.0,  # Long burst
        time_off_sec=1.0,
        jitter_percent=100.0,  # Maximum jitter
        session_duration_min=480,  # 8 hour session
    )


def test_therapy_config_low_battery_threshold() -> TherapyConfig:
    """
    Create configuration with custom battery thresholds for testing.

    Returns:
        TherapyConfig: Custom battery threshold configuration

    Usage:
        config = test_therapy_config_low_battery_threshold()
        assert config.battery_warning_voltage == 3.5
        assert config.battery_critical_voltage == 3.4
    """
    return TherapyConfig(
        actuator_type=ActuatorType.LRA,
        frequency_hz=250,
        amplitude_percent=100,
        battery_warning_voltage=3.5,  # Higher warning threshold
        battery_critical_voltage=3.4,  # Higher critical threshold
    )
