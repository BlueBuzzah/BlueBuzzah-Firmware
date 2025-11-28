"""
Test Therapy Profile Fixtures
==============================

Predefined therapy profiles and profile creation helpers for testing.
These profiles represent common therapeutic configurations used in
vCR therapy research and clinical applications.

Functions:
    create_test_profile: Factory for custom therapy profiles
    noisy_vcr_profile: Standard noisy vCR profile
    regular_vcr_profile: Standard regular vCR profile
    hybrid_vcr_profile: Hybrid vCR profile with frequency variation
    calibration_profile: Profile for haptic calibration
    low_intensity_profile: Gentle therapy profile
    high_intensity_profile: Maximum intensity profile

Version: 2.0.0
"""

from typing import Dict, Any, Optional
from config.therapy import TherapyConfig, ActuatorType, PatternType


# ============================================================================
# Profile Creation Helpers
# ============================================================================

def create_test_profile(
    name: str,
    description: str,
    config: TherapyConfig,
    metadata: Optional[Dict[str, Any]] = None
) -> Dict[str, Any]:
    """
    Create a complete therapy profile with metadata.

    A therapy profile includes the configuration and additional metadata
    for identification, categorization, and clinical tracking.

    Args:
        name: Profile name
        description: Profile description
        config: TherapyConfig for this profile
        metadata: Optional additional metadata

    Returns:
        dict: Complete profile with config and metadata

    Usage:
        profile = create_test_profile(
            name="Test Profile",
            description="Profile for unit testing",
            config=TherapyConfig.default_noisy_vcr(),
            metadata={"category": "test", "version": "1.0"}
        )

        assert profile["name"] == "Test Profile"
        assert profile["config"].jitter_percent > 0
    """
    profile = {
        "name": name,
        "description": description,
        "config": config,
        "metadata": metadata or {},
    }

    # Add standard metadata fields
    if "category" not in profile["metadata"]:
        profile["metadata"]["category"] = "test"

    if "created_date" not in profile["metadata"]:
        import datetime
        profile["metadata"]["created_date"] = datetime.datetime.now().isoformat()

    return profile


# ============================================================================
# Standard Therapy Profiles
# ============================================================================

def noisy_vcr_profile(
    session_duration_min: int = 120,
    amplitude_percent: int = 100
) -> Dict[str, Any]:
    """
    Create standard Noisy vCR therapy profile.

    This is the primary therapeutic profile used in Parkinson's disease
    research, featuring 23.5% timing jitter for enhanced desynchronization.

    Args:
        session_duration_min: Session duration in minutes (default: 120)
        amplitude_percent: Vibration amplitude 0-100% (default: 100)

    Returns:
        dict: Noisy vCR profile

    Usage:
        profile = noisy_vcr_profile()
        config = profile["config"]
        assert config.jitter_percent == 23.5
        assert config.is_noisy_vcr()

        # 30-minute session
        profile = noisy_vcr_profile(session_duration_min=30)
        assert profile["config"].session_duration_min == 30
    """
    config = TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=250,
        amplitude_percent=amplitude_percent,

        # Pattern settings
        pattern_type=PatternType.RNDP,
        time_on_sec=0.100,
        time_off_sec=0.067,
        jitter_percent=23.5,
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

    return create_test_profile(
        name="Noisy vCR",
        description="Standard noisy vibrotactile Coordinated Reset with 23.5% jitter",
        config=config,
        metadata={
            "category": "therapeutic",
            "research_validated": True,
            "indication": "Parkinson's disease",
            "evidence_level": "experimental",
        }
    )


def regular_vcr_profile(
    session_duration_min: int = 120,
    amplitude_percent: int = 100
) -> Dict[str, Any]:
    """
    Create standard Regular vCR therapy profile.

    This profile uses precise, non-jittered timing for predictable
    stimulation patterns. Useful for baseline comparisons and
    patients sensitive to timing variability.

    Args:
        session_duration_min: Session duration in minutes (default: 120)
        amplitude_percent: Vibration amplitude 0-100% (default: 100)

    Returns:
        dict: Regular vCR profile

    Usage:
        profile = regular_vcr_profile()
        config = profile["config"]
        assert config.jitter_percent == 0.0
        assert not config.is_noisy_vcr()
    """
    config = TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=250,
        amplitude_percent=amplitude_percent,

        # Pattern settings
        pattern_type=PatternType.RNDP,
        time_on_sec=0.100,
        time_off_sec=0.067,
        jitter_percent=0.0,  # No jitter
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

    return create_test_profile(
        name="Regular vCR",
        description="Regular vibrotactile Coordinated Reset with precise timing",
        config=config,
        metadata={
            "category": "therapeutic",
            "research_validated": True,
            "indication": "Parkinson's disease",
            "evidence_level": "experimental",
        }
    )


