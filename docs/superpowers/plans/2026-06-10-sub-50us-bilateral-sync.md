# Sub-50µs Bilateral Synchronization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce bilateral motor-activation skew between PRIMARY and SECONDARY gloves from ~1ms average / ~5ms worst-case to <50µs average, without changing therapy semantics, the phone protocol, or fail-safe behavior.

**Architecture:** Three layers, each independently shippable: (A) replace the tick-quantized ~976µs `micros()` clock with a crystal-accurate 1MHz hardware timer (NRF_TIMER4 + HFXO); (B) eliminate software-path jitter from PTP timestamps by serializing PING/PONG at the SoftDevice handoff and hardening the offset filters; (C) optionally derive clock offset from hardware-timestamped BLE connection-event anchors (radio notifications), which both radios observe as the same physical instant — this is the <50µs enabler. Every layer preserves a fallback to current behavior.

**Tech Stack:** Arduino C++20 (PlatformIO, `adafruit_feather_nrf52840`), nRF52840 + S140 SoftDevice (Bluefruit), FreeRTOS (1024Hz tick), Unity native tests (`pio test -e native`).

---

## Background: Why the current floor is ~1ms

Findings from the 2026-06-10 timing analysis (verified against source and `docs/TIMING_BASELINE.md`):

1. **`micros()` is tick-quantized.** The Adafruit core's `micros()` (`cores/nRF5/delay.h:60`) uses `DWT->CYCCNT/64` only if the DWT is enabled — and nothing in the firmware or framework ever calls `dwt_enable()`. The fallback is `tick2us(xTaskGetTickCount())` at `configTICK_RATE_HZ = 1024` → **976.5µs resolution**. The baseline's +473µs/+500µs average execution drift is the mean of a uniform [0, 976µs] quantization error, not motor latency. Every PTP timestamp (T1–T4), every `baseTime`, and the motor task's busy-wait all inherit this.
   - Do **not** "fix" this by enabling DWT: `CYCCNT/64` wraps every 67.1s (not 2³²µs), which breaks `getMicros()`'s 64-bit composition, and CYCCNT halts in tickless idle (`configUSE_TICKLESS_IDLE = 1`).
2. **T1/T3 are stamped milliseconds before transmission.** `sendPing()` (`src/main.cpp:2338`) stamps T1, then the message waits in `_txQueue` until the *next* loop iteration's `ble.update()` (sendPing runs at `main.cpp:908`, after `ble.update()` at line 729). SECONDARY's T3 (`main.cpp:1650`) similarly waits for its main loop. These queue delays are asymmetric (PRIMARY runs the therapy engine + two BLE links) → offset bias plus ms-scale sample noise; SECONDARY's delay also inflates measured RTT and therefore lead time.
3. **Maintenance offset updates are unfiltered.** Once synced, `updateOffsetEMA()` is fed every PONG with **no RTT gating** (`main.cpp:1741-1746`). One retransmission-affected exchange during therapy can corrupt the offset by milliseconds for ~10s.
4. **Drift-rate estimation is noise-dominated** at current clock resolution (±~2000ppm sample noise vs ±150ppm cap) — becomes meaningful only after (1) is fixed.
5. Minor: 1Hz sync cadence (`SYNC_MAINTENANCE_INTERVAL_MS 500` is dead config), unconditional `[LEADTIME]` printf on the macrocycle critical path (`therapy_engine.cpp:636`), `Serial.readStringUntil()` can block `loop()` up to 1s, first macrocycle event always misses the I2C pre-selection fast path.

## Design invariants (must not regress)

| Invariant | Guarantee mechanism |
|---|---|
| Therapy pattern timing/semantics (macrocycle structure, lead time bounds, ACK flow) | Untouched. Only timestamp *sources* and *filters* change. |
| Fail-safe behavior (keepalive timeouts, emergency stop, MACROCYCLE staleness rejection) | Untouched. Cadence change only *increases* keepalive frequency. |
| Phone BLE protocol | Untouched. PONG payload extension (Phase F) is glove↔glove only and feature-flagged. |
| Clock fallback | If TIMER4/HFXO init fails, `getMicros()` transparently falls back to current `micros()` behavior. |
| Anchor sync fallback | `SYNC_ANCHOR_TIMESTAMPING_ENABLED` defaults to 0; when enabled, every sample without valid anchors falls back to the PTP path. |
| Native test suite | All existing tests keep passing; hardware-only code is compile-guarded with `NRF52840_XXAA`/`NATIVE_TEST_BUILD`. |
| Deployment | Both gloves are always flashed together (`deploy.py`), so protocol-affecting flags are compiled identically into both. |

Resource budget: ~+0.25mA during sessions (HFXO held on), ~+300 bytes RAM (TX-entry fields + anchor ring), ~+2KB flash.

## Phase gates

- **Phase A–E** (Tasks 1–10) are unconditional: they fix measurement and execution precision. Gate: re-baseline shows execution drift avg <100µs and bilateral skew (GPIO ground truth) <300µs avg.
- **Phase F** (Tasks 11–13) is experimental and feature-flagged. Go/no-go after Phase E re-baseline: proceed if measured skew is still >50µs (expected — PTP over a connection-interval-quantized link bottoms out around 100–300µs).
- **Phase G** (Task 14): docs + final regression.

Verified preconditions (already checked): no firmware/library user of `NRF_TIMER1-4` (the `NRF52_TimerInterrupt` lib_deps entry is unused), no `SWI1_EGU1_IRQHandler` defined anywhere in the framework, `sd_clock_hfclk_request()` available via `nrf_soc.h`.

---

# Phase A — Crystal-accurate 1MHz timebase

### Task 1: `hires_clock` module

**Files:**
- Create: `include/hires_clock.h`
- Create: `src/hires_clock.cpp`

No platformio.ini changes needed: the implementation is guarded by `NRF52840_XXAA` (defined only in the embedded env), so native test builds compile the stub branch.

- [ ] **Step 1: Write the header**

```cpp
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
bool hiresClockBegin();

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
uint32_t hiresClockRead32();

#endif // HIRES_CLOCK_H
```

- [ ] **Step 2: Write the implementation**

```cpp
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
        return false;
    }

    NRF_TIMER4->TASKS_STOP = 1;
    NRF_TIMER4->MODE      = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;
    NRF_TIMER4->BITMODE   = TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos;
    NRF_TIMER4->PRESCALER = 4;  // 16MHz / 2^4 = 1MHz -> 1 tick = 1us
    NRF_TIMER4->TASKS_CLEAR = 1;
    NRF_TIMER4->TASKS_START = 1;

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
        sd_clock_hfclk_request();
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
```

- [ ] **Step 3: Verify both builds compile**

Run: `pio run -e adafruit_feather_nrf52840`
Expected: SUCCESS

Run: `pio test -e native`
Expected: all existing tests PASS (stub branch compiles cleanly)

- [ ] **Step 4: Commit**

```bash
git add include/hires_clock.h src/hires_clock.cpp
git commit -m "feat: add 1MHz hardware timebase module (TIMER4 + HFXO)"
```

---

### Task 2: Switch `getMicros()` to the hardware clock; unify sync time domains

**Files:**
- Modify: `src/sync_protocol.cpp` (getMicros at :22, resetMicrosOverflow at :54, all `millis()` uses inside `SimpleSyncProtocol`)
- Test: `test/test_sync_protocol/test_sync_protocol.cpp`

Two changes: (1) `getMicros()` reads TIMER4 when running, with a one-time epoch re-seed at the source switch (the timer starts from 0, which would otherwise register as a false overflow → +71-minute jump); (2) `SimpleSyncProtocol` measures elapsed time with `getMicros()/1000` instead of `millis()`, so drift-rate math compares HFXO-µs against HFXO-ms rather than against the LFXO-derived tick (two different crystals = up to ~40ppm bias, the same magnitude as the real drift being measured).

- [ ] **Step 1: Write failing test for epoch re-seed protection**

Append to `test/test_sync_protocol/test_sync_protocol.cpp` (and register in the runner at the bottom of the file with the other `RUN_TEST` calls):

```cpp
void test_getMicros_no_false_overflow_after_reset(void) {
    // After resetMicrosOverflow(), a fresh low time value must not be
    // interpreted as a wrap (regression guard for clock-source switching)
    mockAdvanceMicros(4000000000UL);
    (void)getMicros();
    // Simulate a clock-source restart: tracking reset + time rebased lower
    resetMicrosOverflow();
    mockResetTime();
    mockAdvanceMicros(100);
    uint64_t t = getMicros();
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)(t >> 32));
    TEST_ASSERT_EQUAL_UINT32(100, (uint32_t)t);
}
```

(`mockAdvanceMicros()`/`mockResetTime()` are existing helpers in `test/mocks/src/Arduino.h`.)

Run: `pio test -e native -f test_sync_protocol`
Expected: new test PASSES already if reset works, FAILS if not — either way it now guards the behavior. Keep it.

