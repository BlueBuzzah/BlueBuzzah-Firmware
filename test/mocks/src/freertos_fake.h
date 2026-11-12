/**
 * @file freertos_fake.h
 * @brief Minimal FreeRTOS stand-ins for host tests of ActivationQueue
 */

#ifndef FREERTOS_FAKE_H
#define FREERTOS_FAKE_H

#include <stdint.h>

typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;
typedef uint32_t TickType_t;

#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)

// Test controls
inline int g_fakeSemaphoreTakeResult = pdTRUE;
inline int g_fakeTaskNotifyCount = 0;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    static int dummy;
    return &dummy;
}

inline int xSemaphoreTake(SemaphoreHandle_t, TickType_t) {
    return g_fakeSemaphoreTakeResult;
}

inline int xSemaphoreGive(SemaphoreHandle_t) {
    return pdTRUE;
}

inline int xTaskNotifyGive(TaskHandle_t) {
    g_fakeTaskNotifyCount++;
    return pdTRUE;
}

#endif  // FREERTOS_FAKE_H
