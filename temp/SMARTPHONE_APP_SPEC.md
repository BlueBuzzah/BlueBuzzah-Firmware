# BlueBuzzah Smartphone App Specification

**Version:** 2.0.2
**Date:** 2025-11-23
**Status:** Protocol Implemented (Firmware Complete)
**Platform:** .NET MAUI (iOS/Android)

## Overview

This specification defines **HOW** the .NET MAUI smartphone app communicates with BlueBuzzah haptic therapy gloves and **WHAT** features the app can control. The app UI is already implemented with mocks - this document specifies the BLE protocol for connecting that UI to the gloves.

**Key Architecture:**

```
Smartphone App
      |
      | BLE UART (Nordic UART Service)
      v
  VL (PRIMARY) ────[BLE UART]──── VR (SECONDARY)
   Left Glove                     Right Glove
```

**Important:** App only connects to **VL (PRIMARY)**. VL automatically relays commands to VR as needed.

---

## BLE Protocol

### Connection Setup

**Service:** Nordic UART Service (NUS)

- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- TX Characteristic: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` (Write - App → Glove)
- RX Characteristic: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` (Notify - Glove → App)

**Connection Flow:**

1. App scans for BLE device named **"VL"**
2. App connects to VL
3. App subscribes to RX characteristic (notifications)
4. App sends commands via TX, receives responses via RX

---

### Message Format

**Commands (App → Glove):**

```
COMMAND_NAME\n
```

**Example:**

```
BATTERY\n
SESSION_START\n
PROFILE_LOAD:1\n
```

**Responses (Glove → App):**

```
KEY:VALUE\n
KEY2:VALUE2\n
\x04
```

**Rules:**

- Commands end with newline (`\n`)
- Responses are `KEY:VALUE` pairs, one per line
- Response terminates with `\x04` (EOT character)
- Errors: First line is `ERROR:description`
- No JSON, no brackets, no prefixes - just clean data

**Example Response:**

```
BATP:3.72\n
BATS:3.68\n
\x04
```

**Error Response:**

```
ERROR:Invalid profile ID\n
\x04
```

---

## Important Implementation Notes

### Message Interleaving & Filtering

During active therapy sessions, the VL (PRIMARY) glove sends internal synchronization messages to the VR (SECONDARY) glove. **Your app may receive these messages if listening to the RX characteristic.** You must implement filtering to ignore these internal messages.

**Internal VL↔VR Messages to Ignore:**

- `EXECUTE_BUZZ:N` - Command to execute buzz sequence N (sent every ~200ms during therapy)
- `BUZZ_COMPLETE:N` - Acknowledgment that sequence N completed
- `PARAM_UPDATE:KEY:VAL:...` - Profile parameter synchronization
- `SEED:N` - Random seed for jitter synchronization
- `SEED_ACK` - Seed acknowledgment
- `GET_BATTERY` - Internal battery query to VR
- `BATRESPONSE:V` - VR battery response
- `ACK_PARAM_UPDATE` - Parameter update acknowledgment

**Implementation Strategy:**

Filter messages that don't end with `\x04` (EOT character). App-directed responses **always** include EOT; internal messages typically don't.

```csharp
void OnBleNotification(byte[] data) {
    string message = Encoding.UTF8.GetString(data);

    // Ignore internal messages (no EOT)
    if (!message.Contains("\x04")) {
        return; // Skip internal VL↔VR messages
    }

    // Process app-directed response
    ProcessResponse(message);
}
```

---

### Command Restrictions During Active Sessions

**Critical:** You **cannot** change profiles or parameters during active therapy sessions.

**Blocked Commands During Sessions:**

- `PROFILE_LOAD` - Cannot change profiles
- `PROFILE_CUSTOM` - Cannot set custom parameters
- `PARAM_SET` - Cannot modify individual parameters

**Required Workflow:**

```
SESSION_STOP → PROFILE_LOAD/PROFILE_CUSTOM → SESSION_START
```

**Error Response:**

```
ERROR:Cannot modify parameters during active session
\x04
```

**App Should:**

- Disable profile/parameter controls during active sessions
- Show "Stop session to change settings" message if user attempts
- Only enable controls when `SESSION_STATUS:IDLE`

