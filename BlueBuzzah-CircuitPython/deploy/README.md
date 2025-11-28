# BlueBuzzah v2 Deployment System

Automated deployment tool for BlueBuzzah bilateral haptic therapy devices.

## Overview

The deployment system provides a streamlined way to deploy firmware to PRIMARY and SECONDARY CircuitPython devices with automatic role assignment, library installation, and comprehensive validation.

### Features

- **Auto-detection** - Automatically finds CIRCUITPY or BLUEBUZZAH drives on macOS, Linux, and Windows
- **Smart role assignment** - Automatically assigns PRIMARY/SECONDARY roles for 2-device deployments
- **Interactive mode** - Guided deployment with user prompts for manual control
- **Clean deployment** - Optional device wipe before deployment
- **Library management** - Automatic installation of required CircuitPython libraries
- **Validation** - Comprehensive post-deployment checks
- **Colored output** - Clear visual feedback with status indicators

## Quick Start

### Basic Usage

```bash
# Auto-detect and deploy to connected devices
python deploy/deploy.py

# Deploy to 2 devices with auto-assigned roles
python deploy/deploy.py --role both

# Deploy PRIMARY role to specific device
python deploy/deploy.py --role primary --device /Volumes/CIRCUITPY

# Clean deployment (wipe device first)
python deploy/deploy.py --clean
```

### Requirements

- Python 3.7+
- CircuitPython devices connected via USB
- Source code in `src/`
- Libraries in `lib/`

## Command Line Options

### Role Selection

```bash
--role {primary,secondary,both}
```

Specify the device role to deploy:

- `primary` - Deploy PRIMARY device (initiates therapy, controls timing)
- `secondary` - Deploy SECONDARY device (follows PRIMARY commands)
- `both` - Deploy both roles to 2 detected devices

If not specified, the tool will prompt interactively.

### Device Selection

```bash
--device PATH
```

Deploy to a specific device path instead of auto-detecting:

```bash
# macOS
python deploy/deploy.py --device /Volumes/CIRCUITPY --role primary

# Linux
python deploy/deploy.py --device /media/username/CIRCUITPY --role primary

# Windows
python deploy/deploy.py --device D:\ --role primary
```

### Deployment Options

```bash
--clean
```

Clean the device before deployment by removing all existing files (except system files).

**Warning:** This will delete all files on the device!

```bash
--no-validate
```

Skip post-deployment validation checks.

```bash
--verbose
```

Enable verbose output showing detailed file operations.

```bash
--no-color
```

Disable colored terminal output (useful for log files or unsupported terminals).

## Deployment Process

The deployment tool performs the following steps:

### 1. Device Detection

Automatically scans for CIRCUITPY drives:

- **macOS**: `/Volumes/CIRCUITPY*`
- **Linux**: `/media/*/CIRCUITPY*`
- **Windows**: Drive letters with CIRCUITPY volume name

### 2. Role Assignment

Based on the number of detected devices:

| Devices | Behavior                                          |
| ------- | ------------------------------------------------- |
| 0       | Error - no devices found                          |
| 1       | Prompt for role (PRIMARY or SECONDARY)            |
| 2       | Auto-assign PRIMARY to first, SECONDARY to second |
| 3+      | Interactive selection required                    |

### 3. File Deployment

Copies the following to the device:

**Required files:**

- `code.py` - Main entry point
- `src/` - Complete source directory
- `settings.json` - Role-specific configuration

**Optional files:**

- `boot.py` - Boot configuration (if exists)
- `profiles/` - Therapy profiles (if exists)

**Ignored patterns:**

- `__pycache__/`, `*.pyc` - Python cache files
- `.DS_Store` - macOS system files
- `tests/` - Test files
- `docs/` - Documentation

### 4. Library Installation

Installs required CircuitPython libraries from `v2/lib/`:

- `adafruit_ble` - Bluetooth Low Energy support
- `adafruit_drv2605` - Haptic motor driver
- `adafruit_tca9548a` - I2C multiplexer
- `adafruit_itertools` - Iteration utilities
- `neopixel` - LED control

### 5. Configuration

Creates `settings.json` on the device:

```json
{
  "deviceRole": "Primary"
}
```

or

```json
{
  "deviceRole": "Secondary"
}
```

This file is read by the device's `src/config/loader.py` at startup.

### 6. Validation

Verifies successful deployment:

- ✓ Required files present
- ✓ settings.json has correct role
- ✓ Source directory structure intact
- ✓ Libraries installed

## Usage Examples

### Example 1: First-Time Deployment

Connect both devices via USB, then:

```bash
$ python deploy/deploy.py

BlueBuzzah Deployment Tool v2.0.0
==================================================

Scanning for CIRCUITPY devices...
✓ Found 2 device(s)

Auto-assigning roles...

Deploying Primary to /Volumes/CIRCUITPY...
==================================================
  Copying source files...
    ✓ Copied src/
    ✓ Copied code.py
  Writing device configuration...
    ✓ Wrote Primary configuration to settings.json
  Installing libraries...
    ✓ Copied adafruit_ble/
    ✓ Copied adafruit_drv2605.mpy
    ✓ Copied adafruit_tca9548a.mpy
    ✓ Copied adafruit_itertools/
    ✓ Copied neopixel.mpy
  Validating deployment...
    ✓ Deployment validated successfully

✓ Deployment successful!

Deploying Secondary to /Volumes/CIRCUITPY1...
==================================================
  [... similar output ...]

==================================================
Deployment Complete!
  Primary:   /Volumes/CIRCUITPY
  Secondary: /Volumes/CIRCUITPY1

Ready to power cycle devices and begin therapy.
```

### Example 2: Update Single Device

Update only the PRIMARY device with clean deployment:

```bash
$ python deploy/deploy.py --device /Volumes/CIRCUITPY --role primary --clean

Deploying Primary to /Volumes/CIRCUITPY...
==================================================
  Cleaning device...
    Removed directory: src
    Removed file: code.py
    Removed file: settings.json
  ✓ Device cleaned
  Copying source files...
    ✓ Copied src/
    ✓ Copied code.py
  Writing device configuration...
    ✓ Wrote Primary configuration to settings.json
  Installing libraries...
    [... library installation ...]
  Validating deployment...
    ✓ Deployment validated successfully

✓ Deployment successful!
```

### Example 3: Interactive Multi-Device Selection

With 3+ devices connected:

```bash
$ python deploy/deploy.py

Found 3 devices:
  1. /Volumes/CIRCUITPY
  2. /Volumes/CIRCUITPY1
  3. /Volumes/CIRCUITPY2

Select devices to deploy:
  Enter device numbers and roles (e.g., '1:primary 2:secondary')
  Or enter 'auto' to auto-assign PRIMARY and SECONDARY to first two devices

Selection: 1:primary 3:secondary

[... deployment proceeds for devices 1 and 3 ...]
```

### Example 4: Development Workflow

Quick redeployment during development (no validation for speed):

```bash
$ python deploy/deploy.py --no-validate --no-color > deploy.log
```

## Configuration File

The deployment system is configured via `deploy/config.json`:

### Deployment Settings

```json
"deployment": {
  "clean_by_default": false,        // Clean device before each deploy
  "validate_after_deploy": true,    // Run validation after deploy
  "backup_before_deploy": true,     // Backup existing files (future)
  "auto_detect_timeout": 5          // Device detection timeout
}
```

### Required Libraries

```json
"libraries": {
  "required": [
    "adafruit_ble",
    "adafruit_drv2605",
    "adafruit_tca9548a",
    "adafruit_itertools",
    "neopixel"
  ],
  "optional": [
    "adafruit_debug"
  ]
}
```

### Ignore Patterns

```json
"ignore_patterns": [
  "__pycache__",
  "*.pyc",
  ".DS_Store",
  "*.swp",
  "tests/",
  "docs/"
]
```

## Troubleshooting

### No devices found

**Problem:** `✗ No CIRCUITPY devices found!`

**Solutions:**

1. Verify device is connected via USB
2. Check that CircuitPython is installed on the device
3. Ensure CIRCUITPY drive is mounted:
   - macOS: Check `/Volumes/`
   - Linux: Check `/media/username/`
   - Windows: Check drive letters in File Explorer

### Permission denied

**Problem:** `Error copying files: Permission denied`

**Solutions:**

1. macOS/Linux: Ensure device is not write-protected
2. Close any applications accessing the device (e.g., serial monitor, code editor)
3. Try unmounting and remounting the device
4. Check file system permissions

### Validation failed

**Problem:** `✗ Validation error: Missing required file: code.py`

**Solutions:**

1. Check that source files exist in `v2/src/`
2. Verify `code.py` exists in `v2/`
3. Re-run deployment with `--verbose` flag for details
4. Try clean deployment with `--clean` flag

### Library not found

**Problem:** `⚠ Library not found: adafruit_ble`

**Solutions:**

1. Download required libraries from CircuitPython Library Bundle
2. Place in `v2/lib/` directory:
   - Directory bundles: `v2/lib/adafruit_ble/`
   - Single files: `v2/lib/adafruit_drv2605.mpy`
3. Match CircuitPython version (e.g., 8.x bundle for CircuitPython 8.x)

### Role mismatch

**Problem:** `✗ Role mismatch in configuration`

**Solutions:**

1. Manually check `settings.json` on device
2. Delete `settings.json` and re-run deployment
3. Verify deployment completed fully

## Advanced Usage

### Programmatic Deployment

Use the deployer as a Python module:

```python
from pathlib import Path
from deploy import BlueBuzzahDeployer, DeviceInfo, DeviceRole

# Create deployer
deployer = BlueBuzzahDeployer()
deployer.config.clean_deploy = True
deployer.config.verbose = True

# Deploy to specific device
device = DeviceInfo(path=Path("/Volumes/CIRCUITPY"))
success = deployer.deploy_to_device(device, DeviceRole.PRIMARY)

if success:
    print("Deployment successful!")
```

### Custom Deployment Script

Create a custom deployment workflow:

```python
from deploy import BlueBuzzahDeployer, DeviceRole

def deploy_test_devices():
    """Deploy to test devices with custom configuration."""
    deployer = BlueBuzzahDeployer()

    # Find devices
    devices = deployer.auto_detect_devices()

    # Deploy with custom settings
    for device, role in zip(devices[:2], [DeviceRole.PRIMARY, DeviceRole.SECONDARY]):
        print(f"Deploying {role} to {device.path}")
        deployer.deploy_to_device(device, role)

        # Run custom post-deployment tasks
        custom_validation(device.path)

if __name__ == "__main__":
    deploy_test_devices()
```

## Device Configuration

### settings.json Format

The deployment tool creates a minimal `settings.json` on each device:

```json
{
  "deviceRole": "Primary"
}
```

This is read by `src/config/loader.py`:

```python
from src.core.types import DeviceConfig

# Load configuration at device startup
config = DeviceConfig.from_settings_file("/settings.json")

if config.role.is_primary():
    # PRIMARY device behavior
    advertise_ble()
    wait_for_connections()
else:
    # SECONDARY device behavior
    scan_for_primary()
    connect_to_primary()
```

### Role Behavior

**PRIMARY Device:**

- Advertises as "BlueBuzzah" via BLE
- Accepts connections from SECONDARY and phone
- Controls therapy timing and synchronization
- Processes commands from phone app
- Sends sync commands to SECONDARY

**SECONDARY Device:**

- Scans for "BlueBuzzah" PRIMARY device
- Connects to PRIMARY via BLE
- Follows PRIMARY timing commands
- Mirrors therapy patterns
- Reports status to PRIMARY

## Continuous Integration

### GitHub Actions Example

```yaml
name: Deploy to Hardware

on:
  push:
    tags:
      - "v*"

jobs:
  deploy:
    runs-on: [self-hosted, hardware]

    steps:
      - uses: actions/checkout@v3

      - name: Set up Python
        uses: actions/setup-python@v4
        with:
          python-version: "3.11"

      - name: Deploy to test devices
        run: |
          python deploy/deploy.py --role both --no-validate

      - name: Run hardware tests
        run: |
          python tests/hardware_test.py
```

## File Structure

```
v2/
├── deploy/
│   ├── deploy.py       # Main deployment script
│   ├── config.json     # Deployment configuration
│   └── README.md       # This file
├── src/                # Source code to deploy
│   ├── config/
│   ├── core/
│   ├── hardware/
│   └── ...
├── lib/                # CircuitPython libraries
│   ├── adafruit_ble/
│   ├── adafruit_drv2605.mpy
│   └── ...
├── code.py             # Main entry point
└── boot.py             # Boot configuration (optional)
```

## Best Practices

1. **Always validate** - Use `--no-validate` only when necessary
2. **Clean deployment for major updates** - Use `--clean` for significant changes
3. **Test with single device first** - Deploy to one device and verify before deploying to both
4. **Keep libraries updated** - Use matching CircuitPython library bundle version
5. **Version control settings** - Track `settings.json` changes for each device
6. **Backup before clean** - Save important device files before using `--clean`
7. **Serial monitor** - Keep a serial monitor connected to see boot messages
8. **Power cycle after deploy** - Always power cycle devices after deployment

## Safety Features

### Protected System Files

The deployment tool never removes these system files:

- `boot_out.txt` - CircuitPython version info
- `.fseventsd/` - macOS file system events
- `.Trashes/` - macOS trash
- `.Spotlight-V100/` - macOS Spotlight index
- `.metadata_never_index` - System metadata
- `.TemporaryItems` - Temporary files

### Validation Checks

Post-deployment validation ensures:

- All required files are present
- Device role configuration is correct
- Source directory structure is intact
- Libraries are installed
- settings.json is valid JSON with correct role

### Error Handling

The deployment tool handles errors gracefully:

- Clear error messages with suggested solutions
- Non-destructive failures (won't corrupt existing deployment)
- Verbose mode for detailed debugging
- Exit codes for script integration (0 = success, 1 = failure)

## Future Enhancements

Planned features for future versions:

- **Backup/restore** - Save and restore device state
- **Incremental deployment** - Deploy only changed files
- **Watch mode** - Auto-deploy on file changes
- **Profile deployment** - Deploy with specific therapy profiles
- **Self-test mode** - Run device self-tests after deployment
- **OTA updates** - Over-the-air updates via BLE
- **Multi-version support** - Maintain multiple firmware versions
- **Rollback capability** - Quick rollback to previous version

## Support

For issues or questions:

1. Check this README for troubleshooting steps
2. Verify your setup meets all requirements
3. Run with `--verbose` flag for detailed output
4. Check device serial output for boot messages
5. Review `deploy/config.json` for configuration issues

## Version History

### v2.0.0 (Current)

- Initial deployment system for BlueBuzzah v2
- Auto-detection for macOS, Linux, Windows
- Smart role assignment
- Interactive and CLI modes
- Comprehensive validation
- Colored terminal output

---

**BlueBuzzah v2 Deployment System**
Part of the BlueBuzzah bilateral haptic therapy platform
Version 2.0.0
