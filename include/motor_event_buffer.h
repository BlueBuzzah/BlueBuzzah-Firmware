/**
 * @file motor_event_buffer.h
 * @brief Lock-free motor event staging buffer for BLE callback -> main loop
 * @version 1.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * TP-1: Provides ISR-safe staging of motor events from BLE callbacks.
 * Events are staged without mutex acquisition, then forwarded to
 * ActivationQueue in the main loop context.
 *
 * Design:
 * - Lock-free SPSC (single-producer, single-consumer) ring buffer
 * - BLE callbacks (producer) write via stage()
 * - Main loop (consumer) reads via unstage()
 * - Memory barriers (DMB) ensure ARM memory ordering
 */

#ifndef MOTOR_EVENT_BUFFER_H
#define MOTOR_EVENT_BUFFER_H

#include <Arduino.h>
#include <stdint.h>

// =============================================================================
// STAGED MOTOR EVENT
// =============================================================================

/**
 * @brief Motor event staged from BLE callback for later processing
 *
 * Contains all parameters needed to call activationQueue.enqueue()
 * from the main loop context.
 */
struct StagedMotorEvent {
    uint64_t activateTimeUs;   // Absolute activation time (microseconds)
    uint8_t finger;            // Finger index (0-3)
    uint8_t amplitude;         // Amplitude percentage (0-100)
    uint16_t durationMs;       // Duration in milliseconds
    uint16_t frequencyHz;      // Frequency in Hz
    bool isMacrocycleLast;     // True if this is the last event in a macrocycle batch
    volatile bool valid;       // Marks slot as ready for consumption

    StagedMotorEvent() :
        activateTimeUs(0),
        finger(0),
        amplitude(0),
        durationMs(0),
        frequencyHz(0),
        isMacrocycleLast(false),
        valid(false) {}

    void clear() {
        activateTimeUs = 0;
        finger = 0;
        amplitude = 0;
        durationMs = 0;
        frequencyHz = 0;
        isMacrocycleLast = false;
        valid = false;
    }
};

// =============================================================================
// MOTOR EVENT BUFFER (Lock-Free Ring Buffer)
// =============================================================================

/**
 * @brief Lock-free ring buffer for staging motor events from BLE callbacks
 *
 * Uses SPSC (single-producer, single-consumer) model:
 * - Producer: BLE callbacks (ISR context)
 * - Consumer: Main loop
 *
 * Thread safety:
 * - stage() is ISR-safe (no mutex, uses memory barriers)
 * - unstage() is main-loop-only (no mutex needed)
 * - hasPending() is safe from any context
 *
 * Usage:
 *   // In BLE callback:
 *   motorEventBuffer.stage(timeUs, finger, amp, dur, freq);
 *
 *   // In main loop():
 *   StagedMotorEvent event;
 *   while (motorEventBuffer.unstage(event)) {
 *       activationQueue.enqueue(event.activateTimeUs, event.finger,
 *                               event.amplitude, event.durationMs, event.frequencyHz);
 *   }
 */
class MotorEventBuffer {
public:
    static constexpr uint8_t MAX_STAGED = 16;  // Power of 2 for efficient modulo

    MotorEventBuffer();

    /**
     * @brief Stage a motor event from ISR/BLE callback context
     *
     * ISR-safe: Uses no mutex, only memory barriers for ARM ordering.
     *
     * @param activateTimeUs Absolute activation time (microseconds)
     * @param finger Finger index (0-3)
     * @param amplitude Amplitude percentage (0-100)
     * @param durationMs Duration in milliseconds
     * @param frequencyHz Frequency in Hz
     * @param isMacrocycleLast True if this is the last event in a macrocycle batch
     * @return true if staged successfully, false if buffer full
     */
    bool stage(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
               uint16_t durationMs, uint16_t frequencyHz, bool isMacrocycleLast = false);

    /**
     * @brief Begin a new macrocycle batch (ISR-safe)
     *
     * Sets flag indicating incoming events are part of a macrocycle.
     * Main loop will clear the activation queue before processing these events.
     */
    void beginMacrocycle();

    /**
     * @brief Check if a macrocycle batch is pending
     *
     * @return true if a macrocycle needs to be processed
     */
    bool isMacrocyclePending() const;

    /**
     * @brief Unstage the next pending event (main loop only)
     *
     * Must only be called from main loop context.
     *
     * @param event Output: The unstaged event data
     * @return true if an event was unstaged, false if buffer empty
     */
    bool unstage(StagedMotorEvent& event);

    /**
     * @brief Check if there are pending events
     *
     * Safe to call from any context.
     *
     * @return true if at least one event is pending
     */
    bool hasPending() const;

    /**
     * @brief Get number of pending events
     *
     * Note: This is approximate in concurrent access scenarios.
     *
     * @return Number of pending events
     */
    uint8_t getPendingCount() const;

    /**
     * @brief Clear all pending events
     *
     * Should only be called from main loop when no staging is occurring.
     */
    void clear();

private:
    StagedMotorEvent _buffer[MAX_STAGED];
    volatile uint8_t _head;  // Next write position (producer)
    volatile uint8_t _tail;  // Next read position (consumer)
    volatile bool _macrocyclePending;  // True when macrocycle batch needs processing
};

// =============================================================================
// GLOBAL INSTANCE
// =============================================================================

extern MotorEventBuffer motorEventBuffer;

#endif // MOTOR_EVENT_BUFFER_H