---

### Timing Recommendations

**Wait times for reliable processing:**

- **Between commands:** Wait 100ms for reliable processing
- **SESSION_START:** May take up to 500ms to respond (establishes VL↔VR sync)
- **BATTERY command:** May take up to 1 second (queries both gloves via BLE)
- **CALIBRATE_BUZZ:** Duration varies (50-2000ms depending on duration parameter)
- **Profile changes:** Instant but sync to VR takes ~200ms

**Recommended Command Rate:**

- Maximum: 10 commands/second (100ms spacing)
- Commands are processed sequentially
- Rapid firing may cause delays

**Implementation:**

```csharp
async Task SendCommand(string cmd) {
    await txCharacteristic.WriteAsync(Encoding.UTF8.GetBytes(cmd + "\n"));
    await Task.Delay(100); // Recommended delay
}
```

---

## App Features & Commands

### 1. Device Discovery & Connection

**What App Does:**

- Scan for "VL" device
- Connect to Nordic UART Service
- Subscribe to notifications

**No Command Needed** - Standard BLE connection flow

---

### 2. Device Information

#### Get Device Info

**Command:** `INFO\n`

**Response:**

```
ROLE:PRIMARY
NAME:VL
FW:2.0.0
BATP:3.72
BATS:3.68
STATUS:IDLE
\x04
```

**App Should Display:**

- Firmware version
- Battery levels (both gloves)
- Current status (IDLE/RUNNING/PAUSED)
- Connection indicator

---

#### Check Battery

**Command:** `BATTERY\n`

**Response:**

```
BATP:3.72
BATS:3.68
\x04
```

**App Should:**

- Parse voltage values
- Apply color thresholds:
  - **Green:** > 3.6V (Good)
  - **Yellow:** 3.3 - 3.6V (Medium)
  - **Red:** < 3.3V (Critical)
- Display battery percentage estimate
- Show warning if < 3.3V

**Legacy Shortcut:** `g` (backward compatible)

---

#### Connection Test

**Command:** `PING\n`

**Response:**

```
PONG
\x04
```

**App Should:**

- Use for periodic connection health checks
- Display connection status indicator
- Timeout after 2 seconds if no response

---

### 3. Therapy Profiles

#### List Available Profiles

**Command:** `PROFILE_LIST\n`

**Response:**

```
PROFILE:1:Regular VCR
PROFILE:2:Noisy VCR
PROFILE:3:Hybrid VCR
\x04
```

**App Should Display:**

- Profile selector/picker with 3 options
- Profile descriptions:
  - **Regular VCR:** 100ms ON, 67ms OFF, no jitter
  - **Noisy VCR:** 23.5% jitter, mirrored patterns - **DEFAULT**
  - **Hybrid VCR:** Mixed frequency stimulation

**Important:** Noisy VCR (Profile 2) is the **DEFAULT** profile loaded on gloves at startup.

---

#### Load Profile

**Command:** `PROFILE_LOAD:1\n`

**Parameters:**

- `1` = Regular VCR
- `2` = Noisy VCR
- `3` = Hybrid VCR

**Response:**

```
STATUS:LOADED
PROFILE:Regular VCR
\x04
```

**Error:**

```
ERROR:Invalid profile ID
\x04
```

**App Should:**

- Send profile ID when user selects
- Show confirmation message
- Update UI with profile name
- Auto-sync happens on gloves (no manual sync needed)

**Legacy Shortcuts:** `1`, `2`, `3` (backward compatible)

---

#### Get Current Profile Settings

**Command:** `PROFILE_GET\n`

**Response:**

```
TYPE:LRA
FREQ:250
VOLT:2.50
ON:0.100
OFF:0.067
SESSION:120
AMPMIN:100
AMPMAX:100
JITTER:23.5
MIRROR:True
PATTERN:RNDP
\x04
```

**Note:** Example shows default **Noisy VCR** profile settings.

**App Should:**

- Parse all parameters
- Display in advanced settings screen
- Allow viewing (editing via PROFILE_CUSTOM or PARAM_SET)

