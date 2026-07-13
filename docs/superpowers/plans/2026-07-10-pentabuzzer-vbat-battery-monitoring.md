# PentaBuzzer Battery Monitoring via DRV2605 VBAT Register — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the PentaBuzzer (ESP32-S3, no battery ADC) real battery telemetry by reading the DRV2605L `VBAT` register (0x21) over I2C, with full behavioral parity with the v2 (nRF52840) board: same `BatteryMonitor` API, same boot/periodic serial logs, same low/critical state-machine behavior, and the same `BATTERY`/`INFO` BLE responses (`BATP`/`BATS`) to the mobile app and PRIMARY↔SECONDARY `GET_BATTERY` relay.

**Architecture:** The five DRV2605L drivers run directly from the VBat rail and are held active (EN high, RTP mode, standby cleared) from haptic init until deep sleep, so register 0x21 (`VDD = raw × 5.6 V / 255`, ~22 mV LSB) is a valid battery voltmeter at any time except after a brownout POR (chip reverts to standby — detected by the existing FEEDBACK-register canary). A new pure module (`vbat_estimator`) converts and filters raw samples (median-of-burst + EMA) and is unit-testable natively; a small `HapticController` method does the I2C burst read under the existing non-blocking mutex; `BatteryMonitor` gets a Penta backend behind the existing API so every downstream consumer (menu/BLE, sync relay, state machine, LED, logs) works unchanged.

**Tech Stack:** Arduino C++20 via PlatformIO; Unity test framework (`pio test -e native` / `native_penta`); Adafruit_DRV2605 + TCA9548A libs; FreeRTOS mutex (existing `I2CMutexLock` pattern).

## Global Constraints

- No planning artifacts in source code (no WP/phase/plan references) — per `.claude/rules/code-standards.md`.
- No AI attribution anywhere (commits, comments, docs).
- C++20 (gnu++20); no dynamic allocation in runtime paths; non-blocking patterns (existing codebase rules).
- All existing test suites must keep passing: `pio test -e native` and `pio test -e native_penta`.
- Both firmware targets must compile: `pio run -e adafruit_feather_nrf52840` and `pio run -e pentabuzzer_esp32s3`.
- v2 (nRF52840) behavior must be byte-for-byte unchanged (its ADC path is untouched; only shared guards are renamed).
- Accuracy policy: never sample VBAT while a motor is active (load sag reads falsely low); hold the last idle reading instead. Median-of-burst rejects transient sag outliers; EMA smooths across bursts.
- Charging note (deliberate parity): while USB-charging, the pack reads elevated (~4.2 V). The v2 board's ADC divider has the same artifact, so reporting it unmodified IS parity. No charging clamp.

## Design Facts (verified against the codebase — do not re-derive)

- `power_controller_esp32.cpp:25` drives EN (GPIO1, shared by all 5 DRV2605s + mux reset) HIGH in `begin()`; it only goes LOW for deep sleep.
- `configureDRV2605()` (`src/hardware.cpp`) sets `MODE = DRV2605_MODE_REALTIME`, clearing STANDBY — chips stay active from init onward.
- A VBat brownout PORs a DRV2605 back to standby; `VBAT` reads are then invalid. The FEEDBACK-register canary (`DRV_REG_FEEDBACK = 0x1A`, bit `DRV_FB_N_ERM_LRA = 0x80`, cleared by POR) already detects this (`verifyAndHeal()`).
- Boot order in `main.cpp` (`initializeHardware()`): `haptic.begin()` (line ~1130) → boot motor presence probe (~1152) → `battery.begin()` (~1208). Chips are guaranteed active and configured before the first battery read.
- Native test envs exclude `src/hardware.cpp` from the build (`platformio.ini` `build_src_filter`), so anything needing unit tests must live outside it. New `src/*.cpp` files are included automatically (`+<*>`).
- `test/test_menu_controller` defines its own mock `BatteryMonitor`; changes to the real class do not affect it.
- Mobile-app path: `BATTERY`/`INFO` commands → `MenuController::handleBattery()`/`handleInfo()` → `BATP` (local voltage, 2 dp) and `BATS` (secondary voltage via `GET_BATTERY`/`BATRESPONSE:` relay in `main.cpp:~1672`). All driven by `BatteryMonitor::getStatus()` — no protocol changes needed.
- Config values already shared: `BATTERY_LOW_VOLTAGE 3.4f`, `BATTERY_CRITICAL_VOLTAGE 3.3f`, `BATTERY_CHECK_INTERVAL_MS 60000`, `VOLTAGE_CURVE` LiPo table in `src/hardware.cpp:58`.

