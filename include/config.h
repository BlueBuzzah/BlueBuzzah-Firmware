/**
 * @file config.h
 * @brief BlueBuzzah firmware configuration - Constants and parameters
 * @version 2.0.0
 *
 * Board-specific values (pins, actuator count, ADC/battery hardware) live in
 * board_config.h and are selected by the build environment's board macro.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "board_config.h"

// =============================================================================
// FIRMWARE VERSION
// =============================================================================

#define FIRMWARE_VERSION "2.0.0"
#define FIRMWARE_NAME "BlueBuzzah"

// =============================================================================
// PIN DEFINITIONS (per-board values from board_config.h)
// =============================================================================

// NeoPixel status LED
#define NEOPIXEL_PIN NEOPIXEL_PIN_OVERRIDE
#define NEOPIXEL_COUNT 1

// Battery voltage monitor (only on boards with a battery ADC)
#if BATTERY_SENSE_ADC
#define BATTERY_PIN BATTERY_PIN_OVERRIDE
#endif

// =============================================================================
// I2C CONFIGURATION
// =============================================================================
// TCA9548A_ADDRESS / DRV2605_ADDRESS come from board_config.h (identical on
// both boards).

#define I2C_FREQUENCY 400000  // 400kHz Fast Mode

// I2C timing
#define I2C_INIT_DELAY_MS 5      // Delay after channel select (fingers 0-3)
#define I2C_INIT_DELAY_CH4_MS 10 // Extended delay for channel 4 (longer I2C path)
#define I2C_RETRY_COUNT 3        // Max retries for DRV2605 initialization
#define I2C_RETRY_DELAY_MS 50    // Delay between retries

// =============================================================================
// TIMING CONSTANTS (milliseconds)
// =============================================================================

// Boot sequence
#define STARTUP_WINDOW_MS 30000      // 30 seconds boot timeout
#define CONNECTION_TIMEOUT_MS 30000  // 30 seconds connection timeout

// BLE parameters
#define BLE_INTERVAL_MIN_MS 7.5f     // Minimum connection interval (7.5ms = 6 units, BLE spec minimum)
#define BLE_INTERVAL_MAX_MS 10       // Maximum connection interval (tighter than default 15ms)
#define BLE_TIMEOUT_MS 6000          // Supervision timeout (6 seconds - increased for interference tolerance)
#define BLE_ADV_INTERVAL_MS 500      // Advertising interval
#define BLE_USE_2M_PHY 1             // Enable 2M PHY for faster BLE transmission
#define BLE_INTERVAL_WARNING_THRESHOLD_MS 12.0f  // Warn if negotiated interval > 12ms

// Sync protocol
#define SYNC_TIMEOUT_MS 2000         // Sync command timeout
#define COMMAND_TIMEOUT_MS 5000      // General BLE command timeout

// PTP-style clock synchronization
#define SYNC_LEAD_TIME_US 35000      // 35ms default lead time (BLE RTT is 16-24ms; 35ms provides ample margin)
                                      // Actual lead time is adaptive based on measured RTT + margin
#define SYNC_PROCESSING_OVERHEAD_US 10000  // 10ms overhead for processing on SECONDARY
                                            // Actual processing is 3-5ms; 10ms is 2-3x margin
                                            // Accounts for: BLE callback (~2ms), deserialization (~1ms),
                                            // event staging (~1ms), queue forwarding (~1ms)
#define SYNC_GENERATION_OVERHEAD_US 5000   // 5ms overhead for macrocycle generation + serialization on PRIMARY
                                            // Accounts for: pattern generation (~1-5ms), serialization (~2-3ms)
                                            // Added to lead time to prevent systematic lateness
// NOTE: MACROCYCLE_TRANSMISSION_OVERHEAD_US was removed after fixing the DEBUG_FLASH
// blocking bug. The old code's delayMicroseconds() blocked BLE callbacks for ~300ms,
// making it appear that MACROCYCLE transmission took much longer than it actually does.
// Actual MACROCYCLE BLE transmission is ~40-50ms (included in RTT-based calculation).
#define SYNC_MIN_VALID_SAMPLES 5     // Minimum samples before clock sync is valid
#define SYNC_OFFSET_EMA_ALPHA_NUM 1  // Slow EMA α = 1/10 = 0.1 for continuous updates
#define SYNC_OFFSET_EMA_ALPHA_DEN 10
#define SYNC_ACTIVE_INTERVAL_MS 250       // PING cadence while therapy is running (4Hz)
                                           // Idle cadence stays KEEPALIVE_INTERVAL_MS (1Hz)
#define SYNC_RTT_QUALITY_THRESHOLD_US 60000 // 60ms RTT threshold - reject retransmission-affected samples
                                             // (reduced from 120ms for stricter quality filtering)
#define SYNC_OUTLIER_THRESHOLD_US 5000   // 5ms threshold for offset outlier rejection (was hardcoded)

// Maintenance-mode sample gating (post-convergence quality filters)
#define SYNC_LUCKY_RTT_MARGIN_US 10000      // Accept only RTT <= minRTT + 10ms ("lucky packets")
#define SYNC_MIN_RTT_DECAY_US 200           // Per-sample creep of tracked min RTT (adapts to degradation)
#define SYNC_INNOVATION_GATE_US 5000        // Reject offset jumps > 5ms...
#define SYNC_INNOVATION_REJECT_LIMIT 5      // ...unless persistent across this many samples
#define SYNC_MAX_DRIFT_RATE_US_PER_MS 0.15f  // 150 ppm max drift rate (cap for safety)
#define SYNC_MAX_APPLIED_DRIFT_RATE_US_PER_MS 0.1f  // 100 ppm max for corrections
                                                     // More conservative than measurement cap (0.15f)
                                                     // Applied in getCorrectedOffset() and getProjectedOffset()

// Warm-start sync configuration (quick recovery after brief disconnects)
#define SYNC_WARM_START_VALIDITY_MS 15000    // Cache valid for 15 seconds (reduced drift extrapolation window)
#define SYNC_WARM_START_MIN_SAMPLES 3        // Confirmatory samples required for warm-start
#define SYNC_WARM_START_TOLERANCE_US 5000    // 5ms tolerance for confirmatory sample validation

// Connection-anchor timestamping (EXPERIMENTAL)
// Hardware-timestamps BLE radio events via SoftDevice radio notifications and
// derives clock offset from paired anchors. Falls back to PTP per-sample.
// Both gloves MUST run the same setting (PONG payload format changes).
#ifndef SYNC_ANCHOR_TIMESTAMPING_ENABLED
#define SYNC_ANCHOR_TIMESTAMPING_ENABLED 0
#endif
#define SYNC_ANCHOR_RING_SIZE 16              // Recent radio-event timestamps kept
#define SYNC_ANCHOR_RX_WINDOW_US 15000        // Max age of rx anchor vs rx callback (event len 12.5ms + margin)
#define SYNC_ANCHOR_TX_WINDOW_US 25000        // Max lookahead from PING handoff to its tx anchor (2x max CI + margin)
#define SYNC_ANCHOR_BIAS_US 0                 // Calibrated central-vs-peripheral constant (set during bench validation). Positive corrects for peripheral RX-window widening - PRIMARY's anchor fires slightly early.
#define SYNC_ANCHOR_PREFILTER_US 1500         // Reject anchor pairs deviating > 1.5ms from the
                                               // converged offset (catches within-gate mispairs
                                               // from phone-link/advertising anchors on PRIMARY)

// Path asymmetry compensation (measurement mode - correction not yet implemented)
#define SYNC_ASYMMETRY_CORRECTION_ENABLED 0   // 0 = measure only, 1 = apply correction
#define SYNC_ASYMMETRY_MIN_SAMPLES 10         // Samples before correction applied
#define SYNC_ASYMMETRY_STABLE_VARIANCE_US 3000 // Variance threshold for stable estimate
#define SYNC_ASYMMETRY_MAX_CORRECTION_US 20000 // Maximum correction (20ms cap)

// Drift rate and lead time calculation constants
#define SYNC_MAX_CORRECTION_ELAPSED_MS 10000  // 10s max elapsed time for drift correction
#define SYNC_MIN_DRIFT_INTERVAL_MS 500        // 500ms min interval for drift rate calculation
#define SYNC_DRIFT_EMA_ALPHA 0.3f             // Drift rate EMA smoothing factor (α=0.3)
#define SYNC_MIN_LEAD_TIME_US 70000           // 70ms minimum lead time for MACROCYCLE
#define SYNC_MAX_LEAD_TIME_US 150000          // 150ms maximum lead time for MACROCYCLE

// Unified keepalive + clock sync (PING/PONG)
#define KEEPALIVE_INTERVAL_MS 1000   // 1 second between PING messages (unified keepalive + clock sync)
#define KEEPALIVE_TIMEOUT_MS 6000    // 6 seconds = 6 missed keepalives

// How long the CONNECTION_LOST state (purple blink) is shown after losing the
// peer before demoting to IDLE (blue breathe). Scanning/advertising for the
// peer continues either way; this only affects the LED indication.
#define CONNECTION_LOST_TIMEOUT_MS 30000

// Battery monitoring
#define BATTERY_CHECK_INTERVAL_MS 60000  // 60 seconds between checks

// Therapy timing
#define THERAPY_CYCLE_MS 100            // Main therapy cycle period
// Note: Therapy timing (TIME_ON, TIME_OFF, etc.) is defined in therapy profiles
// See ProfileManager and ORIGINAL_PARAMETERS.md for v1 reference values

// Test session duration (quick hardware verification, separate from profile settings)
constexpr uint32_t TEST_DURATION_SEC = 120;  // 2 minutes

// =============================================================================
// BATTERY CONFIGURATION
// =============================================================================

#define BATTERY_LOW_VOLTAGE 3.4f        // Low battery warning threshold (V)
#define BATTERY_CRITICAL_VOLTAGE 3.3f   // Critical battery shutdown threshold (V)
#define BATTERY_FULL_VOLTAGE 4.2f       // Fully charged voltage (V)
#define BATTERY_EMPTY_VOLTAGE 3.27f     // Empty battery voltage (V)

// ADC configuration comes from board_config.h (ADC_RESOLUTION_BITS,
// ADC_MAX_VALUE, ADC_REFERENCE_VOLTAGE, BATTERY_VOLTAGE_DIVIDER).
#define BATTERY_SAMPLE_COUNT 10         // Number of samples to average

// DRV2605 VBAT backend (PentaBuzzer): number of readings taken per burst
#define VBAT_BURST_SAMPLES 9

// =============================================================================
// HAPTIC CONFIGURATION
// =============================================================================

// MAX_ACTUATORS comes from board_config.h (4 on BlueBuzzah, 5 on PentaBuzzer)
#define MAX_AMPLITUDE 100               // Maximum amplitude percentage (0-100)
#define DRV2605_MAX_RTP 127             // DRV2605 RTP register max value

// LRA (Linear Resonant Actuator) parameters
#define MIN_FREQUENCY_HZ 150            // Minimum LRA resonant frequency
#define MAX_FREQUENCY_HZ 255            // Maximum LRA resonant frequency (v1 randrange excludes 260)
#define DEFAULT_FREQUENCY_HZ 250        // Default/standard LRA frequency (v1 reference: 250Hz)

// Finger indices (boards with MAX_ACTUATORS == 5 add the thumb on index 4)
#define FINGER_INDEX 0
#define FINGER_MIDDLE 1
#define FINGER_RING 2
#define FINGER_PINKY 3
#define FINGER_THUMB 4

// =============================================================================
// LED COLORS (RGB values)
// =============================================================================

#define LED_BRIGHTNESS 4                // NeoPixel brightness (0-255), ~1.5%

// Color definitions (R, G, B)
#define LED_COLOR_OFF       0, 0, 0
#define LED_COLOR_RED       255, 0, 0
#define LED_COLOR_GREEN     0, 255, 0
#define LED_COLOR_BLUE      0, 0, 255
#define LED_COLOR_WHITE     255, 255, 255
#define LED_COLOR_YELLOW    255, 255, 0
#define LED_COLOR_ORANGE    255, 128, 0
#define LED_COLOR_PURPLE    128, 0, 255
#define LED_COLOR_CYAN      0, 255, 255

// =============================================================================
// LED PATTERN TIMING (milliseconds)
// =============================================================================

// Breathe/pulse patterns (smooth fade in/out)
#define LED_BREATHE_SLOW_MS     2000    // IDLE: 2s full cycle (1s in, 1s out)
#define LED_PULSE_SLOW_MS       1500    // RUNNING: 1.5s full cycle

// Blink patterns (on/off)
#define LED_BLINK_FAST_ON_MS    200     // Fast blink: 200ms on
#define LED_BLINK_FAST_OFF_MS   200     // Fast blink: 200ms off
#define LED_BLINK_SLOW_ON_MS    1000    // Slow blink: 1s on (ERROR, LOW_BATTERY)
#define LED_BLINK_SLOW_OFF_MS   1000    // Slow blink: 1s off
#define LED_BLINK_URGENT_ON_MS  150     // Urgent blink: 150ms (CRITICAL_BATTERY)
#define LED_BLINK_URGENT_OFF_MS 150     // Urgent blink: 150ms
#define LED_BLINK_CONNECT_ON_MS 250     // Connecting blink: 250ms
#define LED_BLINK_CONNECT_OFF_MS 250    // Connecting blink: 250ms

// =============================================================================
// BLE PROTOCOL CONSTANTS
// =============================================================================

#define BLE_EOT_CHAR 0x04               // End of transmission marker (ASCII EOT)
#define BLE_CHUNK_SIZE 100              // Max bytes per BLE packet
#define BLE_MAX_MESSAGE_SIZE 512        // Max total message size
#define BLE_NAME "BlueBuzzah"           // Default BLE device name

// =============================================================================
// DEVELOPMENT/DEBUG FLAGS
// =============================================================================

#ifndef DEBUG_ENABLED
#define DEBUG_ENABLED 0
#endif

#ifndef SKIP_BOOT_SEQUENCE
#define SKIP_BOOT_SEQUENCE 0
#endif

// Bilateral sync ground-truth instrumentation: toggles a GPIO on every motor
// ACTIVATE so a logic analyzer across both gloves measures true skew.
// Compile-time only - keep 0 for release builds.
#ifndef SYNC_DEBUG_GPIO_ENABLED
#define SYNC_DEBUG_GPIO_ENABLED 0
#endif
#define SYNC_DEBUG_GPIO_PIN PIN_A0

// Debug macros
#if DEBUG_ENABLED
    #define DEBUG_PRINT(x) Serial.print(x)
    #define DEBUG_PRINTLN(x) Serial.println(x)
    #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(...)
#endif

// =============================================================================
// LATENCY METRICS CONFIGURATION
// =============================================================================

// Reporting
#define LATENCY_REPORT_INTERVAL_MS 30000  // Auto-report every 30s when enabled
#define LATENCY_LATE_THRESHOLD_US 1000    // >1ms considered "late"

// =============================================================================
// MEMORY MANAGEMENT
// =============================================================================

// Buffer sizes for static allocation.
// The largest message is a serialized macrocycle: ~65-byte worst-case header
// plus up to 16 bytes per event, 3 events per actuator (12 events -> 272,
// 15 events -> 320). RX must hold the same message on the receiving glove.
#define MESSAGE_BUFFER_SIZE 512 // TX queue entry / RX buffer size; matches RESPONSE_BUFFER_SIZE so HELP (~400 B) fits
#define RX_BUFFER_SIZE MESSAGE_BUFFER_SIZE  // BLE receive buffer
#define TX_BUFFER_SIZE MESSAGE_BUFFER_SIZE  // BLE transmit buffer

#endif // CONFIG_H
