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
    // SWI1_EGU1_IRQHandler is the canonical MDK handler name for nRF52840.
    // nrf_soc.h defines RADIO_NOTIFICATION_IRQHandler as (SWI1_IRQHandler) —
    // parenthesised, so unusable as a function definition name. Using the MDK
    // symbol directly is both correct and the most portable spelling available.

    // Fires a fixed notification distance before every radio event.
    // getMicros() is interrupt-safe (IRQ-off section protects overflow state
    // and the TIMER4 capture register) and keeps anchors in the same 64-bit
    // epoch as every other sync timestamp.
    if (!s_active) {
        return;  // May drop one anchor in the begin() window between cfg_set and s_active=true - harmless.
    }
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
        sd_nvic_DisableIRQ(SWI1_EGU1_IRQn);  // roll back the NVIC enable
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