- [ ] **Step 2: Rewrite `getMicros()` with source switching**

Replace the `getMicros()`/`resetMicrosOverflow()` block in `src/sync_protocol.cpp` (lines 17-62) with:

```cpp
#include "hires_clock.h"

// Overflow tracking state (file-scope, single instance)
// volatile for ISR visibility
static volatile uint32_t s_lastMicros = 0;
static volatile uint32_t s_overflowCount = 0;
static volatile bool s_usingHires = false;

static inline uint32_t readMicrosSource(bool hires) {
#if defined(NRF52840_XXAA) && !defined(NATIVE_TEST_BUILD)
    if (hires) {
        return hiresClockRead32();
    }
#else
    (void)hires;
#endif
    return micros();
}

uint64_t getMicros() {
    // Interrupt-safe: called from main loop, BLE task, motor task and (Phase F)
    // the radio-notification ISR. The IRQ-off section makes overflow tracking
    // and the CC[5] capture register exclusive.
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    bool hires = false;
#if defined(NRF52840_XXAA) && !defined(NATIVE_TEST_BUILD)
    hires = hiresClockIsRunning();
#endif

    uint32_t now = readMicrosSource(hires);

    if (hires != s_usingHires) {
        // Clock source switched (boot-time transition to TIMER4):
        // re-seed the epoch instead of counting a false overflow.
        // hiresClockBegin() is called before any sync traffic, so no
        // cross-source timestamps are ever compared.
        s_usingHires = hires;
        s_lastMicros = now;
        s_overflowCount = 0;
    } else {
        if (now < s_lastMicros) {
            s_overflowCount++;
        }
        s_lastMicros = now;
    }

    uint32_t overflows = s_overflowCount;
    __set_PRIMASK(primask);

    return ((uint64_t)overflows << 32) | now;
}

void resetMicrosOverflow() {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    s_lastMicros = 0;
    s_overflowCount = 0;
    s_usingHires = false;

    __set_PRIMASK(primask);
}
```

- [ ] **Step 3: Unify `SimpleSyncProtocol` time domain**

In `src/sync_protocol.cpp`, add this helper above the `SimpleSyncProtocol` implementation section (around line 706):

```cpp
// Milliseconds derived from the sync timebase. Using getMicros()/1000 instead
// of millis() keeps drift-rate math in a single clock domain (HFXO) - millis()
// is LFXO/tick-derived and differs by up to ~40ppm, the same magnitude as the
// crystal drift being estimated.
static inline uint32_t syncNowMs() {
    return static_cast<uint32_t>(getMicros() / 1000ULL);
}
```

Replace every `millis()` call inside `SimpleSyncProtocol` methods with `syncNowMs()`. Exact occurrences (current line numbers): `calculateOffset` (:739), `getTimeSinceSync` (:753), `calculatePTPOffset` (:781), `updateOffsetEMA` (:936), `getCorrectedOffset` (:986), `tryWarmStart` (:1042, :1059), `getProjectedOffset` (:1070).

Do NOT touch `millis()` uses outside the class (none exist in this file beyond these).

- [ ] **Step 4: Run the native suite**

Run: `pio test -e native`
Expected: ALL tests PASS (mock `millis`/`micros` are coherent, so behavior is identical natively)

- [ ] **Step 5: Commit**

```bash
git add src/sync_protocol.cpp test/test_sync_protocol/test_sync_protocol.cpp
git commit -m "feat: route getMicros() through hardware timebase, unify sync clock domain"
```

---

### Task 3: Boot integration + HFXO watchdog

**Files:**
- Modify: `src/main.cpp` (includes at top; `initializeBLE()`; `loop()`)

- [ ] **Step 1: Add include**

At the top of `src/main.cpp` with the other project includes:

```cpp
#include "hires_clock.h"
```

- [ ] **Step 2: Start the clock right after the BLE stack comes up**

In `initializeBLE()` (locate `bool initializeBLE()` in main.cpp), immediately after the successful `ble.begin(...)` call and **before** any `startAdvertising()`/`startScanning()` call, insert:

```cpp
    // Start the 1MHz hardware timebase now that the SoftDevice is enabled.
    // Must happen before any connection/sync traffic (getMicros() re-seeds
    // its epoch on the source switch).
    if (hiresClockBegin()) {
        Serial.println(F("[CLOCK] 1MHz hardware timebase active (TIMER4 + HFXO)"));
    } else {
        Serial.println(F("[CLOCK] WARNING: hardware timebase unavailable - falling back to tick clock (~1ms resolution)"));
    }
```

- [ ] **Step 3: Add the HFXO watchdog to `loop()`**

In `loop()`, next to the existing 1-second keepalive block (`main.cpp:900`), add:

```cpp
    // HFXO watchdog: the SoftDevice HFCLK request is a single shared flag and
    // TinyUSB releases it on USB suspend - re-assert so TIMER4 stays accurate
    static uint32_t lastClockCheck = 0;
    if (now - lastClockCheck >= 1000) {
        lastClockCheck = now;
        hiresClockEnsureHfclk();
    }
```

- [ ] **Step 4: Build and flash both gloves; verify on target**

Run: `pio run -e adafruit_feather_nrf52840 -t upload` then `pio device monitor`
Expected serial output at boot: `[CLOCK] 1MHz hardware timebase active (TIMER4 + HFXO)`

Then run a therapy test (`TEST` over BLE or serial) with `latencyMetrics` enabled and check the 30s report:
Expected: **EXECUTION DRIFT average drops from ~473-500µs to <100µs** (the busy-wait now exits within µs of the scheduled time + I2C latency). If it does not drop, the timer is not active — debug before proceeding (this gate validates the whole phase).

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: enable hardware timebase at boot with HFXO watchdog"
```

---

# Phase B — Late TX timestamping (T1/T3 at SoftDevice handoff)

### Task 4: Deferred-stamp entries in the BLE TX queue

**Files:**
- Modify: `include/ble_manager.h` (callback types ~:119, public API, `TxEntry` at :390, private members)
- Modify: `src/ble_manager.cpp` (constructor, `enqueueTx` at :460, `processTxQueue` at :497, new methods)

Sync messages are enqueued *unserialized*; `processTxQueue()` serializes them with a fresh `getMicros()` immediately before the SoftDevice write, and reports the stamped time via callback after the first byte is accepted. Retries before the first accepted byte re-serialize with a fresh timestamp, so a full SoftDevice buffer cannot stale-stamp.

- [ ] **Step 1: Header changes**

In `include/ble_manager.h`, after the existing callback typedefs (around line 122), add:

```cpp
// Deferred-timestamp kinds for sync messages (serialized at radio handoff)
enum class TxStampKind : uint8_t {
    NONE = 0,
    PING_T1,   // PRIMARY -> SECONDARY: stamp T1 at write time
    PONG_T3    // SECONDARY -> PRIMARY: stamp T3 at write time
};

typedef void (*BLETxStampCallback)(TxStampKind kind, uint32_t seqId, uint64_t txTimeUs);
```

In the `public:` section of `BLEManager` (near `sendToSecondary`/`sendToPrimary`), add:

```cpp
    /**
     * @brief Enqueue a PING whose T1 is stamped at SoftDevice handoff (PRIMARY)
     * @return true if enqueued
     */
    bool sendPingStamped(uint32_t seqId);

    /**
     * @brief Enqueue a PONG whose T3 is stamped at SoftDevice handoff (SECONDARY)
     * @param connHandle PRIMARY connection handle (from the message callback)
     * @param t2 Receive timestamp of the corresponding PING
     * @param anchorUs Optional rx connection-event anchor (0 = none; Phase F)
     */
    bool sendPongStamped(uint16_t connHandle, uint32_t seqId, uint64_t t2, uint64_t anchorUs = 0);

    /**
     * @brief Register callback fired when a stamped message is handed to the SoftDevice
     */
    void setTxStampCallback(BLETxStampCallback callback);
```

Extend `TxEntry` (line 390) to:

```cpp
    struct TxEntry {
        char data[MESSAGE_BUFFER_SIZE];
        uint16_t length;
        uint16_t bytesSent;
        uint16_t connHandle;
        bool pending;
        TxStampKind stampKind;   // NONE for normal messages
        uint32_t stampSeqId;     // sequence id for deferred serialization
        uint64_t stampT2;        // PONG only: T2 echoed back
        uint64_t stampAnchor;    // PONG only: rx anchor timestamp (0 = absent)
    };
```

In the `private:` section, add:

```cpp
    BLETxStampCallback _txStampCallback;

    /**
     * @brief Enqueue an unserialized sync message for stamping at write time
     */
    bool enqueueStamped(uint16_t connHandle, TxStampKind kind, uint32_t seqId,
                        uint64_t t2, uint64_t anchorUs);
