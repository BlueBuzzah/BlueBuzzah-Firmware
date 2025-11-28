"""
BlueBuzzah Arduino Firmware - Cross-Platform Deploy Script
==========================================================

Usage (from BlueBuzzah-Arduino directory):
    python deploy.py          # Interactive deploy
    python deploy.py --list   # List connected devices

Deployment:
    - 1 device:  Uploads firmware, prompts for role (P/S/N)
    - 2 devices: Prompts for PRIMARY/SECONDARY assignment,
                 uploads to both, configures roles automatically

Requirements:
    - PlatformIO CLI (pio)
    - Connected Adafruit Feather nRF52840 device(s)

Note: Automatically uses PlatformIO's Python if pyserial is not available.
"""

import sys
import os
import time
import subprocess


# =============================================================================
# Auto-bootstrap: Re-run with PlatformIO's Python if pyserial not available
# =============================================================================

def find_pio_python():
    """Find PlatformIO's bundled Python interpreter (cross-platform)"""
    home = os.path.expanduser("~")

    if sys.platform == "win32":
        candidates = [
            os.path.join(home, ".platformio", "penv", "Scripts", "python.exe"),
            os.path.join(home, ".platformio", "python3", "python.exe"),
        ]
    else:
        candidates = [
            os.path.join(home, ".platformio", "penv", "bin", "python"),
            os.path.join(home, ".platformio", "penv", "bin", "python3"),
            os.path.join(home, ".platformio", "python3", "bin", "python3"),
        ]

    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def bootstrap_with_pio_python():
    """Re-execute this script using PlatformIO's Python"""
    pio_python = find_pio_python()
    if not pio_python:
        print("Error: Could not find PlatformIO's Python.")
        print("Make sure PlatformIO is installed: pip install platformio")
        sys.exit(1)

    # Re-run this script with PlatformIO's Python
    script_path = os.path.abspath(__file__)
    os.execv(pio_python, [pio_python, script_path] + sys.argv[1:])


# Check for pyserial, bootstrap if needed
try:
    import serial
    import serial.tools.list_ports
except ImportError:
    # Not running in PlatformIO's Python - re-execute with it
    bootstrap_with_pio_python()


# =============================================================================
# Cross-Platform Color Support
# =============================================================================

class Colors:
    """ANSI color codes with Windows support"""

    def __init__(self):
        self.enabled = True
        # Enable ANSI on Windows 10+
        if sys.platform == "win32":
            try:
                import ctypes
                kernel32 = ctypes.windll.kernel32
                kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
            except Exception:
                self.enabled = False

    @property
    def CYAN(self):
        return "\033[0;36m" if self.enabled else ""

    @property
    def GREEN(self):
        return "\033[0;32m" if self.enabled else ""

    @property
    def YELLOW(self):
        return "\033[0;33m" if self.enabled else ""

    @property
    def RED(self):
        return "\033[0;31m" if self.enabled else ""

    @property
    def NC(self):
        return "\033[0m" if self.enabled else ""


C = Colors()


# =============================================================================
# Device Detection (Cross-Platform)
# =============================================================================

# Adafruit Vendor ID
ADAFRUIT_VID = 0x239A

# Known Feather nRF52840 Product IDs
FEATHER_PIDS = [
    0x8029,  # Feather nRF52840 Express
    0x0029,  # Feather nRF52840 Express (bootloader)
    0x802A,  # Feather nRF52840 Sense
    0x002A,  # Feather nRF52840 Sense (bootloader)
]


def find_devices():
    """Find connected Feather nRF52840 devices (cross-platform)"""
    devices = []
    for port in serial.tools.list_ports.comports():
        # Match by Adafruit VID and known PIDs
        if port.vid == ADAFRUIT_VID and port.pid in FEATHER_PIDS:
            devices.append(port.device)
        # Fallback: match by description
        elif port.description and "nRF52" in port.description:
            devices.append(port.device)
    return sorted(devices)


