"""
BlueBuzzah v2 - Boot Configuration
===================================

Boot-time configuration for CircuitPython.

This file is executed before code.py and configures:
- USB drive visibility
- REPL (serial console) access
- USB device name
- Storage permissions

Boot configuration is critical for deployment:
- Development: USB drive enabled, REPL enabled
- Production: USB drive disabled, REPL disabled for security

The boot.py file is executed on device boot, before code.py.

Module: boot
Version: 2.0.0
"""

import storage
import usb_cdc
import usb_hid

# =============================================================================
# Configuration
# =============================================================================

# Development mode - set to False for deployed devices
DEVELOPMENT_MODE = True

# Device name for USB
DEVICE_NAME = "BlueBuzzah v2"

# =============================================================================
# Boot Configuration
# =============================================================================

print("=" * 60)
print("BlueBuzzah v2 - Boot Configuration")
print("=" * 60)

if DEVELOPMENT_MODE:
    print("Mode: DEVELOPMENT")
    print("  - USB drive: ENABLED")
    print("  - REPL: ENABLED")
    print("  - Storage: READ/WRITE")
    print()
    print("WARNING: Development mode should NOT be used for deployment!")
    print("Set DEVELOPMENT_MODE = False for production devices.")

else:
    print("Mode: PRODUCTION")
    print("  - USB drive: DISABLED")
    print("  - REPL: ENABLED (for debugging)")
    print("  - Storage: READ-ONLY")
    print()
    print("Production mode active - device is ready for deployment.")

print("=" * 60)
print()

# =============================================================================
# USB Drive Configuration
# =============================================================================

if not DEVELOPMENT_MODE:
    # Production mode - disable USB drive for deployed devices
    # This prevents users from accidentally modifying code
    try:
        storage.disable_usb_drive()
        print("[Boot] USB drive disabled (production mode)")
    except Exception as e:
        print("[Boot] WARNING: Failed to disable USB drive: {}".format(e))

else:
    # Development mode - keep USB drive enabled
    print("[Boot] USB drive enabled (development mode)")

# =============================================================================
# Storage Permissions
# =============================================================================

if not DEVELOPMENT_MODE:
    # Production mode - make storage read-only from CircuitPython
    # This prevents code from modifying itself
    try:
        storage.remount("/", readonly=True)
        print("[Boot] Storage mounted as read-only (production mode)")
    except Exception as e:
        print("[Boot] WARNING: Failed to remount storage: {}".format(e))

else:
    # Development mode - allow write access
    print("[Boot] Storage writable (development mode)")

# =============================================================================
# REPL Configuration
# =============================================================================

# Keep REPL enabled for both development and production
# This allows debugging via serial console if needed
# Note: REPL is only accessible via USB serial, not Bluetooth
try:
    usb_cdc.enable(console=True, data=False)
    print("[Boot] REPL enabled via USB serial")
except Exception as e:
    print("[Boot] WARNING: Failed to enable REPL: {}".format(e))

# =============================================================================
# USB HID Configuration
# =============================================================================

# Disable USB HID (keyboard/mouse emulation)
# BlueBuzzah doesn't need HID functionality
try:
    usb_hid.disable()
    print("[Boot] USB HID disabled")
except Exception as e:
    print("[Boot] WARNING: Failed to disable USB HID: {}".format(e))

# =============================================================================
# Device Name
# =============================================================================

# Set USB device name (appears in device manager/system info)
try:
    import supervisor

    supervisor.set_usb_identification(manufacturer="BlueBuzzah", product=DEVICE_NAME)
    print("[Boot] USB device name: {}".format(DEVICE_NAME))
except ImportError:
    # supervisor module may not be available on all CircuitPython versions
    print("[Boot] WARNING: supervisor module not available, could not set device name")
except Exception as e:
    print("[Boot] WARNING: Failed to set device name: {}".format(e))

# =============================================================================
# Boot Complete
# =============================================================================

print()
print("[Boot] Configuration complete - proceeding to code.py")
print()
