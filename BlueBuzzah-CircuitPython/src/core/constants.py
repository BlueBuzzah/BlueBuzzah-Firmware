"""
Core Constants
==============
System-wide constants for the BlueBuzzah v2 system.

This module defines all configuration constants used throughout the
BlueBuzzah bilateral haptic therapy system, including:

- Firmware version information
- Timing and synchronization parameters
- Hardware configuration (I2C, pins, addresses)
- LED color definitions
- Battery thresholds
- Memory management settings
- BLE protocol parameters

All constants are defined at module level for easy import and use.

Module: core.constants
Version: 2.0.0
"""

# Import const for memory-efficient constant storage (stores in flash, not RAM)
from micropython import const

# LED colors are tuples of (R, G, B)

# ============================================================================
# Firmware Version
# ============================================================================

FIRMWARE_VERSION= "2.0.0"
"""
Current firmware version for BlueBuzzah v2.

This version follows semantic versioning (MAJOR.MINOR.PATCH):
- MAJOR: Incompatible API changes
- MINOR: Backwards-compatible functionality additions
- PATCH: Backwards-compatible bug fixes

Used in:
    - BLE protocol version negotiation
    - Over-the-air update compatibility checking
    - Device status reporting
"""

# ============================================================================
# Timing Constants (seconds unless otherwise noted)
# ============================================================================

STARTUP_WINDOW = const(30)
"""
Boot sequence connection window in seconds.

During this window, devices establish BLE connections:
- PRIMARY: Waits for SECONDARY and optionally phone
- SECONDARY: Scans for and connects to PRIMARY

If timeout occurs:
- PRIMARY with SECONDARY: Proceeds with default therapy
- PRIMARY without SECONDARY: Fails boot (safety requirement)
- SECONDARY without PRIMARY: Fails boot
"""

CONNECTION_TIMEOUT = const(30)
"""
BLE connection establishment timeout in seconds.

Maximum time to wait for:
- Initial connection handshake
- Authentication completion
- Service discovery

If exceeded, connection attempt is aborted.
"""

BLE_INTERVAL_MS = const(8)
"""
BLE connection interval in milliseconds (8ms, rounded from 7.5ms).

Lower intervals provide:
- Reduced latency for synchronization messages
- Faster command response times
- Higher power consumption

The 8ms interval provides optimal balance for therapy synchronization
while maintaining reasonable power usage. Code using time.sleep() should
divide by 1000: time.sleep(BLE_INTERVAL_MS / 1000)
"""

# Deprecated: Use BLE_INTERVAL_MS / 1000 for seconds
BLE_INTERVAL = BLE_INTERVAL_MS / 1000  # 0.008 seconds

BLE_LATENCY = const(0)
"""
BLE connection latency (slave latency).

Number of connection events the peripheral can skip.
Set to 0 for minimal latency in time-critical therapy synchronization.
"""

BLE_TIMEOUT_MS = const(4000)
"""
BLE supervision timeout in milliseconds (4 seconds).

CRITICAL FIX: Changed from 100ms to 4000ms per industry standards.

Maximum time without communication before connection is considered lost.
Must be larger than: (1 + latency) * interval * 2

Industry standard (iOS/Android): 4-8 seconds
Nordic nRF52 SDK recommendation: 4 seconds minimum

Prevents false disconnects due to:
- Brief RF interference
- Distance/obstacles
- Temporary signal attenuation
"""

# Deprecated: Use BLE_TIMEOUT_MS / 1000 for seconds
BLE_TIMEOUT = BLE_TIMEOUT_MS / 1000  # 4.0 seconds

BLE_ADV_INTERVAL_MS = const(500)
"""
BLE advertising interval in milliseconds (500ms).

PRIMARY device advertising frequency during boot sequence.
Shorter intervals allow faster discovery but increase power consumption.

Code using time.sleep() should divide by 1000: time.sleep(BLE_ADV_INTERVAL_MS / 1000)
"""

# Deprecated: Use BLE_ADV_INTERVAL_MS / 1000 for seconds
BLE_ADV_INTERVAL = BLE_ADV_INTERVAL_MS / 1000  # 0.5 seconds

SYNC_INTERVAL_MS = const(1000)
"""
Periodic synchronization interval in milliseconds (1000ms = 1 second).

Frequency of SYNC_ADJ messages from PRIMARY to SECONDARY during therapy
to maintain temporal alignment. Sent at the start of each macrocycle.

Code using time.sleep() should divide by 1000: time.sleep(SYNC_INTERVAL_MS / 1000)
"""

# Deprecated: Use SYNC_INTERVAL_MS / 1000 for seconds
SYNC_INTERVAL = SYNC_INTERVAL_MS / 1000  # 1.0 seconds