def list_devices():
    """List connected devices and exit"""
    print(f"\n{C.CYAN}Connected Feather devices:{C.NC}\n")
    devices = find_devices()
    if not devices:
        print(f"{C.YELLOW}No devices found.{C.NC}")
        print("Make sure your Feather nRF52840 is connected via USB.")
    else:
        for dev in devices:
            print(f"  - {dev}")
    print()
    return devices


# =============================================================================
# Serial Communication
# =============================================================================

def configure_role(port, role, retries=3):
    """Send role configuration command to device via serial with retry logic"""
    print(f"{C.YELLOW}Configuring role as {role}...{C.NC}")
    for attempt in range(retries):
        try:
            with serial.Serial(port, 115200, timeout=2) as ser:
                time.sleep(0.5)  # Let serial settle
                command = f"SET_ROLE:{role}\n"
                ser.write(command.encode())
                time.sleep(0.5)
                # Try to read response
                response = ser.read(100).decode(errors='ignore')
                if response:
                    print(f"  Response: {response.strip()}")
            return True
        except serial.SerialException as e:
            if attempt < retries - 1:
                print(f"  {C.YELLOW}Retry {attempt + 1}/{retries}...{C.NC}")
                time.sleep(2)
            else:
                print(f"{C.RED}Serial error after {retries} attempts: {e}{C.NC}")
                return False
    return False


# =============================================================================
# PlatformIO Integration
# =============================================================================

# Project directory - set when running as PlatformIO script
_pio_project_dir = [None]


def get_project_dir():
    """Get the project directory (works both as PIO script and standalone)"""
    if _pio_project_dir[0]:
        return _pio_project_dir[0]
    # Standalone mode - use script location
    return os.path.dirname(os.path.abspath(__file__))


def run_pio_command(args):
    """Run a PlatformIO command with full terminal passthrough"""
    cmd = ["pio"] + args
    print(f"{C.YELLOW}Running: {' '.join(cmd)}{C.NC}")
    # Use inherited stdin/stdout/stderr for proper terminal handling
    # This is needed for nrfutil upload which monitors for port changes
    result = subprocess.run(
        cmd,
        cwd=get_project_dir(),
        stdin=sys.stdin,
        stdout=sys.stdout,
        stderr=sys.stderr
    )
    return result.returncode == 0


def build_firmware():
    """Build the firmware"""
    print(f"\n{C.CYAN}Building firmware...{C.NC}\n")
    if run_pio_command(["run"]):
        print(f"\n{C.GREEN}Build complete!{C.NC}")
        return True
    else:
        print(f"\n{C.RED}Build failed!{C.NC}")
        return False


def upload_firmware(port):
    """Upload firmware to specified port"""
    print(f"\n{C.YELLOW}Uploading firmware to {port}...{C.NC}\n")
    if run_pio_command(["run", "-t", "upload", "--upload-port", port]):
        print(f"\n{C.GREEN}Upload complete!{C.NC}")
        return True
    else:
        print(f"\n{C.RED}Upload failed!{C.NC}")
        return False


# =============================================================================
# Deployment Workflows
# =============================================================================

def deploy_single_device(device):
    """Deploy to a single device with interactive role selection"""
    print(f"\n{C.GREEN}Found 1 device:{C.NC}\n")
    print(f"  1) {device}")

    if not upload_firmware(device):
        return False

    # Interactive role selection
    print()
    print("Configure role? [P]rimary, [S]econdary, or [N]one: ", end="", flush=True)
    choice = input().strip().upper()
    if choice == "P":
        print(f"\n{C.YELLOW}Waiting for device to restart...{C.NC}")
        time.sleep(5)
        configure_role(device, "PRIMARY")
        print(f"\n{C.GREEN}=== Device configured as PRIMARY! ==={C.NC}\n")
    elif choice == "S":
        print(f"\n{C.YELLOW}Waiting for device to restart...{C.NC}")
        time.sleep(5)
        configure_role(device, "SECONDARY")
        print(f"\n{C.GREEN}=== Device configured as SECONDARY! ==={C.NC}\n")
    else:
        print(f"\n{C.GREEN}=== Deploy complete (no role configured) ==={C.NC}\n")

    return True


