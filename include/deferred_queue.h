/**
 * @file deferred_queue.h
 * @brief ISR-safe work queue for deferred execution
 * @version 1.0.0
 *
 * Provides a mechanism to defer work from ISR/callback context to main loop.
 * Operations that aren't safe in callback context (blocking I2C, delays)
 * are enqueued here and processed in the main loop.
 */

#ifndef DEFERRED_QUEUE_H
#define DEFERRED_QUEUE_H

#include <Arduino.h>

/**
 * @brief Types of work that can be deferred
 */
enum class DeferredWorkType : uint8_t {
    NONE = 0,
    HAPTIC_PULSE,        // finger, amplitude, duration_ms (single pulse)
    HAPTIC_DOUBLE_PULSE, // finger, amplitude, duration_ms (double pulse with 100ms gap)
    HAPTIC_DEACTIVATE,   // finger, 0, 0
    HAPTIC_HEAL,         // no params - round-robin DRV2605 reset check/heal
    SCANNER_RESTART,     // 0, 0, delay_ms
    LED_FLASH            // r, g, b (packed in param1/2/3)
};

/**
 * @class DeferredQueue
 * @brief ISR-safe queue for deferring work to main loop
 *
 * BLE callbacks (Bluefruit's Callback task on v2, the NimBLE host task on v3)
 * must not block (I2C, delays). This queue allows safe deferral.
 *
 * Usage:
 *   // In a BLE callback:
 *   deferredQueue.enqueue(DeferredWorkType::HAPTIC_PULSE, finger, amplitude, durationMs);
 *
 *   // In main loop:
 *   deferredQueue.processOne();  // Executes one item per loop iteration
 *
 * Multi-producer / single-consumer. enqueue() runs inside a critical section,
 * so the BLE callback and the main loop can both produce. Only the main loop
 * consumes (processOne(), clear()).
 */
class DeferredQueue {
public:
    DeferredQueue();

    /**
     * @brief Enqueue work for deferred execution
     * @param type Type of work
     * @param param1 First parameter (work-type specific)
     * @param param2 Second parameter (work-type specific)
     * @param param3 Third parameter (work-type specific)
     * @return true if enqueued successfully, false if queue full
     *
     * ISR-safe: claims the slot inside a critical section
     */
    bool enqueue(DeferredWorkType type, uint8_t param1 = 0, uint8_t param2 = 0, uint32_t param3 = 0);

    /**
     * @brief Process one queued work item
     *
     * Call from main loop. Processes at most one item per call
     * to maintain loop responsiveness.
     *
     * @return true if work was processed, false if queue empty
     */
    bool processOne();

    /**
     * @brief Check if queue has pending work
     */
    bool hasPending() const;

    /**
     * @brief Get number of pending items
     */
    uint8_t getPendingCount() const;

    /**
     * @brief Clear all pending work
     */
    void clear();

    /**
     * @brief Set callback for executing work
     *
     * The executor callback is responsible for actually performing
     * the deferred work based on type and parameters.
     */
    typedef void (*WorkExecutor)(DeferredWorkType type, uint8_t p1, uint8_t p2, uint32_t p3);
    void setExecutor(WorkExecutor executor);

    static constexpr uint8_t MAX_WORK = 8;

private:
    struct Work {
        DeferredWorkType type;
        uint8_t param1;
        uint8_t param2;
        uint32_t param3;
    };

    volatile Work _queue[MAX_WORK];
    volatile uint8_t _head;  // Write index (producer)
    volatile uint8_t _tail;  // Read index (consumer)

    WorkExecutor _executor;
};

// Global instance
extern DeferredQueue deferredQueue;

#endif // DEFERRED_QUEUE_H