SYNC_TIMEOUT_MS = const(2000)
"""
Synchronization command timeout in milliseconds (2000ms = 2 seconds).

Maximum time to wait for:
- SYNC_ADJ acknowledgment from SECONDARY
- EXECUTE_BUZZ response from SECONDARY
- BUZZ_COMPLETE confirmation

If exceeded, therapy continues with logged warning.

Code using time.sleep() should divide by 1000: time.sleep(SYNC_TIMEOUT_MS / 1000)
"""

# Deprecated: Use SYNC_TIMEOUT_MS / 1000 for seconds
SYNC_TIMEOUT = SYNC_TIMEOUT_MS / 1000  # 2.0 seconds

COMMAND_TIMEOUT_MS = const(5000)
"""
General BLE command timeout in milliseconds (5000ms = 5 seconds).

Default timeout for non-time-critical commands:
- GET_STATUS requests
- Configuration updates
- Battery queries

Longer than SYNC_TIMEOUT to accommodate slower operations.

Code using time.sleep() should divide by 1000: time.sleep(COMMAND_TIMEOUT_MS / 1000)
"""

# Deprecated: Use COMMAND_TIMEOUT_MS / 1000 for seconds
COMMAND_TIMEOUT = COMMAND_TIMEOUT_MS / 1000  # 5.0 seconds

# ============================================================================
# Hardware Constants - I2C
# ============================================================================

I2C_MULTIPLEXER_ADDR = const(0x70)
"""
TCA9548A I2C multiplexer default address.

The multiplexer enables communication with multiple DRV2605 haptic drivers
(one per finger) on the same I2C bus by switching between 8 channels.

Channel mapping:
    0-4: Fingers (thumb, index, middle, ring, pinky)
    5-7: Reserved for future expansion
"""

DRV2605_DEFAULT_ADDR = const(0x5A)
"""
DRV2605 haptic driver default I2C address.

Each finger's haptic driver uses this same address, accessed through
different TCA9548A multiplexer channels.

The DRV2605 provides:
- Waveform generation and playback
- LRA/ERM motor control
- Automatic resonance tracking
- Library effects (not used in vCR therapy)
"""

I2C_FREQUENCY = const(400000)
"""
I2C bus frequency in Hz (400 kHz = Fast Mode).

Standard modes:
    - 100 kHz: Standard Mode
    - 400 kHz: Fast Mode (used here)
    - 1 MHz: Fast Mode Plus (not supported by all devices)

400 kHz provides good balance of speed and reliability for
multiple DRV2605 communications per therapy cycle.
"""

# ============================================================================
# Hardware Constants - Pin Assignments (nRF52840)
# ============================================================================

# Note: These are string identifiers used with board.* in CircuitPython
# Actual pin objects are created at runtime via getattr(board, PIN_NAME)

NEOPIXEL_PIN= "D13"
"""
NeoPixel LED data pin.

Connected to single RGB LED for visual feedback:
- Boot sequence status (blue/green/red flashing)
- Therapy state (breathing green, yellow pulse, etc.)
- Battery warnings (orange/red)
- Error conditions (red flashing)
"""

BATTERY_PIN= "A6"
"""
Battery voltage monitoring analog pin.

Connected through voltage divider to LiPo battery for voltage monitoring.
Divider ratio must be configured to scale 3.3-4.2V battery range to
0-3.3V ADC input range.

Voltage thresholds:
    - 4.2V: Fully charged
    - 3.7V: Nominal voltage
    - 3.4V: Low battery warning (LOW_VOLTAGE)
    - 3.3V: Critical battery (CRITICAL_VOLTAGE)
    - 3.0V: Safe discharge minimum
"""

I2C_SDA_PIN= "SDA"
"""
I2C data line pin.

Connected to TCA9548A multiplexer SDA, which connects to all DRV2605 drivers.
Requires external pull-up resistor (typically 4.7kΩ to 3.3V).
"""

I2C_SCL_PIN= "SCL"
"""
I2C clock line pin.

Connected to TCA9548A multiplexer SCL, which connects to all DRV2605 drivers.
Requires external pull-up resistor (typically 4.7kΩ to 3.3V).
"""

# ============================================================================
# LED Color Definitions (RGB tuples, 0-255)
# ============================================================================

LED_BLUE= (0, 0, 255)
"""
Blue LED color for BLE operations.

Used during:
- Boot sequence BLE initialization (rapid flash)
- Waiting for phone connection (slow flash)
- Phone disconnection acknowledgment (brief flash)
"""