```

- [ ] **Step 2: Implementation changes in `src/ble_manager.cpp`**

Add `#include "sync_protocol.h"` to the includes if not already present (needed for `SyncCommand`/`getMicros`).

In the `BLEManager` constructor, initialize `_txStampCallback(nullptr)` (match the existing initializer style).

In `enqueueTx()` (line 460), after `entry->pending = true;` add:

```cpp
    entry->stampKind = TxStampKind::NONE;
```

Add the new methods (near `sendToSecondary` at :548):

```cpp
void BLEManager::setTxStampCallback(BLETxStampCallback callback) {
    _txStampCallback = callback;
}

bool BLEManager::sendPingStamped(uint32_t seqId) {
    uint16_t handle = getSecondaryHandle();
    if (handle == CONN_HANDLE_INVALID) {
        return false;
    }
    return enqueueStamped(handle, TxStampKind::PING_T1, seqId, 0, 0);
}

bool BLEManager::sendPongStamped(uint16_t connHandle, uint32_t seqId, uint64_t t2, uint64_t anchorUs) {
    if (connHandle == CONN_HANDLE_INVALID) {
        return false;
    }
    return enqueueStamped(connHandle, TxStampKind::PONG_T3, seqId, t2, anchorUs);
}

bool BLEManager::enqueueStamped(uint16_t connHandle, TxStampKind kind, uint32_t seqId,
                                uint64_t t2, uint64_t anchorUs) {
    if (_txCount >= TX_QUEUE_SIZE) {
        Serial.println(F("[BLE] TX queue full, dropping sync message"));
        return false;
    }

    TxEntry* entry = &_txQueue[_txTail];
    if (entry->pending) {
        Serial.println(F("[BLE] TX queue corruption detected"));
        return false;
    }

    entry->stampKind = kind;
    entry->stampSeqId = seqId;
    entry->stampT2 = t2;
    entry->stampAnchor = anchorUs;
    entry->length = 0;      // serialized at write time
    entry->bytesSent = 0;
    entry->connHandle = connHandle;
    entry->pending = true;

    _txTail = static_cast<uint8_t>((_txTail + 1) % TX_QUEUE_SIZE);
    _txCount++;
    return true;
}
```

Replace `processTxQueue()` (line 497) in full with:

```cpp
void BLEManager::processTxQueue() {
    // Process up to 4 queue entries per update for responsiveness
    for (uint8_t i = 0; i < 4 && _txCount > 0; i++) {
        TxEntry* entry = &_txQueue[_txHead];
        if (!entry->pending) {
            // Advance head if slot is empty (shouldn't happen)
            _txHead = static_cast<uint8_t>((_txHead + 1) % TX_QUEUE_SIZE);
            continue;
        }

        // Late timestamping: serialize sync messages at radio handoff so the
        // embedded T1/T3 reflects when bytes actually reach the SoftDevice.
        // Re-serialized with a fresh timestamp on every retry until the first
        // byte is accepted.
        uint64_t stampTime = 0;
        if (entry->stampKind != TxStampKind::NONE && entry->bytesSent == 0) {
            stampTime = getMicros();
            SyncCommand cmd = (entry->stampKind == TxStampKind::PING_T1)
                ? SyncCommand::createPingWithT1(entry->stampSeqId, stampTime)
                : SyncCommand::createPongWithTimestamps(entry->stampSeqId, entry->stampT2, stampTime);

            char msg[128];
            if (!cmd.serialize(msg, sizeof(msg))) {
                Serial.println(F("[BLE] ERROR: stamped sync serialize failed"));
                entry->pending = false;
                _txHead = static_cast<uint8_t>((_txHead + 1) % TX_QUEUE_SIZE);
                _txCount--;
                continue;
            }
            size_t msgLen = strlen(msg);
            memcpy(entry->data, msg, msgLen);
            entry->data[msgLen] = EOT_CHAR;
            entry->length = static_cast<uint16_t>(msgLen + 1);
        }

        // Try to write remaining bytes (non-blocking)
        size_t remaining = entry->length - entry->bytesSent;
        size_t written = tryWriteImmediate(entry->connHandle,
                                           (const uint8_t*)(entry->data + entry->bytesSent),
                                           remaining);

        if (written > 0) {
            // First bytes accepted: the deferred stamp is now final
            if (entry->stampKind != TxStampKind::NONE && entry->bytesSent == 0) {
                if (_txStampCallback) {
                    _txStampCallback(entry->stampKind, entry->stampSeqId, stampTime);
                }
                entry->stampKind = TxStampKind::NONE;
            }

            entry->bytesSent = static_cast<uint16_t>(entry->bytesSent + written);

            // Check if complete
            if (entry->bytesSent >= entry->length) {
                // Note: flush() clears the RX FIFO (not TX). With _tx_buffered=false,
                // write() already sends via _txd.notify() immediately — no TX flush needed.
                if (_role == DeviceRole::PRIMARY) {
                    _uartService.flush();
                } else {
                    _clientUart.flush();
                }

                // Mark slot free and advance head
                entry->pending = false;
                _txHead = static_cast<uint8_t>((_txHead + 1) % TX_QUEUE_SIZE);
                _txCount--;
            }
        } else {
            // Buffer full - stop processing this iteration, will retry next update()
            break;
        }
    }
}
```

Note: `entry->stampAnchor` is intentionally unused here; Task 12 extends this block for anchor-carrying PONGs.

- [ ] **Step 3: Build**

Run: `pio run -e adafruit_feather_nrf52840`
Expected: SUCCESS (native suite unaffected — `ble_manager.cpp` is excluded from native builds)

- [ ] **Step 4: Commit**

```bash
git add include/ble_manager.h src/ble_manager.cpp
git commit -m "feat: defer PING/PONG serialization to SoftDevice handoff for accurate T1/T3"
```

---

### Task 5: Rewire `sendPing()` and the PING handler

**Files:**
- Modify: `src/main.cpp` (`sendPing()` at :2329, PING case in `onBLEMessage` at :1631, callback registration in `initializeBLE()`)

- [ ] **Step 1: Replace `sendPing()`**

```cpp
void sendPing()
{
    if (deviceRole != DeviceRole::PRIMARY || !ble.isSecondaryConnected())
    {
        return;
    }
    // T1 is stamped by the BLE manager at SoftDevice handoff and reported via
    // onTxStamped() - not here. Stamping at creation time added one main-loop
    // iteration of asymmetric delay to every PTP sample.
    ble.sendPingStamped(g_sequenceGenerator.next());
}
```

- [ ] **Step 2: Add the stamp callback and register it**

Near the other BLE callbacks in main.cpp, add:

```cpp
void onTxStamped(TxStampKind kind, uint32_t seqId, uint64_t txTimeUs)
{
    (void)seqId;
    // Runs in main-loop context (processTxQueue via ble.update())
    if (kind == TxStampKind::PING_T1)
    {
        atomicWrite64(&pingT1, txTimeUs);
        atomicWrite64(&pingStartTime, txTimeUs);
    }
}
```

Add its declaration to the forward declarations block (around `main.cpp:360`):

```cpp
void onTxStamped(TxStampKind kind, uint32_t seqId, uint64_t txTimeUs);
```

In `initializeBLE()`, next to `ble.setMessageCallback(...)`, add:

```cpp
    ble.setTxStampCallback(onTxStamped);
```

- [ ] **Step 3: Replace the SECONDARY PING case body**

In `onBLEMessage`, replace the `case SyncCommandType::PING:` block (lines 1631-1667) with:

```cpp
        case SyncCommandType::PING:
            // SECONDARY: Reply with PONG. T2 = rxTimestamp (earliest BLE-stack
            // capture); T3 is stamped by the BLE manager at SoftDevice handoff
            // so SECONDARY main-loop latency no longer inflates measured RTT.
            if (deviceRole == DeviceRole::SECONDARY)
            {
                // Track connectivity - PING proves PRIMARY is alive
                lastKeepaliveReceived = millis();

                ble.sendPongStamped(connHandle, cmd.getSequenceId(), rxTimestamp);
            }
            break;
```

Note: `connHandle` is the function parameter currently marked `[[maybe_unused]]` (`main.cpp:1389`) — remove the `[[maybe_unused]]` attribute since it is now used.

- [ ] **Step 4: Build, flash both gloves, verify**

