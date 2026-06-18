/**
 * @file hires_clock.h
 * @brief 1MHz hardware timebase (NRF_TIMER4 + HFXO) for sync-critical timestamps
 * @version 1.0.0
 *
 * Provides a true microsecond counter to replace the FreeRTOS-tick-backed
 * micros() (~976us resolution). TIMER4 runs at 1MHz in 32-bit mode and keeps
 * counting through CPU sleep. The HF crystal (+-20ppm) is held on via the
 * SoftDevice so both gloves share crystal-grade frequency accuracy.
 *
 * On native test builds all functions are inert stubs and getMicros() keeps
 * using the mocked micros().
 */

#ifndef HIRES_CLOCK_H
#define HIRES_CLOCK_H

#include <stdint.h>

/**
 * @brief Start TIMER4 @1MHz and request the HF crystal.
 *
 * Must be called AFTER the SoftDevice is enabled (i.e. after ble.begin())
 * and BEFORE any clock-sync traffic, because getMicros() re-seeds its
 * 64-bit epoch when the clock source switches.
 *
 * @return true if the timer is running on the HF crystal
 */
[[nodiscard]] bool hiresClockBegin();

/**
 * @brief Whether the hardware timebase is active
 */
bool hiresClockIsRunning();

/**
 * @brief Re-assert the HFXO request if another module released it.
 *
 * The SoftDevice HFCLK request is a single shared flag; TinyUSB releases it
 * on USB suspend. Call periodically (~1s) from loop() as a watchdog.
 */
void hiresClockEnsureHfclk();

/**
 * @brief Raw 32-bit microsecond counter (wraps every ~71.6 minutes)
 *
 * Callers must serialize access to the CC[5] capture register — getMicros()
 * does this inside its IRQ-off section, which also makes it safe when
 * invoked from ISRs (the IRQ-off section cannot be preempted).
 */
[[nodiscard]] uint32_t hiresClockRead32();

#endif // HIRES_CLOCK_H
