# BlueBuzzah Synchronization Protocol
**Version:** 2.0.0 (Command-Driven Architecture)
**Date:** 2025-01-23

---

## Table of Contents

1. [Protocol Overview](#protocol-overview)
2. [BLE Connection Establishment](#ble-connection-establishment)
3. [Time Synchronization](#time-synchronization)
4. [Command-Driven Execution](#command-driven-execution)
5. [Parameter Synchronization](#parameter-synchronization)
6. [Multi-Connection Support](#multi-connection-support)
7. [Error Recovery](#error-recovery)
8. [Timing Analysis](#timing-analysis)
9. [Message Catalog](#message-catalog)

---

### Terminology Note

This document uses the following device role terminology:
- **PRIMARY** (also known as VL, left glove): Initiates therapy, controls timing
- **SECONDARY** (also known as VR, right glove): Follows PRIMARY commands

Code examples may show `"VL"` as the BLE advertisement name for backward compatibility.

---

## Protocol Overview

### Core Principles

1. **PRIMARY commands, SECONDARY obeys**: PRIMARY sends explicit commands before every action
2. **Blocking waits**: SECONDARY blocks on `receive_execute_buzz()` until PRIMARY commands
3. **Acknowledgments**: SECONDARY confirms completion with `BUZZ_COMPLETE`
4. **Safety timeout**: SECONDARY halts therapy if PRIMARY disconnects (10s timeout)

### Synchronization Accuracy

| Metric | Value | Source |
|--------|-------|--------|
| BLE latency | 7.5ms (nominal) | BLE connection interval |
| BLE latency | 21ms (compensated) | Measured +21ms offset |
| Execution jitter | ±10-20ms | Processing overhead |
| **Total bilateral sync** | **±7.5-20ms** | Command receipt to motor activation |

**Acceptable for therapy?** YES
- Human temporal resolution: ~20-40ms
- vCR therapy tolerance: <50ms bilateral lag
- Observed performance: 7.5-20ms well within spec

---

## BLE Connection Establishment

### Phase 1: Initial Connection (Legacy Single Connection)

**PRIMARY Sequence** (`ble_connection.py:291-405`):

```python
# 1. Create UART service and advertise
uart = UARTService()
advertisement = ProvideServicesAdvertisement(uart)
advertisement.complete_name = "VL"
ble.start_advertising(advertisement, interval=0.5)

# 2. Wait for connection (15 second timeout)
while time.monotonic() - start < CONNECTION_TIMEOUT:
    if len(ble.connections) > 0:
        connection = ble.connections[-1]  # Most recent
        break

# 3. Optimize BLE parameters
connection.connection_interval = 7.5  # ms

# 4. Wait for READY signal (8 second timeout)
while time.monotonic() - start < 8:
    if uart.in_waiting:
        message = uart.readline().decode().strip()
        if message == "READY":
            ready_received = True
            break

# 5. Send FIRST_SYNC with retry (3 attempts)
timestamp = int(time.monotonic() * 1000)
uart.write(f"FIRST_SYNC:{timestamp}\n")

# 6. Wait for ACK (0.5s timeout per attempt)
if uart.in_waiting:
    message = uart.readline().decode().strip()
    if message == "ACK":
        ack_received = True

# 7. Send VCR_START command
uart.write("VCR_START\n")
```

**SECONDARY Sequence** (`ble_connection.py:407-514`):

```python
# 1. Scan for "VL" advertisement (15 second timeout)
while time.monotonic() - start < CONNECTION_TIMEOUT:
    for adv in ble.start_scan(timeout=0.1, interval=0.0075, window=0.0075):
        if adv.complete_name == "VL":
            connection = ble.connect(adv)
            break

# 2. Get UART service
uart = connection[UARTService]

# 3. Send READY signal
uart.write("READY\n")

# 4. Wait for FIRST_SYNC
while True:
    message = uart.readline().decode().strip()
    if message.startswith("FIRST_SYNC:"):
        received_timestamp = int(message.split(":")[1])
        current_time = int(time.monotonic() * 1000)

        # Apply BLE latency compensation (+21ms)
        adjusted_sync_time = received_timestamp + 21
        applied_time_shift = adjusted_sync_time - current_time
        initial_time_offset = current_time + applied_time_shift

        # Store for future sync corrections
        self.initial_time_offset = initial_time_offset

        uart.write("ACK\n")
        break

# 5. Wait for VCR_START
while True:
    message = uart.readline().decode().strip()
    if message == "VCR_START":
        return True  # Ready for therapy
```

**Message Flow Diagram:**

```mermaid
sequenceDiagram
    participant VL as VL (PRIMARY)
    participant VR as VR (SECONDARY)

    Note over VL: Power on
    VL->>VL: Advertise as "VL"<br/>interval=500ms

    Note over VR: Power on (within 15s)
    VR->>VR: Scan for "VL"<br/>interval=7.5ms

    VR->>VL: Connect
    Note over VL,VR: BLE Connection Established<br/>interval=7.5ms, latency=0, timeout=100ms

    VR->>VL: READY

    VL->>VR: FIRST_SYNC:12345
    Note over VR: Apply +21ms BLE compensation<br/>Calculate time offset

    VR->>VL: ACK

    VL->>VR: VCR_START
    Note over VL,VR: Therapy Session Begins
```

**Timing Breakdown:**

| Step | Duration | Notes |
|------|----------|-------|
| Advertisement start | 0.1s | Immediate |
| VR scan window | 0-15s | Until "VL" found |
| Connection establishment | 0.5-2s | BLE handshake |
| READY signal | <0.1s | Single message |
| FIRST_SYNC handshake | 0.5-1.5s | 3 retry attempts |
| VCR_START | <0.1s | Single message |
| **Total connection time** | **2-20s** | Typical: 5-10s |

### Phase 2: Multi-Connection Detection (PRIMARY Only)

**New Feature** (code.py:90-132):

PRIMARY supports **simultaneous connections** to phone + VR. Connection detection identifies device types by analyzing first message received.

**Detection Logic** (`ble_connection.py:92-150`):

```python
def _detect_connection_type(connection, timeout=3.0):
    """
    Identify connection as PHONE or VR by first message received.

    Phone sends: INFO, PING, BATTERY, PROFILE, SESSION commands
    VR sends: READY (immediately after connecting)

    Returns: "PHONE", "VR", or None
    """
    if UARTService not in connection:
        return None

    uart = connection[UARTService]
    timeout_end = time.monotonic() + timeout

    while time.monotonic() < timeout_end:
        if uart.in_waiting:
            message = uart.readline().decode().strip()

            if message == "READY":
                return "VR"

            phone_commands = ["INFO", "PING", "BATTERY", "PROFILE", "SESSION", "HELP", "PARAM"]
            if any(cmd in message for cmd in phone_commands):
                return "PHONE"

        time.sleep(0.1)

    return None  # Timeout - unknown device
```

**Connection Assignment** (`ble_connection.py:152-196`):

```python
def _assign_connections_by_type(typed_connections):
    """
    Assign connections to phone_uart/vr_uart based on detected types.

    Args:
        typed_connections: [(connection, "PHONE"), (connection, "VR"), ...]

    Returns:
        {"phone": bool, "vr": bool}
    """
    for conn, conn_type in typed_connections:
        if conn_type == "PHONE":
            self.phone_connection = conn
            self.phone_uart = conn[UARTService]
            conn.connection_interval = 7.5

        elif conn_type == "VR":
            self.vr_connection = conn
            self.vr_uart = conn[UARTService]
            conn.connection_interval = 7.5
```

**Multi-Connection Scenarios** (code.py:110-131):

```python
# Scenario 1: Both phone and VR connected during startup
if has_phone and has_vr:
    connection_success = ble._complete_vr_handshake()

# Scenario 2: Phone only - wait for VR
elif has_phone and not has_vr:
    connection_success = ble._scan_for_vr_while_advertising()
    if connection_success:
        connection_success = ble._complete_vr_handshake()

# Scenario 3: VR only - proceed without phone
elif has_vr and not has_phone:
    connection_success = ble._complete_vr_handshake()

# Scenario 4: Unknown devices - cannot proceed
else:
    connection_success = False
```

**VR Handshake** (`ble_connection.py:644-724`):

```python
def _complete_vr_handshake():
    """Complete READY → SYNC → ACK → VCR_START handshake"""

    # 1. Wait for READY (8s timeout)
    while time.monotonic() - start < 8:
        if vr_uart.in_waiting:
            message = vr_uart.readline().decode().strip()
            if message == "READY":
                ready_received = True
                break

    # 2. Send FIRST_SYNC (3 retry attempts)
    timestamp = int(time.monotonic() * 1000)
    vr_uart.write(f"FIRST_SYNC:{timestamp}\n")

    # 3. Wait for ACK (0.5s timeout per attempt)
    if vr_uart.in_waiting:
        message = vr_uart.readline().decode().strip()
        if message == "ACK":
            ack_received = True

    # 4. Send VCR_START
    vr_uart.write("VCR_START\n")

    return True
```

---

## Time Synchronization

### Initial Sync (FIRST_SYNC)

**Purpose**: Establish common time reference between gloves

**PRIMARY Sends** (ble_connection.py:375-405):
```python
timestamp = int(time.monotonic() * 1000)  # Milliseconds
sync_message = f"FIRST_SYNC:{timestamp}\n"
uart.write(sync_message.encode())
```

**SECONDARY Receives** (ble_connection.py:475-502):
```python
# 1. Extract PRIMARY timestamp
received_timestamp = int(message.split(":")[1])  # e.g., 12345

# 2. Get local timestamp
current_secondary_time = int(time.monotonic() * 1000)  # e.g., 12320

# 3. Apply BLE latency compensation (+21ms)
adjusted_sync_time = received_timestamp + 21  # 12345 + 21 = 12366

# 4. Calculate time shift
applied_time_shift = adjusted_sync_time - current_secondary_time
# 12366 - 12320 = +46ms

# 5. Store offset for future corrections
initial_time_offset = current_secondary_time + applied_time_shift
# 12320 + 46 = 12366

# 6. Send acknowledgment
uart.write("ACK\n")
```

**Why +21ms compensation?**
- Measured BLE transmission latency: 7.5-35ms
- Average observed latency: 21ms
- Applied as fixed offset for deterministic sync

**Sync Accuracy:**
```
PRIMARY sends at:     T0 = 12345ms
BLE transmission:     +21ms
SECONDARY receives:   T1 = 12366ms (adjusted)
SECONDARY clock:      12320ms (before adjustment)
Applied offset:       +46ms
```

### Periodic Sync (SYNC_ADJ) - Legacy/Optional

**Status**: Still implemented but **optional** for diagnostics

**PRIMARY Sends** (sync_protocol.py:43-88):
```python
# Every 10th buzz cycle (optional timing check)
if buzz_cycle_count % 10 == 0:
    sync_adj_timestamp = int(time.monotonic() * 1000)
    uart.write(f"SYNC_ADJ:{sync_adj_timestamp}\n")

    # Wait for ACK (2 second timeout)
    while time.monotonic() - start < 2:
        if uart.in_waiting:
            response = uart.readline().decode().strip()
            if response == "ACK_SYNC_ADJ":
                ack_received = True
                break

    # Send start signal
    uart.write("SYNC_ADJ_START\n")
```

**SECONDARY Receives** (sync_protocol.py:91-138):
```python
if message.startswith("SYNC_ADJ:"):
    received_timestamp = int(message.split(":")[1])

    # Calculate adjusted time using initial offset
    current_secondary_time = int(time.monotonic() * 1000)
    adjusted_secondary_time = received_timestamp + \
        (current_secondary_time - initial_time_offset)
    offset = adjusted_secondary_time - received_timestamp

    # Send ACK
    uart.write("ACK_SYNC_ADJ\n")

elif message == "SYNC_ADJ_START":
    # Ready signal received
    pass
```

**Note**: SYNC_ADJ is **not required** for command-driven synchronization. It's kept for:
- Monitoring clock drift during long sessions
- Debugging timing issues
- Future time-based coordination features

---

## Command-Driven Execution

### EXECUTE_BUZZ Protocol

**Core Synchronization Mechanism** - Introduced 2025-01-09

**PRIMARY Sends** (sync_protocol.py:171-194):
```python
def send_execute_buzz(uart_service, sequence_index):
    """
    Command SECONDARY to execute buzz sequence.

    Args:
        sequence_index: 0, 1, or 2 (three buzzes per macrocycle)

    Returns:
        bool: True if sent successfully
    """
    command = f"EXECUTE_BUZZ:{sequence_index}\n"
    uart_service.write(command)
    print(f"[VL] Sent EXECUTE_BUZZ:{sequence_index}")
    return True
```

**SECONDARY Receives (BLOCKING)** (sync_protocol.py:197-244):
```python
def receive_execute_buzz(uart_service, timeout_sec=10.0):
    """
    Wait for EXECUTE_BUZZ command from PRIMARY.

    THIS IS A BLOCKING CALL - VR will not proceed until command received.

    Args:
        timeout_sec: Maximum wait time (default 10s)

    Returns:
        int: Sequence index (0-2) or None on timeout
    """
    start_wait = time.monotonic()

    while (time.monotonic() - start_wait) < timeout_sec:
        if uart_service.in_waiting:
            message = uart_service.readline().decode().strip()

            if message.startswith("EXECUTE_BUZZ:"):
                sequence_index = int(message.split(":")[1])
                return sequence_index

            # Special case: Handle battery queries during therapy
            elif message == "GET_BATTERY":
                _handle_battery_query_inline(uart_service)
                # Continue waiting for EXECUTE_BUZZ

        time.sleep(0.001)  # 1ms polling interval

    # TIMEOUT - PRIMARY likely disconnected
    return None
```

**VR Safety Timeout** (vcr_engine.py:202-218):
```python
received_idx = receive_execute_buzz(uart_service, timeout_sec=10.0)

if received_idx is None:
    # PRIMARY disconnected - HALT THERAPY
    print(f"[VR] [ERROR] EXECUTE_BUZZ timeout! PRIMARY disconnected.")
    print(f"[VR] Stopping all motors for safety...")

    haptic.all_off()  # Immediate motor shutoff
    pixels.fill((255, 0, 0))  # Red error indicator

    # Infinite loop to prevent restart
    while True:
        pixels.fill((255, 0, 0))
        time.sleep(0.5)
        pixels.fill((0, 0, 0))
        time.sleep(0.5)
```

### BUZZ_COMPLETE Acknowledgment

**SECONDARY Sends** (sync_protocol.py:269-291):
```python
def send_buzz_complete(uart_service, sequence_index):
    """
    Confirm buzz sequence completed.

    Args:
        sequence_index: Index just completed (0-2)

    Returns:
        bool: True if sent successfully
    """
    command = f"BUZZ_COMPLETE:{sequence_index}\n"
    uart_service.write(command)
    print(f"[VR] Sent BUZZ_COMPLETE:{sequence_index}")
    return True
```

**PRIMARY Receives** (sync_protocol.py:294-337):
```python
def receive_buzz_complete(uart_service, expected_index, timeout_sec=3.0):
    """
    Wait for BUZZ_COMPLETE acknowledgment from SECONDARY.

    Args:
        expected_index: Expected sequence index for validation
        timeout_sec: Maximum wait time (default 3s)

    Returns:
        bool: True if ACK received with correct index
    """
    start_wait = time.monotonic()

    while (time.monotonic() - start_wait) < timeout_sec:
        if uart_service.in_waiting:
            message = uart_service.readline().decode().strip()

            if message.startswith("BUZZ_COMPLETE:"):
                sequence_index = int(message.split(":")[1])

                if sequence_index == expected_index:
                    return True  # Success
                else:
                    print(f"[VL] [WARNING] Index mismatch: expected {expected_index}, got {sequence_index}")

        time.sleep(0.001)  # 1ms polling interval

    # TIMEOUT - Log warning but continue
    print(f"[VL] [WARNING] BUZZ_COMPLETE timeout for sequence {expected_index}")
    return False
```

### Therapy Loop Integration

**PRIMARY Execution** (vcr_engine.py:185-200):
```python
for sequence_idx in range(3):  # Three buzzes per macrocycle
    # 1. Send command to VR
    send_execute_buzz(uart_service, sequence_idx)

    # 2. Execute local buzz immediately
    haptic.buzz_sequence(rndp_sequence())

    # 3. Wait for VR acknowledgment (non-blocking)
    ack_received = receive_buzz_complete(uart_service, sequence_idx, timeout_sec=3.0)
    if not ack_received:
        print(f"[VL] [WARNING] No BUZZ_COMPLETE from VR for sequence {sequence_idx}")
        # Continue anyway - PRIMARY maintains its own operation
```

**SECONDARY Execution** (vcr_engine.py:202-227):
```python
for sequence_idx in range(3):
    # 1. Wait for command from VL (BLOCKING)
    received_idx = receive_execute_buzz(uart_service, timeout_sec=10.0)

    if received_idx is None:
        # TIMEOUT - VL disconnected, halt therapy
        print(f"[VR] [ERROR] EXECUTE_BUZZ timeout! VL disconnected.")
        haptic.all_off()
        pixels.fill((255, 0, 0))
        # Enter infinite error loop
        while True:
            pass

    if received_idx != sequence_idx:
        print(f"[VR] [WARNING] Sequence mismatch: expected {sequence_idx}, got {received_idx}")

    # 2. Execute buzz sequence
    haptic.buzz_sequence(rndp_sequence())

    # 3. Send acknowledgment
    send_buzz_complete(uart_service, sequence_idx)
```

**Synchronization Guarantee:**
- VR **blocks** until VL commands
- VL executes **immediately** after sending command
- BLE latency: 7.5ms (connection interval)
- Processing overhead: ~5-10ms
- **Total lag**: VR buzzes 7.5-20ms after VL
- **Acceptable**: Well within human temporal resolution (20-40ms)

---

## Parameter Synchronization

### Protocol: PARAM_UPDATE

**When Triggered:**
1. **PROFILE_LOAD**: Broadcast ALL parameters (menu_controller.py:638)
2. **PROFILE_CUSTOM**: Broadcast changed parameters only (menu_controller.py:690)
3. **PARAM_SET**: Broadcast single parameter (menu_controller.py:964)

**PRIMARY Sends** (menu_controller.py:894-931):
```python
def _broadcast_param_update(params_dict):
    """
    Broadcast parameter update to VR.

    Protocol: PARAM_UPDATE:KEY1:VALUE1:KEY2:VALUE2:...\n

    Example:
        params_dict = {"TIME_ON": 0.150, "TIME_OFF": 0.080, "JITTER": 10}
        → PARAM_UPDATE:TIME_ON:0.150:TIME_OFF:0.080:JITTER:10\n
    """
    cmd_parts = ["PARAM_UPDATE"]

    for key, value in params_dict.items():
        cmd_parts.append(str(key))
        if isinstance(value, float):
            cmd_parts.append(f"{value:.3f}")
        else:
            cmd_parts.append(str(value))

    cmd_string = ":".join(cmd_parts) + "\n"
    vr_uart.write(cmd_string.encode())
    print(f"[VL] [SYNC] Broadcast {len(params_dict)} parameter(s) to VR")
```

**SECONDARY Receives** (menu_controller.py:844-892):
```python
def _handle_param_update(args):
    """
    Apply parameter update from VL.

    Args:
        args: ["KEY1", "VALUE1", "KEY2", "VALUE2", ...]

    Example:
        PARAM_UPDATE:TIME_ON:0.150:TIME_OFF:0.080:JITTER:10
        args = ["TIME_ON", "0.150", "TIME_OFF", "0.080", "JITTER", "10"]
    """
    # 1. Parse args into key:value dict
    params_dict = parse_kvp_params(args)

    # 2. Validate and apply via ProfileManager
    success, message, applied_params = profile_manager.apply_custom_params(params_dict)

    if success:
        print(f"[VR] [SYNC] Applied {len(applied_params)} parameter(s) from VL:")
        for key, value in applied_params.items():
            print(f"[VR]   {key} = {value}")

        # 3. Send acknowledgment (optional, for debugging)
        ble.uart.write("ACK_PARAM_UPDATE\n")
    else:
        print(f"[VR] [ERROR] Failed to apply parameters: {message}")
```

### Synchronization Scenarios

**Scenario 1: PROFILE_LOAD** (ALL parameters)
```
Phone → VL: PROFILE_LOAD:2\n
VL: <loads Noisy VCR profile>
VL → Phone: STATUS:LOADED\nPROFILE:Noisy VCR\n\x04
VL → VR: PARAM_UPDATE:ACTUATOR_TYPE:LRA:ACTUATOR_FREQUENCY:250:...\n
VR: <applies all 11 parameters>
VR → VL: ACK_PARAM_UPDATE\n (optional)
```

**Scenario 2: PROFILE_CUSTOM** (changed parameters only)
```
Phone → VL: PROFILE_CUSTOM:TIME_ON:0.150:JITTER:10\n
VL: <updates 2 parameters>
VL → Phone: STATUS:CUSTOM_LOADED\nTIME_ON:0.150\nJITTER:10\n\x04
VL → VR: PARAM_UPDATE:TIME_ON:0.150:JITTER:10\n
VR: <applies 2 parameters>
VR → VL: ACK_PARAM_UPDATE\n (optional)
```

**Scenario 3: PARAM_SET** (single parameter)
```
Phone → VL: PARAM_SET:TIME_ON:0.150\n
VL: <updates 1 parameter>
VL → Phone: PARAM:TIME_ON\nVALUE:0.150\n\x04
VL → VR: PARAM_UPDATE:TIME_ON:0.150\n
VR: <applies 1 parameter>
VR → VL: ACK_PARAM_UPDATE\n (optional)
```

### Validation and Error Handling

**PRIMARY Validation** (profile_manager.py:176-207):
```python
# 1. Validate parameters before sending
is_valid, error, validated_params = validate_multiple_parameters(params_dict)

if not is_valid:
    # Return error to phone, do NOT send to VR
    send_error(phone_uart, error)
    return

# 2. Apply to local profile
self.params.update(validated_params)

# 3. Broadcast to VR
_broadcast_param_update(validated_params)
```

**SECONDARY Validation** (profile_manager.py:176-207):
```python
# 1. Validate received parameters
is_valid, error, validated_params = validate_multiple_parameters(params_dict)

if not is_valid:
    # Log error, do NOT apply
    print(f"[VR] [ERROR] Invalid parameters from VL: {error}")
    return

# 2. Apply to local profile
self.params.update(validated_params)
```

**Validation Rules** (validators.py):
```python
PARAM_RANGES = {
    "TIME_ON": (0.050, 0.500),        # 50-500ms
    "TIME_OFF": (0.020, 0.200),       # 20-200ms
    "ACTUATOR_FREQUENCY": (150, 300), # 150-300Hz
    "ACTUATOR_VOLTAGE": (1.0, 3.3),   # 1.0-3.3V
    "AMPLITUDE_MIN": (0, 100),        # 0-100%
    "AMPLITUDE_MAX": (0, 100),        # 0-100%
    "JITTER": (0, 50),                # 0-50%
    "TIME_SESSION": (1, 180),         # 1-180 minutes
}
```

### No Bidirectional Validation

**Important**: There is **no verification** that both gloves are running identical profiles.

**Why?**
- Fire-and-forget design for minimal latency
- Optional ACK for debugging (not required)
- Both gloves validate independently via same validation rules
- Parameter changes rare (only during configuration, not therapy)

**Risk**: If VR rejects parameters (e.g., out of range), VL doesn't know
**Mitigation**: Identical validation logic on both sides ensures consistency

---

## Multi-Connection Support

### Connection Types

**Three Device Types:**
1. **PHONE**: Smartphone app for configuration/monitoring
2. **VR**: SECONDARY glove for bilateral therapy
3. **UNKNOWN**: Unidentified devices (rejected)

### Connection Detection

**Identification Strategy** (ble_connection.py:92-150):

```python
# Phone detection: Commands within 3 seconds
phone_commands = ["INFO", "PING", "BATTERY", "PROFILE", "SESSION", "HELP", "PARAM"]
if any(cmd in first_message for cmd in phone_commands):
    return "PHONE"

# VR detection: READY message within 3 seconds
if first_message == "READY":
    return "VR"

# Unknown: Timeout or unrecognized message
return None
```

**Why 3-second timeout?**
- Phone app should send command immediately after connecting
- VR sends READY within 100ms of connection
- 3s provides margin for slower devices

### UART Routing

**PRIMARY has three UART references:**
```python
self.phone_uart = None    # Smartphone communication
self.vr_uart = None       # VR glove communication
self.uart = None          # Legacy single connection (fallback)
```

**Routing Logic:**
```python
# Menu commands: Use phone_uart (or uart fallback)
uart_to_check = self.phone_uart if self.phone_uart else self.uart

# VR commands: Use vr_uart (or uart fallback)
vr_uart_for_therapy = self.vr_uart if self.vr_uart else self.uart

# Parameter broadcast: Use vr_uart only
if self.vr_uart:
    self.vr_uart.write(param_update_message)
```

### Connection Scenarios

**Scenario 1: Phone Only** (No VR)
```
1. Phone connects → Detected as PHONE
2. phone_uart assigned
3. VR not connected → Therapy unavailable
4. Phone can: Check battery (VL only), modify profiles, calibrate VL fingers
5. SESSION_START → ERROR:VR not connected
```

**Scenario 2: VR Only** (No Phone)
```
1. VR connects → Detected as VR
2. vr_uart assigned
3. Complete handshake: READY → SYNC → ACK → VCR_START
4. Auto-start therapy after startup window
5. No smartphone monitoring/control
```

**Scenario 3: Phone + VR** (Full Featured)
```
1. Both connect during startup window
2. Identify types: phone_uart + vr_uart assigned
3. Complete VR handshake
4. Phone can: Monitor battery (both gloves), control session, modify profiles
5. VR synchronized for bilateral therapy
6. Phone sees interleaved VL↔VR messages (EXECUTE_BUZZ, PARAM_UPDATE, etc.)
```

**Scenario 4: Phone First, VR Later**
```
1. Phone connects → phone_uart assigned
2. VR not connected → vr_uart = None
3. Wait for VR: _scan_for_vr_while_advertising()
4. VR connects → vr_uart assigned
5. Complete VR handshake
6. Proceed with therapy
```

### Message Interleaving (Phone Perspective)

**Problem**: Phone may receive internal VL↔VR messages

**Example RX stream at phone:**
```
PONG\n\x04                           ← Response to PING
EXECUTE_BUZZ:0\n                     ← VL→VR internal message (NO EOT)
BATP:3.72\nBATS:3.68\n\x04           ← Response to BATTERY
BUZZ_COMPLETE:0\n                    ← VR→VL internal message (NO EOT)
EXECUTE_BUZZ:1\n                     ← VL→VR internal message (NO EOT)
SESSION_STATUS:RUNNING\n...\n\x04    ← Response to SESSION_STATUS
```

**Filtering Strategy** (recommended for phone app):
```csharp
void OnBleNotification(byte[] data) {
    string message = Encoding.UTF8.GetString(data);

    // Filter internal VL↔VR messages (no EOT terminator)
    if (!message.Contains("\x04")) {
        // Ignore: EXECUTE_BUZZ, BUZZ_COMPLETE, PARAM_UPDATE, etc.
        return;
    }

    // Process app-directed response (has EOT)
    ProcessResponse(message);
}
```

**Internal Messages to Ignore:**
- `EXECUTE_BUZZ:N` (every ~200ms during therapy)
- `BUZZ_COMPLETE:N` (every ~200ms during therapy)
- `PARAM_UPDATE:KEY:VAL:...` (during profile changes)
- `GET_BATTERY` (when phone queries battery)
- `BAT_RESPONSE:V` (VR response to VL)
- `ACK_PARAM_UPDATE` (VR acknowledgment)
- `SEED:N` / `SEED_ACK` (random seed sync)

---

## Error Recovery

### Connection Failures

**PRIMARY Timeout** (ble_connection.py:347-350):
```python
if not secondary_found:
    print(f"[VL] [ERROR] No Secondary found in {CONNECTION_TIMEOUT}s! Restart required.")
    pixels.fill((255, 0, 0))  # Red indicator
    return False
```

**SECONDARY Timeout** (ble_connection.py:461-464):
```python
if not primary_found:
    print(f"[VR] [ERROR] No Primary found in {CONNECTION_TIMEOUT}s! Restart required.")
    pixels.fill((255, 0, 0))
    return False
```

**Recovery**: Manual power cycle required (no auto-retry)

### Handshake Failures

**READY Timeout** (ble_connection.py:369-372):
```python
if not ready_received:
    print(f"[VL] [ERROR] No READY received! Restart required.")
    pixels.fill((255, 0, 0))
    return False
```

**ACK Timeout** (ble_connection.py:396-399):
```python
if not ack_received:
    print(f"[VL] [ERROR] No ACK received! Restart required.")
    pixels.fill((255, 0, 0))
    return False
```

**Recovery**: Manual power cycle (no retry after handshake failure)

### Therapy Execution Errors

**VR EXECUTE_BUZZ Timeout** (vcr_engine.py:206-218):
```python
if received_idx is None:
    print(f"[VR] [ERROR] EXECUTE_BUZZ timeout! PRIMARY disconnected.")
    print(f"[VR] Stopping all motors for safety...")
    haptic.all_off()
    pixels.fill((255, 0, 0))

    # Enter infinite error loop
    while True:
        pixels.fill((255, 0, 0))
        time.sleep(0.5)
        pixels.fill((0, 0, 0))
        time.sleep(0.5)
```

**Recovery**: None - device halts, requires manual restart

**VL BUZZ_COMPLETE Timeout** (vcr_engine.py:198-199):
```python
ack_received = receive_buzz_complete(uart_service, sequence_idx, timeout_sec=3.0)
if not ack_received:
    print(f"[VL] [WARNING] No BUZZ_COMPLETE from VR for sequence {sequence_idx}")
    # Continue anyway - PRIMARY maintains operation
```

**Recovery**: Log warning, continue therapy (non-fatal)

### Connection Health Monitoring

#### Heartbeat Protocol

During active therapy sessions, PRIMARY sends periodic heartbeat messages to SECONDARY to detect connection loss early.

**Heartbeat Parameters** (app.py:227-234):

| Parameter | Value | Description |
|-----------|-------|-------------|
| `HEARTBEAT_INTERVAL_SEC` | 2.0 | PRIMARY sends heartbeat every 2 seconds |
| `HEARTBEAT_TIMEOUT_SEC` | 6.0 | SECONDARY timeout (3 missed heartbeats) |

**Heartbeat Message Format**:
```
SYNC:HEARTBEAT:ts|<timestamp_microseconds>
```

**PRIMARY Heartbeat Sender**:
```python
# During therapy, PRIMARY sends heartbeat every 2 seconds
if time.monotonic() - last_heartbeat_time >= HEARTBEAT_INTERVAL_SEC:
    timestamp_us = int(time.monotonic_ns() // 1000)
    ble.send("secondary", f"SYNC:HEARTBEAT:ts|{timestamp_us}")
    last_heartbeat_time = time.monotonic()
```

**SECONDARY Heartbeat Receiver**:
```python
# SECONDARY monitors for heartbeat timeout
if time.monotonic() - last_heartbeat_received > HEARTBEAT_TIMEOUT_SEC:
    print("[SECONDARY] Heartbeat timeout - PRIMARY connection lost")
    transition_to_state(TherapyState.CONNECTION_LOST)
```

**Recovery Behavior**:
- SECONDARY detects timeout after 6 seconds (3 missed heartbeats)
- Motors are stopped immediately for safety
- LED shows red flashing pattern
- Device requires manual restart

#### Periodic Health Check

**Connection Status Check** (ble_connection.py:734-777):
```python
def check_connection_health():
    result = {"phone": False, "vr": False, "phone_lost": False, "vr_lost": False}

    # Check phone connection
    if phone_connection and not phone_connection.connected:
        result["phone_lost"] = True
        phone_connection = None  # Clear stale reference

    # Check VR connection (CRITICAL)
    if vr_connection and not vr_connection.connected:
        result["vr_lost"] = True
        vr_connection = None  # Clear stale reference

    return result
```

**Usage**:
- Called periodically during therapy (~every 10 seconds)
- Phone disconnect: Non-fatal (therapy continues)
- VR disconnect: Detected via EXECUTE_BUZZ timeout (10s), then halt

---

## Timing Analysis

### Critical Path Timing

**Buzz Sequence Execution** (one of three per macrocycle):

| Event | Time (ms) | Cumulative |
|-------|-----------|------------|
| VL: Generate pattern | 0-5 | 0-5 |
| VL: Send EXECUTE_BUZZ | 5-10 | 5-15 |
| BLE transmission | 7.5 | 12.5-22.5 |
| VR: Receive command | 0-2 | 12.5-24.5 |
| VR: Generate pattern | 0-5 | 12.5-29.5 |
| **VL: Start buzz** | **0** | **15-20** |
| **VR: Start buzz** | **0** | **12.5-29.5** |
| VL: TIME_ON duration | 100 | 115-120 |
| VR: TIME_ON duration | 100 | 112.5-129.5 |
| VL: TIME_OFF + jitter | 67-90 | 182-210 |
| VR: TIME_OFF + jitter | 67-90 | 179.5-219.5 |
| VR: Send BUZZ_COMPLETE | 0-5 | 179.5-224.5 |
| BLE transmission | 7.5 | 187-232 |
| VL: Receive ACK | 0-2 | 187-234 |

**Key Observation**: VR buzzes 12.5-29.5ms after VL (dominated by BLE latency)

**Acceptable?** YES - Human temporal resolution ~20-40ms

### Macrocycle Timing

**One Macrocycle** (3 buzzes + 2 relax periods):

```
Buzz 1: 100ms ON + 67ms OFF = 167ms
Buzz 2: 100ms ON + 67ms OFF = 167ms
Buzz 3: 100ms ON + 67ms OFF = 167ms
Relax 1: 4 * (100 + 67) = 668ms
Relax 2: 4 * (100 + 67) = 668ms
--------------------------------------
Total: 1837ms (~1.8 seconds per macrocycle)
```

**Per Session** (120 minutes):
```
Total macrocycles: (120 * 60) / 1.837 ≈ 3,920 macrocycles
Total buzzes: 3,920 * 3 = 11,760 buzzes per glove
Total EXECUTE_BUZZ messages: 11,760 messages over 2 hours
```

**BLE Bandwidth** (~12,000 messages over 2 hours):
```
Messages: EXECUTE_BUZZ (11,760) + BUZZ_COMPLETE (11,760) + SYNC_ADJ (~1,200)
Total: ~24,720 messages
Rate: 24,720 / 7200s = 3.4 messages/second
Data: ~20 bytes/message * 24,720 = 494KB over 2 hours
```

**Conclusion**: Extremely low bandwidth usage (~0.07 KB/s average)

### Latency Budget

**TARGET: <50ms bilateral synchronization** (therapy requirement)

| Component | Latency | Contribution |
|-----------|---------|--------------|
| Pattern generation | 0-5ms | 10% |
| UART write | 0-2ms | 4% |
| BLE transmission | 7.5ms (nominal) | 15% |
| BLE transmission (worst) | 35ms | 70% |
| UART read | 0-2ms | 4% |
| Processing overhead | 0-5ms | 10% |
| **Total (nominal)** | **~15-20ms** | **40% of budget** |
| **Total (worst-case)** | **~45ms** | **90% of budget** |

**Result**: Well within spec, even at worst-case BLE latency

---

## Message Catalog

### Handshake Messages (VL ↔ VR)

| Message | Direction | Purpose | Timeout | Response |
|---------|-----------|---------|---------|----------|
| `READY\n` | VR → VL | Signal connection ready | - | `FIRST_SYNC` |
| `FIRST_SYNC:12345\n` | VL → VR | Initial time sync | 8s | `ACK` |
| `ACK\n` | VR → VL | Acknowledge sync | 0.5s | `VCR_START` |
| `VCR_START\n` | VL → VR | Begin therapy | - | (none) |

### Therapy Execution Messages (VL ↔ VR)

| Message | Direction | Purpose | Timeout | Response |
|---------|-----------|---------|---------|----------|
| `EXECUTE_BUZZ:0\n` | VL → VR | Command buzz execution | - | (VR executes) |
| `EXECUTE_BUZZ:1\n` | VL → VR | Command buzz execution | - | (VR executes) |
| `EXECUTE_BUZZ:2\n` | VL → VR | Command buzz execution | - | (VR executes) |
| `BUZZ_COMPLETE:0\n` | VR → VL | Confirm completion | 3s | (none) |
| `BUZZ_COMPLETE:1\n` | VR → VL | Confirm completion | 3s | (none) |
| `BUZZ_COMPLETE:2\n` | VR → VL | Confirm completion | 3s | (none) |
| `SYNC_ADJ:12345\n` | VL → VR | Optional time check | - | `ACK_SYNC_ADJ` |
| `ACK_SYNC_ADJ\n` | VR → VL | Acknowledge sync | 2s | `SYNC_ADJ_START` |
| `SYNC_ADJ_START\n` | VL → VR | Resume therapy | - | (none) |

### Parameter Synchronization (VL → VR)

| Message | Direction | Purpose | Response |
|---------|-----------|---------|----------|
| `PARAM_UPDATE:KEY:VAL:...\n` | VL → VR | Broadcast parameter changes | `ACK_PARAM_UPDATE` (optional) |
| `ACK_PARAM_UPDATE\n` | VR → VL | Acknowledge update | (none) |
| `SEED:123456\n` | VL → VR | Random seed for jitter sync | `SEED_ACK` |
| `SEED_ACK\n` | VR → VL | Acknowledge seed | (none) |

### Battery Query (VL ↔ VR)

| Message | Direction | Purpose | Timeout | Response |
|---------|-----------|---------|---------|----------|
| `GET_BATTERY\n` | VL → VR | Query VR battery voltage | - | `BAT_RESPONSE` |
| `BAT_RESPONSE:3.68\n` | VR → VL | Report voltage | 1s | (none) |

### BLE Protocol Commands (Phone → VL)

See **COMMAND_REFERENCE.md** for complete BLE Protocol v2.0.0 specification.

**Categories:**
- Device Information: INFO, BATTERY, PING
- Therapy Profiles: PROFILE_LIST, PROFILE_LOAD, PROFILE_GET, PROFILE_CUSTOM
- Session Control: SESSION_START, SESSION_PAUSE, SESSION_RESUME, SESSION_STOP, SESSION_STATUS
- Parameter Adjustment: PARAM_SET
- Calibration: CALIBRATE_START, CALIBRATE_BUZZ, CALIBRATE_STOP
- System: HELP, RESTART

**All phone-directed responses end with `\x04` (EOT terminator)**

---

**Document Maintenance:**
Update this document when:
- Modifying handshake protocol
- Changing command message formats
- Adding new synchronization messages
- Updating timeout values
- Changing BLE connection parameters

**Last Updated:** 2025-01-23
**Protocol Version:** 2.0.0
**Reviewed By:** Firmware Engineering Team
