"""
Test Fixtures
=============

Predefined test fixtures for BlueBuzzah v2 testing including configurations,
therapy profiles, and test data.

Modules:
    configs: Test configuration fixtures
    profiles: Test therapy profile fixtures

Version: 2.0.0
"""

from .configs import (
    primary_config,
    secondary_config,
    test_therapy_config_noisy,
    test_therapy_config_regular,
    test_therapy_config_hybrid,
)
from .profiles import (
    create_test_profile,
    noisy_vcr_profile,
    regular_vcr_profile,
    hybrid_vcr_profile,
)

__all__ = [
    "primary_config",
    "secondary_config",
    "test_therapy_config_noisy",
    "test_therapy_config_regular",
    "test_therapy_config_hybrid",
    "create_test_profile",
    "noisy_vcr_profile",
    "regular_vcr_profile",
    "hybrid_vcr_profile",
]
