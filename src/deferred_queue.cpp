/**
 * @file deferred_queue.cpp
 * @brief ISR-safe work queue implementation
 */

#include "deferred_queue.h"

// Global instance
DeferredQueue deferredQueue;

DeferredQueue::DeferredQueue()
    : _head(0)
    , _tail(0)
    , _executor(nullptr)
{
    for (uint8_t i = 0; i < MAX_WORK; i++) {
        _queue[i].type = DeferredWorkType::NONE;
        _queue[i].param1 = 0;
        _queue[i].param2 = 0;
        _queue[i].param3 = 0;
    }
}

bool DeferredQueue::enqueue(DeferredWorkType type, uint8_t param1, uint8_t param2, uint32_t param3) {
    // Calculate next head position
    uint8_t nextHead = static_cast<uint8_t>((_head + 1) % MAX_WORK);

    // SP-H4 fix: Memory barrier before reading _tail to ensure we see consumer's updates
    __DMB();

    // Check if queue is full
    if (nextHead == _tail) {
        return false;  // Queue full
    }

    // Store work item
    _queue[_head].type = type;
    _queue[_head].param1 = param1;
    _queue[_head].param2 = param2;
    _queue[_head].param3 = param3;

    // Memory barrier to ensure stores complete before head update
    __DMB();

    // Advance head (makes item visible to consumer)
    _head = nextHead;

    return true;
}

bool DeferredQueue::processOne() {
    // SP-H4 fix: Memory barrier before reading _head to ensure we see producer's updates
    __DMB();

    // Check if queue is empty
    if (_tail == _head) {
        return false;  // Queue empty
    }

    // SP-H4 fix: Another barrier to ensure we read data AFTER producer finished writing
    __DMB();

    // Read work item
    DeferredWorkType type = _queue[_tail].type;
    uint8_t p1 = _queue[_tail].param1;
    uint8_t p2 = _queue[_tail].param2;
    uint32_t p3 = _queue[_tail].param3;

    // Memory barrier before advancing tail
    __DMB();

    // Advance tail (frees slot)
    _tail = static_cast<uint8_t>((_tail + 1) % MAX_WORK);

    // Execute work through callback
    if (_executor && type != DeferredWorkType::NONE) {
        _executor(type, p1, p2, p3);
    }

    return true;
}

bool DeferredQueue::hasPending() const {
    return _tail != _head;
}

uint8_t DeferredQueue::getPendingCount() const {
    uint8_t head = _head;
    uint8_t tail = _tail;

    if (head >= tail) {
        return head - tail;
    } else {
        return static_cast<uint8_t>(MAX_WORK - tail + head);
    }
}

void DeferredQueue::clear() {
    _tail = _head;
}

void DeferredQueue::setExecutor(WorkExecutor executor) {
    _executor = executor;
}