Run: `pio run -e adafruit_feather_nrf52840 -t upload` (both gloves), then enable debug mode and watch `[SYNC] RTT=... offset=...` lines on PRIMARY.
Expected: RTT drops (typically from ~16-40ms to ~8-20ms) and offset sample scatter visibly tightens; sync still reaches "valid" within ~5s of connection; therapy session runs normally end-to-end.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: stamp T1/T3 at radio handoff in PING/PONG flow"
```

---

# Phase C — Filter hardening + sync cadence

### Task 6: Quality-gated maintenance updates (`updateOffsetEMAWithQuality`)

**Files:**
- Modify: `include/config.h` (sync section, around line 90)
- Modify: `include/sync_protocol.h` (`SimpleSyncProtocol` public API + private members)
- Modify: `src/sync_protocol.cpp`
- Test: `test/test_sync_protocol/test_sync_protocol.cpp`

Three gates for post-convergence samples: hard RTT ceiling (existing 60ms), "lucky packet" gate (only RTTs near the observed minimum have minimal queuing in both directions — the PTP symmetry assumption holds best there), and an innovation gate (reject offset jumps >5ms unless persistent, so one bad sample can't corrupt the EMA but a genuine step change still re-converges).

- [ ] **Step 1: Add config constants**

In `include/config.h`, after `SYNC_OUTLIER_THRESHOLD_US` (line 94), add:

```cpp
// Maintenance-mode sample gating (post-convergence quality filters)
#define SYNC_LUCKY_RTT_MARGIN_US 10000      // Accept only RTT <= minRTT + 10ms ("lucky packets")
#define SYNC_MIN_RTT_DECAY_US 200           // Per-sample creep of tracked min RTT (adapts to degradation)
#define SYNC_INNOVATION_GATE_US 5000        // Reject offset jumps > 5ms...
#define SYNC_INNOVATION_REJECT_LIMIT 5      // ...unless persistent across this many samples
```

- [ ] **Step 2: Write failing tests**

Append to `test/test_sync_protocol/test_sync_protocol.cpp` (register all in the runner):

```cpp
void test_maintenance_rejects_high_rtt(void) {
    SimpleSyncProtocol sync;
    for (int i = 0; i < 5; i++) sync.addOffsetSample(1000);
    TEST_ASSERT_TRUE(sync.isClockSyncValid());

    // Seed min-RTT with a clean sample
    TEST_ASSERT_TRUE(sync.updateOffsetEMAWithQuality(1000, 10000));
    // Sample far above min+margin is rejected even though below the 60ms ceiling
    TEST_ASSERT_FALSE(sync.updateOffsetEMAWithQuality(4000, 35000));
    // Sample above the hard ceiling is always rejected
    TEST_ASSERT_FALSE(sync.updateOffsetEMAWithQuality(1000, 70000));
}

void test_maintenance_innovation_gate(void) {
    SimpleSyncProtocol sync;
    for (int i = 0; i < 5; i++) sync.addOffsetSample(0);
    TEST_ASSERT_TRUE(sync.isClockSyncValid());
    TEST_ASSERT_TRUE(sync.updateOffsetEMAWithQuality(0, 10000));

    // A 20ms jump is rejected SYNC_INNOVATION_REJECT_LIMIT times...
    for (int i = 0; i < SYNC_INNOVATION_REJECT_LIMIT; i++) {
        TEST_ASSERT_FALSE(sync.updateOffsetEMAWithQuality(20000, 10000));
    }
    // ...then accepted as a genuine step change
    TEST_ASSERT_TRUE(sync.updateOffsetEMAWithQuality(20000, 10000));
}

void test_maintenance_min_rtt_decay(void) {
    SimpleSyncProtocol sync;
    for (int i = 0; i < 5; i++) sync.addOffsetSample(0);
    TEST_ASSERT_TRUE(sync.updateOffsetEMAWithQuality(0, 10000));  // min = 10ms

    // Link degraded to 25ms RTT: initially rejected (25 > 10+10),
    // but min creeps up by SYNC_MIN_RTT_DECAY_US per sample until accepted
    bool accepted = false;
    for (int i = 0; i < 40 && !accepted; i++) {
        accepted = sync.updateOffsetEMAWithQuality(0, 25000);
    }
    TEST_ASSERT_TRUE(accepted);
}

void test_maintenance_routes_to_sample_collection_before_valid(void) {
    SimpleSyncProtocol sync;
    TEST_ASSERT_FALSE(sync.isClockSyncValid());
    // Before convergence the method must behave like addOffsetSampleWithQuality
    TEST_ASSERT_TRUE(sync.updateOffsetEMAWithQuality(500, 10000));
    TEST_ASSERT_EQUAL_UINT8(1, sync.getOffsetSampleCount());
}
```

Run: `pio test -e native -f test_sync_protocol`
Expected: FAIL — `updateOffsetEMAWithQuality` not declared.

- [ ] **Step 3: Declare in `include/sync_protocol.h`**

In the PTP section of `SimpleSyncProtocol` (after `updateOffsetEMA` at :607), add:

```cpp
    /**
     * @brief Quality-gated offset update for continuous maintenance
     *
     * Routes to addOffsetSampleWithQuality() until initial sync is valid.
     * After convergence applies three gates before the EMA update:
     *   1. Hard RTT ceiling (SYNC_RTT_QUALITY_THRESHOLD_US)
     *   2. Lucky-packet gate: RTT must be near the tracked minimum
     *      (minimal queuing both ways = PTP symmetry assumption holds)
     *   3. Innovation gate: offset jumps > SYNC_INNOVATION_GATE_US rejected
     *      unless persistent for SYNC_INNOVATION_REJECT_LIMIT samples
     *
     * @return true if the sample was accepted
     */
    bool updateOffsetEMAWithQuality(int64_t offset, uint32_t rttUs);

    /**
     * @brief Tracked minimum RTT used by the lucky-packet gate (µs)
     */
    uint32_t getMinRtt() const { return _minRttUs; }
```

In the private section (after `_driftRateUsPerMs`), add:

```cpp
    // Maintenance-mode gating state
    uint32_t _minRttUs;           // Decaying minimum RTT (lucky-packet gate)
    uint8_t _innovationRejects;   // Consecutive innovation-gate rejections
```

- [ ] **Step 4: Implement in `src/sync_protocol.cpp`**

Initialize in the constructor list: `_minRttUs(UINT32_MAX), _innovationRejects(0),` (insert after `_driftRateUsPerMs(0.0f),`).

Reset in `resetLatency()` (header, :544): add `_minRttUs = UINT32_MAX;` — note this method is defined inline in `sync_protocol.h`; add the line there. Reset `_innovationRejects = 0;` in `resetClockSync()` (`sync_protocol.cpp:1015`).

Add after `addOffsetSampleWithQuality` (:1106):

```cpp
bool SimpleSyncProtocol::updateOffsetEMAWithQuality(int64_t offset, uint32_t rttUs) {
    if (!_clockSyncValid) {
        // Still converging - use the initial sample-collection path
        return addOffsetSampleWithQuality(offset, rttUs);
    }

    // Gate 1: hard RTT ceiling (retransmission-affected exchanges)
    if (rttUs > SYNC_RTT_QUALITY_THRESHOLD_US) {
        return false;
    }

    // Gate 2: lucky-packet selection. Track a slowly-decaying minimum RTT;
    // only exchanges near it have minimal queuing in both directions, which
    // is when the PTP symmetric-delay assumption actually holds.
    if (rttUs < _minRttUs) {
        _minRttUs = rttUs;
    } else if (_minRttUs != UINT32_MAX) {
        _minRttUs += SYNC_MIN_RTT_DECAY_US;  // creep up so the gate adapts if the link degrades
    }
    if (rttUs > _minRttUs + SYNC_LUCKY_RTT_MARGIN_US) {
        return false;
    }

    // Gate 3: innovation gate. One implausible jump is noise; a persistent
    // one means reality changed and we must re-converge.
    int64_t innovation = offset - _medianOffset;
    if (innovation < 0) innovation = -innovation;
    if (innovation > static_cast<int64_t>(SYNC_INNOVATION_GATE_US)) {
        if (_innovationRejects < SYNC_INNOVATION_REJECT_LIMIT) {
            _innovationRejects++;
            return false;
        }
        // Persistent: fall through and accept
    }
    _innovationRejects = 0;

    updateOffsetEMA(offset);
    return true;
}
```

- [ ] **Step 5: Run tests**

Run: `pio test -e native -f test_sync_protocol`
Expected: ALL PASS (including the four new tests)

- [ ] **Step 6: Commit**

```bash
git add include/config.h include/sync_protocol.h src/sync_protocol.cpp test/test_sync_protocol/test_sync_protocol.cpp
git commit -m "feat: add lucky-packet and innovation gating to maintenance clock sync"
```

---

### Task 7: Wire gating into the PONG handler; 4Hz sync during therapy

**Files:**
- Modify: `src/main.cpp` (PONG case ~:1740-1751; keepalive block ~:900-909)
- Modify: `include/config.h` (remove dead constant, add cadence constant)

- [ ] **Step 1: Replace the sample-routing block in the PONG case**

Replace (`main.cpp:1738-1751`):

```cpp
                // Add sample with RTT-based quality filtering
                // High-RTT samples are rejected as they likely have asymmetric delays
                bool sampleAccepted = false;
                if (syncProtocol.isClockSyncValid())
                {
                    // Already synced - use EMA update (no RTT filtering for maintenance)
                    syncProtocol.updateOffsetEMA(offset);
                    sampleAccepted = true;
                }
                else
                {
                    // Building initial sync - use quality filtering
                    sampleAccepted = syncProtocol.addOffsetSampleWithQuality(offset, rtt);
                }