## File Structure

| File | Action | Responsibility |
| --- | --- | --- |
| `include/vbat_estimator.h` | Create | Pure conversion + filtering API (no Arduino deps) |
| `src/vbat_estimator.cpp` | Create | raw→volts, median-of-burst, EMA |
| `test/test_vbat_estimator/test_vbat_estimator.cpp` | Create | Unit tests for the above |
| `include/hardware.h` | Modify | `HapticController::readVBatBurst()`, `anyMotorActive()`; `BatteryMonitor::attachHaptic()` + members |
| `src/hardware.cpp` | Modify | Burst read impl; `BatteryMonitor` Penta backend in `begin()`/`readVoltage()` |
| `include/board_config.h` | Modify | Penta: `BATTERY_SENSE_ENABLED 1`, new `BATTERY_SENSE_ADC` per board |
| `include/config.h` | Modify | Guard `BATTERY_PIN` with `BATTERY_SENSE_ADC`; add `VBAT_BURST_SAMPLES` |
| `src/main.cpp` | Modify | `battery.attachHaptic(&haptic)`; unconditional USB-power warning |
| `.claude/rules/project-overview.md`, `CLAUDE.md`, `docs/ARCHITECTURE.md` | Modify | Battery-sense row/notes |

---

### Task 1: `vbat_estimator` pure module (TDD)

**Files:**
- Create: `include/vbat_estimator.h`
- Create: `src/vbat_estimator.cpp`
- Test: `test/test_vbat_estimator/test_vbat_estimator.cpp`

**Interfaces:**
- Consumes: nothing (pure, stdlib only — must compile in native env with no Arduino headers).
- Produces: `float vbatRawToVolts(uint8_t raw)`; `class VbatEstimator { float addBurst(const uint8_t* raw, size_t count); float voltage() const; bool hasReading() const; }`. Task 3 relies on these exact signatures.

- [ ] **Step 1: Write the failing tests**

Create `test/test_vbat_estimator/test_vbat_estimator.cpp` (mirror the structure of an existing suite, e.g. `test/test_types/`):

```cpp
#include <unity.h>
#include "vbat_estimator.h"

void setUp() {}
void tearDown() {}

// DRV2605 datasheet: VDD = raw * 5.6V / 255
void test_raw_to_volts_zero() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vbatRawToVolts(0));
}

void test_raw_to_volts_full_scale() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.6f, vbatRawToVolts(255));
}

void test_raw_to_volts_typical_lipo() {
    // 3.7V nominal -> raw = 3.7 * 255 / 5.6 = 168.48 -> raw 168 = 3.689V
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.689f, vbatRawToVolts(168));
}

void test_first_burst_sets_estimate_directly() {
    VbatEstimator e;
    TEST_ASSERT_FALSE(e.hasReading());
    uint8_t burst[] = {168, 168, 168, 168, 168};
    float v = e.addBurst(burst, 5);
    TEST_ASSERT_TRUE(e.hasReading());
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.689f, v);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.689f, e.voltage());
}

void test_median_rejects_sag_outlier() {
    VbatEstimator e;
    // One sample caught a motor-pulse sag (raw 120 ~= 2.6V); median ignores it
    uint8_t burst[] = {168, 167, 120, 168, 169};
    float v = e.addBurst(burst, 5);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.689f, v);  // median = 168
}

void test_empty_burst_keeps_previous_estimate() {
    VbatEstimator e;
    uint8_t burst[] = {168, 168, 168};
    e.addBurst(burst, 3);
    float before = e.voltage();
    float v = e.addBurst(nullptr, 0);  // bus busy / canary tripped
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, before, v);
    TEST_ASSERT_TRUE(e.hasReading());
}

void test_empty_burst_on_fresh_estimator_reports_no_reading() {
    VbatEstimator e;
    float v = e.addBurst(nullptr, 0);
    TEST_ASSERT_FALSE(e.hasReading());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

void test_ema_smooths_subsequent_bursts() {
    VbatEstimator e;
    uint8_t b1[] = {150, 150, 150};  // 3.294V
    uint8_t b2[] = {170, 170, 170};  // 3.733V
    e.addBurst(b1, 3);
    float v = e.addBurst(b2, 3);
    // EMA alpha 0.3: 0.7*3.294 + 0.3*3.733 = 3.426
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.426f, v);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_raw_to_volts_zero);
    RUN_TEST(test_raw_to_volts_full_scale);
    RUN_TEST(test_raw_to_volts_typical_lipo);
    RUN_TEST(test_first_burst_sets_estimate_directly);
    RUN_TEST(test_median_rejects_sag_outlier);
    RUN_TEST(test_empty_burst_keeps_previous_estimate);
    RUN_TEST(test_empty_burst_on_fresh_estimator_reports_no_reading);
    RUN_TEST(test_ema_smooths_subsequent_bursts);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the suite to verify it fails**

Run: `pio test -e native -f test_vbat_estimator`
Expected: build FAILURE — `vbat_estimator.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `include/vbat_estimator.h`:

```cpp
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
```

Create `src/vbat_estimator.cpp`:

```cpp
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
```

- [ ] **Step 4: Run tests to verify they pass (both actuator configs)**

Run: `pio test -e native -f test_vbat_estimator && pio test -e native_penta -f test_vbat_estimator`
Expected: all 8 tests PASS in both envs.

- [ ] **Step 5: Run the full native suites to confirm nothing broke**

Run: `pio test -e native && pio test -e native_penta`
Expected: all suites PASS.

- [ ] **Step 6: Commit**

```bash
git add include/vbat_estimator.h src/vbat_estimator.cpp test/test_vbat_estimator/
git commit -m "feat: add VBAT estimator (DRV2605 raw-to-volts, median burst, EMA)"
```

---

### Task 2: `HapticController` VBAT burst read + activity check

**Files:**
- Modify: `include/hardware.h` (HapticController public section)
- Modify: `src/hardware.cpp`

**Interfaces:**
- Consumes: existing private `selectChannel()`, `closeChannels()`, `_drv[]`, `_fingerEnabled[]`, `_fingerActive[]`, `_i2cMutex`, `I2CMutexLock`, and the file-scope constants `DRV_REG_FEEDBACK` / `DRV_FB_N_ERM_LRA` (defined in `src/hardware.cpp` just above `verifyAndHeal()` — the new method must be placed AFTER those definitions).
- Produces: `uint8_t readVBatBurst(uint8_t* out, uint8_t maxSamples)` and `bool anyMotorActive() const` — Task 3's `BatteryMonitor` backend calls exactly these.

No native unit test is possible (`hardware.cpp` is excluded from native builds); the check for this task is that both embedded targets compile. Hardware behavior is verified in Task 5.

- [ ] **Step 1: Declare the methods in `include/hardware.h`**

Insert into `HapticController`'s public section, directly after the `verifyAndHeal()` declaration:

```cpp
    /**
     * @brief Read a burst of DRV2605 VBAT register (0x21) samples.
     * The drivers run directly from VBat, so with the chip active (EN high,
     * out of standby - true from init until deep sleep) the register is a
     * battery voltmeter: VDD = raw * 5.6V / 255. Reads from the first
     * enabled finger. Non-blocking: returns 0 samples if the I2C bus is
     * busy (motor task mid-activation) or the chip's reset canary shows a
     * POR (brownout -> standby -> register invalid; verifyAndHeal recovers).
     * Callers should skip sampling while motors are active (load sag).
     * @param out Buffer for raw samples
     * @param maxSamples Buffer capacity
     * @return Number of samples written (0 = no valid reading this call)
     */
    uint8_t readVBatBurst(uint8_t* out, uint8_t maxSamples);

    /**
     * @brief Whether any motor is currently being driven
     */
    bool anyMotorActive() const;
```

- [ ] **Step 2: Implement in `src/hardware.cpp`**

Place immediately after the `verifyAndHeal()` implementation (so `DRV_REG_FEEDBACK` / `DRV_FB_N_ERM_LRA` are in scope):

