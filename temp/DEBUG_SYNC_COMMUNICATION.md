# DEBUG: BLE Sync Communication Tracing

## Problem
SECONDARY device connects to PRIMARY successfully but NEVER receives sync commands. The PRIMARY appears to send commands, but the SECONDARY just waits forever without receiving anything.

## Root Cause Analysis

The original code had **silent failure modes** at multiple points in the BLE communication chain:

1. **BLE send() method** - Caught all exceptions with bare `except:` and returned `False` without logging
2. **send_sync_callback** - Only logged errors when `DEBUG_ENABLED` was True
3. **Receive processing** - Limited logging of incoming messages
4. **Sync command handling** - Minimal logging of received commands

## Changes Made

### 1. Enhanced BLE Send Logging (`src/ble.py`)

**Location**: `BLEManager.send()` method (lines 230-258)

**Added logging**:
- `[BLE] SEND FAILED: Connection 'X' not found` - Connection ID not in `_connections` dict
- `[BLE] Available connections: [...]` - Shows which connection IDs exist
- `[BLE] SEND FAILED: Connection 'X' not connected` - Connection exists but not active
- `[BLE] Sending to 'X': 'message...'` - Shows first 100 chars of message being sent
- `[BLE] Send SUCCESS to 'X'` - UART write succeeded
- `[BLE] SEND EXCEPTION on 'X': error` - Shows actual exception with traceback

**What to look for**:
```
[BLE] Sending to 'secondary-01': 'SYNC:START_SESSION:duration_sec|7200|pattern_type|rndp...'
[BLE] Send SUCCESS to 'secondary-01'
```

If you see `SEND FAILED` or `SEND EXCEPTION`, that's the smoking gun.

### 2. Enhanced Send Sync Callback (`src/app.py`)

**Location**: `send_sync_to_secondary()` function in `_initialize_application()` (lines 422-456)

**Added logging**:
- `[SYNC] send_sync_to_secondary() CALLED: type=X, data={...}` - Callback invoked
- `[SYNC] SKIPPED: Not PRIMARY (role=X)` - Wrong device role
- `[SYNC] SKIPPED: No SECONDARY connection` - `self.secondary_connection` is None
- `[SYNC] Calling ble.send('conn_id', 'message...')` - About to call BLE send
- `[SYNC] ble.send() returned SUCCESS` - Send returned True
- `[SYNC] ble.send() returned FAILURE` - Send returned False
- `[SYNC] EXCEPTION in send_sync_to_secondary: error` - Exception with traceback

**What to look for on PRIMARY**:
```
[PRIMARY] [SYNC] send_sync_to_secondary() CALLED: type=START_SESSION, data={'duration_sec': 7200, ...}
[PRIMARY] [SYNC] Calling ble.send('secondary-01', 'SYNC:START_SESSION:...')
[PRIMARY] [SYNC] ble.send() returned SUCCESS
```

If callback is never called, the SessionManager isn't invoking it.
If `SKIPPED` appears, connection setup failed.
If `FAILURE` appears, BLE send is broken.

### 3. Enhanced SessionManager Sync Logging (`src/application/session/manager.py`)

**Location**: `start_session()` method (lines 266-282)

**Added logging**:
- `[SessionManager] _send_sync_callback IS SET - preparing to send START_SESSION`
- `[SessionManager] CALLING _send_sync_callback('START_SESSION', {...})`
- `[SessionManager] _send_sync_callback RETURNED - START_SESSION sent to SECONDARY`
- `[SessionManager] WARNING: _send_sync_callback is NOT SET - cannot send to SECONDARY`

**What to look for on PRIMARY**:
```
[SessionManager] _send_sync_callback IS SET - preparing to send START_SESSION
[SessionManager] CALLING _send_sync_callback('START_SESSION', {'duration_sec': 7200, ...})
[SessionManager] _send_sync_callback RETURNED - START_SESSION sent to SECONDARY
```

If `WARNING: _send_sync_callback is NOT SET` appears, the callback wasn't registered during initialization.

### 4. Enhanced Message Reception (`src/app.py`)