LED_GREEN= (0, 255, 0)
"""
Green LED color for success and normal operation.

Used during:
- Connection success (5x rapid flash)
- Ready state (solid)
- Therapy running (breathing effect)
- Therapy stopping (fade out)
"""

LED_RED= (255, 0, 0)
"""
Red LED color for errors and critical conditions.

Used during:
- Connection failure (slow flash)
- Critical battery (rapid flash)
- Connection lost (alternating with white)
"""

LED_WHITE= (255, 255, 255)
"""
White LED color for special indicators.

Used during:
- Therapy start acknowledgment (3x flash)
- Connection lost (alternating with red)
"""

LED_YELLOW= (255, 255, 0)
"""
Yellow LED color for paused state.

Used during:
- Therapy paused (slow pulse, 1Hz)
"""

LED_ORANGE= (255, 128, 0)
"""
Orange LED color for warnings.

Used during:
- Low battery warning (solid, < 3.4V)
"""

LED_OFF= (0, 0, 0)
"""
LED off (all channels at 0).

Used to turn off LED completely.
"""

# ============================================================================
# Battery Monitoring
# ============================================================================

CRITICAL_VOLTAGE = 3.3  # Note: const() works best with integers, keeping as float
"""
Critical battery voltage threshold in volts.

When battery drops below this level:
1. TherapyState transitions to CRITICAL_BATTERY
2. LED shows rapid red flashing (5Hz)
3. All motors stop after 5 seconds
4. Device enters safe shutdown mode

This threshold protects LiPo battery from over-discharge damage
(safe minimum is typically 3.0V).
"""

LOW_VOLTAGE = 3.4  # Note: const() works best with integers, keeping as float
"""
Low battery voltage warning threshold in volts.

When battery drops below this level:
1. TherapyState transitions to LOW_BATTERY
2. LED shows solid orange
3. Therapy continues normally
4. User should plan to charge soon

Provides ~10-15 minutes warning before CRITICAL_VOLTAGE.
"""

BATTERY_CHECK_INTERVAL = const(60)
"""
Battery voltage check interval in seconds.

Frequency of battery voltage monitoring during therapy.
More frequent checks provide better safety but increase overhead.

60-second interval provides good balance of:
- Timely warning of battery issues
- Minimal impact on therapy timing
- Reduced ADC read overhead
"""

# ============================================================================
# Memory Management
# ============================================================================

GC_THRESHOLD = const(4096)
"""
Garbage collection threshold in bytes.

When free memory drops below this threshold, automatic garbage collection
is triggered. Lower values provide more proactive memory management but
may cause more frequent GC pauses.

CircuitPython's GC is generally fast enough that this doesn't impact
therapy timing significantly.
"""

GC_ENABLED = True
"""
Enable automatic garbage collection.

When True, periodic GC is performed to prevent memory fragmentation.
Should generally remain enabled unless debugging memory issues.

Disable only for:
- Memory profiling and leak detection
- Investigating GC-related timing issues
- Controlled manual GC during safe periods
"""

# ============================================================================
# BLE Protocol
# ============================================================================

EOT = const(0x04)
"""
End of Transmission (EOT) character for BLE message framing.

ASCII control character 0x04 (Ctrl+D) marks the end of each BLE message.
This prevents message corruption when messages are split across multiple
reads or arrive in rapid succession.

Protocol requirement:
- All outgoing messages MUST end with EOT
- Message parser MUST buffer bytes until EOT received
- Messages without EOT are incomplete and should be buffered

Example: "INFO\x04" or bytes([0x49, 0x4E, 0x46, 0x4F, 0x04])
"""

CHUNK_SIZE = const(100)
"""
BLE message chunk size in bytes.

Maximum size of individual BLE UART messages for:
- File transfer (defaults.py sync)
- Large command payloads
- Response messages

Limited by BLE MTU (typically 20-244 bytes depending on negotiation).
100 bytes provides good balance of:
- Efficient transfer speed
- Compatibility across devices
- Reliable transmission
"""

AUTH_TOKEN = "bluebuzzah-secure-v1"
"""
Authorization token for BLE connections.

Used to authenticate phone app connections and prevent unauthorized access.
Both PRIMARY and SECONDARY must use the same token.

Security notes:
- This is a shared secret, not cryptographically secure
- Provides basic access control against casual interference
- For production use, implement proper authentication/encryption
- Token should be user-configurable for multi-device environments
"""

MAX_MESSAGE_SIZE = const(512)
"""
Maximum BLE message size in bytes.

Upper limit for any single BLE message including:
- Command payloads
- Response data
- Status information

Messages exceeding this size must be chunked using CHUNK_SIZE.
Provides safety against buffer overflows and memory issues.
"""

