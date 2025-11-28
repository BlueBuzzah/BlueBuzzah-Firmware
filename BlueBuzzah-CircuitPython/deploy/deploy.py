#!/usr/bin/env python3
"""
BlueBuzzah v2 Deployment Tool
==============================

Automated deployment system for BlueBuzzah bilateral haptic therapy devices.

This tool provides streamlined deployment of firmware to PRIMARY and SECONDARY
devices with automatic role assignment, library installation, and validation.

Features:
    - Auto-detection of CIRCUITPY or BLUEBUZZAH drives (macOS/Linux/Windows)
    - Automatic role assignment for 2-device deployments
    - Interactive mode for manual device selection
    - Clean deployment option (wipe before deploy)
    - Comprehensive deployment validation
    - Colored terminal output for status feedback

Usage:
    # Auto-detect and deploy
    python deploy.py

    # Deploy PRIMARY to specific device
    python deploy.py --role primary --device /Volumes/CIRCUITPY

    # Deploy to both devices (auto-assign roles)
    python deploy.py --role both

    # Clean deployment (wipe device first)
    python deploy.py --clean

Module: deploy.deploy
Version: 2.0.0
"""

import argparse
import json
import os
import shutil
import string
import sys
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import List, Optional


# ANSI color codes for terminal output
class Colors:
    """ANSI color codes for terminal output."""

    HEADER = "\033[95m"
    OKBLUE = "\033[94m"
    OKCYAN = "\033[96m"
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"
    UNDERLINE = "\033[4m"

    @classmethod
    def disable(cls):
        """Disable color output."""
        cls.HEADER = ""
        cls.OKBLUE = ""
        cls.OKCYAN = ""
        cls.OKGREEN = ""
        cls.WARNING = ""
        cls.FAIL = ""
        cls.ENDC = ""
        cls.BOLD = ""
        cls.UNDERLINE = ""


class DeviceRole(Enum):
    """Device role in the bilateral therapy system."""

    PRIMARY = "Primary"
    SECONDARY = "Secondary"

    def __str__(self) -> str:
        return self.value


@dataclass
class DeviceInfo:
    """Information about a detected CIRCUITPY or BLUEBUZZAH device."""

    path: Path
    role: Optional[DeviceRole] = None

    def __str__(self) -> str:
        role_str = f" ({self.role})" if self.role else ""
        return f"{self.path}{role_str}"


@dataclass
class DeploymentConfig:
    """Deployment configuration settings."""

    clean_deploy: bool = False
    validate: bool = True
    backup: bool = True
    verbose: bool = False


