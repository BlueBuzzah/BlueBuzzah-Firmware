/**
 * @file motor_check.h
 * @brief Per-finger result of the boot motor check, and its wire format
 *
 * One token per finger, in finger order (index, middle, ring, pinky, thumb),
 * joined by commas: "OK,OK,OPEN,OPEN,OPEN". The SECONDARY sends its list to
 * the PRIMARY as MOTOR_STATUS:<tokens>; INFO reports both as MOTP / MOTS.
 *
 * The check is a warning only, never a fault: it relies on DRV2605 auto-
 * calibration, which can misreport a working motor.
 *
 * Header-only with no Arduino dependencies, so native tests can include it.
 */

#ifndef MOTOR_CHECK_H
#define MOTOR_CHECK_H

#include <stdint.h>
#include <string.h>
#include "board_config.h"

enum class MotorCheck : uint8_t {
    OK,        // motor present
    OPEN,      // auto-calibration failed: no motor seen on the port
    NODRIVER,  // the port's DRV2605 never initialised
    UNKNOWN    // not checked, probe timed out, mux select failed, or supply dipped
};

// Fingers every session plays, on both boards (index to pinky)
constexpr uint8_t MOTOR_CHECK_BASE_FINGERS = 4;

// Longest list: MAX_ACTUATORS x "NODRIVER" + commas + NUL
constexpr size_t MOTOR_CHECKS_BUFFER_SIZE = 48;
static_assert(MOTOR_CHECKS_BUFFER_SIZE >= MAX_ACTUATORS * 9,
              "MOTOR_CHECKS_BUFFER_SIZE too small for MAX_ACTUATORS tokens");

inline const char* motorCheckToken(MotorCheck check) {
    switch (check) {
        case MotorCheck::OK:       return "OK";
        case MotorCheck::OPEN:     return "OPEN";
        case MotorCheck::NODRIVER: return "NODRIVER";
        default:                   return "UNKNOWN";
    }
}

/**
 * @brief Write the tokens as a comma-separated list
 * @return false (with a truncated, NUL-terminated out) if out is too short
 */
inline bool formatMotorChecks(const MotorCheck* checks, uint8_t count, char* out, size_t len) {
    if (out == nullptr || len == 0) {
        return false;
    }
    out[0] = '\0';
    if (checks == nullptr) {
        return false;
    }
    size_t used = 0;
    for (uint8_t i = 0; i < count; i++) {
        const char* token = motorCheckToken(checks[i]);
        const size_t tokenLen = strlen(token);
        const size_t need = (i > 0 ? 1 : 0) + tokenLen;
        if (used + need >= len) {
            return false;
        }
        if (i > 0) {
            out[used++] = ',';
        }
        memcpy(out + used, token, tokenLen);
        used += tokenLen;
        out[used] = '\0';
    }
    return true;
}

/**
 * @brief Parse a comma-separated list of exactly count tokens
 * @return false on a wrong count, an unknown or empty token, or null input
 */
inline bool parseMotorChecks(const char* text, MotorCheck* out, uint8_t count) {
    if (text == nullptr || out == nullptr || count == 0) {
        return false;
    }
    const char* p = text;
    for (uint8_t i = 0; i < count; i++) {
        const char* comma = strchr(p, ',');
        const size_t tokenLen = comma ? static_cast<size_t>(comma - p) : strlen(p);
        bool matched = false;
        for (uint8_t v = 0; v <= static_cast<uint8_t>(MotorCheck::UNKNOWN); v++) {
            const MotorCheck check = static_cast<MotorCheck>(v);
            const char* token = motorCheckToken(check);
            if (strlen(token) == tokenLen && strncmp(p, token, tokenLen) == 0) {
                out[i] = check;
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
        const bool last = (i + 1 == count);
        if (last != (comma == nullptr)) {
            return false;  // too many or too few tokens
        }
        if (comma) {
            p = comma + 1;
        }
    }
    return true;
}

/**
 * @brief Whether a finger's check result warns (boot [WARN] + red double
 *        blink, and the app's warning: the same rule on both sides)
 *
 * Index to pinky warn on OPEN or NODRIVER. The thumb warns only on NODRIVER:
 * 4-motor v3 gloves leave that port empty, so OPEN there is normal. UNKNOWN
 * never warns.
 */
inline bool isMotorCheckProblem(uint8_t finger, MotorCheck check) {
    if (finger < MOTOR_CHECK_BASE_FINGERS) {
        return check == MotorCheck::OPEN || check == MotorCheck::NODRIVER;
    }
    return check == MotorCheck::NODRIVER;
}

/**
 * @brief Number of fingers whose check result warns (see isMotorCheckProblem)
 */
inline uint8_t countMotorCheckProblems(const MotorCheck* checks, uint8_t count) {
    if (checks == nullptr) {
        return 0;
    }
    uint8_t problems = 0;
    for (uint8_t f = 0; f < count; f++) {
        if (isMotorCheckProblem(f, checks[f])) {
            problems++;
        }
    }
    return problems;
}

/**
 * @brief Fingers therapy patterns use: index to pinky, plus the thumb on
 *        boards that have one when the check found it
 */
constexpr uint8_t therapyFingerCount(bool thumbPresent) {
    return (MAX_ACTUATORS > MOTOR_CHECK_BASE_FINGERS && thumbPresent)
        ? static_cast<uint8_t>(MOTOR_CHECK_BASE_FINGERS + 1)
        : MOTOR_CHECK_BASE_FINGERS;
}

#endif // MOTOR_CHECK_H
