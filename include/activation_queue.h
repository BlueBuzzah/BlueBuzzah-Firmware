/**
 * @file activation_queue.h
 * @brief Unified motor event queue for FreeRTOS-based motor control
 * @version 3.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * Pure FreeRTOS architecture - no hardware timer dependency.
 * Motor task handles both activations AND deactivations with unified timing.
 *
 * Architecture:
 * - enqueue() adds both activation AND deactivation events
 * - Motor task sleeps until next event time, then executes
 * - FreeRTOS timing + busy-wait for sub-ms precision
 */

#ifndef ACTIVATION_QUEUE_H
#define ACTIVATION_QUEUE_H

#include <Arduino.h>
#include "rtos.h"

// Forward declarations
class HapticController;

/**
 * @brief Type of motor event
 */
enum class MotorEventType : uint8_t {
    ACTIVATE,    // Turn motor ON
    DEACTIVATE   // Turn motor OFF
};

/**
 * @brief Single scheduled motor event (activation or deactivation)
 *
 * NOTE: This struct is accessed from multiple contexts (main loop, BLE callbacks,
 * motor task). The `active` flag is marked volatile to prevent cache issues.
 */
struct MotorEvent {
    uint64_t timeUs;        // Event time (local clock, microseconds)
    uint8_t  finger;        // Motor index (0-3)
    uint8_t  amplitude;     // Intensity (0-100), only used for ACTIVATE
    uint16_t frequencyHz;   // Motor frequency, only used for ACTIVATE
    MotorEventType type;    // ACTIVATE or DEACTIVATE
    volatile bool active;   // Slot in use (volatile: accessed from multiple contexts)

    MotorEvent()
        : timeUs(0)
        , finger(0)
        , amplitude(0)
        , frequencyHz(250)
        , type(MotorEventType::ACTIVATE)
        , active(false) {}

    void clear() {
        timeUs = 0;
        finger = 0;
        amplitude = 0;
        frequencyHz = 250;
        type = MotorEventType::ACTIVATE;
        active = false;
    }
};

/**
 * @class ActivationQueue
 * @brief Unified queue for motor events (activations AND deactivations)
 *
 * Manages a fixed-size array of motor events. The motor task processes
 * events in time order using FreeRTOS timing with busy-wait for precision.
 *
 * Usage:
 * 1. Call enqueue() to add motor activations (auto-adds deactivation event)
 * 2. Motor task calls peekNextEvent() to get next event time
 * 3. Motor task sleeps until event time
 * 4. Motor task calls dequeueNextEvent() and executes it
 */
class ActivationQueue {
public:
    static constexpr uint8_t MAX_EVENTS = 32;  // 12 activations + 12 deactivations + margin

    ActivationQueue();

    /**
     * @brief Initialize queue
     * @param haptic Pointer to HapticController for motor operations
     * @param motorTaskHandle Handle to motor task for notifications
     */
    void begin(HapticController* haptic, TaskHandle_t motorTaskHandle);

    /**
     * @brief Clear all scheduled events
     */
    void clear();

    /**
     * @brief Add a motor activation (auto-enqueues corresponding deactivation)
     * @param activateTimeUs Absolute activation time (local clock, microseconds)
     * @param finger Motor index (0-3)
     * @param amplitude Intensity (0-100)
     * @param durationMs ON duration in milliseconds
     * @param frequencyHz Motor frequency in Hz
     * @return true if added, false if queue full
     */
    bool enqueue(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
                 uint16_t durationMs, uint16_t frequencyHz);

    /**
     * @brief Peek at next event without removing it
     * @param event Output: the next event (if any)
     * @return true if an event exists, false if queue empty
     */
    bool peekNextEvent(MotorEvent& event) const;

    /**
     * @brief Get next event and remove it from queue
     * @param event Output: the dequeued event
     * @return true if an event was dequeued, false if queue empty
     */
    bool dequeueNextEvent(MotorEvent& event);

    /**
     * @brief Get time of next event
     * @return Next event time, or UINT64_MAX if queue empty
     */
    uint64_t getNextEventTime() const;

    /**
     * @brief Get count of pending events
     */
    uint8_t eventCount() const;

    /**
     * @brief Check if queue is empty
     */
    bool isEmpty() const;

    /**
     * @brief Notify motor task that new event was added
     * Call this after enqueue() to wake motor task if it's sleeping
     */
    void notifyMotorTask();

    // =========================================================================
    // DEPRECATED - Kept for backward compatibility during migration
    // =========================================================================

    /**
     * @brief Legacy: Arm hardware timer for next event (NO-OP in pure FreeRTOS)
     * @deprecated Use motor task with peekNextEvent()/dequeueNextEvent() instead
     */
    void scheduleNext() { /* NO-OP: Motor task handles timing */ }

    /**
     * @brief Legacy: Hardware timer callback (NO-OP in pure FreeRTOS)
     * @deprecated Motor task handles all event processing
     */
    void onActivationComplete() { /* NO-OP: Motor task handles all events */ }

    /**
     * @brief Legacy: Process deactivations from main loop (NO-OP in pure FreeRTOS)
     * @deprecated Motor task handles deactivations
     * @return 0 (no deactivations processed - motor task handles them)
     */
    uint8_t processDeactivations(uint64_t nowUs) { (void)nowUs; return 0; }

    /**
     * @brief Legacy: Get pending activation count
     * @deprecated Use eventCount() instead
     */
    uint8_t pendingCount() const { return eventCount(); }

    /**
     * @brief Legacy: Get active (waiting for deactivation) count
     * @deprecated Returns 0 - deactivations are now separate events
     */
    uint8_t activeCount() const { return 0; }

    /**
     * @brief Legacy: Check if all events completed
     * @deprecated Use isEmpty() instead
     */
    bool isComplete() const { return isEmpty(); }

    /**
     * @brief Legacy: Get next activation time
     * @deprecated Use getNextEventTime() instead
     */
    uint64_t getNextActivationTime() const { return getNextEventTime(); }

private:
    MotorEvent _events[MAX_EVENTS];
    HapticController* _haptic;
    TaskHandle_t _motorTaskHandle;
    SemaphoreHandle_t _queueMutex;   // Mutex for thread-safe queue access
    bool _initialized;

    /**
     * @brief Find an empty slot in the queue
     * @return Slot index, or -1 if full
     */
    int8_t findEmptySlot() const;

    /**
     * @brief Find next event by time
     * @return Slot index of earliest event, or -1 if none
     */
    int8_t findNextEvent() const;

    /**
     * @brief Add a single event to queue
     * @return Slot index if added, -1 if full
     */
    int8_t addEvent(const MotorEvent& event);
};

// Global instance
extern ActivationQueue activationQueue;

#endif // ACTIVATION_QUEUE_H