def deploy_two_devices(devices):
    """Deploy to exactly 2 devices with auto-assigned roles (first=PRIMARY, second=SECONDARY)"""
    primary_dev = devices[0]
    secondary_dev = devices[1]

    print(f"\n{C.GREEN}Found 2 devices:{C.NC}\n")
    print(f"  PRIMARY:   {primary_dev}")
    print(f"  SECONDARY: {secondary_dev}")
    print()

    print("Proceed with deployment? [Y/n]: ", end="", flush=True)
    confirm = input().strip().lower()
    if confirm == 'n':
        print("Aborted.")
        return False

    # Execute deployment
    print(f"\n{C.YELLOW}[1/4] Uploading to PRIMARY ({primary_dev})...{C.NC}")
    if not upload_firmware(primary_dev):
        return False

    print(f"\n{C.YELLOW}[2/4] Uploading to SECONDARY ({secondary_dev})...{C.NC}")
    if not upload_firmware(secondary_dev):
        return False

    print(f"\n{C.YELLOW}[3/4] Waiting for devices to restart...{C.NC}")
    time.sleep(5)

    print(f"\n{C.YELLOW}[4/4] Configuring roles...{C.NC}")
    configure_role(primary_dev, "PRIMARY")
    time.sleep(1)
    configure_role(secondary_dev, "SECONDARY")

    print(f"\n{C.GREEN}=== Both devices deployed and configured! ==={C.NC}")
    print(f"  PRIMARY:   {primary_dev}")
    print(f"  SECONDARY: {secondary_dev}")
    print()

    return True


def deploy_multiple_devices(devices):
    """Interactive deployment to 3+ devices with manual role assignment"""
    print(f"\n{C.GREEN}Found {len(devices)} devices:{C.NC}\n")
    for i, dev in enumerate(devices, 1):
        print(f"  {i}) {dev}")

    print(f"\n{C.CYAN}Assign device roles for deployment:{C.NC}\n")

    # Get PRIMARY selection
    print("Which device is PRIMARY? (enter number, or 'q' to quit): ", end="", flush=True)
    primary_choice = input().strip()
    if primary_choice.lower() == 'q':
        print("Aborted.")
        return False

    # Get SECONDARY selection
    print("Which device is SECONDARY? (enter number, or 'q' to quit): ", end="", flush=True)
    secondary_choice = input().strip()
    if secondary_choice.lower() == 'q':
        print("Aborted.")
        return False

    # Validate selections
    try:
        primary_idx = int(primary_choice) - 1
        secondary_idx = int(secondary_choice) - 1
    except ValueError:
        print(f"{C.RED}Invalid selection!{C.NC}")
        return False

    if primary_idx == secondary_idx:
        print(f"{C.RED}Error: PRIMARY and SECONDARY must be different devices!{C.NC}")
        return False

    if not (0 <= primary_idx < len(devices)):
        print(f"{C.RED}Invalid PRIMARY selection!{C.NC}")
        return False

    if not (0 <= secondary_idx < len(devices)):
        print(f"{C.RED}Invalid SECONDARY selection!{C.NC}")
        return False

    primary_dev = devices[primary_idx]
    secondary_dev = devices[secondary_idx]

    # Confirm deployment plan
    print(f"\n{C.CYAN}Deployment plan:{C.NC}")
    print(f"  PRIMARY:   {primary_dev}")
    print(f"  SECONDARY: {secondary_dev}")
    print()

    print("Proceed? [y/N]: ", end="", flush=True)
    confirm = input().strip().lower()
    if confirm != 'y':
        print("Aborted.")
        return False

    # Execute deployment
    print(f"\n{C.YELLOW}[1/4] Uploading to PRIMARY ({primary_dev})...{C.NC}")
    if not upload_firmware(primary_dev):
        return False

    print(f"\n{C.YELLOW}[2/4] Uploading to SECONDARY ({secondary_dev})...{C.NC}")
    if not upload_firmware(secondary_dev):
        return False

    print(f"\n{C.YELLOW}[3/4] Waiting for devices to restart...{C.NC}")
    time.sleep(5)

    print(f"\n{C.YELLOW}[4/4] Configuring roles...{C.NC}")
    configure_role(primary_dev, "PRIMARY")
    time.sleep(1)
    configure_role(secondary_dev, "SECONDARY")

    print(f"\n{C.GREEN}=== Both devices deployed and configured! ==={C.NC}")
    print(f"  PRIMARY:   {primary_dev}")
    print(f"  SECONDARY: {secondary_dev}")
    print()

    return True