```cpp
uint8_t HapticController::readVBatBurst(uint8_t* out, uint8_t maxSamples) {
    constexpr uint8_t DRV_REG_VBAT = 0x21;

    if (out == nullptr || maxSamples == 0) {
        return 0;
    }

    I2CMutexLock lock(_i2cMutex);
    if (!lock.acquired()) {
        return 0;  // Bus busy (motor task mid-activation) - caller keeps last estimate
    }

    // First enabled finger is the voltmeter; all chips share the VBat rail
    uint8_t finger = MAX_ACTUATORS;
    for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
        if (_fingerEnabled[f]) { finger = f; break; }
    }
    if (finger == MAX_ACTUATORS || !selectChannel(finger)) {
        return 0;
    }

    // POR canary: a brownout resets the chip into standby, where VBAT is
    // invalid. Skip this burst; verifyAndHeal() reconfigures the chip.
    if ((_drv[finger].readRegister8(DRV_REG_FEEDBACK) & DRV_FB_N_ERM_LRA) == 0) {
        closeChannels();
        return 0;
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < maxSamples; i++) {
        out[count++] = _drv[finger].readRegister8(DRV_REG_VBAT);
    }
    closeChannels();
    return count;
}

bool HapticController::anyMotorActive() const {
    for (uint8_t f = 0; f < MAX_ACTUATORS; f++) {
        if (_fingerActive[f]) {
            return true;
        }
    }
    return false;
}
```

- [ ] **Step 3: Verify both embedded targets compile**

Run: `pio run -e adafruit_feather_nrf52840 && pio run -e pentabuzzer_esp32s3`
Expected: SUCCESS for both.

- [ ] **Step 4: Verify native suites still pass**

Run: `pio test -e native && pio test -e native_penta`
Expected: all suites PASS (hardware.cpp is excluded there; this catches header breakage).

- [ ] **Step 5: Commit**

```bash
git add include/hardware.h src/hardware.cpp
git commit -m "feat: DRV2605 VBAT burst read and motor-activity check on HapticController"
```

---

### Task 3: `BatteryMonitor` Penta backend + board config + wiring

**Files:**
- Modify: `include/board_config.h`
- Modify: `include/config.h:31-34` (BATTERY_PIN guard) and battery section (~line 157)
- Modify: `include/hardware.h` (BatteryMonitor class)
- Modify: `src/hardware.cpp` (`BatteryMonitor::begin()`, `readVoltage()`)
- Modify: `src/main.cpp` (attach call ~line 1206; boot battery print ~line 651)

**Interfaces:**
- Consumes: `VbatEstimator`/`vbatRawToVolts` (Task 1), `HapticController::readVBatBurst`/`anyMotorActive` (Task 2).
- Produces: unchanged public `BatteryMonitor` API (`begin/readVoltage/getPercentage/getStatus/isLow/isCritical`) now returning real values on PentaBuzzer, plus new `void attachHaptic(HapticController* haptic)`. Everything downstream (menu `BATP`/`BATS`, `GET_BATTERY` relay, state machine, 60 s log) is already wired to this API — do not touch those call sites.

- [ ] **Step 1: Flip board macros in `include/board_config.h`**

In the nRF52 block, after `#define BATTERY_SENSE_ENABLED 1`, add:

```cpp
  #define BATTERY_SENSE_ADC        1              // VBAT divider on ADC
```

In the Penta block, replace:

```cpp
  #define BATTERY_SENSE_ENABLED    0              // no VBAT divider / fuel gauge on this board
```

with:

```cpp
  // No VBAT divider or fuel gauge; battery voltage is read from the
  // DRV2605s' VBAT monitor register (0x21) - the drivers run directly
  // from VBat (see BatteryMonitor in hardware.cpp)
  #define BATTERY_SENSE_ENABLED    1
  #define BATTERY_SENSE_ADC        0
```

- [ ] **Step 2: Update `include/config.h`**

Change the battery pin guard (lines 31-34):

```cpp
// Battery voltage monitor pin (only on boards with battery ADC hardware)
#if BATTERY_SENSE_ADC
#define BATTERY_PIN BATTERY_PIN_OVERRIDE
#endif
```

In the BATTERY CONFIGURATION section (~line 157), after `BATTERY_SAMPLE_COUNT`, add:

```cpp
#define VBAT_BURST_SAMPLES 9            // DRV2605 VBAT reads per burst (median-reduced; odd)
```

- [ ] **Step 3: Extend `BatteryMonitor` in `include/hardware.h`**

Add to the public section (after `begin()`):

```cpp
    /**
     * @brief Attach the haptic controller used as the VBat voltmeter on
     * boards without battery ADC hardware (DRV2605 VBAT register). Call
     * before begin(). No-op on boards with a battery ADC.
     */
    void attachHaptic(HapticController* haptic) { _haptic = haptic; }
```

Add to the private section:

```cpp
    HapticController* _haptic = nullptr;
    VbatEstimator _estimator;
```

And add `#include "vbat_estimator.h"` to the include block at the top of `hardware.h`.

