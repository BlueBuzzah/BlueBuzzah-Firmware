/**
 * @file power_controller_esp32.cpp
 * @brief PentaBuzzer power path: slide switch, chip enables, deep sleep
 *
 * The power switch drives GPIO3 (strapping pin - read only after boot):
 * HIGH = on, LOW = shutdown request. Deep sleep wakes when the switch goes
 * HIGH again.
 *
 * GPIO1 is NOT a power-rail switch (there is no switched rail on this board;
 * the DRV2605s and LED run directly from VBat). It is the shared EN pin of
 * all five DRV2605s plus the TCA9548A reset. Driving it LOW puts the drivers
 * in shutdown and holds the mux in reset; driving it HIGH again RESETS EVERY
 * DRV2605 REGISTER TO POR DEFAULTS - any runtime toggle must be followed by
 * re-running the full haptic configuration for all fingers.
 */

#include "power_controller.h"
#include "board_config.h"
#include "hardware.h"

#include "esp_sleep.h"

void PowerController::begin() {
    pinMode(ENABLE_PIN_OVERRIDE, OUTPUT);
    digitalWrite(ENABLE_PIN_OVERRIDE, HIGH);  // DRV2605 EN + mux out of reset
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

    digitalWrite(ENABLE_PIN_OVERRIDE, LOW);  // DRV2605s to shutdown, mux in reset

    // Wake when the power switch returns HIGH
    esp_sleep_enable_ext1_wakeup_io(1ULL << POWER_SWITCH_PIN_OVERRIDE,
                                    ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_deep_sleep_start();
    // Never returns; wake is a clean reboot through setup()
}

bool PowerController::usbPowerPresent() {
    return digitalRead(USB_POW_DETECT_PIN_OVERRIDE) == HIGH;
}