**Location**: `_process_connection_messages()` method (lines 1029-1060)

**Added logging**:
- `[RX] Received from primary: 'message...'` - Shows first 100 chars of received message
- `[RX] SYNC command detected, calling _handle_sync_command()` - SYNC message recognized
- `[RX] Non-SYNC message, calling menu.handle_command()` - Regular command
- `[RX] Response queued: 'response...'` - Response prepared for sending
- `[RX] EXCEPTION processing X message: error` - Exception with traceback

**What to look for on SECONDARY**:
```
[SECONDARY] [RX] Received from primary: 'SYNC:START_SESSION:duration_sec|7200|pattern_type|rndp...'
[SECONDARY] [RX] SYNC command detected, calling _handle_sync_command()
```

If no `[RX] Received` messages appear, the SECONDARY isn't receiving anything from PRIMARY.

### 5. Enhanced Sync Command Handling (`src/app.py`)

**Location**: `_handle_sync_command()` method (lines 1062-1084)

**Added logging**:
- `[SYNC_RX] _handle_sync_command() CALLED with: 'message'` - Handler invoked with full message
- `[SYNC_RX] ERROR: Invalid SYNC command format` - Message parsing failed
- `[SYNC_RX] Parsed: command_type='X', data_str='...'` - Successfully parsed command

**What to look for on SECONDARY**:
```
[SECONDARY] [SYNC_RX] _handle_sync_command() CALLED with: 'SYNC:START_SESSION:duration_sec|7200...'
[SECONDARY] [SYNC_RX] Parsed: command_type='START_SESSION', data_str='duration_sec|7200|pattern_type|rndp...'
```

If parsing fails, the message format is corrupted.

## Expected Log Flow (Happy Path)

### PRIMARY Side (when session starts)
```
[SessionManager] _send_sync_callback IS SET - preparing to send START_SESSION
[SessionManager] CALLING _send_sync_callback('START_SESSION', {'duration_sec': 7200, 'pattern_type': 'rndp', ...})
[PRIMARY] [SYNC] send_sync_to_secondary() CALLED: type=START_SESSION, data={'duration_sec': 7200, ...}
[PRIMARY] [SYNC] Calling ble.send('secondary-01', 'SYNC:START_SESSION:duration_sec|7200|pattern_type|rndp...')
[BLE] Sending to 'secondary-01': 'SYNC:START_SESSION:duration_sec|7200|pattern_type|rndp|time_on_ms|100...'
[BLE] Send SUCCESS to 'secondary-01'
[PRIMARY] [SYNC] ble.send() returned SUCCESS
[SessionManager] _send_sync_callback RETURNED - START_SESSION sent to SECONDARY
[SessionManager] Starting therapy engine with session parameters
```

### SECONDARY Side (when receiving START_SESSION)
```
[SECONDARY] Entering SECONDARY main loop
[SECONDARY] Waiting for PRIMARY sync commands
[SECONDARY] [RX] Received from primary: 'SYNC:START_SESSION:duration_sec|7200|pattern_type|rndp|time_on_ms|100...'
[SECONDARY] [RX] SYNC command detected, calling _handle_sync_command()
[SECONDARY] [SYNC_RX] _handle_sync_command() CALLED with: 'SYNC:START_SESSION:duration_sec|7200|pattern_type|rndp...'
[SECONDARY] [SYNC_RX] Parsed: command_type='START_SESSION', data_str='duration_sec|7200|pattern_type|rndp|time_on_ms|100...'
[SECONDARY] [DEBUG] SYNC: Start session
[SECONDARY] [DEBUG] Session config: duration=7200s, pattern=rndp, jitter=23.5%
[SECONDARY] SECONDARY: Therapy session started
```

## Diagnostic Checklist

Run both devices and check the logs:

### Step 1: Is the callback being registered?
**Look for on PRIMARY during boot**:
- ✅ `[PRIMARY] [DEBUG] Session manager initialized`
- ❌ `[SessionManager] WARNING: _send_sync_callback is NOT SET`

If callback is not set, check `_initialize_application()` in `src/app.py` line 441.