```

with:

```cpp
                // Quality-gated update: routes to initial sample collection
                // until valid, then applies RTT + lucky-packet + innovation
                // gates before the maintenance EMA (a single retransmission-
                // affected exchange must not corrupt the offset mid-therapy)
                bool sampleAccepted = syncProtocol.updateOffsetEMAWithQuality(offset, rtt);
```

- [ ] **Step 2: Cadence constants**

In `include/config.h`: delete the dead line `#define SYNC_MAINTENANCE_INTERVAL_MS 500` (line 91) and add in its place:

```cpp
#define SYNC_ACTIVE_INTERVAL_MS 250       // PING cadence while therapy is running (4Hz)
                                           // Idle cadence stays KEEPALIVE_INTERVAL_MS (1Hz)
```

- [ ] **Step 3: Adaptive cadence in `loop()`**

Replace the keepalive block (`main.cpp:900-909`):

```cpp
    // Unified keepalive + clock sync (PRIMARY only). 4Hz during therapy for
    // tighter drift tracking and more samples for the quality gates; 1Hz idle.
    // More PINGs only strengthens keepalive semantics (timeouts unchanged).
    uint32_t pingIntervalMs = therapy.isRunning() ? SYNC_ACTIVE_INTERVAL_MS : KEEPALIVE_INTERVAL_MS;
    if (deviceRole == DeviceRole::PRIMARY &&
        isConnected &&
        (now - lastKeepalive >= pingIntervalMs))
    {
        lastKeepalive = now;
        sendPing();
    }
```

- [ ] **Step 4: Build + native suite**

Run: `pio run -e adafruit_feather_nrf52840 && pio test -e native`
Expected: both SUCCEED

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp include/config.h
git commit -m "feat: gate maintenance sync samples and raise sync cadence to 4Hz during therapy"
```

---

# Phase D — Motor execution path

### Task 8: I2C pre-selection for the first event of each macrocycle

**Files:**
- Modify: `src/main.cpp` (`motorTask`, the `delayUs > 2000` branch at :309-319)

Currently `preSelectNextActivation()` only runs after a DEACTIVATE, so event 0 of every macrocycle takes the ~500µs slow path while events 1-11 take the ~100µs fast path — a systematic ~400µs first-event asymmetry whenever the two gloves' queues load at slightly different times. Pre-select during the sleep window, keeping all I2C in motor-task context.

- [ ] **Step 1: Modify the sleep branch**

Replace (`main.cpp:309-319`):

```cpp
        if (delayUs > 2000) {
            // Event is far away (>2ms) - use FreeRTOS sleep
            // Sleep until 1ms before event, then busy-wait
            TickType_t ticks = pdMS_TO_TICKS((delayUs - 1000) / 1000);
            if (ticks > 0) {
                // Wake early if new event is enqueued (may be earlier than current)
                ulTaskNotifyTake(pdTRUE, ticks);
                // H1 fix: Re-capture time after sleep - original `now` is stale
                continue;  // Re-check queue in case new event is earlier
            }
        }
```

with:

```cpp
        if (delayUs > 2000) {
            // Use the idle window to pre-select the upcoming activation's I2C
            // channel + frequency. Covers the FIRST event of a macrocycle,
            // which otherwise always takes the ~500us slow path (pre-selection
            // normally only happens after a DEACTIVATE). All I2C stays in
            // motor-task context.
            if (event.type == MotorEventType::ACTIVATE &&
                haptic.isEnabled(event.finger) &&
                haptic.getPreSelectedFinger() != static_cast<int8_t>(event.finger)) {
                if (haptic.selectChannelPersistent(event.finger)) {
                    haptic.setFrequencyDirect(event.finger, event.frequencyHz);
                }
                continue;  // Re-evaluate timing - pre-selection took ~400us
            }

            // Event is far away (>2ms) - use FreeRTOS sleep
            // Sleep until 1ms before event, then busy-wait
            TickType_t ticks = pdMS_TO_TICKS((delayUs - 1000) / 1000);
            if (ticks > 0) {
                // Wake early if new event is enqueued (may be earlier than current)
                ulTaskNotifyTake(pdTRUE, ticks);
                // H1 fix: Re-capture time after sleep - original `now` is stale
                continue;  // Re-check queue in case new event is earlier
            }
        }
```

Design note: the busy-wait margin (sleep until 1ms before, spin <2ms) is deliberately **not** tightened — FreeRTOS sleep granularity is 1 tick (~976µs) regardless of the new clock, so a smaller margin risks late events. With the Task 1 clock, the spin now exits within ~1-2µs of target; the bounded ~1-2ms BLE-task starvation per event remains, and its effect on sync samples is filtered by the Task 6 gates.

- [ ] **Step 2: Build, flash, verify**

Run: `pio run -e adafruit_feather_nrf52840 -t upload`, run a debug-mode session.
Expected: `[MOTOR_TASK] ACTIVATE F.. [FAST]` now appears for the first event of each macrocycle too (look for the FAST suffix on the activation that follows `[MACROCYCLE] Forwarded ...`).

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: pre-select I2C channel for first macrocycle event in motor task idle window"
```

---

### Task 9: Critical-path hygiene

**Files:**
- Modify: `src/therapy_engine.cpp` (remove `[LEADTIME]` printf at :635-639)
- Modify: `src/main.cpp` (non-blocking serial input at :756-766)

- [ ] **Step 1: Remove the unconditional printf**

Delete from `src/therapy_engine.cpp` (lines 635-639):

```cpp
            // DEBUG: Log lead time calculation
            Serial.printf("[LEADTIME] leadTime=%lu nowUs=%lu baseTime=%lu\n",
                          (unsigned long)leadTimeUs,
                          (unsigned long)(nowUs / 1000),
                          (unsigned long)(_macrocycleBaseTime / 1000));
```

(It sits between baseTime capture and the macrocycle send — serial I/O consuming lead-time margin on every macrocycle. The equivalent data is already available via debug-mode `[MACROCYCLE] Sent ...` logging in main.cpp.)

- [ ] **Step 2: Non-blocking serial command input**

Replace in `src/main.cpp` (lines 756-766):

```cpp
    // Process Serial commands (uses serial-only handler for SET_ROLE, GET_ROLE)
    if (Serial.available())
    {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0)
        {
            Serial.printf("[SERIAL] Command: %s\n", input.c_str());
            handleSerialCommand(input.c_str());
        }
    }
```

with:

```cpp
    // Process Serial commands (non-blocking accumulation - readStringUntil()
    // blocks up to 1s on partial input, which would blow the 70-150ms
    // macrocycle lead window mid-therapy)
    {
        static char serialBuf[96];
        static uint8_t serialLen = 0;
        while (Serial.available())
        {
            char c = static_cast<char>(Serial.read());
            if (c == '\n' || c == '\r')
            {
                if (serialLen > 0)
                {
                    serialBuf[serialLen] = '\0';
                    serialLen = 0;
                    Serial.printf("[SERIAL] Command: %s\n", serialBuf);
                    handleSerialCommand(serialBuf);
                }
            }
            else if (serialLen < sizeof(serialBuf) - 1)
            {
                serialBuf[serialLen++] = c;
            }
            else
            {
                serialLen = 0;  // Overflow - discard the line
            }
        }
    }
```

- [ ] **Step 3: Build + native suite + serial smoke test**

Run: `pio run -e adafruit_feather_nrf52840 && pio test -e native`
Expected: both SUCCEED. On target: `GET_ROLE` over serial still answers (commands are newline-terminated by `pio device monitor` input).

- [ ] **Step 4: Commit**

```bash
git add src/therapy_engine.cpp src/main.cpp
git commit -m "fix: remove blocking serial read and critical-path logging from therapy loop"
```

---

# Phase E — Ground truth & re-baseline (validation gate)

### Task 10: GPIO ground-truth instrumentation + measurement protocol

**Files:**
- Modify: `include/config.h`
- Modify: `src/main.cpp` (`executeMotorEvent` at :186, hardware init)
- Create: `docs/SYNC_VALIDATION.md`

`latencyMetrics` measures each device against its *own* clock; bilateral skew needs an external observer. A debug-gated GPIO toggle on every ACTIVATE lets a logic analyzer (or 2-channel scope) on both gloves' A0 pins measure true inter-glove skew per event.

- [ ] **Step 1: Config flag**

Add to `include/config.h` in the debug section (after `SKIP_BOOT_SEQUENCE`, line 223):