def hybrid_vcr_profile(
    session_duration_min: int = 120,
    amplitude_percent: int = 100,
    jitter_percent: float = 15.0
) -> Dict[str, Any]:
    """
    Create Hybrid vCR therapy profile.

    This profile combines timing jitter with frequency randomization
    for maximum stimulus variability. May provide enhanced therapeutic
    effects through multi-dimensional variability.

    Args:
        session_duration_min: Session duration in minutes (default: 120)
        amplitude_percent: Vibration amplitude 0-100% (default: 100)
        jitter_percent: Timing jitter percentage (default: 15.0)

    Returns:
        dict: Hybrid vCR profile

    Usage:
        profile = hybrid_vcr_profile()
        config = profile["config"]
        assert config.jitter_percent == 15.0
        assert config.random_frequency
    """
    config = TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=250,
        amplitude_percent=amplitude_percent,

        # Pattern settings
        pattern_type=PatternType.RNDP,
        time_on_sec=0.100,
        time_off_sec=0.067,
        jitter_percent=jitter_percent,
        mirror_pattern=True,
        random_frequency=True,  # Frequency randomization enabled
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

    return create_test_profile(
        name="Hybrid vCR",
        description="Hybrid vCR with timing jitter and frequency randomization",
        config=config,
        metadata={
            "category": "therapeutic",
            "research_validated": False,
            "indication": "Parkinson's disease",
            "evidence_level": "exploratory",
        }
    )


def calibration_profile(
    test_frequency_hz: int = 250,
    test_amplitude_percent: int = 75
) -> Dict[str, Any]:
    """
    Create calibration profile for haptic testing.

    This profile is used for individual finger calibration and
    threshold testing. Uses sequential patterns for predictable
    stimulation order.

    Args:
        test_frequency_hz: Test frequency (default: 250)
        test_amplitude_percent: Test amplitude (default: 75)

    Returns:
        dict: Calibration profile

    Usage:
        profile = calibration_profile()
        config = profile["config"]
        assert config.pattern_type == PatternType.SEQUENTIAL
        assert config.amplitude_percent == 75
    """
    config = TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=test_frequency_hz,
        amplitude_percent=test_amplitude_percent,

        # Pattern settings
        pattern_type=PatternType.SEQUENTIAL,  # Predictable order
        time_on_sec=0.500,  # Longer for perception testing
        time_off_sec=0.500,
        jitter_percent=0.0,
        mirror_pattern=True,
        random_frequency=False,

        # LED settings
        led_enabled=True,
        led_brightness_percent=100,  # Bright for calibration
        led_breathing_rate_sec=2.0,

        # Battery settings
        battery_warning_enabled=True,
        battery_warning_voltage=3.4,
        battery_critical_voltage=3.3,

        # Session settings
        session_duration_min=10,  # Short calibration session
    )

    return create_test_profile(
        name="Calibration",
        description="Haptic calibration profile with sequential patterns",
        config=config,
        metadata={
            "category": "calibration",
            "research_validated": False,
            "purpose": "threshold_testing",
        }
    )


def low_intensity_profile(
    session_duration_min: int = 120
) -> Dict[str, Any]:
    """
    Create low-intensity therapy profile.

    This profile uses reduced amplitude and frequency for gentle
    stimulation, suitable for initial sessions or sensitive patients.

    Args:
        session_duration_min: Session duration in minutes (default: 120)

    Returns:
        dict: Low-intensity profile

    Usage:
        profile = low_intensity_profile()
        config = profile["config"]
        assert config.amplitude_percent == 60
        assert config.frequency_hz == 175
    """
    config = TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=175,  # Lower frequency
        amplitude_percent=60,  # Reduced amplitude

        # Pattern settings
        pattern_type=PatternType.RNDP,
        time_on_sec=0.100,
        time_off_sec=0.100,  # Longer off time
        jitter_percent=23.5,
        mirror_pattern=True,
        random_frequency=False,

        # LED settings
        led_enabled=True,
        led_brightness_percent=25,  # Dimmer
        led_breathing_rate_sec=6.0,  # Slower

        # Battery settings
        battery_warning_enabled=True,
        battery_warning_voltage=3.4,
        battery_critical_voltage=3.3,

        # Session settings
        session_duration_min=session_duration_min,
    )

    return create_test_profile(
        name="Low Intensity vCR",
        description="Gentle vCR therapy with reduced amplitude and frequency",
        config=config,
        metadata={
            "category": "therapeutic",
            "research_validated": False,
            "indication": "Parkinson's disease - initial sessions",
            "intensity_level": "low",
        }
    )