def interactive_deploy():
    """Main interactive deployment workflow"""
    print(f"\n{C.CYAN}=== Deploying firmware ==={C.NC}\n")
    print(f"{C.YELLOW}Scanning for connected devices...{C.NC}\n")

    devices = find_devices()

    if not devices:
        print(f"{C.RED}No devices found!{C.NC}")
        print("Make sure your Feather nRF52840 is connected via USB.")
        return False

    if len(devices) == 1:
        return deploy_single_device(devices[0])
    elif len(devices) == 2:
        return deploy_two_devices(devices)
    else:
        return deploy_multiple_devices(devices)


# =============================================================================
# PlatformIO Custom Targets (when imported as extra_script)
# =============================================================================

try:
    Import("env")

    # Set project directory from PlatformIO environment
    _pio_project_dir[0] = env.subst("$PROJECT_DIR")

    def pio_deploy(source, target, env):
        print(f"\n{C.YELLOW}NOTE: Interactive deploy requires running directly:{C.NC}")
        print(f"  {C.CYAN}python deploy.py{C.NC}\n")
        print("PlatformIO custom targets don't support interactive input.")
        print("The script auto-detects and uses PlatformIO's Python if needed.\n")

    def pio_list_devices(source, target, env):
        list_devices()

    env.AddCustomTarget(
        name="deploy",
        dependencies=None,
        actions=pio_deploy,
        title="Deploy",
        description="Build and deploy with interactive role assignment"
    )

    env.AddCustomTarget(
        name="list_devices",
        dependencies=None,
        actions=pio_list_devices,
        title="List Devices",
        description="List connected Feather nRF52840 devices"
    )

except NameError:
    # Not running as PlatformIO script - allow standalone execution
    pass


# =============================================================================
# Standalone CLI
# =============================================================================

def print_help():
    """Print help message"""
    print(f"""
{C.CYAN}BlueBuzzah Arduino Firmware - Deploy Script{C.NC}
=============================================

{C.GREEN}Usage (from BlueBuzzah-Arduino directory):{C.NC}
  python deploy.py        Interactive deploy (build + upload + configure)
  python deploy.py --list List connected devices
  python deploy.py --help Show this help

{C.YELLOW}Deployment workflow:{C.NC}
  1 device:  Uploads firmware, prompts for role (P/S/N)
  2 devices: Prompts for PRIMARY/SECONDARY assignment,
             uploads to both, configures roles automatically

{C.YELLOW}Query current role:{C.NC}
  Type: GET_ROLE (in serial monitor)

{C.YELLOW}Note:{C.NC}
  Automatically uses PlatformIO's Python if pyserial is not installed.
""")


def main():
    """CLI entry point"""
    if len(sys.argv) < 2:
        # No arguments - interactive deploy
        if build_firmware():
            interactive_deploy()
    elif sys.argv[1] in ("--help", "-h"):
        print_help()
    elif sys.argv[1] == "--list":
        list_devices()
    else:
        print(f"{C.RED}Unknown option: {sys.argv[1]}{C.NC}")
        print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
