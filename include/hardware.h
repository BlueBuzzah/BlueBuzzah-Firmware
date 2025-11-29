/**
 * @file hardware.h
 * @brief BlueBuzzah hardware abstraction layer - Class declarations
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * Hardware components:
 * - TCA9548A I2C multiplexer @ 0x70
 * - 5x DRV2605 haptic drivers @ 0x5A (one per finger via multiplexer)
 * - Battery voltage monitor via ADC
 * - NeoPixel RGB LED for status indication
 */

#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <Wire.h>
#include <TCA9548A.h>
#include <Adafruit_DRV2605.h>
#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "types.h"

// =============================================================================
// HAPTIC CONTROLLER
// =============================================================================

/**
 * @brief Controls 5 DRV2605 haptic drivers via TCA9548A I2C multiplexer
 *
 * Each finger (thumb through pinky) has a dedicated DRV2605 driver connected
 * to the TCA9548A multiplexer on channels 0-4. All drivers share the same
 * I2C address (0x5A) and are accessed by selecting the appropriate channel.
 *
 * Usage:
 *   HapticController haptic;
 *   if (haptic.begin()) {
 *       haptic.activate(FINGER_INDEX, 80);  // 80% amplitude
 *       delay(100);
 *       haptic.deactivate(FINGER_INDEX);
 *   }
 */
class HapticController {
public:
    HapticController();

    /**
     * @brief Initialize I2C bus, multiplexer, and all DRV2605 drivers
     * @return true if at least one finger initialized successfully
     */
    bool begin();

    /**
     * @brief Initialize a specific finger's DRV2605 driver
     * @param finger Finger index (0-4: thumb through pinky)
     * @return true if initialization successful
     */
    bool initializeFinger(uint8_t finger);

    /**
     * @brief Activate motor on specified finger
     * @param finger Finger index (0-4)
     * @param amplitude Amplitude percentage (0-100)
     * @return Result code indicating success or error
     */
    Result activate(uint8_t finger, uint8_t amplitude);

    /**
     * @brief Deactivate motor on specified finger
     * @param finger Finger index (0-4)
     * @return Result code indicating success or error
     */
    Result deactivate(uint8_t finger);

    /**
     * @brief Stop all active motors
     */
    void stopAll();

    /**
     * @brief Emergency stop - immediately stops all motors regardless of state
     */
    void emergencyStop();

    /**
     * @brief Check if a finger's motor is currently active
     * @param finger Finger index (0-4)
     * @return true if motor is active
     */
    bool isActive(uint8_t finger) const;

    /**
     * @brief Check if a finger's driver is enabled (initialized successfully)
     * @param finger Finger index (0-4)
     * @return true if driver is enabled
     */
    bool isEnabled(uint8_t finger) const;

    /**
     * @brief Set resonant frequency for LRA actuator
     * @param finger Finger index (0-4)
     * @param frequencyHz Frequency in Hz (150-250)
     * @return Result code indicating success or error
     */
    Result setFrequency(uint8_t finger, uint16_t frequencyHz);

    /**
     * @brief Get number of successfully initialized fingers
     * @return Count of enabled fingers (0-5)
     */
    uint8_t getEnabledCount() const;

private:
    TCA9548A _tca;
    Adafruit_DRV2605 _drv[MAX_ACTUATORS];
    bool _fingerActive[MAX_ACTUATORS];
    bool _fingerEnabled[MAX_ACTUATORS];
    bool _initialized;

    /**
     * @brief Select multiplexer channel and prepare for DRV2605 communication
     * @param finger Finger index (0-4)
     * @return true if channel selected successfully
     */
    bool selectChannel(uint8_t finger);

    /**
     * @brief Close all multiplexer channels
     */
    void closeChannels();

    /**
     * @brief Configure DRV2605 for LRA mode with RTP
     * @param drv Reference to DRV2605 driver
     */
    void configureDRV2605(Adafruit_DRV2605& drv);

    /**
     * @brief Convert amplitude percentage to DRV2605 RTP value
     * @param amplitude Percentage (0-100)
     * @return RTP value (0-127)
     */
    uint8_t amplitudeToRTP(uint8_t amplitude) const;
};

// =============================================================================
// BATTERY MONITOR
// =============================================================================

/**
 * @brief Monitors battery voltage and calculates charge percentage
 *
 * Uses the nRF52840's ADC to read battery voltage through a voltage divider.
 * Provides accurate percentage estimation using a LiPo discharge curve.
 *
 * Usage:
 *   BatteryMonitor battery;
 *   battery.begin();
 *   BatteryStatus status = battery.getStatus();
 *   Serial.printf("Battery: %.2fV (%d%%)\n", status.voltage, status.percentage);
 */
class BatteryMonitor {
public:
    BatteryMonitor();

    /**
     * @brief Initialize battery monitor
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Read current battery voltage
     * @return Voltage in volts
     */
    float readVoltage();

