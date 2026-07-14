/**
 * @file hardware.cpp
 * @brief BlueBuzzah hardware abstraction layer - Implementations
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "hardware.h"

// =============================================================================
// I2C MUTEX RAII LOCK
// =============================================================================

/**
 * @brief RAII lock for I2C mutex - automatically releases on scope exit
 *
 * Usage:
 *   I2CMutexLock lock(_i2cMutex);
 *   if (!lock.acquired()) return Result::ERROR_BUSY;
 *   // ... I2C operations are now protected ...
 *   // Mutex released automatically when lock goes out of scope
 */
class I2CMutexLock {
public:
    I2CMutexLock(SemaphoreHandle_t mutex, TickType_t timeout = pdMS_TO_TICKS(100))
        : _mutex(mutex), _acquired(false) {
        if (_mutex != nullptr) {
            _acquired = (xSemaphoreTake(_mutex, timeout) == pdTRUE);
        } else {
            // No mutex (failed creation or single-threaded) - allow operation
            _acquired = true;
        }
    }

    ~I2CMutexLock() {
        if (_mutex != nullptr && _acquired) {
            xSemaphoreGive(_mutex);
        }
    }

    // Prevent copying
    I2CMutexLock(const I2CMutexLock&) = delete;
    I2CMutexLock& operator=(const I2CMutexLock&) = delete;

    bool acquired() const { return _acquired; }

private:
    SemaphoreHandle_t _mutex;
    bool _acquired;
};

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

HapticController::HapticController() : _tca(TCA9548A_ADDRESS), _initialized(false), _preSelectedFinger(-1), _i2cMutex(nullptr) {
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) {
        _fingerActive[i] = false;
        _fingerEnabled[i] = false;
    }
}

bool HapticController::begin() {
    Serial.println(F("[INFO] Initializing haptic controller..."));

    // Create I2C mutex for thread-safe access
    _i2cMutex = xSemaphoreCreateMutex();
    if (_i2cMutex == nullptr) {
        Serial.println(F("[WARN] Failed to create I2C mutex - proceeding without thread safety"));
    }

    // Initialize I2C at 400kHz
#if defined(BOARD_PENTABUZZER_ESP32S3)
    Wire.begin(SDA_PIN_OVERRIDE, SCL_PIN_OVERRIDE);
#else
    Wire.begin();
#endif
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

        // Apply initialization delay for I2C stabilization (channel 4 has the
        // longest PCB trace and needs extra settling time)
        delay(finger == 4 ? I2C_INIT_DELAY_CH4_MS : I2C_INIT_DELAY_MS);

        // Configure for LRA + RTP mode
        configureDRV2605(_drv[finger]);

        // SAFETY: Immediately stop motor after init - DRV2605 retains RTP value
        // across MCU resets, so motor may be buzzing from pre-power-off state
        _drv[finger].setRealtimeValue(0);

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
    // 0. Read register 0x00 in attempt to un-brick corrupted boards
    drv.readRegister8(DRV2605_REG_STATUS);

    // 1. Configure for LRA (Linear Resonant Actuator)
    drv.useLRA();

    // 2. Enable open-loop mode (v1 requirement for proper LRA operation)
    // Register 0x1D (CONTROL3): Set bits 5 (N_PWM_ANALOG) and 0 (LRA_OPEN_LOOP)
    uint8_t control3 = drv.readRegister8(0x1D);
    drv.writeRegister8(0x1D, control3 | 0x21);

    // 3. Set peak voltage to 2.50V (v1 default: ACTUATOR_VOLTAGE = 2.50)
    // Register 0x17 (OD_CLAMP): voltage = value * 0.02122V
    // 2.50V / 0.02122 ≈ 118
    drv.writeRegister8(0x17, 118);

    // 4. Set driving frequency to 250Hz (v1 default)
    // Register 0x20 (OL_LRA_PERIOD): period = 1 / (freq * 0.00009849)
    // 250Hz: 1 / (250 * 0.00009849) ≈ 40
    drv.writeRegister8(0x20, 40);

    // 5. Set Real-Time Playback (RTP) mode
    drv.setMode(DRV2605_MODE_REALTIME);

    // 6. Initialize RTP value to 0 (motor off)
    drv.setRealtimeValue(0);
}

// DRV2605 FEEDBACK register and its N_ERM_LRA bit: set by configureDRV2605
// (LRA mode) and cleared by a chip reset, so it doubles as a reset canary.
constexpr uint8_t DRV_REG_FEEDBACK = 0x1A;
constexpr uint8_t DRV_FB_N_ERM_LRA = 0x80;