```cpp
// Bilateral sync ground-truth instrumentation: toggles a GPIO on every motor
// ACTIVATE so a logic analyzer across both gloves measures true skew.
// Compile-time only - keep 0 for release builds.
#ifndef SYNC_DEBUG_GPIO_ENABLED
#define SYNC_DEBUG_GPIO_ENABLED 0
#endif
#define SYNC_DEBUG_GPIO_PIN PIN_A0
```

- [ ] **Step 2: Toggle in `executeMotorEvent`**

In `src/main.cpp`, at the very top of `executeMotorEvent()` (line 187, before `uint64_t beforeOp = getMicros();`), add:

```cpp
#if SYNC_DEBUG_GPIO_ENABLED
    if (event.type == MotorEventType::ACTIVATE) {
        digitalToggle(SYNC_DEBUG_GPIO_PIN);
    }
#endif
```

In `initializeHardware()` (locate it in main.cpp), add near the top:

```cpp
#if SYNC_DEBUG_GPIO_ENABLED
    pinMode(SYNC_DEBUG_GPIO_PIN, OUTPUT);
    digitalWrite(SYNC_DEBUG_GPIO_PIN, LOW);
#endif
```

- [ ] **Step 3: Write `docs/SYNC_VALIDATION.md`**

```markdown
# Bilateral Sync Validation Protocol

## Setup
1. Build both gloves with `-DSYNC_DEBUG_GPIO_ENABLED=1` (add to build_flags or
   `pio run -e adafruit_feather_nrf52840` after setting the define).
2. Connect logic analyzer: CH0 -> PRIMARY A0, CH1 -> SECONDARY A0, shared GND.
   Sample rate >= 1MHz.
3. Start a therapy session (TEST command or auto-start).

## Measurement
- Every motor ACTIVATE toggles the pin. With mirrored patterns, edges should
  appear pairwise. Skew = |t(CH0 edge) - t(CH1 edge)| per pair.
- Capture >= 5 minutes (~1000 events). Report avg / P95 / max skew.
- Also capture the soft metrics: enable latencyMetrics and record the 30s
  reports (execution drift per device) plus `[SYNC] RTT=...` lines.

## Acceptance gates
| Milestone | Avg skew | P95 skew |
|---|---|---|
| After Phase A-E (Tasks 1-10) | < 300 µs | < 1 ms |
| After Phase F (Tasks 11-13)  | < 50 µs  | < 200 µs |

## Soak test
8-hour session at ~2m separation; verify no sync resets
(`[SYNC] Clock sync reset` absent), no keepalive timeouts, no late-event
spikes in latencyMetrics, skew stable start-to-end (drift compensation
working).

## Record results
Append dated results tables to docs/TIMING_BASELINE.md.
```

- [ ] **Step 4: Build both variants**

Run: `pio run -e adafruit_feather_nrf52840` (flag off — default), then once with the flag on to confirm it compiles.
Expected: SUCCESS both ways.

- [ ] **Step 5: Execute the measurement protocol and record the Phase A-E re-baseline**

Expected outcome (gate for Phase F): avg skew <300µs. Append results to `docs/TIMING_BASELINE.md`.

- [ ] **Step 6: Commit**

```bash
git add include/config.h src/main.cpp docs/SYNC_VALIDATION.md docs/TIMING_BASELINE.md
git commit -m "feat: add GPIO ground-truth sync instrumentation and validation protocol"
```

---

# Phase F — Connection-anchor timestamping (EXPERIMENTAL, feature-flagged)

**Concept:** every BLE connection event is a single physical radio instant observed by both devices. The S140 SoftDevice's radio-notification IRQ (SWI1) fires a fixed distance before each radio event; timestamping it on both sides and differencing the two timestamps for the *same* connection event yields the clock offset with only IRQ-latency jitter (~5-20µs) — no TX queues, no main-loop phase, no RTT symmetry assumption.

**Pairing strategy:** SECONDARY (central, single link, scanner stopped while connected) has unambiguous anchors. It reports the anchor of the connection event that delivered each PING inside its PONG. PRIMARY pairs it with its own first anchor after the (late-stamped, Task 4) PING handoff time. PRIMARY-side anchors can be polluted by phone-link/advertising events; mispaired anchors produce offsets shifted by ~one connection interval (≥7.5ms), which the Task 6 innovation gate (5ms) rejects. Every sample without a valid anchor pair falls back to the existing PTP path.

**Known limitations (documented, acceptable for an experimental flag):** with the phone connected, up to ~half of anchor samples are rejected by the gates (still ≥1-2 good samples/s at 4Hz cadence); the SoftDevice suppresses notifications when radio events are closer together than the configured distance (dual-link PRIMARY) — suppressed anchors simply mean PTP fallback for that sample.

### Task 11: `radio_anchor` module

**Files:**
- Create: `include/radio_anchor.h`
- Create: `src/radio_anchor.cpp`
- Modify: `include/config.h`

- [ ] **Step 1: Config flags**

Add to `include/config.h` after the warm-start block (line 103):

```cpp
// Connection-anchor timestamping (EXPERIMENTAL - Phase F)
// Hardware-timestamps BLE radio events via SoftDevice radio notifications and
// derives clock offset from paired anchors. Falls back to PTP per-sample.
// Both gloves MUST run the same setting (PONG payload format changes).
#ifndef SYNC_ANCHOR_TIMESTAMPING_ENABLED
#define SYNC_ANCHOR_TIMESTAMPING_ENABLED 0
#endif
#define SYNC_ANCHOR_RING_SIZE 16              // Recent radio-event timestamps kept
#define SYNC_ANCHOR_RX_WINDOW_US 15000        // Max age of rx anchor vs rx callback (event len 12.5ms + margin)
#define SYNC_ANCHOR_TX_WINDOW_US 25000        // Max lookahead from PING handoff to its tx anchor (2x max CI + margin)
#define SYNC_ANCHOR_BIAS_US 0                 // Calibrated central-vs-peripheral constant (Task 13)
```

- [ ] **Step 2: Write the header**

```cpp
/**
 * @file radio_anchor.h
 * @brief Hardware timestamps of BLE radio events via SoftDevice radio notifications
 * @version 1.0.0
 *
 * Maintains a ring of getMicros() timestamps taken in the SWI1 radio
 * notification IRQ, which the S140 SoftDevice raises a fixed distance before
 * every radio event. Both devices configure the same distance, so paired
 * anchors of the same connection event differ only by clock offset plus a
 * small constant role bias (SYNC_ANCHOR_BIAS_US).
 */

#ifndef RADIO_ANCHOR_H
#define RADIO_ANCHOR_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Enable radio notifications and the SWI1 IRQ. Call after the
 *        SoftDevice is enabled (after ble.begin()).
 * @return true on success
 */
bool radioAnchorBegin();

/**
 * @brief Newest anchor at or before timeUs, no older than maxAgeUs
 * @return true if found
 */
bool radioAnchorFindBefore(uint64_t timeUs, uint64_t maxAgeUs, uint64_t& anchorOut);

/**
 * @brief Oldest anchor strictly after timeUs, no further ahead than maxAheadUs
 * @return true if found
 */
bool radioAnchorFindAfter(uint64_t timeUs, uint64_t maxAheadUs, uint64_t& anchorOut);

#endif // RADIO_ANCHOR_H
```

- [ ] **Step 3: Write the implementation**