**Legacy Shortcut:** `v` (backward compatible)

---

#### Set Custom Profile

**Command:** `PROFILE_CUSTOM:ON:0.150:OFF:0.080:JITTER:10\n`

**Valid Parameters (short form preferred, long form also accepted):**

- `ON` / `TIME_ON` (0.050-0.500 seconds) - Burst duration
- `OFF` / `TIME_OFF` (0.020-0.200 seconds) - Inter-burst interval
- `FREQ` / `FREQUENCY` / `ACTUATOR_FREQUENCY` (150-300 Hz) - Actuator frequency
- `VOLT` / `VOLTAGE` / `ACTUATOR_VOLTAGE` (1.0-3.3 V) - Actuator voltage
- `AMPMIN` / `AMP_MIN` / `AMPLITUDE_MIN` (0-100 %) - Minimum amplitude
- `AMPMAX` / `AMP_MAX` / `AMPLITUDE_MAX` (0-100 %) - Maximum amplitude
- `JITTER` (0-50 %) - Timing jitter percentage
- `MIRROR` (0 or 1) - Mirror pattern across gloves
- `SESSION` / `TIME_SESSION` (1-180 minutes) - Session duration
- `TYPE` / `ACTUATOR_TYPE` (LRA/ERM) - Actuator type
- `PATTERN` / `PATTERN_TYPE` (RNDP/SEQ) - Pattern type

**Response:**

```
STATUS:CUSTOM_LOADED
ON:0.150
OFF:0.080
JITTER:10
\x04
```

**Errors:**

```
ERROR:Invalid parameter name
\x04
```

```
ERROR:Value out of range
\x04
```

**App Should:**

- Provide advanced settings UI with sliders/inputs
- Validate ranges before sending
- Only send changed parameters (not all 11)
- Show confirmation
- **Auto-sync:** VL automatically syncs to VR

**Note:** Cannot modify during active session (returns error)

---

### 4. Session Control

#### Start Therapy Session

**Command:** `SESSION_START\n`

**Response:**

```
SESSION_STATUS:RUNNING
\x04
```

**App Should:**

- Enable pause/stop buttons
- Disable profile selection
- Start session timer
- Begin periodic status polling (every 5-10 seconds)

**What Happens on Gloves:**

- VL starts therapy execution
- VL relays SESSION_START to VR
- Both gloves run synchronized therapy
- Session is **interruptible** (can pause/stop)

---

#### Pause Session

**Command:** `SESSION_PAUSE\n`

**Response:**

```
SESSION_STATUS:PAUSED
\x04
```

**App Should:**

- Show "Resume" button
- Pause session timer
- Display pause indicator

**What Happens on Gloves:**

- VL pauses therapy (motors stop)
- VL relays PAUSE to VR
- Both gloves pause
- Elapsed time tracking excludes pause duration

---

#### Resume Session

**Command:** `SESSION_RESUME\n`

**Response:**

```
SESSION_STATUS:RUNNING
\x04
```

**App Should:**

- Show "Pause" button
- Resume session timer
- Remove pause indicator

---

#### Stop Session

**Command:** `SESSION_STOP\n`

**Response:**

```
SESSION_STATUS:IDLE
\x04
```

**App Should:**

- Reset UI to pre-session state
- Enable profile selection
- Log session to history (locally on phone)
- Show session summary

---

#### Get Session Status

**Command:** `SESSION_STATUS\n`

**Response:**

```
SESSION_STATUS:RUNNING
ELAPSED:1245
TOTAL:7200
PROGRESS:17
\x04
```

**Fields:**

- `SESSION_STATUS`: IDLE | RUNNING | PAUSED
- `ELAPSED`: Seconds elapsed (excluding pauses)
- `TOTAL`: Total session duration (seconds)
- `PROGRESS`: Percentage (0-100)

**App Should:**

- Poll every 5-10 seconds during session
- Update progress bar
- Show elapsed/remaining time
- Display current status

---

### 5. Parameter Adjustment

#### Set Individual Parameter

**Command:** `PARAM_SET:AMPMIN:75\n`

**Response:**

```
PARAM:AMPMIN
VALUE:75
\x04
```

**Error:**