uint8_t HapticController::verifyAndHeal() {
    // A DRV2605 that lost VDD (VBat brownout: sagging/absent/miswired
    // battery) comes back at POR defaults - standby, ERM mode - and then
    // silently ignores RTP drive.
    //
    // Latency budget: probes ONE finger per call (round-robin, ~200us with
    // the I2C mutex held) so the periodic cost never approaches the sync
    // protocol's microsecond regime. Full coverage every MAX_ACTUATORS
    // calls. Only on a detected reset does the (~1ms worst-case) all-chip
    // reconfigure run - the motors are dead at that point, so its mutex
    // hold cannot disturb any activation that would have produced motion.
    //
    // NOTE: the round-robin static assumes single-task use. Both callers
    // (macrocycle-start callback, deferred-queue executor) run in the main
    // loop task; keep it that way.
    static uint8_t nextFinger = 0;
    uint8_t healed = 0;

    I2CMutexLock lock(_i2cMutex);
    if (!lock.acquired()) {
        return 0;  // Bus busy (motor task mid-activation) - probe next cycle
    }

    uint8_t probe = nextFinger;
    nextFinger = static_cast<uint8_t>((nextFinger + 1) % MAX_ACTUATORS);
    if (!_fingerEnabled[probe] || !selectChannel(probe)) return 0;
    bool wasReset = (_drv[probe].readRegister8(DRV_REG_FEEDBACK) & DRV_FB_N_ERM_LRA) == 0;
    closeChannels();
    if (!wasReset) return 0;

    Serial.printf("[FAULT] DRV2605 F%u reset detected (VBat brownout?) - reconfiguring all\n", probe);
    for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
        if (!_fingerEnabled[f]) continue;
        if (!selectChannel(f)) continue;
        if ((_drv[f].readRegister8(DRV_REG_FEEDBACK) & DRV_FB_N_ERM_LRA) == 0) {
            configureDRV2605(_drv[f]);
            _drv[f].setRealtimeValue(0);
            healed++;
        }
        closeChannels();
    }
    return healed;
}

// A burst larger than the estimator's window would be silently truncated
static_assert(VBAT_BURST_SAMPLES <= VBAT_MAX_BURST,
              "VBAT_BURST_SAMPLES exceeds VbatEstimator's burst window");

uint8_t HapticController::readVBatBurst(uint8_t* out, uint8_t maxSamples) {
    constexpr uint8_t DRV_REG_VBAT = 0x21;

    if (out == nullptr || maxSamples == 0) {
        return 0;
    }

    // First enabled finger is the voltmeter; all chips share the VBat rail.
    // _fingerEnabled is set at init, so this scan needs no lock.
    uint8_t finger = MAX_ACTUATORS;
    for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
        if (_fingerEnabled[f]) { finger = f; break; }
    }
    if (finger == MAX_ACTUATORS) {
        return 0;
    }

    // Each sample takes its own short-lived lock so a pending motor
    // activation is delayed at most one mux-select-read-close, not the
    // whole burst. anyMotorActive() is rechecked every iteration because a
    // motor can start mid-burst (driven LRA sags VBat).
    for (uint8_t i = 0; i < maxSamples; i++) {
        if (anyMotorActive()) {
            return 0;  // Motor became active mid-burst - discard the partial burst
        }

        I2CMutexLock lock(_i2cMutex, pdMS_TO_TICKS(5));
        if (!lock.acquired()) {
            return 0;  // Bus busy - discard the partial burst
        }

        if (!selectChannel(finger)) {
            return 0;
        }

        if (i == 0) {
            // POR canary: a brownout resets the chip into standby, where VBAT
            // is invalid. Abort the burst; verifyAndHeal() reconfigures the
            // chip. Only checked on the first sample's transaction.
            if ((_drv[finger].readRegister8(DRV_REG_FEEDBACK) & DRV_FB_N_ERM_LRA) == 0) {
                closeChannels();
                return 0;
            }
        }

        out[i] = _drv[finger].readRegister8(DRV_REG_VBAT);
        closeChannels();
    }

    return maxSamples;
}

bool HapticController::anyMotorActive() const {
    // Read cross-task without the mutex: plain bool loads are atomic on
    // Xtensa/Cortex-M, and a stale value only costs one discarded or late
    // VBAT burst - never a corrupt reading.
    for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
        if (_fingerActive[f]) {
            return true;
        }
    }
    return false;
}