```cpp
/**
 * @file radio_anchor.cpp
 * @brief Radio-event anchor timestamping implementation
 * @version 1.0.0
 */

#include "radio_anchor.h"
#include "config.h"

#if defined(NRF52840_XXAA) && !defined(NATIVE_TEST_BUILD)

#include <Arduino.h>
#include <nrf.h>
#include <nrf_soc.h>
#include <nrf_sdm.h>
#include <nrf_nvic.h>
#include "sync_protocol.h"  // getMicros()

static volatile uint64_t s_anchors[SYNC_ANCHOR_RING_SIZE];
static volatile uint8_t s_anchorHead = 0;
static volatile bool s_active = false;

extern "C" void SWI1_EGU1_IRQHandler(void) {
    // Fires SYNC_ANCHOR notification-distance before every radio event.
    // getMicros() is interrupt-safe (IRQ-off section protects overflow state
    // and the TIMER4 capture register).
    uint64_t now = getMicros();
    uint8_t idx = s_anchorHead;
    s_anchors[idx] = now;
    s_anchorHead = static_cast<uint8_t>((idx + 1) % SYNC_ANCHOR_RING_SIZE);
}

bool radioAnchorBegin() {
    if (s_active) {
        return true;
    }

    uint8_t sdEnabled = 0;
    sd_softdevice_is_enabled(&sdEnabled);
    if (!sdEnabled) {
        return false;
    }

    for (uint8_t i = 0; i < SYNC_ANCHOR_RING_SIZE; i++) {
        s_anchors[i] = 0;
    }
    s_anchorHead = 0;

    // Priority 2 = highest application level under the SoftDevice: minimal
    // IRQ-entry jitter for the timestamp
    if (sd_nvic_SetPriority(SWI1_EGU1_IRQn, 2) != NRF_SUCCESS) {
        return false;
    }
    if (sd_nvic_EnableIRQ(SWI1_EGU1_IRQn) != NRF_SUCCESS) {
        return false;
    }

    // ACTIVE-only notifications, 800us ahead of each radio event. Note: the
    // SoftDevice suppresses notifications when consecutive radio events are
    // closer than the distance (dual-link PRIMARY) - missing anchors are
    // handled as PTP fallback by the consumer.
    if (sd_radio_notification_cfg_set(NRF_RADIO_NOTIFICATION_TYPE_INT_ON_ACTIVE,
                                      NRF_RADIO_NOTIFICATION_DISTANCE_800US) != NRF_SUCCESS) {
        return false;
    }

    s_active = true;
    return true;
}

static bool findAnchor(bool before, uint64_t timeUs, uint64_t rangeUs, uint64_t& anchorOut) {
    if (!s_active) {
        return false;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint64_t best = 0;
    bool found = false;
    for (uint8_t i = 0; i < SYNC_ANCHOR_RING_SIZE; i++) {
        uint64_t a = s_anchors[i];
        if (a == 0) {
            continue;
        }
        if (before) {
            // newest anchor <= timeUs within rangeUs
            if (a <= timeUs && (timeUs - a) <= rangeUs && a > best) {
                best = a;
                found = true;
            }
        } else {
            // oldest anchor > timeUs within rangeUs
            if (a > timeUs && (a - timeUs) <= rangeUs && (!found || a < best)) {
                best = a;
                found = true;
            }
        }
    }

    __set_PRIMASK(primask);
    anchorOut = best;
    return found;
}

bool radioAnchorFindBefore(uint64_t timeUs, uint64_t maxAgeUs, uint64_t& anchorOut) {
    return findAnchor(true, timeUs, maxAgeUs, anchorOut);
}

bool radioAnchorFindAfter(uint64_t timeUs, uint64_t maxAheadUs, uint64_t& anchorOut) {
    return findAnchor(false, timeUs, maxAheadUs, anchorOut);
}

#else  // Native stubs

bool radioAnchorBegin() { return false; }
bool radioAnchorFindBefore(uint64_t, uint64_t, uint64_t& anchorOut) { anchorOut = 0; return false; }
bool radioAnchorFindAfter(uint64_t, uint64_t, uint64_t& anchorOut) { anchorOut = 0; return false; }

#endif
```

- [ ] **Step 4: Build both envs**

Run: `pio run -e adafruit_feather_nrf52840 && pio test -e native`
Expected: both SUCCEED

- [ ] **Step 5: Commit**

```bash
git add include/radio_anchor.h src/radio_anchor.cpp include/config.h
git commit -m "feat: add radio-notification anchor timestamping module (experimental)"
```

---

### Task 12: Anchor exchange in PONG + anchor-based offset samples

**Files:**
- Modify: `include/sync_protocol.h` (new factory declaration)
- Modify: `src/sync_protocol.cpp` (new factory)
- Modify: `src/ble_manager.cpp` (anchor-aware deferred serialization in `processTxQueue`)
- Modify: `src/main.cpp` (PING handler, PONG handler, `radioAnchorBegin()` at boot)
- Test: `test/test_sync_protocol/test_sync_protocol.cpp`

- [ ] **Step 1: Failing test for the new PONG format**

```cpp
void test_createPongWithAnchor_roundtrip(void) {
    // 6-field format: T2High|T2Low|T3High|T3Low|AnchorHigh|AnchorLow
    // (always full-width so the anchor fields are unambiguous)
    SyncCommand pong = SyncCommand::createPongWithAnchor(
        7, 0x100000001ULL, 0x100000002ULL, 0x100000003ULL);
    char buf[160];
    TEST_ASSERT_TRUE(pong.serialize(buf, sizeof(buf)));

    SyncCommand parsed;
    TEST_ASSERT_TRUE(parsed.deserialize(buf));
    TEST_ASSERT_TRUE(parsed.hasData("4"));
    uint64_t t2 = ((uint64_t)parsed.getDataUnsigned("0", 0) << 32) | parsed.getDataUnsigned("1", 0);
    uint64_t t3 = ((uint64_t)parsed.getDataUnsigned("2", 0) << 32) | parsed.getDataUnsigned("3", 0);
    uint64_t anchor = ((uint64_t)parsed.getDataUnsigned("4", 0) << 32) | parsed.getDataUnsigned("5", 0);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)t2);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)t3);
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)anchor);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)(t2 >> 32));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)(anchor >> 32));
}
```

Run: `pio test -e native -f test_sync_protocol` — Expected: FAIL (factory missing).

- [ ] **Step 2: Add the factory**

Declaration in `include/sync_protocol.h` (after `createPongWithTimestamps`, :264):

```cpp
    /**
     * @brief Create PONG carrying T2, T3 and the rx connection-event anchor
     *
     * Always uses full 64-bit field encoding (6 data fields) so the anchor
     * fields are positionally unambiguous:
     *   0=T2High 1=T2Low 2=T3High 3=T3Low 4=AnchorHigh 5=AnchorLow
     */
    static SyncCommand createPongWithAnchor(uint32_t sequenceId, uint64_t t2,
                                            uint64_t t3, uint64_t anchorUs);
```

Implementation in `src/sync_protocol.cpp` (after `createPongWithTimestamps`, :505):

```cpp
SyncCommand SyncCommand::createPongWithAnchor(uint32_t sequenceId, uint64_t t2,
                                              uint64_t t3, uint64_t anchorUs) {
    SyncCommand cmd(SyncCommandType::PONG, sequenceId);
    // Full-width encoding always (unlike createPongWithTimestamps) so the
    // receiver can detect the anchor format via hasData("4")
    cmd.setDataUnsigned("0", (uint32_t)(t2 >> 32));
    cmd.setDataUnsigned("1", (uint32_t)(t2 & 0xFFFFFFFF));
    cmd.setDataUnsigned("2", (uint32_t)(t3 >> 32));
    cmd.setDataUnsigned("3", (uint32_t)(t3 & 0xFFFFFFFF));
    cmd.setDataUnsigned("4", (uint32_t)(anchorUs >> 32));
    cmd.setDataUnsigned("5", (uint32_t)(anchorUs & 0xFFFFFFFF));
    return cmd;
}
```

Run: `pio test -e native -f test_sync_protocol` — Expected: PASS.

- [ ] **Step 3: Anchor-aware deferred serialization**

In `src/ble_manager.cpp` `processTxQueue()`, replace the `SyncCommand cmd = ...` ternary (from Task 4) with:

```cpp
            SyncCommand cmd;
            if (entry->stampKind == TxStampKind::PING_T1) {
                cmd = SyncCommand::createPingWithT1(entry->stampSeqId, stampTime);
            } else if (entry->stampAnchor != 0) {
                cmd = SyncCommand::createPongWithAnchor(entry->stampSeqId, entry->stampT2,
                                                        stampTime, entry->stampAnchor);
            } else {
                cmd = SyncCommand::createPongWithTimestamps(entry->stampSeqId, entry->stampT2, stampTime);
            }
```

- [ ] **Step 4: SECONDARY PING handler — attach the rx anchor**

In `src/main.cpp`, replace the PING case body (from Task 5) with:

```cpp
        case SyncCommandType::PING:
            if (deviceRole == DeviceRole::SECONDARY)
            {
                lastKeepaliveReceived = millis();

                uint64_t rxAnchor = 0;
#if SYNC_ANCHOR_TIMESTAMPING_ENABLED
                // Anchor of the connection event that delivered this PING.
                // SECONDARY is a single-link central: anchors are unambiguous.
                if (!radioAnchorFindBefore(rxTimestamp, SYNC_ANCHOR_RX_WINDOW_US, rxAnchor))
                {
                    rxAnchor = 0;  // No anchor - PRIMARY falls back to PTP
                }
#endif
                ble.sendPongStamped(connHandle, cmd.getSequenceId(), rxTimestamp, rxAnchor);
            }
            break;
```

Add `#include "radio_anchor.h"` to main.cpp includes.

- [ ] **Step 5: PRIMARY PONG handler — anchor-paired offset**

In the PONG case of `onBLEMessage`, the T2/T3 parsing block (`main.cpp:1693-1709`) gains anchor extraction. Replace it with:

