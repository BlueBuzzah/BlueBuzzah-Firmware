# BlueBuzzah BLE Protocol

**Version:** 2.0.0
**Date:** 2025-01-23
**Platform:** .NET MAUI App ↔ BlueBuzzah (PRIMARY) Glove
**Firmware:** Arduino C++ / PlatformIO

## Connection

**BLE Service:** Nordic UART Service (NUS)

- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- TX Characteristic: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` (App → Glove)
- RX Characteristic: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` (Glove → App)

**Connection Flow:**

1. App scans for device named "BlueBuzzah"
2. App connects to BlueBuzzah (PRIMARY device only)
3. App subscribes to RX characteristic
4. App sends commands via TX, receives responses via RX

**Important:** Mobile apps should **only** connect to the BlueBuzzah (PRIMARY) glove. The Primary glove automatically communicates with the BlueBuzzah-Secondary (SECONDARY) glove to coordinate therapy.

**Device Roles:** Both devices are **ambidextrous** and can be worn on either hand. The PRIMARY/SECONDARY designation refers to which device the mobile app connects to, not which hand they are worn on. All protocol references use PRIMARY/SECONDARY terminology to maintain this flexibility.

## Message Format

**Command:**

```
COMMAND_NAME\n
```

**Response:**

```
KEY:VALUE\n
KEY2:VALUE2\n
\x04
```

**Rules:**

- Commands end with `\n`
- Responses are `KEY:VALUE` pairs, one per line
- **ALL messages end with `\x04` (EOT character)** - both app-directed and internal
- Errors: First line is `ERROR:description`
- No brackets, no prefixes, just data

**Example:**

```
Send: BATTERY\n
Recv: BATP:3.72\nBATS:3.68\n\x04
```

## Important Notes for Mobile App Developers

### Message Interleaving

During active therapy sessions, the BlueBuzzah (PRIMARY) glove sends internal synchronization messages to the BlueBuzzah-Secondary (SECONDARY) glove. Your app may receive these messages if listening to the RX characteristic. **You must implement filtering to ignore these internal messages.**

**All messages (both app-directed and internal) end with `\x04` (EOT).** This ensures reliable message framing and parsing.

**Internal PRIMARY↔SECONDARY Messages to Ignore:**

The firmware uses a `SYNC:` prefix for internal synchronization messages:

- `SYNC:EXECUTE_BUZZ:left_finger|N|right_finger|N|amplitude|N|seq|N|timestamp|N\x04` - Execute buzz
- `SYNC:BUZZ_COMPLETE:seq|N\x04` - Buzz completion acknowledgment
- `SYNC:HEARTBEAT:ts|N\x04` - Connection heartbeat (every 2 seconds)
- `SYNC:START_SESSION:duration_sec|N|pattern_type|...\x04` - Start therapy session
- `SYNC:STOP_SESSION:\x04` - Stop therapy session
- `SYNC:PAUSE_SESSION:\x04` / `SYNC:RESUME_SESSION:\x04` - Pause/resume control
- `PARAM_UPDATE:KEY:VAL:...\x04` - Profile parameter synchronization
- `SEED:N\x04` - Random seed for jitter synchronization
- `SEED_ACK\x04` - Seed acknowledgment
- `GET_BATTERY\x04` - Internal battery query to Secondary
- `BATRESPONSE:V\x04` - Secondary battery response
- `ACK_PARAM_UPDATE\x04` - Parameter update acknowledgment

**Filtering Strategy:**

Ignore messages that match these patterns:
- Start with `SYNC:` (all internal synchronization messages)
- Start with `EXECUTE_BUZZ:` (legacy format)
- Start with `BUZZ_COMPLETE:` (legacy format)
- Start with `PARAM_UPDATE:`
- Start with `SEED:` or equal `SEED_ACK`
- Equal `GET_BATTERY`
- Start with `BATRESPONSE:`
- Equal `ACK_PARAM_UPDATE`

**Example Filter Implementation:**

