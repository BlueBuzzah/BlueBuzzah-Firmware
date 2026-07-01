/**
 * @file power_controller.h
 * @brief Board power-path control (power switch, peripheral rail, deep sleep)
 *
 * Implementations (build_src_filter-selected):
 * - power_controller_esp32.cpp: PentaBuzzer slide switch + ESP32 deep sleep
 * - power_controller_nrf52.cpp: no-op (the Feather board has no power switch)
 */

#ifndef POWER_CONTROLLER_H
#define POWER_CONTROLLER_H

#include <Arduino.h>

class LEDController;

class PowerController {
public:
    /** Configure power pins (Penta); no-op on nRF. Call once in setup(). */
    void begin();

    /** @return true when the power switch requests shutdown (Penta); false on nRF */
    bool powerOffRequested();

    /** Shutdown animation, peripheral rail off, deep sleep until switch-on (Penta); no-op on nRF */
    void enterDeepSleep(LEDController& led);

    /** @return true when USB power is present (Penta); false on nRF */
    bool usbPowerPresent();
};

#endif // POWER_CONTROLLER_H