```cpp
                uint64_t t2, t3;
                uint64_t secondaryAnchor = 0;
                if (cmd.hasData("2"))
                {
                    // Full 64-bit: T2High|T2Low|T3High|T3Low[|AnchHigh|AnchLow]
                    uint32_t t2High = cmd.getDataUnsigned("0", 0);
                    uint32_t t2Low = cmd.getDataUnsigned("1", 0);
                    uint32_t t3High = cmd.getDataUnsigned("2", 0);
                    uint32_t t3Low = cmd.getDataUnsigned("3", 0);
                    t2 = ((uint64_t)t2High << 32) | t2Low;
                    t3 = ((uint64_t)t3High << 32) | t3Low;
                    if (cmd.hasData("4"))
                    {
                        secondaryAnchor = ((uint64_t)cmd.getDataUnsigned("4", 0) << 32) |
                                          cmd.getDataUnsigned("5", 0);
                    }
                }
                else
                {
                    // Simple 32-bit: T2|T3
                    t2 = static_cast<uint64_t>(cmd.getDataUnsigned("0", 0));
                    t3 = static_cast<uint64_t>(cmd.getDataUnsigned("1", 0));
                }
```

Then, immediately after the existing `int64_t offset = syncProtocol.calculatePTPOffset(t1, t2, t3, t4);` line, add:

```cpp
#if SYNC_ANCHOR_TIMESTAMPING_ENABLED
                // Anchor-paired offset: PRIMARY's anchor of the connection
                // event that carried the PING (first anchor after the
                // late-stamped T1 handoff) vs SECONDARY's anchor of the same
                // event. Mispairs from phone-link/advertising anchors land
                // ~one connection interval (>=7.5ms) off and are rejected by
                // the innovation gate. Falls back to the PTP offset otherwise.
                if (secondaryAnchor != 0)
                {
                    uint64_t primaryAnchor = 0;
                    if (radioAnchorFindAfter(t1, SYNC_ANCHOR_TX_WINDOW_US, primaryAnchor))
                    {
                        int64_t anchorOffset = (int64_t)secondaryAnchor - (int64_t)primaryAnchor
                                               - (int64_t)SYNC_ANCHOR_BIAS_US;
                        if (profiles.getDebugMode())
                        {
                            Serial.printf("[SYNC] anchorOffset=%ld ptpOffset=%ld delta=%ld\n",
                                          (long)anchorOffset, (long)offset,
                                          (long)(anchorOffset - offset));
                        }
                        offset = anchorOffset;
                    }
                }
#endif
```

- [ ] **Step 6: Enable anchors at boot**

In `initializeBLE()` (next to the Task 3 `hiresClockBegin()` block), add:

```cpp
#if SYNC_ANCHOR_TIMESTAMPING_ENABLED
    if (radioAnchorBegin()) {
        Serial.println(F("[SYNC] Radio-anchor timestamping active"));
    } else {
        Serial.println(F("[SYNC] WARNING: radio-anchor init failed - PTP-only sync"));
    }
#endif
```

- [ ] **Step 7: Build with the flag both off and on; run native suite**

Run: `pio test -e native && pio run -e adafruit_feather_nrf52840`
Then once with `-DSYNC_ANCHOR_TIMESTAMPING_ENABLED=1` added to build_flags.
Expected: all SUCCEED.

- [ ] **Step 8: Commit**

```bash
git add include/sync_protocol.h src/sync_protocol.cpp src/ble_manager.cpp src/main.cpp test/test_sync_protocol/test_sync_protocol.cpp
git commit -m "feat: anchor-paired clock offset over PONG exchange (flag-gated)"
```

---

### Task 13: Anchor calibration + validation + default-on decision

**Files:**
- Modify: `include/config.h` (`SYNC_ANCHOR_BIAS_US`, possibly flag default)
- Modify: `docs/TIMING_BASELINE.md` (results)

- [ ] **Step 1: Calibrate the role bias**

Flash both gloves with `SYNC_ANCHOR_TIMESTAMPING_ENABLED=1` and `SYNC_DEBUG_GPIO_ENABLED=1`, debug mode on. Run a session and record:
1. The `[SYNC] anchorOffset=... ptpOffset=... delta=...` stream — `delta` should be stable (σ < 100µs). Its mean is the candidate bias if ground truth confirms a constant shift.
2. The logic-analyzer skew distribution (per `docs/SYNC_VALIDATION.md`).

Set `SYNC_ANCHOR_BIAS_US` in `config.h` to the value that centers the measured GPIO skew at 0 (expected range: 0-300µs, from central-TX vs peripheral-RX-window radio timing; it is constant for a given PHY).

- [ ] **Step 2: Validate against the acceptance gate**

Re-run the full `docs/SYNC_VALIDATION.md` protocol including the 8-hour soak.
Expected: avg skew < 50µs, P95 < 200µs; anchor-sample acceptance rate ≥ 30% with phone connected and ≥ 90% without; zero sync resets; PTP fallback engages cleanly when the phone link saturates the radio.

- [ ] **Step 3: Flip the default**

If Step 2 passes, change `config.h`:

```cpp
#define SYNC_ANCHOR_TIMESTAMPING_ENABLED 1
```

If it fails, leave the default at 0 and record findings — Phases A-E improvements stand on their own.

- [ ] **Step 4: Commit**

```bash
git add include/config.h docs/TIMING_BASELINE.md
git commit -m "feat: calibrate and enable anchor-based sync after validation"
```

---

# Phase G — Documentation & final regression

### Task 14: Documentation + full regression pass

**Files:**
- Modify: `docs/SYNCHRONIZATION_PROTOCOL.md`
- Modify: `docs/TIMING_BASELINE.md`
- Modify: `docs/LATENCY_METRICS.md` (if metric semantics changed — execution drift is now real µs)

- [ ] **Step 1: Update SYNCHRONIZATION_PROTOCOL.md**

- Replace the "Timestamp Precision" limitations section: document the TIMER4/HFXO timebase, the tick-clock fallback, and that `micros()`-claims of µs precision now hold.
- Document late TX timestamping in the PING/PONG section (T1/T3 = SoftDevice handoff time).
- Document the maintenance gates (RTT ceiling, lucky-packet, innovation) and new constants in the Protocol Parameters table: `SYNC_ACTIVE_INTERVAL_MS 250`, `SYNC_LUCKY_RTT_MARGIN_US`, `SYNC_INNOVATION_GATE_US/LIMIT`; remove `SYNC_MAINTENANCE_INTERVAL_MS`.
- Add an "Anchor Timestamping" section describing the PONG 6-field format, pairing rules, bias constant, fallback behavior, and the dual-link limitation.
- Update the achieved-metrics table from the Task 10/13 measurements (replace the aspirational "<1ms achieved" with measured values).

- [ ] **Step 2: Final regression checklist (execute all)**

```
[ ] pio test -e native                          -> all pass
[ ] pio run -e adafruit_feather_nrf52840        -> clean build, flash usage delta < 4KB
[ ] Cold boot both gloves -> connect -> sync valid in <= 5s (cold) / <= 3s (warm reconnect)
[ ] Full therapy session via phone app: start, pause, resume, stop - all states + LEDs correct
[ ] Pull SECONDARY battery mid-session -> PRIMARY emergency-stops within 6s (PRIMARY_KEEPALIVE_TIMEOUT_MS)
[ ] Walk SECONDARY out of range -> reconnect -> warm-start sync recovery (~3s)
[ ] USB plug/unplug during session -> [CLOCK] watchdog keeps timebase (no sync reset)
[ ] Battery check: idle current increase <= ~0.3mA vs previous firmware
[ ] latencyMetrics 30s report during session: late events < 1%, execution drift avg < 100us
```

- [ ] **Step 3: Commit**

```bash
git add docs/SYNCHRONIZATION_PROTOCOL.md docs/TIMING_BASELINE.md docs/LATENCY_METRICS.md
git commit -m "docs: update sync protocol and baselines for hardware timebase and anchor sync"
```

---

## Rejected alternatives (for the record)

| Alternative | Why rejected |
|---|---|
| Enable DWT `CYCCNT` for `micros()` | Wraps every 67.1s breaking `getMicros()` composition; halts in tickless idle; debugger-state-dependent. |
| Raise `configTICK_RATE_HZ` to 2000+ | Core-level change affecting all timing/power; only halves quantization instead of eliminating it. |
| Tighten motor busy-wait below 1 tick | FreeRTOS sleep granularity is 1 tick; smaller margins risk late events for ~400µs of spin saved. |
| Path-asymmetry *correction* (`SYNC_ASYMMETRY_CORRECTION_ENABLED`) | Late TX stamping (Task 4) removes the dominant asymmetry source; anchors (Phase F) sidestep RTT symmetry entirely. Re-evaluate only if Phase F is abandoned. |
| Timeslot-API raw radio sync packets | Far more invasive (SoftDevice scheduling, certification implications) than radio notifications for similar accuracy. |

## Expected outcome by phase

| Milestone | Bilateral skew (avg) | Dominant residual |
|---|---|---|
| Today | ~1ms (5ms P95) | Tick-quantized clock |
| After Phase A | ~300-500µs | TX-queue jitter in PTP samples |
| After Phases B-E | ~100-300µs | Connection-event alignment of PTP exchanges |
| After Phase F | **<50µs** | Radio-notification IRQ jitter + residual drift between 250ms updates |
