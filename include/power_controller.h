/**
 * @file power_controller.h
 * @brief Board power-path control (power switch, peripheral rail, deep sleep)
 *
 * Implementations (build_src_filter-selected):
 * - power_controller_esp32.cpp: v3 slide switch + ESP32 deep sleep
 * - power_controller_nrf52.cpp: no-op (the Feather board has no power switch)
 */

#ifndef POWER_CONTROLLER_H
#define POWER_CONTROLLER_H

#include <Arduino.h>

class LEDController;

class PowerController {
public:
    /**
     * Stop every motor without I2C by taking the DRV2605s to shutdown (v3); no-op
     * on nRF. Call first in setup(): panic and watchdog restarts reset the CPU
     * only, so EN stays HIGH and RTP keeps driving until begin() re-initialises.
     */
    void cutDrivers();

    /** Configure power pins (v3); no-op on nRF. Call once in setup(). */
    void begin();

    /** @return true when the power switch requests shutdown (v3); false on nRF */
    bool powerOffRequested();

    /** Shutdown animation, motor drivers and LED rail off, deep sleep until switch-on (v3); no-op on nRF */
    void enterDeepSleep(LEDController& led);

    /** @return true when USB power is present (v3); false on nRF */
    bool usbPowerPresent();
};

#endif // POWER_CONTROLLER_H