- [ ] **Step 4: Implement the Penta backend in `src/hardware.cpp`**

Replace the `#else` branch of `BatteryMonitor::begin()`:

```cpp
#else
    // No battery ADC: read the DRV2605s' VBAT register instead. The chips
    // are active (RTP mode, standby cleared) from haptic init onward, so
    // the register is valid here; a failed read at boot means the bus is
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
```

Replace the `#else` branch of `BatteryMonitor::readVoltage()` (currently `return 4.2f; // Healthy sentinel`):

```cpp
#else
    // Refresh from the DRV2605 VBAT register, but only while no motor is
    // driven - an LRA pulse sags VBat by hundreds of mV and would read
    // falsely low. During therapy (or if the bus is busy / a chip PORed)
    // the last idle estimate is returned instead.
    if (_haptic && !_haptic->anyMotorActive()) {
        uint8_t raw[VBAT_BURST_SAMPLES];
        uint8_t n = _haptic->readVBatBurst(raw, VBAT_BURST_SAMPLES);
        _estimator.addBurst(raw, n);
    }
    return _estimator.voltage();
#endif
```

Also change both `#if BATTERY_SENSE_ENABLED` guards inside `begin()` and `readVoltage()` to `#if BATTERY_SENSE_ADC` (the ADC code is what they actually guard; `BATTERY_SENSE_ENABLED` now means "telemetry available" and is 1 on both boards).

Update the `BatteryMonitor` class doc comment in `hardware.h` ("Uses the nRF52840's ADC...") to:

```cpp
/**
 * @brief Monitors battery voltage and calculates charge percentage
 *
 * Two backends selected by BATTERY_SENSE_ADC:
 * - ADC (nRF52840): VBAT divider sampled via analogRead
 * - DRV2605 VBAT register (PentaBuzzer): the haptic drivers run directly
 *   from VBat, so their supply monitor doubles as the battery voltmeter
 *   (attachHaptic() must be called before begin())
 * Both feed the same LiPo discharge curve for percentage estimation.
 */
```

- [ ] **Step 5: Wire up `src/main.cpp`**

In `initializeHardware()` (~line 1206), before `battery.begin()`:

```cpp
    // Initialize battery monitor
    Serial.println(F("\nInitializing Battery Monitor..."));
    battery.attachHaptic(&haptic);  // VBat voltmeter on boards without battery ADC
    if (!battery.begin())
```

In the boot battery print (~line 651), the `#else` branch is now dead on both boards but stays for future boards; move the USB warning out so it always runs (it returns false on nRF):

```cpp
    // Initial battery reading
    Serial.println(F("\n--- Battery Status ---"));
#if BATTERY_SENSE_ENABLED
    BatteryStatus battStatus = battery.getStatus();
    Serial.printf("[BATTERY] %.2fV | %d%% | Status: %s\n",
                  battStatus.voltage, battStatus.percentage, battStatus.statusString());
#else
    // No VBat sense hardware on this board - never print made-up numbers
    // (a fake "4.20V | 100%" masked a miswired battery during bring-up)
    Serial.println(F("[BATTERY] No battery sense on this board"));
#endif
    if (power.usbPowerPresent())
    {
        // Motors run from VBat, not USB: without a charged battery any LRA
        // drive browns the DRV2605s out and therapy is silent
        Serial.println(F("[POWER] USB power detected - motors still require a charged battery"));
    }
```

Note: `main.cpp`'s periodic 60 s check (~line 1029) and the `#if BATTERY_SENSE_ENABLED` at line 653 now activate on Penta automatically — that is the parity goal, leave them as-is.

- [ ] **Step 6: Compile both targets and run all native suites**

Run: `pio run -e adafruit_feather_nrf52840 && pio run -e pentabuzzer_esp32s3 && pio test -e native && pio test -e native_penta`
Expected: both builds SUCCESS; all test suites PASS. The nRF build must produce identical battery behavior (ADC path untouched; only guard names changed).

- [ ] **Step 7: Commit**

```bash
git add include/board_config.h include/config.h include/hardware.h src/hardware.cpp src/main.cpp
git commit -m "feat: PentaBuzzer battery monitoring via DRV2605 VBAT register"
```

---

### Task 4: Documentation sweep

**Files:**
- Modify: `.claude/rules/project-overview.md:18`
- Modify: `docs/ARCHITECTURE.md` (battery/board comparison mentions)
- Modify: `docs/BOOT_SEQUENCE.md` (battery monitor init step, if it describes the "no sense on this board" message)

