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
 *
 * On native test builds all functions are inert stubs.
 */

#ifndef RADIO_ANCHOR_H
#define RADIO_ANCHOR_H

#include <stdint.h>

/**
 * @brief Enable radio notifications and the SWI1 IRQ. Call after the
 *        SoftDevice is enabled (after ble.begin()).
 * @return true on success
 */
[[nodiscard]] bool radioAnchorBegin();

/**
 * @brief Newest anchor at or before timeUs, no older than maxAgeUs
 * @return true if found
 */
[[nodiscard]] bool radioAnchorFindBefore(uint64_t timeUs, uint64_t maxAgeUs, uint64_t& anchorOut);

/**
 * @brief Oldest anchor strictly after timeUs, no further ahead than maxAheadUs
 * @return true if found
 */
[[nodiscard]] bool radioAnchorFindAfter(uint64_t timeUs, uint64_t maxAheadUs, uint64_t& anchorOut);

#endif // RADIO_ANCHOR_H
