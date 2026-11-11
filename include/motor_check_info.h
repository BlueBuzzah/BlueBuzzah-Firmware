/**
 * @file motor_check_info.h
 * @brief INFO's boot motor check keys (MOTP / MOTS)
 *
 * Shared by MenuController::handleInfo and the test_menu_controller mirror,
 * so the menu tests exercise the real emission logic.
 *
 * Header-only with no includes: the menu test defines its own MAX_ACTUATORS
 * and doesn't see menu_controller.h, so this spells out the callback type.
 */

#ifndef MOTOR_CHECK_INFO_H
#define MOTOR_CHECK_INFO_H

/**
 * @brief Add MOTP (this glove's check) and, while the SECONDARY is linked,
 *        MOTS (its last report)
 *
 * Neither key without a callback (boards with no boot check), and each only
 * when the callback returns non-null for it.
 *
 * @param callback        A glove's tokens (false = this glove, true = the
 *                        SECONDARY), or nullptr when there is nothing to report
 * @param secondaryLinked Whether the SECONDARY is connected
 * @param addLine         Called as addLine(key, value) for each key
 */
template <typename AddLine>
inline void addMotorCheckInfoLines(const char* (*callback)(bool secondary),
                                   bool secondaryLinked, AddLine&& addLine) {
    if (callback == nullptr) {
        return;
    }
    if (const char* own = callback(false)) {
        addLine("MOTP", own);
    }
    if (secondaryLinked) {
        if (const char* secondary = callback(true)) {
            addLine("MOTS", secondary);
        }
    }
}

#endif // MOTOR_CHECK_INFO_H