```
ERROR:Invalid parameter name
\x04
```

**App Should:**

- Use for single parameter changes
- Use `PROFILE_CUSTOM` for multiple parameters
- Validate input before sending
- Show confirmation

**Note:** Use this for quick tweaks. Use `PROFILE_CUSTOM` for comprehensive adjustments.

---

### 6. Calibration Mode

#### Enter Calibration

**Command:** `CALIBRATE_START\n`

**Response:**

```
MODE:CALIBRATION
\x04
```

**App Should:**

- Show calibration UI with 8 finger buttons (0-7)
- Display intensity slider (0-100%)
- Display duration slider (50-2000ms)

**Legacy Shortcut:** `c` (backward compatible)

---

#### Test Individual Finger

**Command:** `CALIBRATE_BUZZ:0:80:500\n`

**Parameters:**

- Finger index: `0-7`
  - `0-3`: PRIMARY glove (VL) - Thumb, Index, Middle, Ring
  - `4-7`: SECONDARY glove (VR) - Thumb, Index, Middle, Ring
  - **Note:** Gloves are ambidextrous and can be worn on either hand
- Intensity: `0-100` (percentage)
- Duration: `50-2000` (milliseconds)

**Response:**

```
FINGER:0
INTENSITY:80
DURATION:500
\x04
```

**Errors:**

```
ERROR:Not in calibration mode
\x04
```

```
ERROR:Invalid finger index
\x04
```

**App Should:**

- Provide 8 buttons (one per finger)
- Label them clearly (PRIMARY Thumb, SECONDARY Index, etc.) or with neutral labels (Finger 0-7)
- Show intensity/duration controls
- Give visual feedback when buzzing

**Note:** VL (PRIMARY) automatically relays commands for fingers 4-7 to VR (SECONDARY)

---

#### Exit Calibration

**Command:** `CALIBRATE_STOP\n`

**Response:**

```
MODE:NORMAL
\x04
```

**App Should:**

- Return to normal mode UI
- Hide calibration controls

---

### 7. System Commands

#### List Commands

**Command:** `HELP\n`

**Response:**

```
COMMAND:INFO
COMMAND:BATTERY
COMMAND:PING
COMMAND:PROFILE_LIST
COMMAND:PROFILE_LOAD
COMMAND:PROFILE_GET
COMMAND:PROFILE_CUSTOM
COMMAND:SESSION_START
COMMAND:SESSION_PAUSE
COMMAND:SESSION_RESUME
COMMAND:SESSION_STOP
COMMAND:SESSION_STATUS
COMMAND:PARAM_SET
COMMAND:CALIBRATE_START
COMMAND:CALIBRATE_BUZZ
COMMAND:CALIBRATE_STOP
COMMAND:RESTART
COMMAND:HELP
\x04
```

**App Should:**

- Use for debugging/diagnostics
- Display in developer/advanced menu

---

#### Restart Glove

**Command:** `RESTART\n`

**Response:**

```
STATUS:REBOOTING
\x04
```

**App Should:**

- Show warning dialog
- Wait for disconnect
- Attempt reconnection after 5 seconds
- Handle connection loss gracefully

**Legacy Shortcut:** `r` (backward compatible)

**Note:** BLE connection will drop immediately after this command

---

## Troubleshooting

### Q: Why do I see `EXECUTE_BUZZ:0` or similar messages?

**A:** These are internal device-to-device synchronization messages between VL and VR gloves. Your app should filter/ignore them. They don't end with `\x04` (EOT), so you can use that to distinguish them from app-directed responses.

---

### Q: Why can't I change profiles during therapy?

**A:** Profile changes require stopping the therapy session first for safety. Send `SESSION_STOP`, then `PROFILE_LOAD`, then `SESSION_START`.

---

### Q: Why does BATTERY take so long to respond?

**A:** The VL glove must query the VR glove via BLE, which can take up to 1 second. This is normal.

---

### Q: What happens if VR glove is not connected?

**A:** Most commands will work on VL only. However, `SESSION_START` will return `ERROR:VR not connected` since therapy requires both gloves.

---

### Q: Can I send commands during an active therapy session?

