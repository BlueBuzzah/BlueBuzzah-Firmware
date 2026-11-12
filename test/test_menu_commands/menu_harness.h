/**
 * @file menu_harness.h
 * @brief Compiles the real MenuController on the host with three small fakes.
 *
 * The real hardware.h and ble_manager.h are kept out; HapticController,
 * BatteryMonitor and BLEManager are replaced by recording fakes that offer
 * only the members menu_controller.cpp uses.
 */

#pragma once

#include <Arduino.h>
#include <string>
#include "types.h"
#include "config.h"
#include "motor_check.h"

#define HARDWARE_H
#define BLE_MANAGER_H

class HapticController {
public:
    bool enabled[MAX_ACTUATORS];
    int activateCalls = 0;
    int deactivateCalls = 0;
    int emergencyStopCalls = 0;
    uint8_t lastFinger = 0;
    uint8_t lastAmplitude = 0;

    HapticController() { for (bool& e : enabled) e = true; }
    bool isEnabled(uint8_t finger) const { return finger < MAX_ACTUATORS && enabled[finger]; }
    Result activate(uint8_t finger, uint8_t amplitude) {
        ++activateCalls;
        lastFinger = finger;
        lastAmplitude = amplitude;
        return Result::OK;
    }
    Result deactivate(uint8_t finger) {
        ++deactivateCalls;
        lastFinger = finger;
        return Result::OK;
    }
    void emergencyStop() { ++emergencyStopCalls; }
};

class BatteryMonitor {
public:
    BatteryStatus status;
    BatteryStatus getStatus() { return status; }
};

class BLEManager {
public:
    bool secondaryConnected = false;
    std::string lastToSecondary;

    bool isSecondaryConnected() const { return secondaryConnected; }
    bool sendToSecondary(const char* message) {
        lastToSecondary = message;
        return secondaryConnected;
    }
};

#include "../../src/sync_protocol.cpp"
#include "../../src/therapy_engine.cpp"
#include "../../src/state_machine.cpp"
#include "../../src/profile_manager.cpp"
#include "../../src/menu_controller.cpp"

struct MenuHarness {
    TherapyEngine therapy;
    TherapyStateMachine stateMachine;
    ProfileManager profiles;
    HapticController haptic;
    BatteryMonitor battery;
    BLEManager ble;
    MenuController menu;

    bool lastHandled = false;
    bool syncReady = true;
    bool batteryAllows = true;
    const char* gloveError = nullptr;
    BatteryLevel primaryLevel = BatteryLevel::NORMAL;
    BatteryLevel secondaryLevel = BatteryLevel::NORMAL;
    const char* motorChecks[2] = {nullptr, nullptr};
    int restartCalls = 0;
    int sessionStopCalls = 0;
    std::string lastToSecondaryCallback;
    std::string reply;

    MenuHarness() {
        current() = this;
        profiles.begin(false);
        menu.begin(&therapy, &battery, &haptic, &stateMachine, &profiles, &ble);
        menu.setSendCallback(onSend);
        menu.setRestartCallback(onRestart);
        menu.setSessionStopCallback(onSessionStop);
        menu.setSendToSecondaryCallback(onSendToSecondary);
        menu.setSyncReadyCallback(onSyncReady);
        menu.setBatteryGateCallback(onBatteryGate);
        menu.setGloveErrorCallback(onGloveError);
        menu.setBatteryLevelCallback(onBatteryLevel);
        menu.setMotorCheckCallback(onMotorCheck);
    }

    ~MenuHarness() { if (current() == this) current() = nullptr; }

    /** Sends a command as a phone connection would and returns the reply. */
    std::string send(const char* command) {
        reply.clear();
        MockSerial::clearCaptured();
        lastHandled = menu.handleCommand(command, true);
        return reply;
    }

private:
    static MenuHarness*& current() {
        static MenuHarness* h = nullptr;
        return h;
    }
    static void onSend(const char* response) { current()->reply += response; }
    static void onRestart() { ++current()->restartCalls; }
    static void onSessionStop() { ++current()->sessionStopCalls; }
    static void onSendToSecondary(const char* message) { current()->lastToSecondaryCallback = message; }
    static bool onSyncReady() { return current()->syncReady; }
    static bool onBatteryGate() { return current()->batteryAllows; }
    static const char* onGloveError() { return current()->gloveError; }
    static BatteryLevel onBatteryLevel(bool secondary) {
        return secondary ? current()->secondaryLevel : current()->primaryLevel;
    }
    static const char* onMotorCheck(bool secondary) { return current()->motorChecks[secondary ? 1 : 0]; }
};
