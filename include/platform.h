/**
 * @file platform.h
 * @brief Platform primitives: critical sections, memory barrier, system reset,
 *        RTOS headers, and clock capability flags
 *
 * Exactly one branch is active per build:
 * - PentaBuzzer ESP32-S3 device build (FreeRTOS SMP: spinlock critical sections)
 * - BlueBuzzah nRF52840 device build (single core: PRIMASK critical sections)
 * - Native test build (no-ops; RTOS types come from the test mock)
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#if defined(BOARD_PENTABUZZER_ESP32S3) && !defined(NATIVE_TEST_BUILD)
  #include "freertos/FreeRTOS.h"
  #include "freertos/semphr.h"
  #include "esp_system.h"
  // Single shared spinlock across all translation units (C++20 inline variable).
  inline portMUX_TYPE g_platformMux = portMUX_INITIALIZER_UNLOCKED;
  inline void platformSystemReset()   { esp_restart(); }
  inline void platformMemoryBarrier() { __sync_synchronize(); }
  #define PLATFORM_CRITICAL_ENTER()   portENTER_CRITICAL(&g_platformMux)
  #define PLATFORM_CRITICAL_EXIT()    portEXIT_CRITICAL(&g_platformMux)
  #define PLATFORM_HAS_HIRES_CLOCK    1
  // Adafruit nRF52 core task priority alias (motor task preempts loop @ prio 1)
  #ifndef TASK_PRIO_HIGHEST
  #define TASK_PRIO_HIGHEST           4
  #endif
#elif defined(BOARD_BLUEBUZZAH_NRF52) && !defined(NATIVE_TEST_BUILD)
  #include <Arduino.h>
  #include "rtos.h"  // Adafruit nRF52 core FreeRTOS wrapper (SemaphoreHandle_t etc.)
  inline void platformSystemReset()   { NVIC_SystemReset(); }
  inline void platformMemoryBarrier() { __DMB(); }
  // NOTE: PLATFORM_CRITICAL_ENTER declares a local `_pm`. Each ENTER must be in
  // its own braced block scope; two ENTERs in one block would redeclare `_pm`.
  #define PLATFORM_CRITICAL_ENTER()   uint32_t _pm = __get_PRIMASK(); __disable_irq()
  #define PLATFORM_CRITICAL_EXIT()    __set_PRIMASK(_pm)
  #define PLATFORM_HAS_HIRES_CLOCK    1
#else // NATIVE_TEST_BUILD (regardless of which board macro is set)
  // No RTOS header: the files that use FreeRTOS types (main.cpp, hardware.cpp,
  // activation_queue.cpp) are excluded from native builds.
  inline void platformSystemReset()   {}
  inline void platformMemoryBarrier() {}
  #define PLATFORM_CRITICAL_ENTER()   do {} while (0)
  #define PLATFORM_CRITICAL_EXIT()    do {} while (0)
  #define PLATFORM_HAS_HIRES_CLOCK    0
#endif

#endif // PLATFORM_H