**A:** Yes, but only session control commands (PAUSE, RESUME, STOP, STATUS). Profile and parameter changes are blocked during active sessions.

---

### Q: How do I know if a command succeeded?

**A:** All successful responses include relevant data and end with `\x04`. Errors start with `ERROR:` and also end with `\x04`.

---

### Q: What's the maximum command rate?

**A:** Recommended: 10 commands/second (100ms spacing). The glove processes commands sequentially, so rapid firing may cause delays.

---

## Synchronization Architecture

The gloves use **command-driven synchronization** for precise tactor timing:

**How It Works:**

- **VL (PRIMARY)** sends `EXECUTE_BUZZ:N` commands to **VR (SECONDARY)** before each buzz
- **VR** waits for explicit commands before activating tactors (blocking receive)
- **VR** sends `BUZZ_COMPLETE:N` acknowledgment after each buzz
- **Synchronization accuracy:** 7.5-20ms (BLE latency + processing time)

This ensures tactors fire simultaneously on both hands (±20ms) with no drift over time.

**Impact on Mobile App:**

- Your app may receive internal `EXECUTE_BUZZ` messages during therapy
- Implement filtering to ignore messages without `\x04` terminator (see Message Interleaving section)
- Session control commands (PAUSE/RESUME/STOP) work correctly during therapy
- The VL glove handles all synchronization automatically - no manual sync needed

---

## App Implementation Requirements

### 1. BLE Command Sender

**Pseudocode:**

```csharp
async Task SendCommand(string command) {
    byte[] data = Encoding.UTF8.GetBytes(command + "\n");
    await txCharacteristic.WriteAsync(data);
}
```

---

### 2. Response Parser

**Pseudocode:**

```csharp
private Dictionary<string, string> responseCache = new();
private StringBuilder responseBuffer = new();

void OnBleNotification(byte[] data) {
    string chunk = Encoding.UTF8.GetString(data);

    // IMPORTANT: Filter internal VL↔VR messages
    // Only process messages with EOT terminator
    if (!chunk.Contains("\x04")) {
        // Ignore internal sync messages (EXECUTE_BUZZ, BUZZ_COMPLETE, etc.)
        return;
    }

    responseBuffer.Append(chunk);

    if (chunk.Contains("\x04")) {
        // End of transmission detected
        string fullResponse = responseBuffer.ToString();
        responseBuffer.Clear();

        // Parse KEY:VALUE lines
        string[] lines = fullResponse.Split('\n');
        foreach (var line in lines) {
            if (line.Contains(":")) {
                string[] parts = line.Split(':', 2);
                responseCache[parts[0]] = parts[1];
            }
        }

        // Trigger UI update
        OnResponseComplete(responseCache);
        responseCache.Clear();
    }
}
```

**Note:** The EOT (`\x04`) filtering is critical to avoid processing internal synchronization messages during active therapy sessions.

---

### 3. Error Handling

**Check for Errors:**

```csharp
if (responseCache.ContainsKey("ERROR")) {
    string errorMsg = responseCache["ERROR"];
    ShowErrorDialog(errorMsg);
    return;
}
```

**Common Errors:**

- `ERROR:Invalid profile ID` - User selected invalid profile
- `ERROR:Not in calibration mode` - Tried to buzz finger without entering calibration
- `ERROR:Value out of range` - Parameter validation failed
- `ERROR:Invalid parameter name` - Typo in parameter key
- `ERROR:Cannot modify during active session` - Tried to change profile during therapy

---

### Error Codes Reference

All errors follow the format: `ERROR:description\n\x04`

**Complete Error List:**

- `ERROR:Unknown command` - Unrecognized command sent
- `ERROR:Invalid profile ID` - Profile ID not in range 1-3
- `ERROR:VR not connected` - VR glove not connected for session start
- `ERROR:Battery too low` - Battery level below minimum for therapy
- `ERROR:No active session` - Tried to pause/resume without running session
- `ERROR:Cannot modify during active session` - Attempted profile/parameter change during therapy
- `ERROR:Cannot modify parameters during active session` - Attempted parameter change during therapy
- `ERROR:Invalid parameter name` - Unknown parameter in PROFILE_CUSTOM or PARAM_SET
- `ERROR:Value out of range` - Parameter value outside valid range
- `ERROR:Not in calibration mode` - Tried to use CALIBRATE_BUZZ without entering calibration
- `ERROR:Invalid finger index` - Finger index not in range 0-7
- `ERROR:No paused session` - Tried to resume without paused session