void HapticController::diagRegs(const char* tag) {
    if (!_fingerEnabled[0]) return;
    I2CMutexLock lock(_i2cMutex);
    if (!lock.acquired()) {
        Serial.printf("[DIAG:%s] bus busy, register dump skipped\n", tag);
        return;
    }
    selectChannel(0);
    Serial.printf("[DIAG:%s] F0 regs: STATUS=0x%02X MODE=0x%02X RTP=0x%02X FB=0x%02X "
                  "CTRL3=0x%02X ODC=0x%02X OLP=0x%02X\n", tag,
                  _drv[0].readRegister8(0x00), _drv[0].readRegister8(0x01),
                  _drv[0].readRegister8(0x02), _drv[0].readRegister8(DRV_REG_FEEDBACK),
                  _drv[0].readRegister8(0x1D), _drv[0].readRegister8(0x17),
                  _drv[0].readRegister8(0x20));
    closeChannels();
}

void HapticController::diagDriveOne(uint8_t finger) {
    if (finger >= MAX_ACTUATORS || !_fingerEnabled[finger]) {
        Serial.printf("[DIAG] F%u invalid or not enabled\n", finger);
        return;
    }
    {
        I2CMutexLock lock(_i2cMutex);
        if (!lock.acquired()) {
            Serial.println(F("[DIAG] bus busy, test aborted"));
            return;
        }
        selectChannel(finger);
        configureDRV2605(_drv[finger]);
        _drv[finger].setRealtimeValue(127);
        closeChannels();  // RTP keeps driving; the mux only routes I2C
    }
#if defined(BOARD_PENTABUZZER_ESP32S3)
    Serial.printf("[DIAG] >>> DRIVING F%u (silk port %u) at full amplitude for 2s <<<\n",
                  finger, MOTOR_SILK_PORT(finger));
#else
    Serial.printf("[DIAG] >>> DRIVING F%u at full amplitude for 2s <<<\n", finger);
#endif
    delay(2000);
    uint8_t fb = 0;
    {
        // SAFETY: stop the motor even if the mutex is contended - same
        // proceed-anyway policy as emergencyStop()
        I2CMutexLock lock(_i2cMutex);
        selectChannel(finger);
        _drv[finger].setRealtimeValue(0);
        fb = _drv[finger].readRegister8(DRV_REG_FEEDBACK);
        closeChannels();
    }
    Serial.printf("[DIAG] F%u done (FB=0x%02X%s)\n", finger, fb,
                  (fb & DRV_FB_N_ERM_LRA) ? "" : "  *** CHIP RESET - supply dipped");
}

void HapticController::diagSweep() {
    // Assembly-QA sweep: buzz each channel alone at full amplitude, and
    // check afterward whether any DRV2605 reverted to POR defaults
    // (FEEDBACK bit7 lost => chip power-cycled => VBat sagged below UVLO,
    // e.g. missing/discharged/miswired battery).
    //
    // NOTE: the DRV2605's built-in load diagnostics (MODE=0x07) are NOT used
    // here - their pass/fail verdict assumes closed-loop operation and is
    // meaningless with our open-loop LRA configuration. The operator feeling
    // each buzz is the actuator test; the register canary is the supply test.
    diagRegs("pre");

    // Canary = first enabled finger: reading a disabled/absent chip would
    // false-positive as "reset" on every line
    int8_t canary = -1;
    for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
        if (_fingerEnabled[f]) { canary = static_cast<int8_t>(f); break; }
    }
    if (canary < 0) {
        Serial.println(F("[DIAG] no enabled fingers - nothing to test"));
        return;
    }

    {
        I2CMutexLock lock(_i2cMutex);
        if (!lock.acquired()) {
            Serial.println(F("[DIAG] bus busy, sweep aborted"));
            return;
        }
        for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
            if (!_fingerEnabled[f]) continue;
            selectChannel(f);
            configureDRV2605(_drv[f]);
            _drv[f].setRealtimeValue(0);
            closeChannels();
        }
    }
    diagRegs("reconfig");

    // 600ms buzz + 250ms gap = ~4.3s for 5 channels: stays under the 6s
    // glove keepalive timeout so a mid-session sweep can't drop the link.
    // The I2C mutex is held only around the transactions, never across the
    // delays, so a concurrent activation is delayed by microseconds at most.
    for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
        if (!_fingerEnabled[f]) continue;
#if defined(BOARD_PENTABUZZER_ESP32S3)
        Serial.printf("[DIAG] buzzing F%u (silk port %u), RTP=127, 600ms...\n",
                      f, MOTOR_SILK_PORT(f));