    /**
     * @brief Get battery charge percentage
     * @param voltage Optional voltage to use (reads if not provided)
     * @return Percentage (0-100)
     */
    uint8_t getPercentage(float voltage = -1.0f);

    /**
     * @brief Get complete battery status
     * @return BatteryStatus struct with voltage, percentage, and flags
     */
    BatteryStatus getStatus();

    /**
     * @brief Check if battery is low (below warning threshold)
     * @param voltage Optional voltage to use (reads if not provided)
     * @return true if voltage < BATTERY_LOW_VOLTAGE
     */
    bool isLow(float voltage = -1.0f);

    /**
     * @brief Check if battery is critical (below shutdown threshold)
     * @param voltage Optional voltage to use (reads if not provided)
     * @return true if voltage < BATTERY_CRITICAL_VOLTAGE
     */
    bool isCritical(float voltage = -1.0f);

private:
    bool _initialized;

    /**
     * @brief LiPo discharge curve for accurate percentage calculation
     * Format: {voltage, percentage}
     */
    static const float VOLTAGE_CURVE[][2];
    static const uint8_t VOLTAGE_CURVE_SIZE;

    /**
     * @brief Interpolate percentage from voltage curve
     * @param voltage Battery voltage
     * @return Interpolated percentage
     */
    uint8_t interpolatePercentage(float voltage) const;
};

// =============================================================================
// LED PATTERN TYPES
// =============================================================================

/**
 * @brief LED display patterns for status indication
 */
enum class LEDPattern : uint8_t {
    SOLID = 0,          // Constant on
    BREATHE_SLOW,       // Slow fade in/out (2s cycle) - IDLE
    PULSE_SLOW,         // Slow pulse (1.5s cycle) - RUNNING
    BLINK_FAST,         // Fast blink (200ms on/off) - STOPPING
    BLINK_SLOW,         // Slow blink (1s on/off) - ERROR, LOW_BATTERY
    BLINK_URGENT,       // Urgent blink (150ms on/off) - CRITICAL_BATTERY
    BLINK_CONNECT,      // Connection blink (250ms on/off) - CONNECTING, CONNECTION_LOST
    OFF                 // LED off
};

// =============================================================================
// LED CONTROLLER
// =============================================================================

/**
 * @brief Controls the onboard NeoPixel LED for status indication
 *
 * Provides color and pattern control for the single NeoPixel LED on the
 * Adafruit Feather nRF52840 Express board. Supports various patterns
 * including solid, breathing, pulsing, and blinking modes.
 *
 * Usage:
 *   LEDController led;
 *   led.begin();
 *   led.setPattern(Colors::BLUE, LEDPattern::BREATHE_SLOW);  // Idle state
 *   led.setPattern(Colors::GREEN, LEDPattern::PULSE_SLOW);   // Running
 *   led.setPattern(Colors::RED, LEDPattern::BLINK_SLOW);     // Error
 *
 * IMPORTANT: Call update() in the main loop for patterns to animate!
 */
class LEDController {
public:
    LEDController();

    /**
     * @brief Initialize NeoPixel
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Update LED pattern animation
     *
     * MUST be called regularly in the main loop for animated patterns
     * (breathe, pulse, blink) to work. Safe to call even for SOLID pattern.
     */
    void update();

    /**
     * @brief Set LED color and pattern
     * @param color RGBColor to display
     * @param pattern Animation pattern to use
     */
    void setPattern(const RGBColor& color, LEDPattern pattern);

    /**
     * @brief Set LED color using RGB values (defaults to SOLID pattern)
     * @param r Red component (0-255)
     * @param g Green component (0-255)
     * @param b Blue component (0-255)
     */
    void setColor(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Set LED color using RGBColor struct (defaults to SOLID pattern)
     * @param color RGBColor struct
     */
    void setColor(const RGBColor& color);

    /**
     * @brief Turn LED off
     */
    void off();

    /**
     * @brief Set LED brightness
     * @param brightness Brightness level (0-255)
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Get current base color (before pattern modulation)
     * @return Current RGBColor
     */
    RGBColor getColor() const;

    /**
     * @brief Get current pattern
     * @return Current LEDPattern
     */
    LEDPattern getPattern() const;

private:
    Adafruit_NeoPixel _pixel;
    RGBColor _baseColor;        // Base color before pattern modulation
    RGBColor _displayColor;     // Actual displayed color (after modulation)
    LEDPattern _pattern;
    bool _initialized;

    // Pattern animation state
    uint32_t _patternStartTime;
    bool _blinkState;           // For blink patterns: true=on, false=off
    uint32_t _lastBlinkToggle;

    /**
     * @brief Apply color to LED hardware
     * @param color Color to display
     */
    void applyColor(const RGBColor& color);

    /**
     * @brief Calculate brightness multiplier for breathe/pulse patterns
     * @param cycleMs Full cycle duration in milliseconds
     * @return Brightness multiplier (0.0 - 1.0)
     */
    float calculateBreatheBrightness(uint32_t cycleMs) const;
};

#endif // HARDWARE_H
