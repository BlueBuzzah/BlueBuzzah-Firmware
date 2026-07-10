/**
 * @file vbat_estimator.h
 * @brief Battery voltage estimation from DRV2605 VBAT register samples
 *
 * The DRV2605s on boards without a battery ADC run directly from the VBat
 * rail, so their VBAT monitor register (0x21) doubles as a battery
 * voltmeter: VDD = raw * 5.6V / 255 (~22mV LSB). The register is noisy and
 * motor pulses sag the rail, so bursts are reduced by median (rejects sag
 * outliers) and folded into an EMA (smooths across bursts).
 *
 * Pure C++ (no Arduino dependencies) so it builds in native test envs.
 */

#ifndef VBAT_ESTIMATOR_H
#define VBAT_ESTIMATOR_H

#include <stdint.h>
#include <stddef.h>

/** Convert a DRV2605 VBAT register value to volts (raw * 5.6 / 255). */
float vbatRawToVolts(uint8_t raw);

class VbatEstimator {
public:
    /**
     * @brief Fold a burst of raw VBAT samples into the running estimate.
     * Median of the burst, then EMA against the previous estimate (the
     * first burst seeds the estimate directly). Empty bursts (count == 0,
     * e.g. bus busy or chip in POR standby) leave the estimate unchanged.
     * @return The updated voltage estimate (0.0f if no burst ever landed)
     */
    float addBurst(const uint8_t* raw, size_t count);

    float voltage() const { return _voltage; }
    bool hasReading() const { return _hasReading; }

private:
    float _voltage = 0.0f;
    bool _hasReading = false;
};

#endif // VBAT_ESTIMATOR_H