---

### 4. Session Status Polling

**Recommended:**

```csharp
// Poll every 5 seconds during active session
Timer statusTimer = new Timer(5000);
statusTimer.Elapsed += async (s, e) => {
    if (sessionActive) {
        await SendCommand("SESSION_STATUS");
        // Response handled by parser
    }
};
```

---

### 5. Connection Health Monitoring

**Recommended:**

```csharp
// PING every 30 seconds to detect disconnects
Timer pingTimer = new Timer(30000);
pingTimer.Elapsed += async (s, e) => {
    if (connected) {
        await SendCommand("PING");
        // If no PONG received within 2s, mark as disconnected
    }
};
```

---

## Testing Without Hardware

### Mock BLE Service

**App Should Include:**

```csharp
class MockBleService {
    Dictionary<string, string> MockResponses = new() {
        { "BATTERY", "BATP:3.72\nBATS:3.68\n\x04" },
        { "INFO", "ROLE:PRIMARY\nNAME:VL\nFW:2.0.0\nBATP:3.72\nBATS:3.68\nSTATUS:IDLE\n\x04" },
        { "PING", "PONG\n\x04" },
        { "SESSION_START", "SESSION_STATUS:RUNNING\n\x04" },
        // ... all commands
    };

    async Task<string> SendMockCommand(string cmd) {
        await Task.Delay(100); // Simulate BLE latency
        string response = MockResponses[cmd.TrimEnd('\n')];
        return response;
    }
}
```

**Toggle:**

```csharp
bool USE_MOCK_BLE = true; // Set to false for real hardware
```

---

## Implementation Phases

### Phase 1: Glove Firmware ✅ COMPLETE

- ✅ All 18 commands implemented
- ✅ KEY:VALUE\n response formatter with \x04 EOT
- ✅ Session state machine
- ✅ VL→VR auto-sync
- ✅ Desktop simulation tests (8/8 passing)

### Phase 2: .NET MAUI App ⏳ READY TO START

**What to Build:**

1. BLE command sender (`SendCommand()`)
2. Response parser with \x04 detection
3. Error handler
4. Mock BLE service for testing
5. Wire UI controls to commands

**Testing:**

- All features work with mock BLE
- Response parsing handles all commands
- UI updates correctly

### Phase 3: Hardware Testing ⏳ BLOCKED (Need Gloves)

**What to Test:**

1. Real BLE connection VL ↔ App
2. All 18 commands execute correctly
3. Response timing/latency
4. Battery readings accurate
5. Session control end-to-end
6. 2-hour session stability

---

## Success Criteria

### Phase 2 (.NET MAUI App)

- [ ] Mock BLE service returns correct responses
- [ ] App parses KEY:VALUE format
- [ ] App detects \x04 EOT character
- [ ] Error responses handled
- [ ] UI updates from parsed data
- [ ] All 18 commands tested with mocks

### Phase 3 (Hardware)

- [ ] App connects to real VL glove
- [ ] All commands execute on hardware
- [ ] Battery readings accurate (±0.1V)
- [ ] Profile changes sync VL→VR
- [ ] Session control works end-to-end
- [ ] No disconnections during 2-hour session

---

## Optional Features

### Session History (App-Side Storage)

- Store sessions locally on phone
- Track: date/time, duration, profile used, battery levels
- Export to CSV/JSON

### Firmware Updates (Future)

- BLE DFU (Device Firmware Update)
- Wait until core functionality stable

---

## Summary

**Protocol:** Simple, clean KEY:VALUE format
**Commands:** 18 total
**Firmware Status:** ✅ Complete and tested
**App Status:** Ready for BLE integration
**Hardware Dependency:** Only Phase 3 testing

**Next Step:** Implement BLE sender/parser in .NET MAUI app using mock service for testing.
