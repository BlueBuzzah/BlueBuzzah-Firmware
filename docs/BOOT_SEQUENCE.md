# BlueBuzzah v2 Boot Sequence

**Comprehensive boot sequence documentation for bilateral haptic therapy system**

Version: 2.0.0
Last Updated: 2025-01-11

---

## Table of Contents

- [Overview](#overview)
- [Boot Sequence Requirements](#boot-sequence-requirements)
- [PRIMARY Device Boot Sequence](#primary-device-boot-sequence)
- [SECONDARY Device Boot Sequence](#secondary-device-boot-sequence)
- [LED Indicator Reference](#led-indicator-reference)
- [Connection Requirements](#connection-requirements)
- [Timeout Behavior](#timeout-behavior)
- [Error Handling](#error-handling)
- [Timing Analysis](#timing-analysis)
- [Troubleshooting](#troubleshooting)
- [Implementation Details](#implementation-details)

---

## Overview

The BlueBuzzah boot sequence establishes the necessary BLE connections between PRIMARY, SECONDARY, and optionally a phone app within a **30-second window**. Clear LED feedback provides visual indication of boot progress, success, or failure.

### Boot Sequence Goals

1. **Establish bilateral synchronization** between PRIMARY and SECONDARY devices
2. **Optional phone connection** for app control (not required for therapy)
3. **Visual feedback** via LED at every stage
4. **Safety-first approach** - fail gracefully if connections not established
5. **Automatic fallback** to default therapy if minimum requirements met

### Key Design Principles

- **Time-bounded**: 30-second maximum boot window
- **Safety-critical**: SECONDARY connection is required; phone is optional
- **User-friendly**: Clear LED indicators at each stage
- **Fail-safe**: Therapy only begins when bilateral sync is confirmed
- **Graceful degradation**: Default therapy if phone not connected

---

## Boot Sequence Requirements

### Minimum Requirements

**For PRIMARY Device:**
- Must establish connection with SECONDARY device
- Phone connection is optional

**For SECONDARY Device:**
- Must establish connection with PRIMARY device

**For System:**
- PRIMARY-SECONDARY connection required for therapy
- Sub-10ms bilateral synchronization after boot
- 30-second timeout enforced strictly

### Success Criteria

| Scenario | PRIMARY | SECONDARY | Phone | Result |
|----------|---------|-----------|-------|--------|
| Full Success | ✓ | ✓ | ✓ | App control + default therapy available |
| Partial Success | ✓ | ✓ | ✗ | Automatic default therapy starts |
| PRIMARY Failure | ? | ✗ | ? | Boot fails (slow red LED) |
| SECONDARY Failure | ✗ | ? | ✗ | Boot fails (slow red LED) |

---

## PRIMARY Device Boot Sequence

The PRIMARY device acts as the central coordinator, advertising its presence and waiting for connections from both SECONDARY and phone.

### State Diagram

```mermaid
stateDiagram-v2
    [*] --> Initializing: Power On

    Initializing --> Advertising: BLE Init Complete
    note right of Advertising: LED: Rapid Blue Flash

    Advertising --> WaitingForDevices: Start Advertisement
    note right of WaitingForDevices: Timeout: 30 seconds

    WaitingForDevices --> SecondaryConnected: SECONDARY Connects
    note right of SecondaryConnected: LED: 5x Green Flash

    SecondaryConnected --> WaitingForPhone: No Phone Yet
    note right of WaitingForPhone: LED: Slow Blue Flash

    WaitingForPhone --> BothConnected: Phone Connects
    WaitingForPhone --> TherapyMode: 30s Timeout

    SecondaryConnected --> BothConnected: Phone Connects

    BothConnected --> Ready: All Connected
    TherapyMode --> Ready: Therapy Ready
    note right of Ready: LED: Solid Green

    WaitingForDevices --> Failed: 30s Timeout\n(no SECONDARY)
    note right of Failed: LED: Slow Red Flash

    Failed --> [*]: Halt
    Ready --> ExecutingTherapy: Start Therapy
```

### Detailed Steps

#### 1. Initialization Phase

**Duration**: 1-2 seconds

```python
async def _primary_boot_sequence(self) -> BootResult:
    """PRIMARY device boot sequence."""
    print("[PRIMARY] Starting boot sequence")

    # Initialize BLE
    self.led.indicate_ble_init()  # Rapid blue flash
    await self.ble.initialize()

    # Start advertising
    await self.ble.advertise("BlueBuzzah")
```

**LED Indicator**: Rapid blue flash (10Hz)
**Purpose**: Initialize BLE stack and begin advertising

---

#### 2. Connection Wait Phase

**Duration**: 0-30 seconds (depends on connection timing)

```python
    start_time = time.monotonic()
    secondary_connected = False
    phone_connected = False

    while time.monotonic() - start_time < self.connection_timeout:
        # Check for new connections
        connections = await self.ble.check_connections()

        for conn in connections:
            if self._is_secondary(conn):
                secondary_connected = True
                self.secondary_connection = conn
                self.led.indicate_connection_success()  # 5x green flash

                if not phone_connected:
                    self.led.indicate_waiting_for_phone()  # Slow blue flash

            elif self._is_phone(conn):
                phone_connected = True
                self.phone_connection = conn
```

**LED Indicators**:
- **During wait**: Rapid blue flash continues
- **SECONDARY connects**: 5x green flash (success acknowledgment)
- **Waiting for phone**: Slow blue flash (1Hz)

**Purpose**: Accept connections from SECONDARY and phone within timeout window

---

#### 3. Timeout Evaluation

**At 30 seconds**: Evaluate connection status and determine outcome

```python
        # Check if timeout reached
        if time.monotonic() - start_time >= self.connection_timeout:
            if not secondary_connected:
                # CRITICAL FAILURE: No SECONDARY
                print("[PRIMARY] ERROR: SECONDARY not connected")
                self.led.indicate_failure()  # Slow red flash
                return BootResult.FAILED

            # SECONDARY connected, proceed with or without phone
            break
```

**Outcomes**:
- **No SECONDARY**: FAILED (slow red LED, halt)
- **SECONDARY only**: SUCCESS_NO_PHONE (proceed to default therapy)
- **SECONDARY + phone**: SUCCESS_WITH_PHONE (wait for app commands)

---

#### 4. Ready State

**Final LED**: Solid green

```python
    # Determine final state
    self.led.indicate_ready()  # Solid green

    if secondary_connected and phone_connected:
        print("[PRIMARY] All connections established")
        return BootResult.SUCCESS_WITH_PHONE
    elif secondary_connected:
        print("[PRIMARY] SECONDARY connected, no phone")
        return BootResult.SUCCESS_NO_PHONE
```

**Purpose**: Signal that device is ready to begin therapy

---

### PRIMARY Timeline Example

```
Time  | State                | LED                    | Connections
------|----------------------|------------------------|------------------
0.0s  | Initializing         | Rapid Blue Flash       | None
1.0s  | Advertising          | Rapid Blue Flash       | None
3.5s  | SECONDARY Connected  | 5x Green Flash         | SECONDARY
4.0s  | Waiting for Phone    | Slow Blue Flash        | SECONDARY
15.0s | Phone Connected      | (transition)           | SECONDARY + Phone
15.5s | Ready                | Solid Green            | SECONDARY + Phone
```

**Alternative Scenario** (No Phone):
```
Time  | State                | LED                    | Connections
------|----------------------|------------------------|------------------
0.0s  | Initializing         | Rapid Blue Flash       | None
1.0s  | Advertising          | Rapid Blue Flash       | None
3.5s  | SECONDARY Connected  | 5x Green Flash         | SECONDARY
4.0s  | Waiting for Phone    | Slow Blue Flash        | SECONDARY
30.0s | Timeout (no phone)   | (transition)           | SECONDARY
30.5s | Ready                | Solid Green            | SECONDARY
```

---

## SECONDARY Device Boot Sequence

The SECONDARY device actively scans for and connects to the PRIMARY device advertising as "BlueBuzzah".

### State Diagram

```mermaid
stateDiagram-v2
    [*] --> Initializing: Power On

    Initializing --> Scanning: BLE Init Complete
    note right of Scanning: LED: Rapid Blue Flash\nTimeout: 30 seconds

    Scanning --> Connected: Found PRIMARY
    note right of Connected: LED: 5x Green Flash

    Connected --> Ready: Connection Confirmed
    note right of Ready: LED: Solid Green

    Scanning --> Failed: 30s Timeout\n(PRIMARY not found)
    note right of Failed: LED: Slow Red Flash

    Failed --> [*]: Halt
    Ready --> AwaitingCommands: Wait for PRIMARY
```

### Detailed Steps

#### 1. Initialization Phase

**Duration**: 1-2 seconds

```python
async def _secondary_boot_sequence(self) -> BootResult:
    """SECONDARY device boot sequence."""
    print("[SECONDARY] Starting boot sequence")

    # Initialize BLE
    self.led.indicate_ble_init()  # Rapid blue flash
    await self.ble.initialize()
```

**LED Indicator**: Rapid blue flash (10Hz)
**Purpose**: Initialize BLE stack for scanning

---

#### 2. Scanning Phase

**Duration**: 0-30 seconds (until PRIMARY found or timeout)

```python
    start_time = time.monotonic()
    connected = False

    while time.monotonic() - start_time < self.connection_timeout:
        # Scan for PRIMARY device
        devices = await self.ble.scan(timeout=1.0)

        for device in devices:
            if device.name == "BlueBuzzah":
                # Found PRIMARY - attempt connection
                print(f"[SECONDARY] Found PRIMARY: {device.address}")

                connection = await self.ble.connect(device)
                if connection:
                    connected = True
                    self.primary_connection = connection

                    # Success indicators
                    self.led.indicate_connection_success()  # 5x green flash
                    await asyncio.sleep(0.5)  # Brief pause after success flash
                    self.led.indicate_ready()  # Solid green

                    print("[SECONDARY] Connected to PRIMARY")
                    return BootResult.SUCCESS

        # Small delay between scan attempts
        await asyncio.sleep(0.1)
```

**LED Indicator**: Rapid blue flash (continues during scanning)
**Purpose**: Find and connect to PRIMARY device

---

#### 3. Connection Success

**Duration**: < 1 second

**LED Sequence**:
1. 5x green flash (success acknowledgment, synchronized with PRIMARY)
2. Brief pause (0.5s)
3. Solid green (ready state)

```python
        if connected:
            self.led.indicate_connection_success()  # 5x green flash
            await asyncio.sleep(0.5)
            self.led.indicate_ready()  # Solid green
            return BootResult.SUCCESS
```

**Purpose**: Confirm connection establishment to user

---

#### 4. Timeout Failure

**At 30 seconds**: If PRIMARY not found

```python
    # Timeout - connection failed
    print("[SECONDARY] ERROR: PRIMARY not found within timeout")
    self.led.indicate_failure()  # Slow red flash
    return BootResult.FAILED
```

**LED Indicator**: Slow red flash (1Hz)
**Purpose**: Indicate boot failure, require restart

---

### SECONDARY Timeline Example

**Successful Boot**:
```
Time  | State                | LED                    | Status
------|----------------------|------------------------|------------------
0.0s  | Initializing         | Rapid Blue Flash       | BLE init
1.0s  | Scanning             | Rapid Blue Flash       | Looking for PRIMARY
3.5s  | Found PRIMARY        | Rapid Blue Flash       | Attempting connection
4.0s  | Connected            | 5x Green Flash         | Connection success
4.5s  | Ready                | Solid Green            | Awaiting commands
```

**Failed Boot** (PRIMARY not found):
```
Time  | State                | LED                    | Status
------|----------------------|------------------------|------------------
0.0s  | Initializing         | Rapid Blue Flash       | BLE init
1.0s  | Scanning             | Rapid Blue Flash       | Looking for PRIMARY
...   | Scanning             | Rapid Blue Flash       | Still searching
30.0s | Timeout              | (transition)           | PRIMARY not found
30.5s | Failed               | Slow Red Flash         | Boot failed
```

---

## LED Indicator Reference

### Complete LED Color and Pattern Guide

| State | Color | Pattern | Frequency | Meaning |
|-------|-------|---------|-----------|---------|
| **BLE Initialization** | Blue | Rapid Flash | 10 Hz | BLE stack initializing |
| **Connection Success** | Green | 5x Flash | 5 Hz | PRIMARY-SECONDARY connected |
| **Waiting for Phone** | Blue | Slow Flash | 1 Hz | SECONDARY connected, waiting for phone |
| **Ready** | Green | Solid | - | Boot complete, ready for therapy |
| **Connection Failed** | Red | Slow Flash | 1 Hz | Boot failed, restart required |

### LED Pattern Details

#### Rapid Blue Flash (BLE Init)
```python
# Pattern: 100ms on, 100ms off
led.rapid_flash(LED_BLUE, frequency=10.0)
```
- **Duration**: Until connection attempt completes
- **Purpose**: Indicates active BLE operations
- **Devices**: Both PRIMARY and SECONDARY

---

#### 5x Green Flash (Connection Success)
```python
# Pattern: 5 flashes, 100ms on, 100ms off each
led.flash_count(LED_GREEN, count=5)
```
- **Duration**: ~1 second (5 × 200ms)
- **Purpose**: Confirms PRIMARY-SECONDARY connection
- **Devices**: Both PRIMARY and SECONDARY (synchronized)
- **Timing**: Synchronized within sub-10ms

---

#### Slow Blue Flash (Waiting for Phone)
```python
# Pattern: 500ms on, 500ms off
led.slow_flash(LED_BLUE, frequency=1.0)
```
- **Duration**: Until phone connects or 30s timeout
- **Purpose**: Indicates waiting for phone connection
- **Devices**: PRIMARY only

---

#### Solid Green (Ready)
```python
# Pattern: Continuous solid green
led.solid(LED_GREEN)
```
- **Duration**: Until therapy starts
- **Purpose**: Boot complete, system ready
- **Devices**: Both PRIMARY and SECONDARY
- **Transition**: Changes to breathing green when therapy starts

---

#### Slow Red Flash (Failure)
```python
# Pattern: 500ms on, 500ms off
led.slow_flash(LED_RED, frequency=1.0)
```
- **Duration**: Continuous (until power cycle)
- **Purpose**: Boot sequence failed
- **Devices**: Both PRIMARY and SECONDARY (when applicable)
- **Recovery**: Requires device restart

---

### LED Pattern Timing Diagram

```
PRIMARY Boot (Success with Phone):
0s     5s     10s    15s    20s    25s    30s
|------|------|------|------|------|------|
[Rapid Blue Flash................]
                 ^
                 SECONDARY connects
                 [5x Green]
                          [Slow Blue Flash...]
                                         ^
                                         Phone connects
                                         [Solid Green]

PRIMARY Boot (Success without Phone):
0s     5s     10s    15s    20s    25s    30s
|------|------|------|------|------|------|
[Rapid Blue Flash................]
                 ^
                 SECONDARY connects
                 [5x Green]
                          [Slow Blue Flash.............]
                                                      ^
                                                      Timeout
                                                      [Solid Green]

SECONDARY Boot (Success):
0s     5s     10s    15s    20s    25s    30s
|------|------|------|------|------|------|
[Rapid Blue Flash...]
                 ^
                 Found PRIMARY
                 [5x Green]
                          [Solid Green...............]

PRIMARY/SECONDARY Boot (Failure):
0s     5s     10s    15s    20s    25s    30s
|------|------|------|------|------|------|
[Rapid Blue Flash................................]
                                              ^
                                              Timeout (no connection)
                                              [Slow Red Flash....]
```

---

## Connection Requirements

### BLE Advertisement Parameters (PRIMARY)

```python
# PRIMARY advertises with these parameters
advertisement_params = {
    "name": "BlueBuzzah",
    "interval": 0.5,  # 500ms advertising interval
    "timeout": 30,    # Advertise for 30 seconds
    "connectable": True,
    "discoverable": True
}
```

**Rationale**:
- 500ms interval balances discoverability and power consumption
- Advertises for full 30-second boot window
- Name "BlueBuzzah" identifies device to SECONDARY and phone

---

### BLE Scanning Parameters (SECONDARY)

```python
# SECONDARY scans with these parameters
scan_params = {
    "scan_duration": 1.0,  # 1-second scan periods
    "scan_interval": 0.1,   # 100ms between scans
    "active_scan": True,    # Request scan responses
    "timeout": 30           # Total scan timeout
}
```

**Rationale**:
- 1-second scan periods provide good discovery rate
- 100ms interval between scans avoids excessive power use
- Active scanning gets additional device information
- 30-second timeout matches boot window

---

### Connection Validation

Both devices validate connections after establishment:

```python
def validate_connection(connection) -> bool:
    """Validate BLE connection quality."""
    # Check connection parameters
    if connection.interval_ms > 10:  # Expect 7.5ms interval
        print(f"Warning: High connection interval ({connection.interval_ms}ms)")

    # Verify service discovery
    services = connection.discover_services()
    if UART_SERVICE_UUID not in services:
        print("Error: UART service not found")
        return False

    # Test round-trip latency
    latency_ms = test_ping_latency(connection)
    if latency_ms > 20:
        print(f"Warning: High latency ({latency_ms}ms)")

    return True
```

---

### Initial Time Synchronization

After PRIMARY-SECONDARY connection, initial time sync is established:

```python
# PRIMARY sends FIRST_SYNC
async def establish_initial_sync(self):
    """Establish initial time synchronization."""
    primary_time = time.monotonic_ns()

    # Send FIRST_SYNC message
    await self.ble.send(
        self.secondary_connection,
        f"FIRST_SYNC:{primary_time}\n".encode()
    )

    # Wait for ACK with SECONDARY timestamp
    response = await self.ble.receive(self.secondary_connection, timeout=2.0)
    secondary_time = int(response.decode().split(':')[1])

    # Calculate offset
    offset = self.sync_protocol.calculate_offset(primary_time, secondary_time)
    self.time_offset = offset

    print(f"[SYNC] Initial offset: {offset / 1e6:.2f}ms")
```

This ensures sub-10ms bilateral synchronization before therapy begins.

---

## Timeout Behavior

### 30-Second Boot Window

The 30-second boot window is **strictly enforced** and begins immediately after BLE initialization completes.

```python
start_time = time.monotonic()
connection_timeout = 30.0  # seconds

while time.monotonic() - start_time < connection_timeout:
    # Connection attempts
    ...

    # Check timeout
    if time.monotonic() - start_time >= connection_timeout:
        break
```

---

### Timeout Scenarios

#### PRIMARY Timeout Scenarios

| Scenario | Connections | Timeout Action | LED | Result |
|----------|-------------|----------------|-----|--------|
| No devices | None | Fail | Slow Red | FAILED |
| SECONDARY only | SECONDARY | Proceed | Solid Green | SUCCESS_NO_PHONE |
| SECONDARY + Phone | Both | Proceed | Solid Green | SUCCESS_WITH_PHONE |

**Code**:
```python
if timeout_reached:
    if not secondary_connected:
        self.led.indicate_failure()
        return BootResult.FAILED
    else:
        self.led.indicate_ready()
        return BootResult.SUCCESS_NO_PHONE if not phone_connected else BootResult.SUCCESS_WITH_PHONE
```

---

#### SECONDARY Timeout Scenarios

| Scenario | Connection | Timeout Action | LED | Result |
|----------|------------|----------------|-----|--------|
| PRIMARY found | Connected | N/A (success before timeout) | Solid Green | SUCCESS |
| PRIMARY not found | None | Fail | Slow Red | FAILED |

**Code**:
```python
if timeout_reached:
    if not connected:
        self.led.indicate_failure()
        return BootResult.FAILED
```

---

### Timeout Extension

**The boot window is NOT extended** even if connections are partially established. This ensures predictable boot behavior.

**Rationale**:
- Predictable user experience
- Prevents indefinite waiting
- Encourages reliable hardware placement
- Allows automatic default therapy if phone not available

---

## Error Handling

### Connection Failure Modes

#### 1. No SECONDARY Connection (PRIMARY)

**Symptom**: PRIMARY times out with no SECONDARY connection

**Causes**:
- SECONDARY not powered on
- SECONDARY out of range
- BLE interference
- Hardware failure

**LED**: Slow red flash

**Recovery**: Power cycle both devices

```python
if not secondary_connected:
    print("[PRIMARY] CRITICAL: SECONDARY not connected")
    print("[PRIMARY] Cannot proceed without bilateral synchronization")
    self.led.indicate_failure()
    return BootResult.FAILED
```

---

#### 2. No PRIMARY Found (SECONDARY)

**Symptom**: SECONDARY times out without finding PRIMARY

**Causes**:
- PRIMARY not powered on
- PRIMARY out of range
- BLE interference
- Hardware failure

**LED**: Slow red flash

**Recovery**: Power cycle both devices

```python
if not connected:
    print("[SECONDARY] CRITICAL: PRIMARY not found")
    print("[SECONDARY] Ensure PRIMARY is powered on and in range")
    self.led.indicate_failure()
    return BootResult.FAILED
```

---

#### 3. Connection Drops During Boot

**Symptom**: Connection established but drops before boot completes

**Causes**:
- BLE interference
- Devices moved out of range
- Low battery
- Hardware issue

**Detection**:
```python
def monitor_connection_during_boot():
    """Monitor connection health during boot."""
    if not self.ble.is_connected(self.secondary_connection):
        print("[PRIMARY] WARNING: SECONDARY connection lost during boot")
        self.led.indicate_failure()
        return BootResult.FAILED
```

**Recovery**: Power cycle both devices

---

#### 4. Invalid Connection Parameters

**Symptom**: Connection established but parameters not suitable for therapy

**Causes**:
- BLE negotiation failure
- Incompatible BLE settings
- Firmware version mismatch

**Detection**:
```python
def validate_connection_params(connection):
    """Validate connection parameters meet therapy requirements."""
    interval = connection.connection_interval
    latency = connection.slave_latency
    timeout = connection.supervision_timeout

    if interval > 10:  # Need < 10ms for sub-10ms sync
        print(f"ERROR: Connection interval too high ({interval}ms)")
        return False

    if latency > 0:  # Need zero latency for real-time sync
        print(f"ERROR: Slave latency not zero ({latency})")
        return False

    return True
```

**Recovery**: Retry connection or power cycle

---

### Error Logging

All boot errors are logged with detailed context:

```python
def log_boot_error(error_type: str, details: Dict[str, Any]):
    """Log boot sequence errors for diagnostics."""
    timestamp = time.monotonic()
    print(f"[BOOT ERROR] {timestamp:.2f}s - {error_type}")
    for key, value in details.items():
        print(f"  {key}: {value}")

    # Store in non-volatile storage for later retrieval
    store_error_log(timestamp, error_type, details)
```

**Example Log**:
```
[BOOT ERROR] 30.15s - SECONDARY_NOT_FOUND
  timeout: 30.0s
  scans_performed: 300
  devices_discovered: 0
  rssi_min: -100 dBm
  battery_voltage: 3.7V
  firmware_version: 2.0.0
```

---

## Timing Analysis

### Boot Sequence Performance Targets

| Metric | Target | Typical | Maximum |
|--------|--------|---------|---------|
| BLE Init | < 2s | 1.5s | 3s |
| Connection Time | < 5s | 3s | 30s |
| Initial Sync | < 1s | 0.5s | 2s |
| Total Boot (success) | < 10s | 5s | 30s |
| Total Boot (failure) | 30s | 30s | 30s |

---

### Typical Boot Timeline (Success)

```
Event                          | Time    | Duration | Cumulative
-------------------------------|---------|----------|------------
Power On                       | 0.0s    | -        | 0.0s
CircuitPython Boot             | 0.0s    | 0.5s     | 0.5s
BLE Stack Init                 | 0.5s    | 1.0s     | 1.5s
Start Advertising/Scanning     | 1.5s    | 0.1s     | 1.6s
Device Discovery               | 1.6s    | 1.5s     | 3.1s
Connection Establishment       | 3.1s    | 0.5s     | 3.6s
Service Discovery              | 3.6s    | 0.3s     | 3.9s
Initial Sync                   | 3.9s    | 0.5s     | 4.4s
LED Success Indicators         | 4.4s    | 1.0s     | 5.4s
Boot Complete                  | 5.4s    | -        | 5.4s
```

**Total Boot Time (Typical Success)**: ~5.4 seconds

---

### Worst-Case Boot Timeline (Failure)

```
Event                          | Time    | Duration | Cumulative
-------------------------------|---------|----------|------------
Power On                       | 0.0s    | -        | 0.0s
CircuitPython Boot             | 0.0s    | 0.5s     | 0.5s
BLE Stack Init                 | 0.5s    | 2.0s     | 2.5s
Start Advertising/Scanning     | 2.5s    | 0.1s     | 2.6s
Scanning (no device found)     | 2.6s    | 27.4s    | 30.0s
Timeout Reached                | 30.0s   | -        | 30.0s
LED Failure Indication         | 30.0s   | 0.5s     | 30.5s
Boot Failed                    | 30.5s   | -        | 30.5s
```

**Total Boot Time (Failure)**: 30.5 seconds

---

### BLE Connection Latency

**Connection Interval**: 7.5ms (BLE_INTERVAL)
**Supervision Timeout**: 100ms (BLE_TIMEOUT)
**Slave Latency**: 0 (BLE_LATENCY)

**Effective Latency**:
- Minimum: 7.5ms (one connection interval)
- Maximum: 15ms (two connection intervals)
- Average: ~10ms

**Sub-10ms Synchronization**: Achieved through:
1. 7.5ms BLE connection interval
2. Time offset calculation and compensation
3. Predictive timing adjustment
4. Periodic resynchronization (SYNC_ADJ every macrocycle)

---

## Troubleshooting

### Problem: Devices Stuck on Blue Flashing LED

**Symptoms**:
- LED continues rapid blue flash indefinitely
- No connection established
- Both devices powered on

**Possible Causes**:
1. Devices out of range (> 1-2 meters)
2. BLE interference (Wi-Fi, other devices)
3. One device not actually powered on
4. Settings file missing or corrupt
5. Hardware failure

**Solutions**:

1. **Verify Both Devices Powered On**:
   - Check that both LEDs are flashing blue
   - Verify battery voltage (> 3.4V)
   - Check USB connections if using USB power

2. **Reduce Distance**:
   - Place devices within 0.5 meters
   - Remove obstructions between devices
   - Avoid metal surfaces

3. **Check Settings Files**:
   ```python
   # Verify settings.json exists and is valid
   import json
   with open("/settings.json", "r") as f:
       settings = json.load(f)
       print(f"Device role: {settings.get('deviceRole')}")
   ```

4. **Reduce BLE Interference**:
   - Move away from Wi-Fi routers
   - Turn off nearby Bluetooth devices
   - Avoid 2.4GHz congested areas

5. **Power Cycle Both Devices**:
   - Turn off both devices
   - Wait 5 seconds
   - Power on simultaneously

6. **Check CircuitPython Version**:
   - Must be 9.0+ for BLE features
   - Update if necessary

---

### Problem: Devices Show Red Flashing LED After 30 Seconds

**Symptoms**:
- LED shows slow red flash after timeout
- Boot sequence failed
- Devices not communicating

**Possible Causes**:
1. PRIMARY and SECONDARY roles not configured correctly
2. Devices not powered on simultaneously
3. Critical BLE hardware failure
4. Severe BLE interference

**Solutions**:

1. **Verify Device Roles**:
   ```bash
   # Check settings.json on both devices
   # PRIMARY:
   {"deviceRole": "Primary"}

   # SECONDARY:
   {"deviceRole": "Secondary"}
   ```

2. **Redeploy Firmware**:
   ```bash
   python deploy/deploy.py --clean
   ```

3. **Test Hardware**:
   ```python
   # Simple BLE test (run on each device)
   from communication.ble.service import BLEService
   ble = BLEService("TestDevice")
   print(f"BLE initialized: {ble.is_initialized()}")
   ```

4. **Check Logs**:
   ```python
   # Review serial console output for errors
   # Look for:
   # - "BLE init failed"
   # - "Connection timeout"
   # - "SECONDARY not found"
   ```

---

### Problem: PRIMARY Shows Green, SECONDARY Shows Red

**Symptoms**:
- PRIMARY LED is solid green
- SECONDARY LED is slow red flash
- Unilateral connection (one-way)

**Possible Causes**:
1. SECONDARY powered on late (after PRIMARY timeout)
2. Connection established but immediately dropped
3. Asymmetric hardware failure

**Solutions**:

1. **Synchronize Power-On**:
   - Turn off both devices
   - Power on both simultaneously
   - PRIMARY should see SECONDARY within 5 seconds

2. **Check SECONDARY BLE**:
   ```python
   # On SECONDARY, test scanning
   from communication.ble.service import BLEService
   ble = BLEService("Secondary")
   devices = await ble.scan(timeout=5.0)
   print(f"Devices found: {[d.name for d in devices]}")
   ```

3. **Verify PRIMARY Advertisement**:
   ```python
   # On PRIMARY, verify advertising
   from communication.ble.service import BLEService
   ble = BLEService("BlueBuzzah")
   await ble.advertise("BlueBuzzah")
   print("Advertising as BlueBuzzah")
   ```

---

### Problem: Boot Succeeds But Therapy Fails

**Symptoms**:
- Boot completes successfully (green LED)
- Therapy starts but motors don't activate
- Or therapy immediately errors

**Possible Causes**:
1. Haptic hardware not initialized
2. I2C communication failure
3. Bilateral synchronization lost
4. Initial sync failed

**Solutions**:

1. **Test Haptic Hardware**:
   ```python
   # Enter calibration mode
   from application.calibration.controller import CalibrationController
   cal = CalibrationController(haptic_controller, event_bus)
   await cal.start_calibration()
   await cal.test_all_fingers()
   ```

2. **Verify I2C Communication**:
   ```python
   # Check I2C bus
   import board
   import busio
   i2c = busio.I2C(board.SCL, board.SDA)
   while not i2c.try_lock():
       pass
   devices = i2c.scan()
   print(f"I2C devices: {[hex(d) for d in devices]}")
   i2c.unlock()
   # Should see: 0x70 (multiplexer), 0x5A (DRV2605 on each channel)
   ```

3. **Check Sync Protocol**:
   ```python
   # Verify time offset was established
   print(f"Time offset: {sync_coordinator.get_time_offset()}ns")
   # Should be non-zero if sync was successful
   ```

---

### Problem: Intermittent Boot Failures

**Symptoms**:
- Boot sometimes succeeds, sometimes fails
- No consistent pattern
- Hardware seems functional

**Possible Causes**:
1. Environmental BLE interference varies
2. Battery voltage borderline low
3. Timing-dependent race condition
4. Memory fragmentation

**Solutions**:

1. **Monitor Battery Voltage**:
   ```python
   from hardware.battery import BatteryMonitor
   battery = BatteryMonitor(board.A6)
   voltage = battery.read_voltage()
   print(f"Battery: {voltage:.2f}V")
   # Should be > 3.5V for reliable operation
   ```

2. **Check Memory Usage**:
   ```python
   import gc
   gc.collect()
   free = gc.mem_free()
   print(f"Free memory: {free} bytes")
   # Should have > 50KB free
   ```

3. **Enable Debug Logging**:
   ```python
   # In core/constants.py
   DEBUG_ENABLED = True
   LOG_LEVEL = "DEBUG"
   ```

4. **Increase Boot Timeout** (temporary diagnostic):
   ```python
   # In boot/manager.py
   connection_timeout = 45.0  # Increase to 45 seconds
   ```

---

## Implementation Details

### BootSequenceManager Class

The boot sequence is implemented in `boot/manager.py`:

```python
class BootSequenceManager:
    """Orchestrates device boot sequence with connection establishment."""

    def __init__(
        self,
        role: DeviceRole,
        ble_service: BLEService,
        led_controller: BootSequenceLEDController,
        connection_timeout: float = 30.0
    ):
        self.role = role
        self.ble = ble_service
        self.led = led_controller
        self.connection_timeout = connection_timeout
        self.tracker = ConnectionTracker(timeout_sec=connection_timeout)

    async def execute_boot_sequence(self) -> BootResult:
        """Execute role-specific boot sequence."""
        if self.role == DeviceRole.PRIMARY:
            return await self._primary_boot_sequence()
        else:
            return await self._secondary_boot_sequence()
```

---

### Connection Tracking

The `ConnectionTracker` class monitors connection attempts:

```python
class ConnectionTracker:
    """Track connection attempts and timing during boot."""

    def __init__(self, timeout_sec: float):
        self.timeout_sec = timeout_sec
        self.start_time = None
        self.connections = {}
        self.scan_count = 0

    def start(self):
        """Start tracking boot sequence."""
        self.start_time = time.monotonic()

    def is_timeout(self) -> bool:
        """Check if boot timeout has been reached."""
        if self.start_time is None:
            return False
        return time.monotonic() - self.start_time >= self.timeout_sec

    def elapsed(self) -> float:
        """Get elapsed time since boot start."""
        if self.start_time is None:
            return 0.0
        return time.monotonic() - self.start_time
```

---

### Boot LED Controller

The `BootSequenceLEDController` provides boot-specific LED patterns:

```python
class BootSequenceLEDController(LEDController):
    """Specialized LED controller for boot sequence."""

    def indicate_ble_init(self) -> None:
        """Rapid blue flash during BLE initialization."""
        self.rapid_flash(LED_BLUE, frequency=10.0)

    def indicate_connection_success(self) -> None:
        """5x green flash for successful connection."""
        self.flash_count(LED_GREEN, count=5)

    def indicate_waiting_for_phone(self) -> None:
        """Slow blue flash while waiting for phone."""
        self.slow_flash(LED_BLUE, frequency=1.0)

    def indicate_ready(self) -> None:
        """Solid green when ready."""
        self.solid(LED_GREEN)

    def indicate_failure(self) -> None:
        """Slow red flash for connection failure."""
        self.slow_flash(LED_RED, frequency=1.0)
```

---

## Best Practices

### For Reliable Boot

1. **Power on devices simultaneously** (within 1-2 seconds)
2. **Keep devices close** (< 1 meter) during boot
3. **Ensure good battery charge** (> 3.5V)
4. **Avoid BLE interference** (Wi-Fi routers, other Bluetooth devices)
5. **Use clean power source** (good battery or stable USB)
6. **Monitor LED feedback** for diagnostic information

### For Troubleshooting

1. **Check LED patterns** first - they tell the story
2. **Monitor serial console** for detailed logs
3. **Test one component at a time** (BLE, haptic, etc.)
4. **Use calibration mode** to verify hardware
5. **Check settings files** before assuming hardware failure

### For Development

1. **Use mock implementations** for testing without hardware
2. **Enable debug logging** for detailed diagnostics
3. **Monitor timing** with monotonic time measurements
4. **Test timeout scenarios** explicitly
5. **Validate all LED patterns** visually

---

## Summary

The BlueBuzzah boot sequence is a **safety-critical** operation that establishes bilateral synchronization between PRIMARY and SECONDARY devices. Key points:

- **30-second timeout** strictly enforced
- **Clear LED feedback** at every stage
- **Fail-safe design** - therapy only starts with confirmed bilateral sync
- **Graceful degradation** - default therapy if phone not connected
- **Comprehensive error handling** with recovery guidance

The boot sequence ensures that therapy can only begin when:
1. PRIMARY and SECONDARY are connected
2. Bilateral time synchronization is established
3. All hardware is initialized and validated
4. User has clear visual confirmation of system readiness

---

## Additional Resources

- [API Reference](API_REFERENCE.md) - Complete API documentation
- [Architecture Guide](ARCHITECTURE.md) - System design and patterns

---

**Last Updated**: 2025-01-11
**Document Version**: 1.0.0
