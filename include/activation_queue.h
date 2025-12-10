/**
 * @file activation_queue.h
 * @brief Queue for scheduling multiple motor activations with hardware timer
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * Provides hardware timer-based scheduling for macrocycle execution.
 * Uses chain scheduling via SyncTimer for microsecond-precision activations.
 *
 * Architecture:
 * - enqueue() adds scheduled activations with absolute timestamps
 * - scheduleNext() arms hardware timer for next pending event
 * - onActivationComplete() chains to next event after each activation
 * - processDeactivations() handles motor OFF (software polling, less critical)
 */

#ifndef ACTIVATION_QUEUE_H
#define ACTIVATION_QUEUE_H

#include <Arduino.h>

// Forward declarations
class HapticController;
class SyncTimer;

/**
 * @brief Single scheduled motor activation
 */
struct ScheduledActivation {
    uint64_t activateTimeUs;    // Local clock time to activate (microseconds)
    uint64_t deactivateTimeUs;  // Time to deactivate motor
    uint8_t  finger;            // Motor index (0-3)
    uint8_t  amplitude;         // Intensity (0-100)
    uint16_t durationMs;        // ON duration in milliseconds (uint16_t allows >255ms)
    uint16_t frequencyHz;       // Motor frequency
    bool     active;            // Slot in use
    bool     activated;         // Motor has been activated (waiting for deactivation)

    ScheduledActivation()
        : activateTimeUs(0)
        , deactivateTimeUs(0)
        , finger(0)
        , amplitude(0)
        , durationMs(0)
        , frequencyHz(250)
        , active(false)
        , activated(false) {}

    void clear() {
        activateTimeUs = 0;
        deactivateTimeUs = 0;
        finger = 0;
        amplitude = 0;
        durationMs = 0;
        frequencyHz = 250;
        active = false;
        activated = false;
    }
};

/**
 * @class ActivationQueue
 * @brief Queue of scheduled motor activations with hardware timer chain scheduling
 *
 * Manages a fixed-size array of scheduled activations. Uses hardware timer
 * (via SyncTimer) for precise activation timing via chain scheduling:
 * 1. scheduleNext() arms timer for first pending event
 * 2. When timer fires, SyncTimer callback triggers onActivationComplete()
 * 3. onActivationComplete() marks event as activated, calls scheduleNext()
 * 4. Repeat until all events complete
 */
class ActivationQueue {
public:
    static constexpr uint8_t MAX_QUEUED = 16;  // Room for 12 events + margin

    ActivationQueue();

    /**
     * @brief Initialize queue with hardware timer reference
     * @param haptic Pointer to HapticController for motor operations
     * @param syncTimer Pointer to SyncTimer for hardware timer scheduling
     */
    void begin(HapticController* haptic, SyncTimer* syncTimer);

    /**
     * @brief Clear all scheduled activations and cancel pending timer
     */
    void clear();

    /**
     * @brief Add a scheduled activation to the queue
     * @param activateTimeUs Absolute activation time (local clock, microseconds)
     * @param finger Motor index (0-3)
     * @param amplitude Intensity (0-100)
     * @param durationMs ON duration in milliseconds (uint16_t allows >255ms)
     * @param frequencyHz Motor frequency in Hz
     * @return true if added, false if queue full
     */
    bool enqueue(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
                 uint16_t durationMs, uint16_t frequencyHz);

    /**
     * @brief Arm hardware timer for next pending event (chain scheduling)
     *
     * Finds the earliest pending event and schedules it via SyncTimer.
     * Call this after enqueueing all events to start execution.
     * Also called internally by onActivationComplete() to chain events.
     */
    void scheduleNext();

    /**
     * @brief Called by SyncTimer callback when activation completes
     *
     * Marks current event as activated (motor is now ON) and chains
     * to the next event by calling scheduleNext().
     * Registered as SyncTimer's activation complete callback.
     */
    void onActivationComplete();

    /**
     * @brief Process motor deactivations (software polling)
     *
     * Deactivations are less time-critical than activations, so software
     * polling is acceptable. Call in main loop.
     *
     * @param nowUs Current time in microseconds
     * @return Number of deactivations processed
     */
    uint8_t processDeactivations(uint64_t nowUs);

    /**
     * @brief Get count of pending (not yet activated) events
     */
    uint8_t pendingCount() const;

    /**
     * @brief Get count of active (activated, not yet deactivated) events
     */
    uint8_t activeCount() const;

    /**
     * @brief Check if queue is empty (no pending or active events)
     */
    bool isEmpty() const;

    /**
     * @brief Check if all events have completed (activated and deactivated)
     */
    bool isComplete() const;

    /**
     * @brief Get the next activation time (for diagnostics)
     * @return Next activation time, or UINT64_MAX if none pending
     */
    uint64_t getNextActivationTime() const;

private:
    ScheduledActivation _queue[MAX_QUEUED];
    HapticController* _haptic;
    SyncTimer* _syncTimer;
    int8_t _currentEventIndex;  // Index of event being executed (-1 if none)
    bool _initialized;

    /**
     * @brief Find an empty slot in the queue
     * @return Slot index, or -1 if full
     */
    int8_t findEmptySlot() const;

    /**
     * @brief Find next pending event (earliest by time)
     * @return Slot index of earliest pending event, or -1 if none
     */
    int8_t findNextPendingEvent() const;
};

// Global instance
extern ActivationQueue activationQueue;

#endif // ACTIVATION_QUEUE_H
