/**
 * @file power_controller_esp32.cpp
 * @brief v3 power path: slide switch, chip enables, deep sleep
 *
 * The power switch drives GPIO3 (strapping pin - read only after boot):
 * HIGH = on, LOW = shutdown request. Sliding the switch off pulls GPIO3 low at
 * once; the firmware stops the motors, saves the session, flashes the LED and
 * deep sleeps. About 2 s after the slide an RC delay disables the 3.3 V
 * regulator, so the board is then fully off (not sleeping). Sliding the switch
 * on powers it up through a normal boot. The ext1 wake on GPIO3 HIGH only
 * covers a quick off-on flip before the cut.
 *
 * GPIO1 is the shared EN pin of all five DRV2605s, the TCA9548A reset and the
 * LED rail switch. The DRV2605s run directly from VBat; the LED has power only
 * while GPIO1 is HIGH. Driving it LOW puts the drivers in shutdown, holds the
 * mux in reset and cuts the LED rail; driving it HIGH again RESETS EVERY
 * DRV2605 REGISTER TO POR DEFAULTS - any runtime toggle must be followed by
 * re-running the full haptic configuration for all fingers.
 */

#include "power_controller.h"
#include "board_config.h"
#include "hardware.h"

#include "esp_sleep.h"

void PowerController::cutDrivers() {
    pinMode(ENABLE_PIN_OVERRIDE, OUTPUT);
    digitalWrite(ENABLE_PIN_OVERRIDE, LOW);
}

void PowerController::begin() {
    pinMode(ENABLE_PIN_OVERRIDE, OUTPUT);
    digitalWrite(ENABLE_PIN_OVERRIDE, HIGH);  // DRV2605 EN + mux out of reset + LED rail on
    pinMode(POWER_SWITCH_PIN_OVERRIDE, INPUT);
    pinMode(USB_POW_DETECT_PIN_OVERRIDE, INPUT);
    Serial.println(F("[POWER] Power controller initialized (motor drivers enabled)"));
}

bool PowerController::powerOffRequested() {
    return digitalRead(POWER_SWITCH_PIN_OVERRIDE) == LOW;
}

void PowerController::enterDeepSleep(LEDController& led) {
    Serial.println(F("[POWER] Power switch off - entering deep sleep"));
    Serial.flush();

    // Brief shutdown indication, then everything off
    led.setColor(255, 64, 0);
    delay(150);
    led.off();

    cutDrivers();  // DRV2605s to shutdown, mux in reset, LED rail off

    // Wake when the power switch returns HIGH
    esp_sleep_enable_ext1_wakeup_io(1ULL << POWER_SWITCH_PIN_OVERRIDE,
                                    ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_deep_sleep_start();
    // Never returns; wake is a clean reboot through setup()
}

bool PowerController::usbPowerPresent() {
    return digitalRead(USB_POW_DETECT_PIN_OVERRIDE) == HIGH;
}
