"""
Therapy Profile Manager
========================

Management of therapy profiles including storage, loading, and validation.

This module provides the ProfileManager class for managing vCR therapy
configurations, supporting multiple profiles with validation and persistence.

Classes:
    TherapyProfile: Profile data structure
    ProfileManager: Profile management and persistence

Module: profiles
Version: 2.0.0
"""

import json
import os

from core.types import ActuatorType


# ============================================================================
# Default Therapy Configuration
# ============================================================================

def create_default_therapy_config():
    """
    Create default noisy vCR therapy configuration.

    Returns:
        Dictionary with default therapy parameters
    """
    return {
        'pattern_type': 'rndp',  # Random permutation
        'actuator_type': ActuatorType.LRA.value,
        'frequency_hz': 175,
        'time_on_ms': 100.0,
        'time_off_ms': 67.0,
        'jitter_percent': 23.5,  # Noisy vCR
        'num_fingers': 5,
        'mirror_pattern': True,
        'max_amplitude': 100,
        'session_duration_min': 120,  # 2 hour default session
    }


# ============================================================================
# Therapy Profile
# ============================================================================

class TherapyProfile:
    """
    Therapy profile configuration (dict-based).

    A profile encapsulates a complete therapy configuration including
    actuator settings, pattern parameters, timing, and session settings.

    Attributes:
        name: Profile name (unique identifier)
        description: Human-readable description
        config: Dict with therapy parameters
        is_default: Whether this is the default profile
        created_at: Profile creation timestamp
        modified_at: Last modification timestamp

    Usage:
        profile = TherapyProfile(
            name="noisy_vcr",
            description="Noisy vCR therapy with 23.5% jitter",
            config=create_default_therapy_config(),
            is_default=True
        )
    """

    def __init__(
        self,
        name,
        description,
        config,
        is_default=False,
        created_at=None,
        modified_at=None
    ):
        """Initialize therapy profile."""
        self.name = name
        self.description = description
        self.config = config
        self.is_default = is_default
        self.created_at = created_at
        self.modified_at = modified_at

    def to_dict(self):
        """
        Convert profile to dictionary.

        Returns:
            Dictionary representation suitable for JSON serialization
        """
        return {
            "name": self.name,
            "description": self.description,
            "config": self.config,
            "is_default": self.is_default,
            "created_at": self.created_at,
            "modified_at": self.modified_at,
        }

    @classmethod
    def from_dict(cls, data):
        """
        Create profile from dictionary.

        Args:
            data: Dictionary with profile data

        Returns:
            TherapyProfile instance

        Raises:
            KeyError: If required fields are missing
            ValueError: If data is invalid
        """
        return cls(
            name=data["name"],
            description=data["description"],
            config=data["config"],
            is_default=data.get("is_default", False),
            created_at=data.get("created_at"),
            modified_at=data.get("modified_at"),
        )

    def validate(self):
        """
        Validate profile configuration.

        Returns:
            List of validation error messages (empty if valid)
        """
        errors = []

        # Validate name
        if not self.name or len(self.name) == 0:
            errors.append("Profile name cannot be empty")

        # Note: isalnum() not available in CircuitPython, skipping alphanumeric validation

        # Validate description
        if not self.description or len(self.description) == 0:
            errors.append("Profile description cannot be empty")

        # Check config exists
        if self.config is None:
            errors.append("Profile must have a config")

        return errors

    def is_valid(self):
        """
        Check if profile is valid.

        Returns:
            True if profile is valid, False otherwise
        """
        return len(self.validate()) == 0