def high_intensity_profile(
    session_duration_min: int = 120
) -> Dict[str, Any]:
    """
    Create high-intensity therapy profile.

    This profile uses maximum safe amplitude and frequency for
    strong stimulation, suitable for patients with reduced sensitivity.

    Args:
        session_duration_min: Session duration in minutes (default: 120)

    Returns:
        dict: High-intensity profile

    Usage:
        profile = high_intensity_profile()
        config = profile["config"]
        assert config.amplitude_percent == 100
        assert config.frequency_hz == 260
    """
    config = TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=260,  # Higher frequency
        amplitude_percent=100,  # Maximum amplitude

        # Pattern settings
        pattern_type=PatternType.RNDP,
        time_on_sec=0.150,  # Longer bursts
        time_off_sec=0.050,  # Shorter off time
        jitter_percent=23.5,
        mirror_pattern=True,
        random_frequency=False,

        # LED settings
        led_enabled=True,
        led_brightness_percent=75,  # Brighter
        led_breathing_rate_sec=3.0,  # Faster

        # Battery settings
        battery_warning_enabled=True,
        battery_warning_voltage=3.5,  # Higher threshold due to power draw
        battery_critical_voltage=3.4,

        # Session settings
        session_duration_min=session_duration_min,
    )

    return create_test_profile(
        name="High Intensity vCR",
        description="Strong vCR therapy with maximum amplitude and frequency",
        config=config,
        metadata={
            "category": "therapeutic",
            "research_validated": False,
            "indication": "Parkinson's disease - reduced sensitivity",
            "intensity_level": "high",
        }
    )


def quick_test_profile() -> Dict[str, Any]:
    """
    Create quick test profile for rapid testing.

    This profile has very short duration and simple patterns for
    quick verification testing during development.

    Returns:
        dict: Quick test profile

    Usage:
        profile = quick_test_profile()
        config = profile["config"]
        assert config.session_duration_min == 1
        assert config.pattern_type == PatternType.SEQUENTIAL
    """
    config = TherapyConfig(
        # Actuator settings
        actuator_type=ActuatorType.LRA,
        actuator_voltage=2.5,
        frequency_hz=250,
        amplitude_percent=75,

        # Pattern settings
        pattern_type=PatternType.SEQUENTIAL,
        time_on_sec=0.200,
        time_off_sec=0.200,
        jitter_percent=0.0,
        mirror_pattern=True,
        random_frequency=False,

        # LED settings
        led_enabled=True,
        led_brightness_percent=50,
        led_breathing_rate_sec=2.0,

        # Battery settings
        battery_warning_enabled=False,  # Disabled for quick tests

        # Session settings
        session_duration_min=1,  # 1 minute only
    )

    return create_test_profile(
        name="Quick Test",
        description="Short test profile for rapid verification",
        config=config,
        metadata={
            "category": "development",
            "purpose": "quick_test",
        }
    )


# ============================================================================
# Profile Collection
# ============================================================================

def get_all_standard_profiles() -> Dict[str, Dict[str, Any]]:
    """
    Get collection of all standard therapy profiles.

    Returns:
        dict: Dictionary mapping profile names to profile data

    Usage:
        profiles = get_all_standard_profiles()
        noisy = profiles["noisy_vcr"]
        regular = profiles["regular_vcr"]

        # List available profiles
        for name in profiles.keys():
            print(f"Available: {name}")
    """
    return {
        "noisy_vcr": noisy_vcr_profile(),
        "regular_vcr": regular_vcr_profile(),
        "hybrid_vcr": hybrid_vcr_profile(),
        "calibration": calibration_profile(),
        "low_intensity": low_intensity_profile(),
        "high_intensity": high_intensity_profile(),
        "quick_test": quick_test_profile(),
    }


def get_profile_by_name(name: str) -> Optional[Dict[str, Any]]:
    """
    Get therapy profile by name.

    Args:
        name: Profile name (case-insensitive)

    Returns:
        dict: Profile data, or None if not found

    Usage:
        profile = get_profile_by_name("noisy_vcr")
        if profile:
            config = profile["config"]
            # Use config...
    """
    profiles = get_all_standard_profiles()
    name_lower = name.lower().replace(" ", "_")
    return profiles.get(name_lower)