#else
        Serial.printf("[DIAG] buzzing F%u, RTP=127, 600ms...\n", f);
#endif
        {
            I2CMutexLock lock(_i2cMutex);
            if (!lock.acquired()) {
                Serial.println(F("[DIAG] bus busy, sweep aborted"));
                return;
            }
            selectChannel(f);
            _drv[f].setRealtimeValue(127);
            closeChannels();  // RTP keeps driving; the mux only routes I2C
        }
        delay(600);
        uint8_t fbSelf = 0;
        uint8_t fbCanary = 0;
        {
            // SAFETY: stop the motor even if the mutex is contended - same
            // proceed-anyway policy as emergencyStop()
            I2CMutexLock lock(_i2cMutex);
            selectChannel(f);
            _drv[f].setRealtimeValue(0);
            fbSelf = _drv[f].readRegister8(DRV_REG_FEEDBACK);
            closeChannels();
            if (static_cast<uint8_t>(canary) != f) {
                selectChannel(static_cast<uint8_t>(canary));
                fbCanary = _drv[canary].readRegister8(DRV_REG_FEEDBACK);
                closeChannels();
            } else {
                fbCanary = fbSelf;
            }
        }
        bool reset = ((fbSelf & DRV_FB_N_ERM_LRA) == 0) || ((fbCanary & DRV_FB_N_ERM_LRA) == 0);
        Serial.printf("[DIAG]   F%u: self FB=0x%02X | canary FB=0x%02X%s\n",
                      f, fbSelf, fbCanary,
                      reset ? "  *** CHIP RESET - supply dipped (check battery)" : "");
        if (reset) {
            // Re-heal so the remaining channels still get a valid test
            I2CMutexLock lock(_i2cMutex);
            for (uint8_t h = 0; h < MAX_ACTUATORS; h++) {
                if (!_fingerEnabled[h]) continue;
                selectChannel(h);
                configureDRV2605(_drv[h]);
                _drv[h].setRealtimeValue(0);
                closeChannels();
            }
        }
        delay(250);
    }
    Serial.println(F("[DIAG] sweep done"));
}