```csharp
bool IsInternalMessage(string msg) {
    // Remove EOT for pattern matching
    msg = msg.Replace("\x04", "").Trim();

    // SYNC: prefix covers all internal sync messages
    return msg.StartsWith("SYNC:") ||
           msg.StartsWith("EXECUTE_BUZZ:") ||
           msg.StartsWith("BUZZ_COMPLETE:") ||
           msg.StartsWith("PARAM_UPDATE:") ||
           msg.StartsWith("SEED:") ||
           msg == "SEED_ACK" ||
           msg == "GET_BATTERY" ||
           msg.StartsWith("BATRESPONSE:") ||
           msg == "ACK_PARAM_UPDATE";
}
```

### Command Restrictions During Active Sessions

- **Cannot change profiles** during active sessions (PROFILE_LOAD, PROFILE_CUSTOM)
- **Cannot change parameters** during active sessions (PARAM_SET)
- **Must send SESSION_STOP first**, then modify settings

If you attempt to change settings during a session, you'll receive:

```
ERROR:Cannot modify parameters during active session
\x04
```

## Timing & Performance

Understanding timing constraints is critical for reliable BLE communication and proper therapy operation.

### Command Timing

**Recommended Command Rate:**
- **Wait 100ms between commands** for reliable processing
- **Maximum rate:** 10 commands/second
- Commands are processed sequentially; rapid firing causes delays

**Command-Specific Response Times:**

| Command | Response Time | Notes |
|---------|---------------|-------|
| PING, INFO | <50ms | Instant response from Primary |
| PROFILE_LOAD | <50ms | Instant on Primary |
| PROFILE_GET | <50ms | Returns cached values |
| SESSION_START | **up to 500ms** | Establishes PRIMARY↔SECONDARY sync |
| SESSION_PAUSE/RESUME | <100ms | Updates both gloves |
| SESSION_STOP | <100ms | Stops both gloves |
| SESSION_STATUS | <50ms | Returns cached values |
| BATTERY | **up to 1 second** | Queries both gloves via BLE |
| PARAM_SET | <50ms | Instant update |
| PROFILE_CUSTOM | <50ms | Instant update |
| CALIBRATE_BUZZ | 50-2000ms | Depends on duration parameter |

### Profile Synchronization Timing

- **Primary update:** Instant (<10ms)
- **Secondary sync:** ~200ms (via BLE transfer)
- **Total time:** ~200-250ms for full synchronization

Profile changes are automatically synced from Primary to Secondary. No manual sync command needed.

### Therapy Session Timing

**Configurable Parameters:**

| Parameter | Range | Unit | Description |
|-----------|-------|------|-------------|
| ON | 0.050-0.500 | seconds | Tactor ON duration per cycle |
| OFF | 0.020-0.200 | seconds | Tactor OFF duration per cycle |
| SESSION | 1-180 | minutes | Total therapy session duration |
| JITTER | 0-50 | % | Timing variation for therapeutic effect |

**Example Cycle Timing (Default "Noisy VCR" Profile):**
- ON: 100ms
- OFF: 67ms
- JITTER: 23.5% (varies each cycle)
- **Effective cycle rate:** ~6 cycles/second

### Internal Synchronization Timing

During active therapy sessions, PRIMARY and SECONDARY communicate continuously:

- **EXECUTE_BUZZ messages:** Sent every ~200ms during therapy
- **BLE latency:** 7.5-20ms (connection interval dependent)
- **Processing time:** <5ms per command
- **Total synchronization accuracy:** ±20ms between gloves
- **Drift:** Zero (command-driven, not time-based)

This ensures tactors fire simultaneously on both hands with no accumulated timing drift.

### BLE Connection Specifications

- **Connection interval:** 7.5-20ms (negotiated during connection)
- **MTU size:** 23-512 bytes (negotiated during connection)
- **Typical MTU:** 185 bytes on modern devices
- **Message fragmentation:** May occur for responses >MTU size
- **Reliable delivery:** Guaranteed by BLE protocol

### Performance Guidelines

**Best Practices:**
1. ✅ Wait 100ms between commands
2. ✅ Expect SESSION_START to take 500ms
3. ✅ Expect BATTERY to take 1 second
4. ✅ Buffer incoming BLE notifications until EOT received
5. ✅ Filter internal messages (EXECUTE_BUZZ, etc.)
6. ⚠️ Avoid rapid command firing (<100ms intervals)
7. ⚠️ Don't assume instant responses for multi-device commands

