/**
 * @file motor_event_buffer.cpp
 * @brief Lock-free motor event staging buffer - Implementation
 * @version 1.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "motor_event_buffer.h"
#include "platform.h"

// =============================================================================
// GLOBAL INSTANCE
// =============================================================================

MotorEventBuffer motorEventBuffer;

// =============================================================================
// CONSTRUCTOR
// =============================================================================

MotorEventBuffer::MotorEventBuffer() :
    _head(0),
    _tail(0),
    _macrocyclePending(false)
{
    for (uint8_t i = 0; i < MAX_STAGED; i++) {
        _buffer[i].clear();
    }
}

// =============================================================================
// STAGING (ISR-SAFE)
// =============================================================================

bool MotorEventBuffer::stage(uint64_t activateTimeUs, uint8_t finger, uint8_t amplitude,
                              uint16_t durationMs, uint16_t frequencyHz, bool isMacrocycleLast) {
    // Memory barrier before reading consumer index (tail)
    platformMemoryBarrier();

    // Calculate next head position
    uint8_t currentHead = _head;
    uint8_t nextHead = (currentHead + 1) % MAX_STAGED;

    // Check if buffer is full (next position would equal tail)
    if (nextHead == _tail) {
        // Buffer full - cannot stage
        return false;
    }

    // Write event data to current head position
    StagedMotorEvent& slot = _buffer[currentHead];
    slot.activateTimeUs = activateTimeUs;
    slot.finger = finger;
    slot.amplitude = amplitude;
    slot.durationMs = durationMs;
    slot.frequencyHz = frequencyHz;
    slot.isMacrocycleLast = isMacrocycleLast;

    // Memory barrier to ensure all data writes complete before marking valid
    platformMemoryBarrier();

    // Mark slot as valid (consumer can now read it)
    slot.valid = true;

    // Memory barrier before updating head
    platformMemoryBarrier();

    // Advance head
    _head = nextHead;

    return true;
}

void MotorEventBuffer::beginMacrocycle() {
    platformMemoryBarrier();
    _macrocyclePending = true;
    platformMemoryBarrier();
}

bool MotorEventBuffer::isMacrocyclePending() const {
    platformMemoryBarrier();
    return _macrocyclePending;
}

// =============================================================================
// UNSTAGING (MAIN LOOP ONLY)
// =============================================================================

bool MotorEventBuffer::unstage(StagedMotorEvent& event) {
    // Memory barrier before reading producer index (head)
    platformMemoryBarrier();

    uint8_t currentTail = _tail;

    // Check if buffer is empty
    if (currentTail == _head) {
        return false;
    }

    // Memory barrier before reading data
    platformMemoryBarrier();

    // Read event data from current tail position
    StagedMotorEvent& slot = _buffer[currentTail];

    // Verify slot is valid (should always be true if head != tail)
    if (!slot.valid) {
        // Should not happen - indicates a bug
        return false;
    }

    // Copy data to output
    event.activateTimeUs = slot.activateTimeUs;
    event.finger = slot.finger;
    event.amplitude = slot.amplitude;
    event.durationMs = slot.durationMs;
    event.frequencyHz = slot.frequencyHz;
    event.isMacrocycleLast = slot.isMacrocycleLast;
    event.valid = true;

    // If this was the last macrocycle event, clear the pending flag
    if (slot.isMacrocycleLast) {
        _macrocyclePending = false;
    }

    // Clear slot
    slot.clear();

    // Memory barrier before advancing tail
    platformMemoryBarrier();

    // Advance tail
    _tail = (currentTail + 1) % MAX_STAGED;

    return true;
}

// =============================================================================
// UTILITY
// =============================================================================

bool MotorEventBuffer::hasPending() const {
    platformMemoryBarrier();
    return _head != _tail;
}

uint8_t MotorEventBuffer::getPendingCount() const {
    platformMemoryBarrier();
    uint8_t h = _head;
    uint8_t t = _tail;
    if (h >= t) {
        return h - t;
    } else {
        return MAX_STAGED - t + h;
    }
}

void MotorEventBuffer::clear() {
    _head = 0;
    _tail = 0;
    _macrocyclePending = false;
    for (uint8_t i = 0; i < MAX_STAGED; i++) {
        _buffer[i].clear();
    }
}
