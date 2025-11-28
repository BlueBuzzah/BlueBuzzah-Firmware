/**
 * @file hardware.cpp
 * @brief BlueBuzzah hardware abstraction layer - Implementations
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "hardware.h"

// =============================================================================
// BATTERY MONITOR - Static Data
// =============================================================================

// LiPo discharge curve: {voltage, percentage}
// 21 calibration points for accurate interpolation
const float BatteryMonitor::VOLTAGE_CURVE[][2] = {
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80},
    {3.98f, 75},  {3.95f, 70}, {3.91f, 65}, {3.87f, 60}, {3.85f, 55},
    {3.84f, 50},  {3.82f, 45}, {3.80f, 40}, {3.79f, 35}, {3.77f, 30},
    {3.75f, 25},  {3.73f, 20}, {3.71f, 15}, {3.69f, 10}, {3.61f, 5},
    {3.27f, 0}
};

const uint8_t BatteryMonitor::VOLTAGE_CURVE_SIZE = sizeof(VOLTAGE_CURVE) / sizeof(VOLTAGE_CURVE[0]);

// =============================================================================
// HAPTIC CONTROLLER - Implementation
// =============================================================================

HapticController::HapticController() : _tca(TCA9548A_ADDRESS), _initialized(false) {
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        _fingerActive[i] = false;
        _fingerEnabled[i] = false;
    }
}

bool HapticController::begin() {
    Serial.println(F("[INFO] Initializing haptic controller..."));

    // Initialize I2C at 400kHz
    Wire.begin();
    Wire.setClock(I2C_FREQUENCY);

    // Initialize TCA9548A multiplexer
    _tca.begin(Wire);
    _tca.closeAll();

    Serial.printf("[INFO] TCA9548A multiplexer initialized at 0x%02X\n", TCA9548A_ADDRESS);

    // Initialize each finger's DRV2605
    uint8_t successCount = 0;
    for (uint8_t finger = 0; finger < MAX_ACTUATORS; finger++) {
        if (initializeFinger(finger)) {
            successCount++;
            Serial.printf("[INFO] DRV2605 finger %d initialized\n", finger);
        } else {
            Serial.printf("[ERROR] DRV2605 finger %d initialization failed\n", finger);
        }
    }

    _initialized = (successCount > 0);

    if (_initialized) {
        Serial.printf("[INFO] Haptic controller ready: %d/%d fingers enabled\n",
                      successCount, MAX_ACTUATORS);
    } else {
        Serial.println(F("[CRITICAL] No haptic drivers initialized!"));
    }

    return _initialized;
}

bool HapticController::initializeFinger(uint8_t finger) {
    if (finger >= MAX_ACTUATORS) {
        return false;
    }

    // Retry logic for reliable initialization
    for (uint8_t attempt = 0; attempt < I2C_RETRY_COUNT; attempt++) {
        if (attempt > 0) {
            Serial.printf("[WARN] Retrying finger %d init (attempt %d/%d)\n",
                          finger, attempt + 1, I2C_RETRY_COUNT);
            delay(I2C_RETRY_DELAY_MS);
        }

        // Select multiplexer channel
        if (!selectChannel(finger)) {
            continue;
        }

        // Initialize DRV2605 on this channel
        if (!_drv[finger].begin()) {
            closeChannels();
            continue;
        }

        // Apply appropriate delay
        // Channel 4 (pinky) requires longer delay due to I2C path length
        if (finger == FINGER_PINKY) {
            delay(I2C_INIT_DELAY_CH4_MS);
        } else {
            delay(I2C_INIT_DELAY_MS);
        }

        // Configure for LRA + RTP mode
        configureDRV2605(_drv[finger]);

        closeChannels();

        // Mark as enabled
        _fingerEnabled[finger] = true;
        _fingerActive[finger] = false;

        return true;
    }

    // All retries failed
    _fingerEnabled[finger] = false;
    return false;
}

void HapticController::configureDRV2605(Adafruit_DRV2605& drv) {
    // 1. Configure for LRA (Linear Resonant Actuator)
    drv.useLRA();

    // 2. Set Real-Time Playback (RTP) mode
    drv.setMode(DRV2605_MODE_REALTIME);

    // 3. Initialize RTP value to 0 (motor off)
    drv.setRealtimeValue(0);
}

bool HapticController::selectChannel(uint8_t finger) {
    if (finger >= MAX_ACTUATORS) {
        return false;
    }

    _tca.openChannel(finger);
    return true;
}

void HapticController::closeChannels() {
    _tca.closeAll();
}

uint8_t HapticController::amplitudeToRTP(uint8_t amplitude) const {
    // Clamp amplitude to valid range
    if (amplitude > MAX_AMPLITUDE) {
        amplitude = MAX_AMPLITUDE;
    }

    // Convert 0-100% to 0-127
    return (uint8_t)((amplitude * DRV2605_MAX_RTP) / MAX_AMPLITUDE);
}

Result HapticController::activate(uint8_t finger, uint8_t amplitude) {
    // Validate finger index
    if (finger >= MAX_ACTUATORS) {
        return Result::ERROR_INVALID_PARAM;
    }

    // Check if finger is enabled
    if (!_fingerEnabled[finger]) {
        return Result::ERROR_DISABLED;
    }

    // Validate amplitude
    if (amplitude > MAX_AMPLITUDE) {
        amplitude = MAX_AMPLITUDE;
    }

    // Select channel
    if (!selectChannel(finger)) {
        return Result::ERROR_HARDWARE;
    }

    // Set RTP value
    uint8_t rtpValue = amplitudeToRTP(amplitude);
    _drv[finger].setRealtimeValue(rtpValue);

    closeChannels();

    // Update state
    _fingerActive[finger] = (amplitude > 0);

    return Result::OK;
}

Result HapticController::deactivate(uint8_t finger) {
    // Validate finger index
    if (finger >= MAX_ACTUATORS) {
        return Result::ERROR_INVALID_PARAM;
    }

    // Check if finger is enabled
    if (!_fingerEnabled[finger]) {
        return Result::ERROR_DISABLED;
    }

    // Select channel
    if (!selectChannel(finger)) {
        return Result::ERROR_HARDWARE;
    }

    // Set RTP value to 0
    _drv[finger].setRealtimeValue(0);

    closeChannels();

    // Update state
    _fingerActive[finger] = false;

    return Result::OK;
}

void HapticController::stopAll() {
    for (uint8_t finger = 0; finger < MAX_ACTUATORS; finger++) {
        if (_fingerEnabled[finger] && _fingerActive[finger]) {
            deactivate(finger);
        }
    }
}

void HapticController::emergencyStop() {
    // Stop all motors regardless of tracked state
    for (uint8_t finger = 0; finger < MAX_ACTUATORS; finger++) {
        if (_fingerEnabled[finger]) {
            if (selectChannel(finger)) {
                _drv[finger].setRealtimeValue(0);
            }
        }
    }
    closeChannels();

    // Clear all active states
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        _fingerActive[i] = false;
    }
}

bool HapticController::isActive(uint8_t finger) const {
    if (finger >= MAX_ACTUATORS) {
        return false;
    }
    return _fingerActive[finger];
}

bool HapticController::isEnabled(uint8_t finger) const {
    if (finger >= MAX_ACTUATORS) {
        return false;
    }
    return _fingerEnabled[finger];
}

Result HapticController::setFrequency(uint8_t finger, uint16_t frequencyHz) {
    // Validate finger index
    if (finger >= MAX_ACTUATORS) {
        return Result::ERROR_INVALID_PARAM;
    }

    // Validate frequency range
    if (frequencyHz < MIN_FREQUENCY_HZ || frequencyHz > MAX_FREQUENCY_HZ) {
        return Result::ERROR_INVALID_PARAM;
    }

    // Check if finger is enabled
    if (!_fingerEnabled[finger]) {
        return Result::ERROR_DISABLED;
    }

    // Select channel
    if (!selectChannel(finger)) {
        return Result::ERROR_HARDWARE;
    }

    // Calculate drive time for LRA frequency
    // Formula from DRV2605 datasheet
    uint8_t driveTime = (uint8_t)((5000 / frequencyHz) & 0x1F);

    // Write to CONTROL1 register (0x1B)
    _drv[finger].writeRegister8(DRV2605_REG_CONTROL1, driveTime);

    closeChannels();

    return Result::OK;
}

uint8_t HapticController::getEnabledCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        if (_fingerEnabled[i]) {
            count++;
        }
    }
    return count;
}

// =============================================================================
// BATTERY MONITOR - Implementation
// =============================================================================

BatteryMonitor::BatteryMonitor() : _initialized(false) {
}

bool BatteryMonitor::begin() {
    // Configure ADC resolution
    analogReadResolution(ADC_RESOLUTION_BITS);

    // Take a test reading to verify ADC works
    uint32_t testReading = analogRead(BATTERY_PIN);

    // Any non-zero reading indicates the ADC is working
    _initialized = true;

    Serial.println(F("[INFO] Battery monitor initialized"));

    return _initialized;
}

float BatteryMonitor::readVoltage() {
    if (!_initialized) {
        return 0.0f;
    }

    // Take multiple samples and average for stability
    uint32_t total = 0;
    for (uint8_t i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        total += analogRead(BATTERY_PIN);
        delay(1);
    }

    float average = (float)total / BATTERY_SAMPLE_COUNT;

    // Convert ADC reading to voltage
    // Formula: (adc_value / max_value) * reference_voltage * divider_ratio
    float voltage = (average / (float)ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE * BATTERY_VOLTAGE_DIVIDER;

    return voltage;
}

uint8_t BatteryMonitor::getPercentage(float voltage) {
    if (voltage < 0) {
        voltage = readVoltage();
    }

    return interpolatePercentage(voltage);
}

uint8_t BatteryMonitor::interpolatePercentage(float voltage) const {
    // Handle edge cases
    if (voltage >= VOLTAGE_CURVE[0][0]) {
        return 100;
    }
    if (voltage <= VOLTAGE_CURVE[VOLTAGE_CURVE_SIZE - 1][0]) {
        return 0;
    }

    // Find the two points to interpolate between
    for (uint8_t i = 0; i < VOLTAGE_CURVE_SIZE - 1; i++) {
        float v1 = VOLTAGE_CURVE[i][0];
        float v2 = VOLTAGE_CURVE[i + 1][0];

        if (voltage <= v1 && voltage > v2) {
            // Linear interpolation
            float p1 = VOLTAGE_CURVE[i][1];
            float p2 = VOLTAGE_CURVE[i + 1][1];

            float ratio = (voltage - v2) / (v1 - v2);
            float percentage = p2 + ratio * (p1 - p2);

            return (uint8_t)percentage;
        }
    }

    // Fallback (should not reach here)
    return 0;
}

BatteryStatus BatteryMonitor::getStatus() {
    BatteryStatus status;

    status.voltage = readVoltage();
    status.percentage = interpolatePercentage(status.voltage);
    status.isLow = (status.voltage < BATTERY_LOW_VOLTAGE);
    status.isCritical = (status.voltage < BATTERY_CRITICAL_VOLTAGE);

    return status;
}

bool BatteryMonitor::isLow(float voltage) {
    if (voltage < 0) {
        voltage = readVoltage();
    }
    return voltage < BATTERY_LOW_VOLTAGE;
}

bool BatteryMonitor::isCritical(float voltage) {
    if (voltage < 0) {
        voltage = readVoltage();
    }
    return voltage < BATTERY_CRITICAL_VOLTAGE;
}

// =============================================================================
// LED CONTROLLER - Implementation
// =============================================================================

LEDController::LEDController()
    : _pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800),
      _currentColor(0, 0, 0),
      _initialized(false) {
}

bool LEDController::begin() {
    _pixel.begin();
    _pixel.setBrightness(LED_BRIGHTNESS);
    _pixel.clear();
    _pixel.show();

    _initialized = true;

    Serial.println(F("[INFO] LED controller initialized"));

    return _initialized;
}

void LEDController::setColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!_initialized) {
        return;
    }

    _currentColor = RGBColor(r, g, b);
    _pixel.setPixelColor(0, _pixel.Color(r, g, b));
    _pixel.show();
}

void LEDController::setColor(const RGBColor& color) {
    setColor(color.r, color.g, color.b);
}

void LEDController::off() {
    setColor(0, 0, 0);
}

void LEDController::setBrightness(uint8_t brightness) {
    if (!_initialized) {
        return;
    }

    _pixel.setBrightness(brightness);
    _pixel.show();
}

RGBColor LEDController::getColor() const {
    return _currentColor;
}