**Typical App Operation Sequence:**
```
1. Connect to PRIMARY         → <1s
2. Send INFO                  → 50ms response
3. Send BATTERY               → 1s response (both gloves)
4. Send PROFILE_LOAD:2        → 50ms response, 200ms sync
5. Send SESSION_START         → 500ms response (sync established)
6. Poll SESSION_STATUS        → 50ms response (every 1-5 seconds)
7. Send SESSION_STOP          → 100ms response
```

**Total startup time:** ~2 seconds from connection to therapy start

## Parameter Reference

All parameter names use shorthand notation to minimize BLE bandwidth usage.

### Therapy Parameters

| Shorthand | Description | Range | Unit |
|-----------|-------------|-------|------|
| `TYPE` | Actuator type | LRA, ERM | - |
| `FREQ` | Actuator frequency | 150-300 | Hz |
| `VOLT` | Actuator voltage | 1.0-3.3 | V |
| `ON` | Time ON duration | 0.050-0.500 | seconds |
| `OFF` | Time OFF duration | 0.020-0.200 | seconds |
| `SESSION` | Session duration | 1-180 | minutes |
| `AMPMIN` | Minimum amplitude | 0-100 | % |
| `AMPMAX` | Maximum amplitude | 0-100 | % |
| `PATTERN` | Pattern type | RNDP, SEQ, etc. | - |
| `MIRROR` | Mirror patterns | 0, 1 (False/True) | - |
| `JITTER` | Timing jitter | 0-50 | % |

### Status & Info Parameters

| Shorthand | Description | Unit |
|-----------|-------------|------|
| `BATP` | PRIMARY glove battery | V |
| `BATS` | SECONDARY glove battery | V |
| `ELAPSED` | Elapsed time | seconds |
| `TOTAL` | Total time | seconds |
| `PROGRESS` | Session progress | % (0-100) |

**Note:** All parameter names are case-sensitive. Use exact shorthand as documented.

## Commands

> **See Also**: [Command Reference](COMMAND_REFERENCE.md) for firmware implementation details including code line references.

This section provides command documentation from the **mobile app developer perspective**. For firmware implementation details, refer to the Command Reference.

### Device Information

#### INFO

Get device information

**Request:**

```
INFO\n
```

**Response:**

```
ROLE:PRIMARY
NAME:Primary
FW:1.0.0
BATP:3.72
BATS:3.68
STATUS:IDLE
\x04
```

#### BATTERY

Get battery voltage for both gloves

**Request:**

```
BATTERY\n
```

**Response:**

```
BATP:3.72
BATS:3.68
\x04
```

**Thresholds:**

- Good: >3.6V
- Medium: 3.3-3.6V
- Critical: <3.3V

#### PING

Connection test

**Request:**

```
PING\n
```

**Response:**

```
PONG
\x04
```

### Therapy Profiles

#### PROFILE_LIST

List available therapy profiles

**Request:**

```
PROFILE_LIST\n
```

**Response:**

```
PROFILE:1:Regular VCR
PROFILE:2:Noisy VCR
PROFILE:3:Hybrid VCR
\x04
```

#### PROFILE_LOAD

Load therapy profile by ID

**Request:**

```
PROFILE_LOAD:1\n
```

**Parameters:**

- 1 = Regular VCR (100ms ON, 67ms OFF, no jitter)
- 2 = Noisy VCR (23.5% jitter, mirrored patterns) - **DEFAULT**
- 3 = Hybrid VCR (mixed frequency)

**Response:**

```
STATUS:LOADED
PROFILE:Noisy VCR
\x04
```

**Error:**

```
ERROR:Invalid profile ID
\x04
```

```
ERROR:Cannot modify parameters during active session
\x04
```

**Note:** Cannot load profiles during active sessions. Send `SESSION_STOP` first.

#### PROFILE_GET

Get current profile settings

**Request:**

