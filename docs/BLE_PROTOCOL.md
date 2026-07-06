# BlueBuzzah BLE Protocol

**This document has moved.** The formal, code-verified source of truth for the phone↔firmware BLE protocol is the wiki:

**→ [BLE-Protocol (firmware wiki)](https://github.com/BlueBuzzah/BlueBuzzah-Firmware/wiki/BLE-Protocol)**

The wiki page documents every command, exact response format, parameter range, error string, and known protocol defect, verified against the implementation (`src/menu_controller.cpp`, `src/ble_manager.cpp`, `src/profile_manager.cpp`).

> Any firmware change to a command, response, error string, or parameter range must update the wiki page in the same PR — and the BuzzahBuddy app's `MockBluetoothService` must be updated to match byte-for-byte.

The previous version of this file contained material inaccuracies against the implementation (parameter ranges/units, profile count, MTU, settings persistence format, and a nonfunctional `PING` example) and was retired on 2026-07-06 as part of the mobile-app gap analysis.