uint8_t HapticController::probeMotorPresence() {
    // Presence detection via LRA auto-calibration (MODE=0x07): calibration
    // locks onto the LRA's resonance using measured back-EMF, which requires
    // a physically attached motor - an empty JST cannot converge and fails
    // (DIAG_RESULT set). The ERM diagnostics route (MODE=0x06) was tried
    // first and rejected: its back-EMF stage expects a DC motor, so attached
    // LRAs failed identically to open ports (STATUS=0xEC on everything).
    // Full LRA open-loop config is restored afterward; the cal coefficients
    // it writes are ignored in open-loop mode. Populated motors buzz
    // noticeably (~0.5s each) during the probe.
    constexpr uint8_t DRV_STATUS_DIAG_RESULT = 0x08;  // STATUS bit3: 1 = cal failed
    constexpr uint32_t DIAG_GO_TIMEOUT_MS = 2000;     // auto-cal can run ~1.2s
    constexpr uint8_t DRV_FB_LRA_DEFAULTS = 0xB6;     // LRA + stock brake/loop/BEMF gains
    constexpr uint8_t DRV_RATEDV_2V0_250HZ = 0x50;    // ~2.0Vrms rated at 250Hz LRA
    constexpr uint8_t DRV_ODCLAMP_2V5 = 118;          // 2.5V clamp (matches run config)
    constexpr uint8_t DRV_CTRL1_DRIVETIME_250HZ = 0x8F;  // DRIVE_TIME=15 = half-period of 250Hz
    constexpr uint8_t DRV_REG_CONTROL3 = 0x1D;
    constexpr uint8_t DRV_CTRL3_POR = 0xA0;           // closed-loop for calibration

    uint8_t mask = 0;
    _lastProbeDipped = false;
    Serial.println(F("[DIAG] Motor presence probe (all ports)"));
    for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
#if defined(BOARD_PENTABUZZER_ESP32S3)
        Serial.printf("[DIAG]   silk port %u (F%u): ", MOTOR_SILK_PORT(f), f);
#else
        Serial.printf("[DIAG]   F%u: ", f);
#endif
        if (!_fingerEnabled[f]) {
            Serial.println(F("NO DRIVER (init failed - check battery power)"));
            continue;
        }

        // Kick off diagnostics in ERM mode
        {
            I2CMutexLock lock(_i2cMutex);
            if (!lock.acquired()) {
                Serial.println(F("bus busy, probe aborted"));
                return getMotorPresentCount();  // keep previous result
            }
            if (!selectChannel(f)) {
                Serial.println(F("MUX SELECT FAILED"));
                continue;
            }
            // Configure for closed-loop LRA auto-calibration
            _drv[f].writeRegister8(DRV_REG_FEEDBACK, DRV_FB_LRA_DEFAULTS);
            _drv[f].writeRegister8(DRV2605_REG_RATEDV, DRV_RATEDV_2V0_250HZ);
            _drv[f].writeRegister8(DRV2605_REG_CLAMPV, DRV_ODCLAMP_2V5);
            _drv[f].writeRegister8(DRV2605_REG_CONTROL1, DRV_CTRL1_DRIVETIME_250HZ);
            _drv[f].writeRegister8(DRV_REG_CONTROL3, DRV_CTRL3_POR);
            _drv[f].writeRegister8(DRV2605_REG_MODE, DRV2605_MODE_AUTOCAL);
            _drv[f].writeRegister8(DRV2605_REG_GO, 1);
            closeChannels();
        }

        // Poll until GO self-clears (mutex per transaction, never across delays)
        bool done = false;
        bool dipped = false;
        uint8_t status = 0;
        uint32_t start = millis();
        while (millis() - start < DIAG_GO_TIMEOUT_MS) {
            delay(10);
            I2CMutexLock lock(_i2cMutex);
            if (!lock.acquired()) continue;
            selectChannel(f);
            if ((_drv[f].readRegister8(DRV2605_REG_GO) & 0x01) == 0) {
                status = _drv[f].readRegister8(DRV2605_REG_STATUS);
                // Brownout canary: we wrote FEEDBACK with N_ERM_LRA (bit7)
                // set before GO; if it reads back clear the chip hit POR
                // mid-cal (supply dip, e.g. USB-only power) and STATUS is
                // POR garbage that would fake a PRESENT verdict
                uint8_t fb = _drv[f].readRegister8(DRV_REG_FEEDBACK);
                dipped = (fb & DRV_FB_N_ERM_LRA) == 0;
                done = true;
            }
            closeChannels();
            if (done) break;
        }

        // Restore LRA open-loop config regardless of outcome (proceed-anyway
        // on mutex timeout - same policy as emergencyStop)
        {
            I2CMutexLock lock(_i2cMutex);
            selectChannel(f);
            configureDRV2605(_drv[f]);
            _drv[f].setRealtimeValue(0);
            closeChannels();
        }

        if (!done) {
            // Discriminate "diag never finished" (GO=0x01, STATUS has valid
            // device ID 0xE0 in bits 7:5) from "I2C reads failing" (0xFF/0x00)
            uint8_t rawGo = 0xAA, rawStatus = 0xAA, rawFb = 0xAA;
            {
                I2CMutexLock lock(_i2cMutex);
                if (lock.acquired() && selectChannel(f)) {
                    rawGo = _drv[f].readRegister8(DRV2605_REG_GO);
                    rawStatus = _drv[f].readRegister8(DRV2605_REG_STATUS);
                    rawFb = _drv[f].readRegister8(DRV_REG_FEEDBACK);
                    closeChannels();
                }
            }
            Serial.printf("PROBE TIMEOUT (raw GO=0x%02X STATUS=0x%02X FB=0x%02X)\n",
                          rawGo, rawStatus, rawFb);
            if (rawFb != 0xAA && (rawFb & DRV_FB_N_ERM_LRA) == 0) {
                _lastProbeDipped = true;
            }
        } else if (dipped) {
            Serial.printf("SUPPLY DIP (chip reset mid-probe, STATUS=0x%02X) - check battery\n",
                          status);
            _lastProbeDipped = true;
        } else if (status & DRV_STATUS_DIAG_RESULT) {
            Serial.printf("NO MOTOR (open load, STATUS=0x%02X)\n", status);
        } else {
            Serial.printf("MOTOR PRESENT (STATUS=0x%02X)\n", status);
            mask |= static_cast<uint8_t>(1u << f);
        }
    }
    if (_lastProbeDipped) {
        // Supply sagged under cal drive: every verdict this pass is suspect.
        // Keep the previous mask instead of committing garbage.
        Serial.println(F("[DIAG] presence probe UNRELIABLE (supply dip) - results discarded"));
        return getMotorPresentCount();
    }
    _motorPresentMask = mask;
    uint8_t count = getMotorPresentCount();
    Serial.printf("[DIAG] presence probe done: %u/%u motors detected\n",
                  count, MAX_ACTUATORS);
    return count;
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
    _preSelectedFinger = -1;  // Invalidate pre-selection
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

    // Acquire I2C mutex for thread-safe access
    I2CMutexLock lock(_i2cMutex);
    if (!lock.acquired()) {
        return Result::ERROR_BUSY;
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

    // Acquire I2C mutex for thread-safe access
    I2CMutexLock lock(_i2cMutex);
    if (!lock.acquired()) {
        return Result::ERROR_BUSY;
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
    // Acquire I2C mutex - use longer timeout for emergency stop
    // If we can't acquire, proceed anyway (emergency takes priority)
    I2CMutexLock lock(_i2cMutex, pdMS_TO_TICKS(200));

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

    // Skip I2C if frequency unchanged (latency optimization)
    if (_lastFrequency[finger] == frequencyHz) {
        return Result::OK;
    }

    // Acquire I2C mutex for thread-safe access
    I2CMutexLock lock(_i2cMutex);
    if (!lock.acquired()) {
        return Result::ERROR_BUSY;
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

    // Cache the frequency for future skip optimization
    _lastFrequency[finger] = frequencyHz;

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
// PRE-SELECTION OPTIMIZATION METHODS
// =============================================================================

bool HapticController::selectChannelPersistent(uint8_t finger) {
    if (finger >= MAX_ACTUATORS) {
        return false;
    }

    // Acquire I2C mutex for thread-safe access
    I2CMutexLock lock(_i2cMutex);
    if (!lock.acquired()) {
        return false;
    }

    // Open channel and leave it open (no closeChannels call)
    _tca.openChannel(finger);
    _preSelectedFinger = static_cast<int8_t>(finger);  // Track pre-selected channel
    return true;
}

Result HapticController::setFrequencyDirect(uint8_t finger, uint16_t frequencyHz) {
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

    // Acquire I2C mutex for thread-safe access
    I2CMutexLock lock(_i2cMutex);
    if (!lock.acquired()) {
        return Result::ERROR_BUSY;
    }

    // PRECONDITION: Channel must already be selected
    // Write directly to DRV2605 without mux operations

    // Calculate drive time for LRA frequency (same formula as setFrequency)
    uint8_t driveTime = (uint8_t)((5000 / frequencyHz) & 0x1F);

    // Write to CONTROL1 register (0x1B)
    _drv[finger].writeRegister8(DRV2605_REG_CONTROL1, driveTime);

    return Result::OK;
}

Result HapticController::activatePreSelected(uint8_t finger, uint8_t amplitude) {
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

    // Acquire I2C mutex for thread-safe access
    I2CMutexLock lock(_i2cMutex);
    if (!lock.acquired()) {
        return Result::ERROR_BUSY;
    }

    // Only re-select channel if pre-selection was invalidated
    // Pre-selection is invalidated when closeChannels() or closeAllChannels() is called
    // (e.g., by deactivate() on another motor)
    if (_preSelectedFinger != static_cast<int8_t>(finger)) {
        if (!selectChannel(finger)) {
            return Result::ERROR_HARDWARE;
        }
        // Note: selectChannel() doesn't update _preSelectedFinger (it's for persistent selection)
    }

    // Write RTP value (frequency was already set during pre-selection)
    // This is the minimal critical-path I2C: just the RTP write (~100-150µs)
    uint8_t rtpValue = amplitudeToRTP(amplitude);
    _drv[finger].setRealtimeValue(rtpValue);

    // Update state
    _fingerActive[finger] = (amplitude > 0);

    return Result::OK;
}

void HapticController::closeAllChannels() {
    // Acquire I2C mutex - if we can't acquire, still try to close
    // (closing is important for safety)
    I2CMutexLock lock(_i2cMutex, pdMS_TO_TICKS(50));

    _tca.closeAll();
    _preSelectedFinger = -1;  // Invalidate pre-selection
}

// =============================================================================
// BATTERY MONITOR - Implementation
// =============================================================================

BatteryMonitor::BatteryMonitor() : _initialized(false) {
}

bool BatteryMonitor::begin() {
#if BATTERY_SENSE_ADC
    // Configure ADC resolution
    analogReadResolution(ADC_RESOLUTION_BITS);

    // Take a test reading to verify ADC works
    uint32_t testReading = analogRead(BATTERY_PIN);

    // Any non-zero reading indicates the ADC is working
    _initialized = (testReading > 0);

    Serial.println(F("[INFO] Battery monitor initialized"));

    return _initialized;
#else
    // No battery ADC: read the DRV2605s' VBAT register instead. The chips
    // run directly from VBat, so their supply monitor doubles as a
    // voltmeter once at least one is active (RTP mode, standby cleared)
    // from haptic init. A failed read at boot means the bus is
    // wedged or every chip browned out - report the failure rather than
    // fake a healthy pack.
    for (uint8_t attempt = 0; attempt < 3 && !_estimator.hasReading(); attempt++) {
        uint8_t raw[VBAT_BURST_SAMPLES];
        uint8_t n = _haptic ? _haptic->readVBatBurst(raw, VBAT_BURST_SAMPLES) : 0;
        _estimator.addBurst(raw, n);
        if (!_estimator.hasReading()) {
            delay(10);
        }
    }

    _initialized = _estimator.hasReading();
    if (_initialized) {
        Serial.println(F("[INFO] Battery monitor initialized (DRV2605 VBAT)"));
    } else {
        Serial.println(F("[ERROR] Battery monitor: no VBAT reading (chips reset? check battery)"));
    }
    return _initialized;
#endif
}

float BatteryMonitor::readVoltage() {
#if BATTERY_SENSE_ADC
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
#else
    // Refresh from the DRV2605 VBAT register, but only while no motor is
    // driven - an LRA pulse sags VBat by hundreds of mV and would read
    // falsely low. During therapy (or if the bus is busy / a chip PORed)
    // the last idle estimate is returned instead.
    // A failed boot burst (chips in POR standby: USB-only brownout, EN
    // toggle) must not latch the monitor dead - keep retrying here and
    // recover on the first successful read.
    if (_haptic && !_haptic->anyMotorActive()) {
        uint8_t raw[VBAT_BURST_SAMPLES];
        uint8_t n = _haptic->readVBatBurst(raw, VBAT_BURST_SAMPLES);
        _estimator.addBurst(raw, n);
        if (!_initialized && _estimator.hasReading()) {
            _initialized = true;
            Serial.println(F("[INFO] Battery monitor recovered (DRV2605 VBAT)"));
        }
    }
    return _estimator.voltage();
#endif
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
      _baseColor(0, 0, 0),
      _displayColor(0, 0, 0),
      _pattern(LEDPattern::OFF),
      _initialized(false),
      _patternStartTime(0),
      _blinkState(false),
      _lastBlinkToggle(0) {
}

bool LEDController::begin() {
    _pixel.begin();
    _pixel.setBrightness(LED_BRIGHTNESS);
    _pixel.clear();
    _pixel.show();

    _initialized = true;
    _patternStartTime = millis();

    Serial.println(F("[INFO] LED controller initialized"));

    return _initialized;
}

void LEDController::update() {
    if (!_initialized) {
        return;
    }

    uint32_t now = millis();

    switch (_pattern) {
        case LEDPattern::SOLID:
            // No animation needed - color already applied
            break;

        case LEDPattern::BREATHE_SLOW: {
            float brightness = calculateBreatheBrightness(LED_BREATHE_SLOW_MS);
            RGBColor modulated(
                (uint8_t)(_baseColor.r * brightness),
                (uint8_t)(_baseColor.g * brightness),
                (uint8_t)(_baseColor.b * brightness)
            );
            if (modulated != _displayColor) {
                applyColor(modulated);
            }
            break;
        }

        case LEDPattern::PULSE_SLOW: {
            float brightness = calculateBreatheBrightness(LED_PULSE_SLOW_MS);
            RGBColor modulated(
                (uint8_t)(_baseColor.r * brightness),
                (uint8_t)(_baseColor.g * brightness),
                (uint8_t)(_baseColor.b * brightness)
            );
            if (modulated != _displayColor) {
                applyColor(modulated);
            }
            break;
        }

        case LEDPattern::BLINK_FAST: {
            uint32_t interval = _blinkState ? LED_BLINK_FAST_ON_MS : LED_BLINK_FAST_OFF_MS;
            if (now - _lastBlinkToggle >= interval) {
                _lastBlinkToggle = now;
                _blinkState = !_blinkState;
                applyColor(_blinkState ? _baseColor : Colors::OFF);
            }
            break;
        }

        case LEDPattern::BLINK_SLOW: {
            uint32_t interval = _blinkState ? LED_BLINK_SLOW_ON_MS : LED_BLINK_SLOW_OFF_MS;
            if (now - _lastBlinkToggle >= interval) {
                _lastBlinkToggle = now;
                _blinkState = !_blinkState;
                applyColor(_blinkState ? _baseColor : Colors::OFF);
            }
            break;
        }

        case LEDPattern::BLINK_URGENT: {
            uint32_t interval = _blinkState ? LED_BLINK_URGENT_ON_MS : LED_BLINK_URGENT_OFF_MS;
            if (now - _lastBlinkToggle >= interval) {
                _lastBlinkToggle = now;
                _blinkState = !_blinkState;
                applyColor(_blinkState ? _baseColor : Colors::OFF);
            }
            break;
        }

        case LEDPattern::BLINK_CONNECT: {
            uint32_t interval = _blinkState ? LED_BLINK_CONNECT_ON_MS : LED_BLINK_CONNECT_OFF_MS;
            if (now - _lastBlinkToggle >= interval) {
                _lastBlinkToggle = now;
                _blinkState = !_blinkState;
                applyColor(_blinkState ? _baseColor : Colors::OFF);
            }
            break;
        }

        case LEDPattern::DOUBLE_BLINK: {
            // Two 150ms blinks, then a 650ms pause (1250ms cycle)
            uint32_t t = (now - _patternStartTime) % 1250;
            bool on = (t < 150) || (t >= 300 && t < 450);
            if (on != _blinkState) {
                _blinkState = on;
                applyColor(on ? _baseColor : Colors::OFF);
            }
            break;
        }

        case LEDPattern::OFF:
            // LED is off, nothing to update
            break;
    }
}

void LEDController::setPattern(const RGBColor& color, LEDPattern pattern) {
    if (!_initialized) {
        return;
    }

    _baseColor = color;
    _pattern = pattern;
    _patternStartTime = millis();
    _lastBlinkToggle = millis();
    _blinkState = true;  // Start blink patterns in ON state

    // Immediately apply the initial state
    switch (pattern) {
        case LEDPattern::SOLID:
            applyColor(color);
            break;

        case LEDPattern::BREATHE_SLOW:
        case LEDPattern::PULSE_SLOW:
            // Start at full brightness, update() will animate
            applyColor(color);
            break;

        case LEDPattern::BLINK_FAST:
        case LEDPattern::BLINK_SLOW:
        case LEDPattern::BLINK_URGENT:
        case LEDPattern::BLINK_CONNECT:
        case LEDPattern::DOUBLE_BLINK:
            // Start in ON state
            applyColor(color);
            break;

        case LEDPattern::OFF:
            applyColor(Colors::OFF);
            break;
    }
}

void LEDController::setColor(uint8_t r, uint8_t g, uint8_t b) {
    setPattern(RGBColor(r, g, b), LEDPattern::SOLID);
}

void LEDController::setColor(const RGBColor& color) {
    setPattern(color, LEDPattern::SOLID);
}

void LEDController::off() {
    setPattern(Colors::OFF, LEDPattern::OFF);
}

void LEDController::setBrightness(uint8_t brightness) {
    if (!_initialized) {
        return;
    }

    // Cap brightness to prevent external code from exceeding max
    uint8_t cappedBrightness = (brightness > LED_BRIGHTNESS) ? LED_BRIGHTNESS : brightness;
    _pixel.setBrightness(cappedBrightness);
    _pixel.show();
}

RGBColor LEDController::getColor() const {
    return _baseColor;
}

LEDPattern LEDController::getPattern() const {
    return _pattern;
}

void LEDController::applyColor(const RGBColor& color) {
    _displayColor = color;
    _pixel.setPixelColor(0, _pixel.Color(color.r, color.g, color.b));
    _pixel.show();
}

float LEDController::calculateBreatheBrightness(uint32_t cycleMs) const {
    // Calculate position in cycle (0.0 to 1.0)
    uint32_t elapsed = millis() - _patternStartTime;
    float position = (float)(elapsed % cycleMs) / (float)cycleMs;

    // Use sine wave for smooth breathing effect
    // sin goes from -1 to 1, we want 0 to 1
    // Position 0 = brightness 0.5, position 0.25 = brightness 1.0
    // position 0.5 = brightness 0.5, position 0.75 = brightness 0.0
    float brightness = (sinf(position * 2.0f * static_cast<float>(PI) - static_cast<float>(PI) / 2.0f) + 1.0f) / 2.0f;

    // Clamp to ensure we don't go below a minimum visibility threshold
    const float MIN_BRIGHTNESS = 0.1f;
    return MIN_BRIGHTNESS + brightness * (1.0f - MIN_BRIGHTNESS);
}
