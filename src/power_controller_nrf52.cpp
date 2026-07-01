/**
 * @file power_controller_nrf52.cpp
 * @brief Power controller no-op backend (Feather nRF52840 has no power switch)
 */

#include "power_controller.h"

void PowerController::begin() {}

bool PowerController::powerOffRequested() {
    return false;
}

void PowerController::enterDeepSleep(LEDController&) {}

bool PowerController::usbPowerPresent() {
    return false;
}
