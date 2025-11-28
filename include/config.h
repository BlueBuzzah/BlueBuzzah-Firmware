/**
 * @file config.h
 * @brief BlueBuzzah firmware configuration - Pin definitions, constants, and parameters
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// FIRMWARE VERSION
// =============================================================================

#define FIRMWARE_VERSION "2.0.0"
#define FIRMWARE_NAME "BlueBuzzah"

// =============================================================================
// PIN DEFINITIONS (nRF52840 Feather)
// =============================================================================

// NeoPixel LED - Uses built-in PIN_NEOPIXEL on Feather nRF52840
#define NEOPIXEL_PIN PIN_NEOPIXEL
#define NEOPIXEL_COUNT 1

// Battery voltage monitor - Uses built-in voltage divider on A6/VBAT
#define BATTERY_PIN PIN_VBAT

// I2C pins - Uses default Wire (SDA/SCL)
// SDA = PIN_WIRE_SDA
// SCL = PIN_WIRE_SCL

// =============================================================================
// I2C CONFIGURATION
// =============================================================================

#define I2C_FREQUENCY 400000  // 400kHz Fast Mode

// TCA9548A I2C Multiplexer
#define TCA9548A_ADDRESS 0x70

// DRV2605 Haptic Driver (same address on all channels)
#define DRV2605_ADDRESS 0x5A

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
#define BLE_INTERVAL_MS 8            // Connection interval (rounded from 7.5ms)
#define BLE_TIMEOUT_MS 4000          // Supervision timeout (4 seconds)
#define BLE_ADV_INTERVAL_MS 500      // Advertising interval

// Sync protocol
#define SYNC_INTERVAL_MS 1000        // Periodic sync interval during therapy
#define SYNC_TIMEOUT_MS 2000         // Sync command timeout
#define COMMAND_TIMEOUT_MS 5000      // General BLE command timeout

// Heartbeat
#define HEARTBEAT_INTERVAL_MS 2000   // 2 seconds between heartbeats
#define HEARTBEAT_TIMEOUT_MS 6000    // 6 seconds = 3 missed heartbeats

// Battery monitoring
#define BATTERY_CHECK_INTERVAL_MS 60000  // 60 seconds between checks

// Therapy timing
#define THERAPY_CYCLE_MS 100            // Main therapy cycle period
#define BURST_DURATION_MS 1500          // Standard burst duration (1.5 seconds)
#define INTER_BURST_INTERVAL_MS 5000    // Pause between bursts

// =============================================================================
// BATTERY CONFIGURATION
// =============================================================================

#define BATTERY_LOW_VOLTAGE 3.4f        // Low battery warning threshold (V)
#define BATTERY_CRITICAL_VOLTAGE 3.3f   // Critical battery shutdown threshold (V)
#define BATTERY_FULL_VOLTAGE 4.2f       // Fully charged voltage (V)
#define BATTERY_EMPTY_VOLTAGE 3.27f     // Empty battery voltage (V)

// ADC configuration for nRF52840
#define ADC_RESOLUTION_BITS 14          // nRF52840 has 14-bit ADC
#define ADC_MAX_VALUE 16383             // 2^14 - 1
#define ADC_REFERENCE_VOLTAGE 3.6f      // nRF52840 reference voltage
#define BATTERY_VOLTAGE_DIVIDER 2.0f    // Voltage divider ratio (4.2V -> 2.1V)
#define BATTERY_SAMPLE_COUNT 10         // Number of samples to average

// =============================================================================
// HAPTIC CONFIGURATION
// =============================================================================

#define MAX_ACTUATORS 5                 // Number of fingers (thumb through pinky)
#define MAX_AMPLITUDE 100               // Maximum amplitude percentage (0-100)
#define DRV2605_MAX_RTP 127             // DRV2605 RTP register max value

// LRA (Linear Resonant Actuator) parameters
#define MIN_FREQUENCY_HZ 150            // Minimum LRA resonant frequency
#define MAX_FREQUENCY_HZ 250            // Maximum LRA resonant frequency
#define DEFAULT_FREQUENCY_HZ 175        // Default/standard LRA frequency

// Finger indices
#define FINGER_THUMB 0
#define FINGER_INDEX 1
#define FINGER_MIDDLE 2
#define FINGER_RING 3
#define FINGER_PINKY 4

// =============================================================================
// LED COLORS (RGB values)
// =============================================================================

#define LED_BRIGHTNESS 25               // NeoPixel brightness (0-255), ~10%

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
// BLE PROTOCOL CONSTANTS
// =============================================================================

#define BLE_EOT_CHAR 0x04               // End of transmission marker (ASCII EOT)
#define BLE_CHUNK_SIZE 100              // Max bytes per BLE packet
#define BLE_MAX_MESSAGE_SIZE 512        // Max total message size
#define BLE_NAME "BlueBuzzah"           // Default BLE device name
#define BLE_AUTH_TOKEN "bluebuzzah-secure-v1"

// =============================================================================
// DEVELOPMENT/DEBUG FLAGS
// =============================================================================

#ifndef DEBUG_ENABLED
#define DEBUG_ENABLED 0
#endif

#ifndef SKIP_BOOT_SEQUENCE
#define SKIP_BOOT_SEQUENCE 0
#endif

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
// MEMORY MANAGEMENT
// =============================================================================

// Buffer sizes for static allocation
#define RX_BUFFER_SIZE 256              // BLE receive buffer
#define TX_BUFFER_SIZE 256              // BLE transmit buffer
#define MESSAGE_BUFFER_SIZE 128         // General message buffer

#endif // CONFIG_H
