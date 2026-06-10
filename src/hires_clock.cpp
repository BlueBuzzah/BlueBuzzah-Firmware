/**
 * @file hires_clock.cpp
 * @brief 1MHz hardware timebase implementation
 * @version 1.0.0
 */

#include "hires_clock.h"

#if defined(NRF52840_XXAA) && !defined(NATIVE_TEST_BUILD)

#include <Arduino.h>
#include <nrf.h>
#include <nrf_soc.h>
#include <nrf_sdm.h>

static volatile bool s_running = false;

bool hiresClockBegin() {
    if (s_running) {
        return true;
    }

    // sd_clock_hfclk_request() requires an enabled SoftDevice
    uint8_t sdEnabled = 0;
    sd_softdevice_is_enabled(&sdEnabled);
    if (!sdEnabled) {
        return false;
    }

    if (sd_clock_hfclk_request() != NRF_SUCCESS) {
        return false;
    }

    // Crystal startup is typically ~360us; allow up to 2ms
    uint32_t running = 0;
    for (uint32_t i = 0; i < 200 && !running; i++) {
        sd_clock_hfclk_is_running(&running);
        delayMicroseconds(10);
    }
    if (!running) {
        sd_clock_hfclk_release();  // balance the request - crystal never started
        return false;
    }

    __DMB();
    // Ensure timer is halted before reconfiguring
    NRF_TIMER4->TASKS_STOP = 1;
    NRF_TIMER4->MODE      = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;
    NRF_TIMER4->BITMODE   = TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos;
    NRF_TIMER4->PRESCALER = 4;  // 16MHz / 2^4 = 1MHz -> 1 tick = 1us
    NRF_TIMER4->TASKS_CLEAR = 1;
    NRF_TIMER4->TASKS_START = 1;
    __DMB();

    s_running = true;
    return true;
}

bool hiresClockIsRunning() {
    return s_running;
}

void hiresClockEnsureHfclk() {
    if (!s_running) {
        return;
    }
    uint32_t running = 0;
    sd_clock_hfclk_is_running(&running);
    if (!running) {
        // Another module (e.g. TinyUSB on USB suspend) released the shared
        // HFCLK request - re-assert it so TIMER4 stays on the crystal
        if (sd_clock_hfclk_request() != NRF_SUCCESS) {
            Serial.println(F("[CLOCK] HFCLK re-request failed"));
        }
    }
}

uint32_t hiresClockRead32() {
    NRF_TIMER4->TASKS_CAPTURE[5] = 1;
    return NRF_TIMER4->CC[5];
}

#else  // Native test build: inert stubs; getMicros() keeps using mocked micros()

bool hiresClockBegin() { return false; }
bool hiresClockIsRunning() { return false; }
void hiresClockEnsureHfclk() {}
uint32_t hiresClockRead32() { return 0; }

#endif