class ProfileManager:
    """
    Therapy profile manager.

    This class manages the storage, loading, and validation of therapy
    profiles. Profiles are stored as JSON files in a profiles directory.

    Features:
        - Load/save profiles from/to disk
        - Profile validation
        - Default profile management
        - Profile listing and search

    Usage:
        manager = ProfileManager(profiles_dir="/profile_data")

        # Load profile
        profile = manager.load_profile("noisy_vcr")

        # Save profile
        new_profile = TherapyProfile(...)
        manager.save_profile(new_profile)

        # Get default
        default = manager.get_default_profile()
    """

    def __init__(self, profiles_dir="/profile_data"):
        """
        Initialize profile manager.

        Args:
            profiles_dir: Directory path for profile storage

        Usage:
            manager = ProfileManager()  # Uses /profile_data
            manager = ProfileManager("/custom/path")

        Note:
            Caching disabled to save RAM. Profiles are loaded from disk each time.
        """
        self.profiles_dir = profiles_dir

        # Verify profiles directory exists (should be deployed with source)
        try:
            os.stat(self.profiles_dir)
        except OSError:
            print("[WARNING] Profiles directory '{}' not found - profile loading may fail".format(self.profiles_dir))
            print("[INFO] The profiles directory should be deployed with the application")

    def load_profile(self, name):
        """
        Load therapy profile by name.

        Args:
            name: Profile name (without .json extension)

        Returns:
            TherapyProfile instance

        Raises:
            OSError: If profile doesn't exist
            ValueError: If profile is invalid
            json.JSONDecodeError: If profile file is corrupted

        Usage:
            profile = manager.load_profile("noisy_vcr")

        Note:
            Profiles are loaded from disk each time (no caching) to save RAM.
        """
        # Load from disk
        profile_path = self.profiles_dir + "/" + name + ".json"

        # Check if file exists (CircuitPython doesn't have os.path.exists)
        try:
            os.stat(profile_path)
        except OSError:
            raise OSError("Profile '{}' not found at {}".format(name, profile_path))

        try:
            with open(profile_path, "r") as f:
                data = json.load(f)

            profile = TherapyProfile.from_dict(data)

            # Validate profile
            errors = profile.validate()
            if errors:
                raise ValueError("Profile '{}' validation failed: {}".format(name, errors))

            return profile

        except json.JSONDecodeError as e:
            raise json.JSONDecodeError(
                "Profile '{}' is corrupted: {}".format(name, e), e.doc, e.pos
            )

    def save_profile(self, profile, overwrite=False):
        """
        Save therapy profile to disk.

        Args:
            profile: TherapyProfile to save
            overwrite: If True, overwrite existing profile

        Raises:
            ValueError: If profile is invalid or already exists
            OSError: If write fails

        Usage:
            profile = TherapyProfile(...)
            manager.save_profile(profile)
        """
        # Validate profile
        errors = profile.validate()
        if errors:
            raise ValueError("Cannot save invalid profile: {}".format(errors))

        # Check if profile exists
        profile_path = self.profiles_dir + "/" + profile.name + ".json"

        # Check if file exists (CircuitPython doesn't have os.path.exists)
        file_exists = False
        try:
            os.stat(profile_path)
            file_exists = True
        except OSError:
            pass

        if file_exists and not overwrite:
            raise ValueError(
                "Profile '{}' already exists. Use overwrite=True to replace.".format(profile.name)
            )

        try:
            # Write to disk
            with open(profile_path, "w") as f:
                json.dump(profile.to_dict(), f, indent=2)

        except OSError as e:
            raise OSError("Failed to save profile '{}': {}".format(profile.name, e))

    def delete_profile(self, name):
        """
        Delete therapy profile.

        Args:
            name: Profile name to delete

        Raises:
            OSError: If profile doesn't exist
            ValueError: If trying to delete default profile

        Usage:
            manager.delete_profile("old_profile")
        """
        # Load profile to check if it's default
        profile = self.load_profile(name)

        if profile.is_default:
            raise ValueError("Cannot delete default profile")

        # Delete from disk
        profile_path = self.profiles_dir + "/" + name + ".json"
        os.remove(profile_path)

    def list_profiles(self):
        """
        List all available profile names.

        Returns:
            List of profile names

        Usage:
            profiles = manager.list_profiles()
            for name in profiles:
                print(name)
        """
        try:
            # List all .json files in profiles directory
            files = os.listdir(self.profiles_dir)
            profile_names = []
            for f in files:
                if f.endswith(".json"):
                    # Remove .json extension
                    profile_names.append(f[:-5])
            return profile_names
        except Exception as e:
            print("[WARNING] Could not list profiles: {}".format(e))
            return []

    def get_default_profile(self):
        """
        Get the default therapy profile.

        Returns:
            Default TherapyProfile

        Raises:
            ValueError: If no default profile is found

        Usage:
            default = manager.get_default_profile()
        """
        # Search for default profile
        for profile_name in self.list_profiles():
            try:
                profile = self.load_profile(profile_name)
                if profile.is_default:
                    return profile
            except Exception:
                continue

        # If no default found, create and return built-in default
        return self._create_builtin_default()

    def set_default_profile(self, name):
        """
        Set a profile as the default.

        This clears the default flag from all other profiles and sets it
        on the specified profile.

        Args:
            name: Profile name to set as default

        Raises:
            OSError: If profile doesn't exist

        Usage:
            manager.set_default_profile("noisy_vcr")
        """
        # Load the target profile
        target_profile = self.load_profile(name)

        # Clear default flag from all profiles
        for profile_name in self.list_profiles():
            try:
                profile = self.load_profile(profile_name)
                if profile.is_default and profile.name != name:
                    profile.is_default = False
                    self.save_profile(profile, overwrite=True)
            except Exception:
                continue

        # Set target as default
        target_profile.is_default = True
        self.save_profile(target_profile, overwrite=True)

    def validate_profile(self, profile):
        """
        Validate a therapy profile.

        Args:
            profile: TherapyProfile to validate

        Returns:
            True if valid, False otherwise

        Usage:
            if manager.validate_profile(profile):
                manager.save_profile(profile)
        """
        return profile.is_valid()

    def _create_builtin_default(self):
        """
        Create built-in default profile.

        Returns:
            Default TherapyProfile (noisy vCR)

        Usage:
            profile = self._create_builtin_default()
        """
        import time

        timestamp = str(int(time.time()))

        profile = TherapyProfile(
            name="noisy_vcr_default",
            description="Default Noisy vCR therapy profile with 23.5% jitter",
            config=create_default_therapy_config(),
            is_default=True,
            created_at=timestamp,
            modified_at=timestamp,
        )

        # Return built-in profile (no need to save to disk - it's defined in code)
        return profile