```
PROFILE_GET\n
```

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
PATTERN:RNDP
MIRROR:True
JITTER:23.5
\x04
```

**Note:** Example shows default Noisy VCR profile settings.

**Note:** Profile changes are automatically synced from Primary to Secondary (~200ms). See "Timing & Performance" section for details.

### Session Control

#### SESSION_START

Start therapy session

**Request:**

```
SESSION_START\n
```

**Response:**

```
SESSION_STATUS:RUNNING
\x04
```

**Error:**

```
ERROR:Secondary not connected
\x04
```

```
ERROR:Battery too low
\x04
```

#### SESSION_PAUSE

Pause active session

**Request:**

```
SESSION_PAUSE\n
```

**Response:**

```
SESSION_STATUS:PAUSED
\x04
```

**Error:**

```
ERROR:No active session
\x04
```

#### SESSION_RESUME

Resume paused session

**Request:**

```
SESSION_RESUME\n
```

**Response:**

```
SESSION_STATUS:RUNNING
\x04
```

**Error:**

```
ERROR:No paused session
\x04
```

#### SESSION_STOP

Stop active session

**Request:**

```
SESSION_STOP\n
```

**Response:**

```
SESSION_STATUS:IDLE
\x04
```

#### SESSION_STATUS

Get current session status

**Request:**

```
SESSION_STATUS\n
```

**Response (Idle):**

```
SESSION_STATUS:IDLE
ELAPSED:0
TOTAL:0
PROGRESS:0
\x04
```

**Response (Running):**

```
SESSION_STATUS:RUNNING
ELAPSED:300
TOTAL:7200
PROGRESS:4
\x04
```

**Response (Paused):**

```
SESSION_STATUS:PAUSED
ELAPSED:1500
TOTAL:7200
PROGRESS:21
\x04
```

**Fields:**

- `ELAPSED` - Seconds elapsed
- `TOTAL` - Total session duration in seconds
- `PROGRESS` - Percentage (0-100)

### Custom Profile & Parameter Adjustment

#### PROFILE_CUSTOM

Set custom therapy parameters (creates custom profile on-the-fly)

**Request:**

```
PROFILE_CUSTOM:ON:0.150:OFF:0.080:FREQ:210:JITTER:10\n
```

**Valid Parameters:**

- `ON` (0.050-0.500 seconds) - Time ON duration
- `OFF` (0.020-0.200 seconds) - Time OFF duration
- `FREQ` (150-300 Hz) - Actuator frequency
- `VOLT` (1.0-3.3 V) - Actuator voltage
- `AMPMIN` (0-100 %) - Minimum amplitude
- `AMPMAX` (0-100 %) - Maximum amplitude
- `JITTER` (0-50 %) - Timing jitter percentage
- `MIRROR` (0 or 1, False/True) - Mirror patterns between hands
- `SESSION` (1-180 minutes) - Session duration

**Response:**

```
STATUS:CUSTOM_LOADED
ON:0.150
OFF:0.080
FREQ:210
JITTER:10
\x04
```

**Error:**

```
ERROR:Invalid parameter name
\x04
```

```
ERROR:Value out of range
\x04
```

```
ERROR:Cannot modify parameters during active session
\x04
```

**Note:** Only include parameters you want to change. Omitted parameters use current values.

**Example - Minimal custom profile:**

```
PROFILE_CUSTOM:ON:0.120:JITTER:15\n
```

This changes only ON and JITTER, keeping all other current settings.

#### PARAM_SET

Set individual therapy parameter (alternative to PROFILE_CUSTOM for single changes)

**Request:**

```
PARAM_SET:ON:0.150\n
```

**Response:**

```
PARAM:ON
VALUE:0.150
\x04
```

**Error:**

```
ERROR:Cannot modify parameters during active session
\x04
```

```
ERROR:Invalid parameter name
\x04
```

```
ERROR:Value out of range
\x04
```

**Note:** Use `PROFILE_CUSTOM` for multiple parameters, `PARAM_SET` for single parameter changes. Cannot modify parameters during active sessions.

### Calibration

#### CALIBRATE_START

Enter calibration mode

**Request:**

```
CALIBRATE_START\n
```

**Response:**

```
MODE:CALIBRATION
\x04
```

#### CALIBRATE_BUZZ

Test individual finger

**Request:**

```
CALIBRATE_BUZZ:0:80:500\n
```

**Parameters:**

- Finger: 0-7 (0-3 = PRIMARY glove, 4-7 = SECONDARY glove)
- Intensity: 0-100 (%)
- Duration: 50-2000 (milliseconds)

**Note:** Devices are ambidextrous. Finger indices 0-3 control the four tactors on the PRIMARY device, and 4-7 control the four tactors on the SECONDARY device. The mapping mirrors across both devices.

**Response:**

```
FINGER:0
INTENSITY:80
DURATION:500
\x04
```

**Error:**

```
ERROR:Not in calibration mode
\x04
```

```
ERROR:Invalid finger index
\x04
```

#### CALIBRATE_STOP

Exit calibration mode

**Request:**

```
CALIBRATE_STOP\n
```

**Response:**

```
MODE:NORMAL
\x04
```

### System

#### HELP

List available commands

**Request:**

```
HELP\n
```

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

#### RESTART

Reboot glove to menu mode

**Request:**

```
RESTART\n
```

**Response:**

```
STATUS:REBOOTING
\x04
```

_Note: Connection will drop after this command_

## Legacy Single-Character Commands

For backward compatibility with BLE terminal apps:

| Old | New Equivalent    | Description              |
| --- | ----------------- | ------------------------ |
| `g` | `BATTERY`         | Check battery            |
| `v` | `PROFILE_GET`     | View settings            |
| `1` | `PROFILE_LOAD:1`  | Load Regular VCR         |
| `2` | `PROFILE_LOAD:2`  | Load Noisy VCR (default) |
| `3` | `PROFILE_LOAD:3`  | Load Hybrid VCR          |
| `c` | `CALIBRATE_START` | Enter calibration        |
| `r` | `RESTART`         | Restart glove            |

**Notes:**

- Legacy `x` command (manual sync) removed - syncing is now automatic
- All legacy commands work with modern response format (KEY:VALUE with `\x04` terminator)

## Parsing Example (.NET MAUI)

```csharp
private Dictionary<string, string> dataCache = new Dictionary<string, string>();
private StringBuilder messageBuffer = new StringBuilder();

