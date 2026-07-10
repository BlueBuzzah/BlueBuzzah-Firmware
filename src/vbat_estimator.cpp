#include "vbat_estimator.h"

// Battery voltage moves over minutes; heavy smoothing costs nothing and
// tames the register's sample-to-sample noise.
static constexpr float VBAT_EMA_ALPHA = 0.3f;
static constexpr size_t VBAT_MAX_BURST = 16;

float vbatRawToVolts(uint8_t raw) {
    return static_cast<float>(raw) * 5.6f / 255.0f;
}

float VbatEstimator::addBurst(const uint8_t* raw, size_t count) {
    if (raw == nullptr || count == 0) {
        return _voltage;
    }
    if (count > VBAT_MAX_BURST) {
        count = VBAT_MAX_BURST;
    }

    // Median of the burst (insertion sort on a stack copy; count <= 16)
    uint8_t sorted[VBAT_MAX_BURST];
    for (size_t i = 0; i < count; i++) {
        uint8_t v = raw[i];
        size_t j = i;
        while (j > 0 && sorted[j - 1] > v) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = v;
    }
    float sample = vbatRawToVolts(sorted[count / 2]);

    if (!_hasReading) {
        _voltage = sample;
        _hasReading = true;
    } else {
        _voltage += VBAT_EMA_ALPHA * (sample - _voltage);
    }
    return _voltage;
}
