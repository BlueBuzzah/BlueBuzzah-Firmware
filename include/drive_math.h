/**
 * @file drive_math.h
 * @brief Motor drive and battery calculations
 *
 * Amplitude to DRV2605 RTP value, frequency to OL_LRA_PERIOD register value,
 * and battery voltage to percentage on the LiPo discharge curve.
 *
 * Header-only with no Arduino dependencies, so native tests can include it.
 */

#ifndef DRIVE_MATH_H
#define DRIVE_MATH_H

#include <stdint.h>
#include "config.h"

// Convert 0-100% amplitude to the DRV2605 RTP value (0-127), clamping above 100%
constexpr uint8_t amplitudeToRtp(uint8_t amplitude) {
    // Clamp amplitude to valid range
    if (amplitude > MAX_AMPLITUDE) {
        amplitude = MAX_AMPLITUDE;
    }

    // Convert 0-100% to 0-127
    return (uint8_t)((amplitude * DRV2605_MAX_RTP) / MAX_AMPLITUDE);
}

// OL_LRA_PERIOD (0x20) is the open-loop drive period in 98.49us units, as in
// configureDRV2605(). This register sets the frequency an LRA is driven at in
// open-loop mode; CONTROL1's drive time only matters in closed loop.
constexpr uint8_t olLraPeriod(uint16_t frequencyHz) {
    return static_cast<uint8_t>(10153 / frequencyHz);  // 1 / 98.49us = 10153Hz
}

// LiPo discharge curve: {voltage, percentage}
// 21 calibration points for accurate interpolation
inline constexpr float BATTERY_VOLTAGE_CURVE[][2] = {
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80},
    {3.98f, 75},  {3.95f, 70}, {3.91f, 65}, {3.87f, 60}, {3.85f, 55},
    {3.84f, 50},  {3.82f, 45}, {3.80f, 40}, {3.79f, 35}, {3.77f, 30},
    {3.75f, 25},  {3.73f, 20}, {3.71f, 15}, {3.69f, 10}, {3.61f, 5},
    {3.27f, 0}
};

inline constexpr uint8_t BATTERY_VOLTAGE_CURVE_SIZE =
    sizeof(BATTERY_VOLTAGE_CURVE) / sizeof(BATTERY_VOLTAGE_CURVE[0]);

// Interpolate battery percentage from the voltage curve
inline uint8_t batteryPercentFromVoltage(float voltage) {
    // Handle edge cases
    if (voltage >= BATTERY_VOLTAGE_CURVE[0][0]) {
        return 100;
    }
    if (voltage <= BATTERY_VOLTAGE_CURVE[BATTERY_VOLTAGE_CURVE_SIZE - 1][0]) {
        return 0;
    }

    // Find the two points to interpolate between
    for (uint8_t i = 0; i < BATTERY_VOLTAGE_CURVE_SIZE - 1; i++) {
        float v1 = BATTERY_VOLTAGE_CURVE[i][0];
        float v2 = BATTERY_VOLTAGE_CURVE[i + 1][0];

        if (voltage <= v1 && voltage > v2) {
            // Linear interpolation
            float p1 = BATTERY_VOLTAGE_CURVE[i][1];
            float p2 = BATTERY_VOLTAGE_CURVE[i + 1][1];

            float ratio = (voltage - v2) / (v1 - v2);
            float percentage = p2 + ratio * (p1 - p2);

            return (uint8_t)percentage;
        }
    }

    // Fallback (should not reach here)
    return 0;
}

#endif // DRIVE_MATH_H
