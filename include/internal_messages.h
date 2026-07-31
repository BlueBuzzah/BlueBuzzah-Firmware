/**
 * @file internal_messages.h
 * @brief Single source of truth for BLE/serial message prefixes that the menu
 *        controller treats as internal (device-to-device or protocol-level)
 *        rather than user/tool commands.
 *
 * Any command NOT covered by a prefix here is treated as a user command and
 * reaches MenuController::handleCommand() from handleSerialCommand() /
 * onBLEMessage(), which is how the desktop Updater (fromPhone == false, over
 * USB serial) and the mobile app both reach the menu. Adding a command here
 * silently severs that path for it.
 *
 * This header is included by both src/menu_controller.cpp (the real
 * implementation) and the native unit tests, so the two can never diverge.
 */

#ifndef INTERNAL_MESSAGES_H
#define INTERNAL_MESSAGES_H

#include <cstddef>

// =============================================================================
// INTERNAL MESSAGE PREFIXES
// =============================================================================

inline constexpr const char* const INTERNAL_MESSAGES[] = {
    "BUZZ",
    "PING",
    "PONG",
    "PARAM_UPDATE",
    "SEED",
    "SEED_ACK",
    "GET_BATTERY",
    "BATRESPONSE",
    "ACK_PARAM_UPDATE",
    "SYNC_",           // Covers SYNC_ADJ, SYNC_PROBE, SYNC_PROBE_ACK
    "FIRST_SYNC",
    "ACK_SYNC",        // Covers ACK_SYNC_ADJ
    "START_SESSION",
    "PAUSE_SESSION",
    "RESUME_SESSION",
    "STOP_SESSION",
    "IDENTIFY:",
    "LED_OFF_SYNC",
    "DEBUG_FLASH",
    "DEBUG_SYNC",
    "MC:",             // Macrocycle batch message
    "MC_ACK:",         // Macrocycle acknowledgment
    "CALIB_BUZZ:",
    "CALIB_STOP"
};

inline constexpr uint8_t INTERNAL_MESSAGE_COUNT =
    sizeof(INTERNAL_MESSAGES) / sizeof(INTERNAL_MESSAGES[0]);

#endif // INTERNAL_MESSAGES_H