class BlueBuzzahDeployer:
    """
    Automated deployment tool for PRIMARY and SECONDARY devices.

    This class handles all aspects of deploying BlueBuzzah firmware to
    CircuitPython devices, including:
        - Device auto-detection
        - Source file copying
        - Library installation
        - Role-specific configuration
        - Deployment validation

    Attributes:
        config: Deployment configuration settings
        dist_path: Path to dist/ directory (built files ready for deployment)
        deploy_config_path: Path to deploy/config.json
    """

    def __init__(self, base_path: Optional[Path] = None):
        """
        Initialize the deployer.

        Args:
            base_path: Base path to BlueBuzzah project (default: auto-detect)
        """
        if base_path is None:
            # Auto-detect base path (assume we're in deploy/)
            base_path = Path(__file__).parent.parent

        self.base_path = base_path
        self.dist_path = base_path / "dist"  # Built files ready for deployment
        self.deploy_config_path = Path(__file__).parent / "config.json"
        self.config = DeploymentConfig()

        # Load deployment configuration
        self.deploy_settings = self._load_deploy_config()

    def _load_deploy_config(self) -> dict:
        """Load deployment configuration from config.json."""
        try:
            with open(self.deploy_config_path, "r") as f:
                return json.load(f)
        except (FileNotFoundError, json.JSONDecodeError) as e:
            print(
                f"{Colors.WARNING}Warning: Could not load config.json: {e}{Colors.ENDC}"
            )
            # Return default configuration
            return {
                "deployment": {
                    "clean_by_default": False,
                    "validate_after_deploy": True,
                    "backup_before_deploy": True,
                },
                "libraries": {
                    "required": [
                        "adafruit_ble",
                        "adafruit_drv2605",
                        "adafruit_tca9548a",
                        "neopixel",
                    ]
                },
                "ignore_patterns": ["__pycache__", "*.pyc", ".DS_Store", "tests/"],
            }

    def auto_detect_devices(self) -> List[DeviceInfo]:
        """
        Auto-detect connected CIRCUITPY or BLUEBUZZAH drives.

        Searches for CIRCUITPY or BLUEBUZZAH volumes on all supported platforms:
            - macOS: /Volumes/CIRCUITPY* or /Volumes/BLUEBUZZAH*
            - Linux: /media/*/CIRCUITPY* or /media/*/BLUEBUZZAH*
            - Windows: Drive letters with CIRCUITPY or BLUEBUZZAH

        Returns:
            List of DeviceInfo objects for detected devices

        Example:
            devices = deployer.auto_detect_devices()
            print(f"Found {len(devices)} device(s)")
        """
        devices = []

        if sys.platform == "darwin":  # macOS
            volumes = Path("/Volumes")
            if volumes.exists():
                devices = [
                    DeviceInfo(path=d)
                    for d in volumes.iterdir()
                    if d.is_dir() and ("CIRCUITPY" in d.name or "BLUEBUZZAH" in d.name)
                ]

        elif sys.platform.startswith("linux"):  # Linux
            media = Path("/media")
            if media.exists():
                for user_dir in media.iterdir():
                    if user_dir.is_dir():
                        devices.extend(
                            [
                                DeviceInfo(path=d)
                                for d in user_dir.iterdir()
                                if d.is_dir() and ("CIRCUITPY" in d.name or "BLUEBUZZAH" in d.name)
                            ]
                        )

        elif sys.platform == "win32":  # Windows
            import wmi

            c = wmi.WMI()
            for drive in c.Win32_LogicalDisk():
                if drive.VolumeName and ("CIRCUITPY" in drive.VolumeName or "BLUEBUZZAH" in drive.VolumeName):
                    devices.append(DeviceInfo(path=Path(f"{drive.DeviceID}\\")))

        return sorted(devices, key=lambda d: d.path.name)

    def wipe_device(self, device_path: Path) -> bool:
        """
        Clean device by removing old files.

        Removes all files and directories from the device except:
            - boot_out.txt (CircuitPython system file)
            - .fseventsd, .Trashes, .Spotlight-V100 (macOS system files)

        Args:
            device_path: Path to CIRCUITPY device

        Returns:
            True if successful, False otherwise
        """
        try:
            # Files/directories to preserve
            preserve = {
                "boot_out.txt",
                ".fseventsd",
                ".Trashes",
                ".Spotlight-V100",
                ".metadata_never_index",
                ".TemporaryItems",
            }

            for item in device_path.iterdir():
                if item.name in preserve:
                    continue

                # Skip macOS AppleDouble metadata files (._filename)
                if item.name.startswith("._"):
                    if self.config.verbose:
                        print(f"    Skipped macOS metadata: {item.name}")
                    continue

                try:
                    if item.is_dir():
                        shutil.rmtree(item)
                        if self.config.verbose:
                            print(f"    Removed directory: {item.name}")
                    else:
                        item.unlink()
                        if self.config.verbose:
                            print(f"    Removed file: {item.name}")
                except Exception as e:
                    print(
                        f"    {Colors.WARNING}Warning: Could not remove {item.name}: {e}{Colors.ENDC}"
                    )

            print(f"    {Colors.OKGREEN}✓{Colors.ENDC} Device cleaned")
            return True

        except Exception as e:
            print(f"  {Colors.FAIL}✗ Error cleaning device: {e}{Colors.ENDC}")
            return False

    def copy_libraries(self, device_path: Path, role: DeviceRole) -> bool:
        """
        Copy required CircuitPython libraries to device.

        NOTE: This method is deprecated - libraries are now copied
        directly from lib/ folder in deploy_to_device().

        Copies library files from lib/ to device's lib/ directory.
        Libraries are specified in deploy/config.json.

        Args:
            device_path: Path to CIRCUITPY device
            role: Device role (both roles use same libraries)

        Returns:
            True if successful, False otherwise
        """
        try:
            device_lib = device_path / "lib"
            device_lib.mkdir(exist_ok=True)

            required_libs = self.deploy_settings["libraries"]["required"]

            for lib_name in required_libs:
                # Check for both directory and .mpy file
                lib_dir = self.lib_path / lib_name
                lib_file = self.lib_path / f"{lib_name}.mpy"

                if lib_dir.exists() and lib_dir.is_dir():
                    # Copy directory
                    dest = device_lib / lib_name
                    if dest.exists():
                        shutil.rmtree(dest)
                    shutil.copytree(lib_dir, dest)
                    print(f"    {Colors.OKGREEN}✓{Colors.ENDC} Copied {lib_name}/")

                elif lib_file.exists():
                    # Copy .mpy file
                    shutil.copy2(lib_file, device_lib / lib_file.name)
                    print(f"    {Colors.OKGREEN}✓{Colors.ENDC} Copied {lib_file.name}")

                else:
                    print(
                        f"    {Colors.WARNING}⚠{Colors.ENDC} Library not found: {lib_name}"
                    )

            return True

        except Exception as e:
            print(f"  {Colors.FAIL}✗ Error copying libraries: {e}{Colors.ENDC}")
            return False

    def write_device_config(self, device_path: Path, role: DeviceRole) -> bool:
        """
        Write role-specific configuration to device.

        Creates settings.json on the device with the specified role.
        This file is read by the device's config.py at startup.

        Args:
            device_path: Path to CIRCUITPY device
            role: Device role (PRIMARY or SECONDARY)

        Returns:
            True if successful, False otherwise

        Example settings.json:
            {
                "deviceRole": "Primary"
            }
        """
        try:
            config = {"deviceRole": role.value}

            config_file = device_path / "settings.json"
            with open(config_file, "w") as f:
                json.dump(config, f, indent=2)

            print(
                f"    {Colors.OKGREEN}✓{Colors.ENDC} Wrote {role} configuration to settings.json"
            )
            return True

        except Exception as e:
            print(f"  {Colors.FAIL}✗ Error writing configuration: {e}{Colors.ENDC}")
            return False

    def validate_deployment(self, device_path: Path, role: DeviceRole) -> bool:
        """
        Validate that deployment was successful.

        Checks for:
            - Required files present (main.py, settings.json)
            - lib/ directory exists
            - settings.json has correct role

        Args:
            device_path: Path to CIRCUITPY device
            role: Expected device role

        Returns:
            True if validation passes, False otherwise
        """
        try:
            required_files = [
                "main.py",
                "settings.json",
                "lib",
            ]

            # Check required files
            for file in required_files:
                if not (device_path / file).exists():
                    print(
                        f"  {Colors.FAIL}✗ Missing required file: {file}{Colors.ENDC}"
                    )
                    return False

            # Verify role configuration
            with open(device_path / "settings.json") as f:
                config = json.load(f)
                if config.get("deviceRole") != role.value:
                    print(
                        f"  {Colors.FAIL}✗ Role mismatch in configuration{Colors.ENDC}"
                    )
                    return False

            print(
                f"    {Colors.OKGREEN}✓{Colors.ENDC} Deployment validated successfully"
            )
            return True

        except Exception as e:
            print(f"  {Colors.FAIL}✗ Validation error: {e}{Colors.ENDC}")
            return False

    def deploy_to_device(self, device: DeviceInfo, role: DeviceRole) -> bool:
        """
        Deploy code to a single device with specified role.

        Complete deployment process:
            1. Verify dist/ directory exists
            2. Clean device (removes old files)
            3. Copy all files from dist/ to device
            4. Write role-specific settings.json
            5. Validate deployment

        Args:
            device: DeviceInfo for target device
            role: Device role (PRIMARY or SECONDARY)

        Returns:
            True if deployment successful, False otherwise

        Example:
            device = DeviceInfo(path=Path("/Volumes/CIRCUITPY"))
            success = deployer.deploy_to_device(device, DeviceRole.PRIMARY)
        """
        print(f"\n{Colors.BOLD}Deploying {role} to {device.path}...{Colors.ENDC}")
        print("=" * 50)

        # 1. Verify dist/ exists
        if not self.dist_path.exists() or not self.dist_path.is_dir():
            print(f"  {Colors.FAIL}✗ dist/ directory not found{Colors.ENDC}")
            print(f"\n{Colors.WARNING}Please build the project first:{Colors.ENDC}")
            print(
                f"  python build/build.py --clean        # Production build (compiled .mpy)"
            )
            print(f"  python build/build.py --no-compile   # Debug build (source .py)")
            return False

        # 2. Clean device (always wipe before deploy)
        print(f"  Cleaning device...")
        if not self.wipe_device(device.path):
            return False

        # 3. Copy all files from dist/
        print(f"  Copying files from dist/...")
        try:
            ignore_patterns = self.deploy_settings.get(
                "ignore_patterns", ["__pycache__", "*.pyc"]
            )

            files_copied = 0
            for item in self.dist_path.iterdir():
                # Skip settings.json (we'll write it later with role)
                # Skip macOS and Python artifacts
                if item.name in [".DS_Store", "__pycache__", "settings.json"]:
                    continue

                dest = device.path / item.name

                if item.is_file():
                    shutil.copy2(item, dest)
                    print(f"    {Colors.OKGREEN}✓{Colors.ENDC} Copied {item.name}")
                    files_copied += 1
                elif item.is_dir():
                    if dest.exists():
                        shutil.rmtree(dest)
                    shutil.copytree(
                        item, dest, ignore=shutil.ignore_patterns(*ignore_patterns)
                    )
                    file_count = sum(1 for _ in dest.rglob("*") if _.is_file())
                    print(
                        f"    {Colors.OKGREEN}✓{Colors.ENDC} Copied {item.name}/ ({file_count} files)"
                    )
                    files_copied += 1

            if files_copied == 0:
                print(f"    {Colors.WARNING}⚠ No files found in dist/{Colors.ENDC}")

        except Exception as e:
            print(f"  {Colors.FAIL}✗ Error copying files: {e}{Colors.ENDC}")
            return False

        # 4. Write role-specific settings.json
        print(f"  Writing device configuration...")
        if not self.write_device_config(device.path, role):
            return False

        # 5. Validate deployment
        if self.config.validate:
            print(f"  Validating deployment...")
            if not self.validate_deployment(device.path, role):
                return False

        print(f"\n{Colors.OKGREEN}✓ Deployment successful!{Colors.ENDC}")
        return True

    def prompt_for_role(self) -> DeviceRole:
        """
        Prompt user to select device role.

        Returns:
            Selected DeviceRole
        """
        print("\nSelect device role:")
        print("  1. PRIMARY")
        print("  2. SECONDARY")

        while True:
            choice = input("Enter choice (1 or 2): ").strip()
            if choice == "1":
                return DeviceRole.PRIMARY
            elif choice == "2":
                return DeviceRole.SECONDARY
            else:
                print("Invalid choice. Please enter 1 or 2.")

    def interactive_multi_deploy(self, devices: List[DeviceInfo]) -> bool:
        """
        Interactive deployment for multiple devices.

        Args:
            devices: List of detected devices

        Returns:
            True if all deployments successful, False otherwise
        """
        print(f"\n{Colors.BOLD}Found {len(devices)} devices:{Colors.ENDC}")
        for i, device in enumerate(devices, 1):
            print(f"  {i}. {device.path}")

        print("\nSelect devices to deploy:")
        print("  Enter device numbers and roles (e.g., '1:primary 2:secondary')")
        print(
            "  Or enter 'auto' to auto-assign PRIMARY and SECONDARY to first two devices"
        )

        selection = input("\nSelection: ").strip().lower()

        if selection == "auto":
            if len(devices) < 2:
                print(
                    f"{Colors.FAIL}Error: Need at least 2 devices for auto mode{Colors.ENDC}"
                )
                return False
            success = True
            success &= self.deploy_to_device(devices[0], DeviceRole.PRIMARY)
            success &= self.deploy_to_device(devices[1], DeviceRole.SECONDARY)
            return success

        # Parse manual selection
        success = True
        for item in selection.split():
            try:
                idx_str, role_str = item.split(":")
                idx = int(idx_str) - 1
                role = (
                    DeviceRole.PRIMARY
                    if role_str.lower() == "primary"
                    else DeviceRole.SECONDARY
                )

                if 0 <= idx < len(devices):
                    success &= self.deploy_to_device(devices[idx], role)
                else:
                    print(
                        f"{Colors.FAIL}Error: Invalid device index: {idx + 1}{Colors.ENDC}"
                    )
                    success = False
            except (ValueError, IndexError):
                print(
                    f"{Colors.FAIL}Error: Invalid selection format: {item}{Colors.ENDC}"
                )
                success = False

        return success

    def deploy_all(self, interactive: bool = True) -> bool:
        """
        Deploy to all detected devices.

        Behavior based on number of devices detected:
            - 0 devices: Error, no devices found
            - 1 device: Prompt for role or use PRIMARY as default
            - 2 devices: Auto-assign PRIMARY and SECONDARY
            - 3+ devices: Interactive selection required

        Args:
            interactive: Enable interactive prompts (default: True)

        Returns:
            True if all deployments successful, False otherwise

        Example:
            deployer = BlueBuzzahDeployer()
            success = deployer.deploy_all()
        """
        print(
            f"\n{Colors.BOLD}{Colors.HEADER}BlueBuzzah Deployment Tool v2.0.0{Colors.ENDC}"
        )
        print("=" * 50)

        # Auto-detect devices
        print("\nScanning for CIRCUITPY/BLUEBUZZAH devices...")
        devices = self.auto_detect_devices()

        if len(devices) == 0:
            print(f"{Colors.FAIL}✗ No CIRCUITPY/BLUEBUZZAH devices found!{Colors.ENDC}")
            print("\nTroubleshooting:")
            print("  1. Ensure device is connected via USB")
            print("  2. Check that CircuitPython is installed")
            print("  3. Verify CIRCUITPY or BLUEBUZZAH drive is mounted")
            return False

        print(f"{Colors.OKGREEN}✓ Found {len(devices)} device(s){Colors.ENDC}")

        if len(devices) == 1:
            # Single device - ask for role
            if interactive:
                role = self.prompt_for_role()
            else:
                role = DeviceRole.PRIMARY  # Default
            return self.deploy_to_device(devices[0], role)

        elif len(devices) == 2:
            # Two devices - auto-assign PRIMARY and SECONDARY
            print(f"\n{Colors.OKCYAN}Auto-assigning roles...{Colors.ENDC}")
            success = True
            success &= self.deploy_to_device(devices[0], DeviceRole.PRIMARY)
            success &= self.deploy_to_device(devices[1], DeviceRole.SECONDARY)

            if success:
                print(f"\n{Colors.BOLD}{'=' * 50}{Colors.ENDC}")
                print(f"{Colors.OKGREEN}Deployment Complete!{Colors.ENDC}")
                print(f"  Primary:   {devices[0].path}")
                print(f"  Secondary: {devices[1].path}")
                print(
                    f"\n{Colors.OKCYAN}Ready to power cycle devices and begin therapy.{Colors.ENDC}"
                )

            return success

        else:
            # Multiple devices - interactive selection
            if interactive:
                return self.interactive_multi_deploy(devices)
            else:
                print(
                    f"{Colors.FAIL}Error: Too many devices for auto-deploy ({len(devices)} found){Colors.ENDC}"
                )
                print("Use interactive mode or specify device path explicitly")
                return False