# ============================================================================
# Therapy Timing Constants
# ============================================================================

THERAPY_CYCLE_MS = const(100)
"""
Main therapy cycle period in milliseconds.

Base timing unit for therapy pattern generation and execution.
All therapy timing is derived from this fundamental period.

vCR therapy uses multiples of this cycle:
- Burst duration: 15 cycles (1.5 seconds)
- Inter-burst interval: Variable based on profile
- Macrocycle: Multiple bursts with pauses
"""

BURST_DURATION_MS = const(1500)
"""
Standard burst duration in milliseconds (1.5 seconds).

Each burst consists of 3 buzzes with precise timing:
- Buzz duration: Variable by profile
- Buzz separation: Variable by profile
- Total burst: Fixed at 1500ms
"""

INTER_BURST_INTERVAL_MS = const(5000)
"""
Standard inter-burst interval in milliseconds (5 seconds).

Pause between bursts within a macrocycle.
Allows neural desynchronization between stimulation periods.
"""

# ============================================================================
# Hardware Limits and Safety
# ============================================================================

MAX_ACTUATORS = const(5)
"""
Maximum number of haptic actuators per device.

BlueBuzzah supports up to 5 actuators (one per finger).
This limit is imposed by:
- TCA9548A multiplexer channel count (8 available, 5 used)
- Memory constraints for driver instances
- Practical therapy design (human hand)
"""

MAX_AMPLITUDE = const(100)
"""
Maximum haptic amplitude percentage.

Valid amplitude range: 0-100%
- 0%: Motor off
- 100%: Maximum intensity

DRV2605 internally maps this to 0-127 register value.
Higher amplitudes may cause:
- Increased battery drain
- Reduced motor lifespan
- User discomfort
"""

MIN_FREQUENCY_HZ = const(150)
"""
Minimum LRA resonant frequency in Hz.

Typical LRA resonant frequencies: 150-250 Hz
DRV2605 requires frequency configuration for optimal LRA operation.
Frequency mismatch causes:
- Reduced amplitude
- Poor haptic quality
- Increased power consumption
"""

MAX_FREQUENCY_HZ = const(250)
"""
Maximum LRA resonant frequency in Hz.

See MIN_FREQUENCY_HZ for details.
"""

# ============================================================================
# Development and Debugging
# ============================================================================

DEBUG_ENABLED = False
"""
Enable debug mode for development.

When True:
- Debug print statements are shown with [DEBUG] prefix
- Additional state validation
- Memory usage reporting
- Timing measurement output

Should be False for production deployment to reduce:
- Serial output overhead
- Memory usage
- Potential timing impacts
"""

SKIP_BOOT_SEQUENCE = False
"""
Skip BLE boot sequence for development/testing.

When True:
- Boot sequence is bypassed entirely
- Device proceeds directly to main loop
- BLE connections are not required
- Useful for testing without paired devices

Should be False for production deployment.
"""

DEVICE_TAG = "[DEVICE]"
"""
Device identifier tag for print output.

This tag prefixes all print statements to identify which device
generated the output. Should be set to:
- "[PRIMARY]" for primary device
- "[SECONDARY]" for secondary device

Note: This is set during device initialization based on role.
Default value is a placeholder.
"""

# ============================================================================
# Constants Validation
# ============================================================================

# Validate BLE timing constraints
assert BLE_TIMEOUT > (1 + BLE_LATENCY) * BLE_INTERVAL * 2, \
    "BLE_TIMEOUT must be > (1 + BLE_LATENCY) * BLE_INTERVAL * 2"

# Validate battery thresholds
assert CRITICAL_VOLTAGE < LOW_VOLTAGE, \
    "CRITICAL_VOLTAGE must be less than LOW_VOLTAGE"
assert LOW_VOLTAGE < 4.2, \
    "LOW_VOLTAGE must be less than maximum LiPo voltage (4.2V)"

# Validate memory settings
assert GC_THRESHOLD > 0, \
    "GC_THRESHOLD must be positive"

# Validate hardware limits
assert 0 < MAX_ACTUATORS <= 8, \
    "MAX_ACTUATORS must be between 1 and 8 (multiplexer limit)"
assert 0 <= MAX_AMPLITUDE <= 100, \
    "MAX_AMPLITUDE must be between 0 and 100"
assert MIN_FREQUENCY_HZ < MAX_FREQUENCY_HZ, \
    "MIN_FREQUENCY_HZ must be less than MAX_FREQUENCY_HZ"

# Validate BLE protocol
assert 0 < CHUNK_SIZE <= MAX_MESSAGE_SIZE, \
    "CHUNK_SIZE must be between 1 and MAX_MESSAGE_SIZE"