**Interfaces:** none (docs only).

- [ ] **Step 1: Update the board table row in `.claude/rules/project-overview.md`**

Replace:

```markdown
| **Battery sense**| VBAT divider on ADC               | none (`BATTERY_SENSE_ENABLED 0`, reports healthy) |
```

with:

```markdown
| **Battery sense**| VBAT divider on ADC               | DRV2605 VBAT register (0x21) over I2C |
```

- [ ] **Step 2: Update `docs/ARCHITECTURE.md` and `docs/BOOT_SEQUENCE.md`**

Search each for the PentaBuzzer battery description (`grep -n -i "battery" docs/ARCHITECTURE.md docs/BOOT_SEQUENCE.md`) and update any statement that PentaBuzzer has no battery monitoring / reports a healthy sentinel to describe the DRV2605 VBAT backend: sampled only while motors are idle, median-of-burst + EMA filtering, POR canary guard, ~22 mV resolution, elevated reading while USB-charging (same artifact as the v2 divider). Keep edits surgical — only sentences that are now wrong.

- [ ] **Step 3: Verify no stale claims remain**

Run: `grep -rn -i "reports healthy\|no battery sense\|BATTERY_SENSE_ENABLED 0" docs/ .claude/ CLAUDE.md include/ src/`
Expected: no hits describing PentaBuzzer as unsensed (the `main.cpp` `#else` branch comment for future boards is fine).

- [ ] **Step 4: Commit**

```bash
git add .claude/rules/project-overview.md docs/
git commit -m "docs: describe PentaBuzzer DRV2605 VBAT battery monitoring"
```

---

### Task 5: Hardware verification (manual, requires a PentaBuzzer + charged LiPo)

**Files:** none (checklist). Record results in the PR description.

- [ ] **Step 1: Flash and observe boot**

Run: `pio run -e pentabuzzer_esp32s3 -t upload && pio device monitor`
Expected serial output: `[INFO] Battery monitor initialized (DRV2605 VBAT)` and a `[BATTERY] X.XXV | N% | Status: ...` line with a plausible pack voltage (3.3–4.2 V), not 0.00 or 4.20-exactly.

- [ ] **Step 2: Cross-check against a multimeter**

Measure the battery terminal voltage; the `[BATTERY]` reading should agree within ~±0.1 V at idle.

- [ ] **Step 3: Mobile-app path**

From the app (or a BLE terminal sending `BATTERY` over NUS), confirm the response contains `BATP:<voltage>` matching the serial value. With a SECONDARY glove connected to the PRIMARY, confirm `BATS:` is populated via the `GET_BATTERY` relay.

- [ ] **Step 4: Therapy-session gating**

Start a therapy session; confirm the 60 s `[BATTERY]` logs keep printing sensible values (held/slowly-moving estimate, no sag dips of hundreds of mV during motor bursts).

- [ ] **Step 5: USB charging behavior**

Plug USB: reading rises toward ~4.2 V (expected parity artifact, same as v2's divider) and the `[POWER] USB power detected` warning prints at boot when on USB.

- [ ] **Step 6: Discharge sanity (long-running, optional but recommended before release)**

Let a glove idle-run until low battery; confirm `LOW_BATTERY` state entry near 3.4 V and `CRITICAL_BATTERY` near 3.3 V, matching v2 behavior. Note any systematic offset for a future `VOLTAGE_CURVE` calibration tweak.

---

## Self-Review (performed at plan time)

- **Spec coverage:** VBAT read mechanics (Task 2), accuracy policy — idle-gated, median, EMA (Tasks 1–3), brownout canary guard (Task 2), boot ordering (verified: `battery.begin()` runs after haptic init + probe), parity of logs/state machine/LED (Task 3 Step 5–6), mobile app `BATP`/`BATS` + secondary relay (no code changes needed — verified wiring; hardware-verified in Task 5 Step 3), docs (Task 4). USB charging handled as documented parity artifact — no clamp by design.
- **Placeholder scan:** none — all steps carry complete code/commands.
- **Type consistency:** `readVBatBurst(uint8_t*, uint8_t)` and `anyMotorActive()` defined in Task 2, consumed with identical signatures in Task 3; `VbatEstimator::addBurst(const uint8_t*, size_t)` defined in Task 1, consumed in Task 3 (implicit `uint8_t`→`size_t` widening is fine); `attachHaptic(HapticController*)` declared and called identically.