### Step 2: Is the callback being called?
**Look for on PRIMARY when session starts**:
- ✅ `[SessionManager] CALLING _send_sync_callback('START_SESSION', ...)`
- ❌ No message = SessionManager isn't calling it

If not called, check SessionManager.start_session() in `src/application/session/manager.py`.

### Step 3: Is the BLE send succeeding?
**Look for on PRIMARY**:
- ✅ `[BLE] Send SUCCESS to 'secondary-01'`
- ❌ `[BLE] SEND FAILED: Connection 'secondary-01' not found`
- ❌ `[BLE] SEND FAILED: Connection 'secondary-01' not connected`
- ❌ `[BLE] SEND EXCEPTION on 'secondary-01': ...`

If send fails, check:
- Is `self.secondary_connection` set correctly during boot?
- Did BLE connection actually succeed?
- Is the connection still active?

### Step 4: Is the SECONDARY receiving?
**Look for on SECONDARY**:
- ✅ `[SECONDARY] [RX] Received from primary: 'SYNC:START_SESSION:...'`
- ❌ No message = BLE receive failing or no data being sent

If not receiving:
- Check if PRIMARY send succeeded (Step 3)
- Check if SECONDARY is calling `_process_incoming_ble_messages()` in main loop
- Check if `self.primary_connection` is set on SECONDARY

### Step 5: Is the SYNC command being parsed?
**Look for on SECONDARY**:
- ✅ `[SECONDARY] [SYNC_RX] Parsed: command_type='START_SESSION', data_str='...'`
- ❌ `[SECONDARY] [SYNC_RX] ERROR: Invalid SYNC command format`

If parsing fails, check message format and EOT handling.

## Common Failure Scenarios

### Scenario A: Callback never called
**Symptoms**: No `[SessionManager] CALLING _send_sync_callback` on PRIMARY

**Causes**:
1. `_send_sync_callback` is None (not registered)
2. SessionManager.start_session() not being called
3. Condition `if self._send_sync_callback:` is False

**Fix**: Check callback registration in `_initialize_application()`

### Scenario B: BLE send fails with "not found"
**Symptoms**: `[BLE] SEND FAILED: Connection 'X' not found`

**Causes**:
1. `self.secondary_connection` has wrong connection ID
2. BLE connection was never established
3. Connection was disconnected

**Fix**: Check BLE connection during boot sequence

### Scenario C: BLE send fails with "not connected"
**Symptoms**: `[BLE] SEND FAILED: Connection 'X' not connected`

**Causes**:
1. Connection dropped after initial connect
2. `is_connected()` check returning False incorrectly

**Fix**: Check connection health monitoring

### Scenario D: SECONDARY not receiving
**Symptoms**: PRIMARY shows `Send SUCCESS` but SECONDARY has no `[RX] Received`

**Causes**:
1. SECONDARY not calling `_process_incoming_ble_messages()`
2. `self.primary_connection` not set on SECONDARY
3. BLE receive timeout too short
4. EOT framing issue

**Fix**: Check SECONDARY main loop and connection setup

## Files Modified

1. `/Users/rbonestell/Development/BlueBuzzah2-Firmware/src/ble.py`
   - Enhanced `send()` method with comprehensive logging

2. `/Users/rbonestell/Development/BlueBuzzah2-Firmware/src/app.py`
   - Enhanced `send_sync_to_secondary()` callback
   - Enhanced `_process_connection_messages()` receive logging
   - Enhanced `_handle_sync_command()` parsing logging

3. `/Users/rbonestell/Development/BlueBuzzah2-Firmware/src/application/session/manager.py`
   - Enhanced `start_session()` sync callback logging

## Next Steps

1. **Deploy to both devices** with these logging changes
2. **Run PRIMARY** and capture full boot sequence
3. **Run SECONDARY** and capture full boot sequence
4. **Start session on PRIMARY** (either via phone or default therapy)
5. **Review logs** following the diagnostic checklist above
6. **Identify failure point** using the expected log flow
7. **Report findings** with specific log excerpts showing where the flow breaks

The enhanced logging will pinpoint EXACTLY where the communication fails.