bool IsInternalMessage(string msg) {
    // Remove EOT for pattern matching
    msg = msg.Replace("\x04", "").Trim();

    return msg.StartsWith("EXECUTE_BUZZ:") ||
           msg.StartsWith("BUZZ_COMPLETE:") ||
           msg.StartsWith("PARAM_UPDATE:") ||
           msg.StartsWith("SEED:") ||
           msg == "SEED_ACK" ||
           msg == "GET_BATTERY" ||
           msg.StartsWith("BATRESPONSE:") ||
           msg == "ACK_PARAM_UPDATE";
}

void OnBleNotification(byte[] data) {
    string message = Encoding.UTF8.GetString(data);
    messageBuffer.Append(message);

    // Check for EOT
    if (messageBuffer.ToString().Contains("\x04")) {
        string completeMessage = messageBuffer.ToString();
        messageBuffer.Clear();

        // Filter internal messages
        if (IsInternalMessage(completeMessage)) {
            return; // Ignore internal device-to-device messages
        }

        // Parse app-directed response
        ProcessResponse(completeMessage);
        dataCache.Clear();
    }
}

void ProcessResponse(string message) {
    // Split into lines and parse KEY:VALUE pairs
    var lines = message.Replace("\x04", "").Split('\n', StringSplitOptions.RemoveEmptyEntries);

    foreach (var line in lines) {
        var parts = line.Trim().Split(':', 2);
        if (parts.Length == 2) {
            dataCache[parts[0]] = parts[1];
        }
    }

    // Check for error
    if (dataCache.ContainsKey("ERROR")) {
        ShowError(dataCache["ERROR"]);
        return;
    }

    // Handle specific responses
    if (dataCache.ContainsKey("BATP") && dataCache.ContainsKey("BATS")) {
        UpdateBattery(
            double.Parse(dataCache["BATP"]),
            double.Parse(dataCache["BATS"])
        );
    }
    else if (dataCache.ContainsKey("SESSION_STATUS")) {
        UpdateSessionUI(
            dataCache["SESSION_STATUS"],
            int.Parse(dataCache.GetValueOrDefault("ELAPSED", "0")),
            int.Parse(dataCache.GetValueOrDefault("TOTAL", "0")),
            int.Parse(dataCache.GetValueOrDefault("PROGRESS", "0"))
        );
    }
}
```

## Mock BLE Service (.NET MAUI Testing)

```csharp
public class MockBleService {
    public byte[] HandleCommand(string cmd) {
        cmd = cmd.Trim();
        string response = "";

        switch (cmd) {
            case "INFO":
                response = "ROLE:PRIMARY\nNAME:Primary\nFW:1.0.0\nBATP:3.72\nBATS:3.68\nSTATUS:IDLE\n\x04";
                break;

            case "BATTERY":
                response = "BATP:3.72\nBATS:3.68\n\x04";
                break;

            case "PING":
                response = "PONG\n\x04";
                break;

            case "PROFILE_LOAD:1":
                response = "STATUS:LOADED\nPROFILE:Regular VCR\n\x04";
                break;

            case "PROFILE_GET":
                response = "TYPE:LRA\nFREQ:250\nON:0.100\nOFF:0.067\n\x04";
                break;

            case "SESSION_START":
                response = "SESSION_STATUS:RUNNING\n\x04";
                break;

            case "SESSION_STATUS":
                response = "SESSION_STATUS:RUNNING\nELAPSED:300\nTOTAL:7200\nPROGRESS:4\n\x04";
                break;

            case "SESSION_STOP":
                response = "SESSION_STATUS:IDLE\n\x04";
                break;

            default:
                response = $"ERROR:Unknown command\n\x04";
                break;
        }

        return Encoding.UTF8.GetBytes(response);
    }
}
```

## Troubleshooting

### Q: Why do I see `EXECUTE_BUZZ:0` or similar messages?

**A:** These are internal device-to-device synchronization messages between Primary and Secondary gloves. Your app should filter/ignore them by checking the message prefix. All messages (including internal ones) end with `\x04` (EOT) for reliable framing. Use the filtering strategy shown in the "Message Interleaving" section to ignore internal messages based on their command patterns (EXECUTE_BUZZ, BUZZ_COMPLETE, PARAM_UPDATE, etc.).

### Q: Why can't I change profiles during therapy?

**A:** Profile changes require stopping the therapy session first for safety. Send `SESSION_STOP`, then `PROFILE_LOAD`, then `SESSION_START`.

### Q: Why does BATTERY take so long to respond?

**A:** The Primary glove must query the Secondary glove via BLE, which can take up to 1 second. This is normal. See "Timing & Performance" section for all command response times.

### Q: What happens if Secondary glove is not connected?

**A:** Most commands will work on Primary only. However, `SESSION_START` will return `ERROR:Secondary not connected` since therapy requires both gloves.

### Q: Can I send commands during an active therapy session?

**A:** Yes, but only session control commands (PAUSE, RESUME, STOP, STATUS). Profile and parameter changes are blocked during active sessions.

### Q: How do I know if a command succeeded?

**A:** All successful responses include relevant data and end with `\x04`. Errors start with `ERROR:` and also end with `\x04`.

### Q: What's the maximum command rate?

**A:** Wait 100ms between commands for reliable processing (max 10 commands/second). See "Timing & Performance" section for complete timing guidelines and best practices.

## Error Codes

All errors follow the format: `ERROR:description\n\x04`

**Common Errors:**

- `ERROR:Unknown command`
- `ERROR:Invalid profile ID`
- `ERROR:Secondary not connected`
- `ERROR:Battery too low`
- `ERROR:No active session`
- `ERROR:No paused session`
- `ERROR:Cannot modify parameters during active session`
- `ERROR:Invalid parameter name`
- `ERROR:Value out of range`
- `ERROR:Not in calibration mode`
- `ERROR:Invalid finger index`

## Implementation Checklist

### Glove Firmware ✅ COMPLETE

- [x] Add new command parser (multi-char commands)
- [x] Implement KEY:VALUE response formatter
- [x] Add `\x04` EOT terminator to all responses
- [x] Implement all 18 commands
- [x] Add session state management
- [x] Add PRIMARY→Secondary auto-sync (session commands + parameters)
- [x] Integrate session manager with VCR engine
- [x] Implement command-driven synchronization (EXECUTE_BUZZ)
- [x] Set Noisy VCR as default profile (JITTER=23.5, MIRROR=True)
- [x] Hardware integration testing (8/18 commands tested - see Test Coverage below)

### .NET MAUI App

- [ ] Implement BLE command sender
- [ ] Parse KEY:VALUE responses
- [ ] Detect `\x04` EOT character (all messages use EOT)
- [ ] Implement internal message filtering (pattern-based)
- [ ] Handle error responses
- [ ] Create mock BLE service for testing
- [ ] Wire up UI to commands
- [ ] Test all command flows

### Test Coverage

**Hardware Integration Tests:** Manual testing on actual Feather nRF52840 devices via BLE.

**Commands Tested (8/18 = 44%):**

| Command | Status | Test File |
|---------|--------|-----------|
| CALIBRATE_START | ✅ Tested | tests/calibrate_commands_test.py |
| CALIBRATE_BUZZ | ✅ Tested | tests/calibrate_commands_test.py |
| CALIBRATE_STOP | ✅ Tested | tests/calibrate_commands_test.py |
| SESSION_START | ✅ Tested | tests/session_commands_test.py |
| SESSION_PAUSE | ✅ Tested | tests/session_commands_test.py |
| SESSION_RESUME | ✅ Tested | tests/session_commands_test.py |
| SESSION_STOP | ✅ Tested | tests/session_commands_test.py |
| SESSION_STATUS | ✅ Tested | tests/session_commands_test.py |

**Commands NOT Tested (10/18 = 56%):**

- INFO, BATTERY, PING (device information)
- PROFILE_LIST, PROFILE_LOAD, PROFILE_GET, PROFILE_CUSTOM (profile management)
- PARAM_SET (parameter adjustment)
- HELP, RESTART (system commands)

**Testing Approach:**
- Manual execution with BLE UART connection
- EOT terminator validation (`\x04`)
- Response format verification (KEY:VALUE pairs)
- Timing validation (commands complete within timeout)

**See:** [Testing Guide](TESTING.md) for detailed test procedures and results.

---

## Summary

**Complete command count:** 18 commands
**Estimated bandwidth:** ~50% reduction vs tagged format (shorthand parameters)
**Response parsing:** Simple split on `:` and detect `\x04` (EOT)
**Timing:** See dedicated "Timing & Performance" section for all timing specs
**Auto-sync:** Profile changes automatically sync Primary → Secondary (~200ms)
**Custom profiles:** Use `PROFILE_CUSTOM` to send dynamic therapy parameters from phone
**Default profile:** Noisy VCR (23.5% jitter, mirrored patterns) for optimal therapeutic effect

### Synchronization Architecture

The gloves use **command-driven synchronization** for precise tactor timing:

- **BlueBuzzah (PRIMARY)** sends `EXECUTE_BUZZ:N` commands to **BlueBuzzah-Secondary (SECONDARY)** before each buzz
- **Secondary** waits for explicit commands before activating tactors (blocking receive)
- **Secondary** sends `BUZZ_COMPLETE:N` acknowledgment after each buzz
- **Zero drift:** Command-driven (not time-based) ensures no accumulated timing errors

This ensures tactors fire simultaneously on both hands with no drift over time.

**For detailed timing specifications (synchronization accuracy, BLE latency, cycle rates), see the "Timing & Performance" section.**

**Impact on Mobile App:**

- Your app may receive internal `EXECUTE_BUZZ`, `BUZZ_COMPLETE`, and other sync messages during therapy
- All messages (including internal ones) end with `\x04` terminator for reliable framing
- Implement filtering based on message patterns (see "Message Interleaving" section)
- Session control commands (PAUSE/RESUME/STOP) work correctly during therapy