def main():
    """Main CLI entry point."""
    parser = argparse.ArgumentParser(
        description="Deploy BlueBuzzah v2 firmware to CircuitPython devices",
        epilog="Examples:\n"
        "  python deploy.py                           # Auto-detect and deploy\n"
        "  python deploy.py --role primary            # Deploy PRIMARY role\n"
        "  python deploy.py --role both               # Deploy both devices\n"
        "  python deploy.py --clean                   # Clean deploy\n"
        "  python deploy.py --device /Volumes/CIRCUITPY --role primary\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument(
        "--role", choices=["primary", "secondary", "both"], help="Device role to deploy"
    )

    parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean device before deployment (remove old files)",
    )

    parser.add_argument(
        "--no-validate", action="store_true", help="Skip deployment validation"
    )

    parser.add_argument(
        "--device", help="Specific device path (e.g., /Volumes/CIRCUITPY)"
    )

    parser.add_argument("--verbose", action="store_true", help="Enable verbose output")

    parser.add_argument(
        "--no-color", action="store_true", help="Disable colored output"
    )

    args = parser.parse_args()

    # Disable colors if requested
    if args.no_color:
        Colors.disable()

    # Create deployer
    deployer = BlueBuzzahDeployer()
    deployer.config.clean_deploy = args.clean
    deployer.config.validate = not args.no_validate
    deployer.config.verbose = args.verbose

    success = False

    try:
        if args.device:
            # Deploy to specific device
            device = DeviceInfo(path=Path(args.device), role=None)

            if not device.path.exists():
                print(
                    f"{Colors.FAIL}Error: Device path does not exist: {args.device}{Colors.ENDC}"
                )
                sys.exit(1)

            if args.role == "both":
                print(
                    f"{Colors.FAIL}Error: Cannot deploy 'both' roles to a single device{Colors.ENDC}"
                )
                sys.exit(1)

            role = (
                DeviceRole.PRIMARY if args.role == "primary" else DeviceRole.SECONDARY
            )
            if args.role is None:
                role = deployer.prompt_for_role()

            success = deployer.deploy_to_device(device, role)

        else:
            # Auto-detect and deploy
            if args.role == "both":
                # Force auto-detect of 2 devices
                devices = deployer.auto_detect_devices()
                if len(devices) < 2:
                    print(
                        f"{Colors.FAIL}Error: Need 2 devices for 'both' mode, found {len(devices)}{Colors.ENDC}"
                    )
                    sys.exit(1)
                success = True
                success &= deployer.deploy_to_device(devices[0], DeviceRole.PRIMARY)
                success &= deployer.deploy_to_device(devices[1], DeviceRole.SECONDARY)
            else:
                success = deployer.deploy_all(interactive=(args.role is None))

    except KeyboardInterrupt:
        print(f"\n\n{Colors.WARNING}Deployment cancelled by user{Colors.ENDC}")
        sys.exit(1)
    except Exception as e:
        print(f"\n{Colors.FAIL}Unexpected error: {e}{Colors.ENDC}")
        if deployer.config.verbose:
            import traceback

            traceback.print_exc()
        sys.exit(1)

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
