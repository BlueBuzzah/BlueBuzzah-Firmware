# BlueBuzzah Firmware Review: Device Communication & Session Synchronization

**Date:** 2025-11-24
**Reviewer:** Claude (Anthropic AI)
**Firmware Version:** BlueBuzzah v2.0.0
**Platform:** Adafruit Feather nRF52840 Express (CircuitPython 9.2.x)
**Focus Area:** PRIMARY-SECONDARY communication and therapy session synchronization

---

## Executive Summary

This review examines the bilateral vibrotactile therapy system using two nRF52840 devices (PRIMARY and SECONDARY) communicating over BLE. While the core architecture is well-designed and memory-efficient, **critical synchronization issues** were identified that could cause motor activation conflicts and pattern desynchronization between devices.

### Key Findings

| Severity | Issue | Impact |
|----------|-------|--------|
| 🔴 CRITICAL | Dual TherapyEngine conflict | Both devices run independent pattern generators |
| 🔴 CRITICAL | SECONDARY runs local engine + receives EXECUTE_BUZZ | Motors activate twice per cycle |
| 🟠 HIGH | Missing RNG seed synchronization | Devices generate different finger sequences |
| 🟡 MEDIUM | Multi-message EOT handling bug | Commands may be dropped if batched |
| 🟡 MEDIUM | No continuous clock sync | >10ms drift possible over 2-hour sessions |
| 🟡 MEDIUM | I2C busy-wait without timeout | Potential device freeze |
| 🟢 GOOD | Recent BLE sync fixes | START/PAUSE/RESUME/STOP transmission working |
| 🟢 GOOD | Memory efficiency | Well-optimized for nRF52840 constraints |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           BlueBuzzah v2 Architecture                        │
└─────────────────────────────────────────────────────────────────────────────┘

PRIMARY DEVICE (Left Glove)              SECONDARY DEVICE (Right Glove)
════════════════════════════             ════════════════════════════════
┌──────────────────────────┐             ┌──────────────────────────────┐
│      Phone App (BLE)     │             │                              │
│  ┌────────────────────┐  │             │                              │
│  │ Commands:          │  │             │                              │
│  │ - SESSION_START    │  │             │                              │
│  │ - SESSION_PAUSE    │  │             │                              │
│  │ - SESSION_STOP     │  │             │                              │
│  └────────────────────┘  │             │                              │
└───────────┬──────────────┘             └──────────────────────────────┘
            │                                          ▲
            ▼                                          │
┌───────────────────────────┐   BLE UART (Nordic)     │
│     MenuController        │◄────────────────────────┘
│  - Parses phone commands  │
│  - Routes to SessionMgr   │
└───────────┬───────────────┘
            │
            ▼
┌───────────────────────────┐   SYNC:START_SESSION    ┌──────────────────────┐
│    SessionManager         │─────────────────────────►│ _handle_sync_command │
│  - Lifecycle management   │   SYNC:PAUSE_SESSION    │ - Parses SYNC msgs   │
│  - State transitions      │─────────────────────────►│ - Routes to engine   │
│  - Sync callback dispatch │   SYNC:STOP_SESSION     │                      │
└───────────┬───────────────┘─────────────────────────►└──────────┬───────────┘
            │                                                      │
            ▼                                                      ▼
┌───────────────────────────┐   SYNC:EXECUTE_BUZZ     ┌──────────────────────┐
│    TherapyEngine          │─────────────────────────►│   TherapyEngine      │
│  - Pattern generation     │   (finger, amplitude)   │   ⚠️ ALSO RUNS       │
│  - Timing control         │                         │   INDEPENDENTLY!     │
│  - Motor activation       │                         │                      │
└───────────┬───────────────┘                         └──────────┬───────────┘
            │                                                      │
            ▼                                                      ▼
┌───────────────────────────┐                         ┌──────────────────────┐
│   DRV2605Controller       │                         │  DRV2605Controller   │
│  - I2C Multiplexer        │                         │  - I2C Multiplexer   │
│  - 5x Haptic Motors       │                         │  - 5x Haptic Motors  │
└───────────────────────────┘                         └──────────────────────┘
```

---

## Critical Issues Detailed Analysis

### Issue #1: Dual TherapyEngine Conflict (CRITICAL)

#### Problem Description

When a therapy session starts, **both** PRIMARY and SECONDARY create and run their own `TherapyEngine` instances that generate patterns **independently**. This creates a fundamental architecture conflict.

#### Code Locations

**PRIMARY starts its engine** (`src/application/session/manager.py:285-293`):
```python
# Start session with parameters from profile
self._therapy_engine.start_session(
    duration_sec=config.get('session_duration_min', 120) * 60,
    pattern_type=config.get('pattern_type', 'rndp'),
    time_on_ms=config.get('time_on_ms', 100.0),
    ...
)
```

**SECONDARY also starts its own engine** (`src/app.py:1197-1228`):
```python
elif command_type == "START_SESSION":
    if self.therapy_engine and not self.therapy_engine.is_running():
        # SECONDARY creates its OWN pattern generation!
        self.therapy_engine.start_session(
            duration_sec=duration_sec,
            pattern_type=pattern_type,  # Generates DIFFERENT random sequence!
            ...
        )
```

**SECONDARY runs its engine in main loop** (`src/app.py:907-911`):
```python
def _run_secondary_loop(self):
    while self.running:
        # PROBLEM: SECONDARY runs its own pattern generator!
        if self.therapy_engine:
            self.therapy_engine.update()  # Activates motors independently!

        # AND processes sync commands from PRIMARY
        self._process_incoming_ble_messages()
```

#### What Actually Happens

```
Time    PRIMARY                          SECONDARY
────    ───────                          ─────────
T+0ms   TherapyEngine generates          TherapyEngine generates
        pattern: [2,0,4,1,3]             pattern: [3,1,0,4,2]  ← DIFFERENT!

T+1ms   Activates finger 2               Activates finger 3 (own pattern)
        Sends EXECUTE_BUZZ(finger=2)

T+15ms                                   Receives EXECUTE_BUZZ(finger=2)
                                         Activates finger 2 AGAIN

RESULT: SECONDARY activates fingers 3 AND 2 in rapid succession!
```

#### Impact

1. **Motor Double-Activation**: SECONDARY activates motors twice per cycle
2. **Pattern Mismatch**: Different finger sequences between devices
3. **Timing Chaos**: Independent timing loops cause unpredictable activation patterns
4. **Therapy Ineffectiveness**: vCR therapy requires precise bilateral coordination (<10ms)

#### Root Cause

The architecture conflates two different synchronization models:
- **Model A (Leader/Follower)**: PRIMARY sends every command, SECONDARY only responds
- **Model B (Synchronized Generation)**: Both generate identical patterns from shared seed

The current implementation attempts **both** simultaneously, causing conflicts.

---

### Issue #2: Missing RNG Seed Synchronization (HIGH)

#### Problem Description

Pattern generation uses `random.seed()` but seeds are **never synchronized** between devices.

#### Code Location (`src/therapy.py:142-144`):
```python
def generate_random_permutation(..., random_seed=None):
    # CircuitPython: Use module-level random.seed()
    if random_seed is not None:
        random.seed(random_seed)  # Seed is NEVER passed!
```

#### What Happens

```python
# PRIMARY device - time.monotonic_ns() = 1234567890
# random.seed() not called, uses default/previous state
# Generates: [2, 0, 4, 1, 3]

# SECONDARY device - time.monotonic_ns() = 1234590000 (different!)
# random.seed() not called, uses default/previous state
# Generates: [3, 1, 0, 4, 2]  ← DIFFERENT SEQUENCE!
```

#### Existing But Unused Code (`src/sync.py:224-242`):
```python
def send_seed(connection, seed):
    """
    Share RNG seed with paired device for synchronized pattern generation.
    THIS FUNCTION EXISTS BUT IS NEVER CALLED!
    """
    cmd = SyncCommand(
        sequence_id=0,
        timestamp=get_precise_time(),
        command_type=SyncCommandType.SYNC_ADJ,
        data={"seed": seed}
    )
```

#### Impact

Even if Issue #1 were fixed (SECONDARY not running local engine), the patterns generated would still differ if using Model B synchronization.

---

### Issue #3: Multi-Message EOT Handling Bug (MEDIUM)

#### Problem Description

BLE receive uses EOT (0x04) framing, but if multiple messages arrive in one BLE packet, only the **first** is processed.

#### Code Location (`src/ble.py:386-405`):
```python
for i in range(bytes_read):
    byte = rx_buf[i]
    if byte == 0x04:  # EOT terminator
        # Message complete - decode and return
        message = msg_buf[:msg_len].decode('utf-8')
        conn._msg_len = 0  # PROBLEM: Discards remaining bytes!
        return message  # Returns immediately, ignores rest of buffer
```

#### Scenario

```
BLE Packet received: "SYNC:EXECUTE_BUZZ:finger|2\x04SYNC:EXECUTE_BUZZ:finger|0\x04"
                                              ↑                              ↑
                                           EOT #1                         EOT #2

Processing:
1. Reads until first EOT
2. Returns "SYNC:EXECUTE_BUZZ:finger|2"
3. Resets buffer (conn._msg_len = 0)
4. "SYNC:EXECUTE_BUZZ:finger|0" is LOST!
```

#### When This Occurs

- High-frequency EXECUTE_BUZZ commands during therapy
- BLE connection interval allows batching
- Multiple commands generated before BLE transmission

---

### Issue #4: No Continuous Clock Synchronization (MEDIUM)

#### Problem Description

`SimpleSyncProtocol` calculates clock offset once but never re-synchronizes during long sessions.

#### Code Location (`src/sync.py:291-305`):
```python
def calculate_offset(self, primary_time, secondary_time):
    offset = secondary_time - primary_time
    self.current_offset = offset
    self.last_sync_time = time.monotonic()
    return offset
    # NEVER CALLED AGAIN DURING SESSION!
```

#### Crystal Oscillator Drift

nRF52840 uses a 32MHz crystal with typical accuracy of ±40ppm:
- Drift rate: 40µs per second per device
- Combined drift (both devices): up to 80µs/second
- Over 2-hour session: 80µs × 7200s = **576ms potential drift**

#### Impact

vCR therapy requires <10ms bilateral synchronization. Without periodic re-sync, drift accumulates.

---

### Issue #5: I2C Busy-Wait Without Timeout (MEDIUM)

#### Problem Description

I2C lock acquisition uses infinite busy-wait that could freeze the device.

#### Code Location (`src/hardware.py:115-121`):
```python
def select_channel(self, channel):
    try:
        channel_mask = 1 << channel
        while not self.i2c.try_lock():
            pass  # INFINITE LOOP if I2C stuck!
```

#### Scenario

If I2C bus gets stuck (EMI, loose connection, driver crash), device freezes completely with no recovery path.

---

## Communication Protocol Analysis

### Current SYNC Message Format

```
Format: SYNC:COMMAND_TYPE:key1|val1|key2|val2...\x04
                                                 ↑ EOT terminator

Examples:
  SYNC:START_SESSION:duration_sec|7200|pattern_type|rndp|time_on_ms|100|time_off_ms|67|jitter_percent|235|num_fingers|5|mirror_pattern|1\x04
  SYNC:EXECUTE_BUZZ:left_finger|2|right_finger|2|amplitude|100\x04
  SYNC:PAUSE_SESSION:\x04
  SYNC:RESUME_SESSION:\x04
  SYNC:STOP_SESSION:reason|USER\x04
```

### Message Flow Timeline

```
Time        PRIMARY                    BLE                      SECONDARY
═════       ═══════                    ═══                      ═════════
0ms         Session starts
5ms         Sends START_SESSION ─────────────────────────────► Receives START_SESSION
                                                               Starts own TherapyEngine ⚠️
10ms        TherapyEngine.update()
15ms        Generates pattern [2,0,4,1,3]                      Generates [3,1,0,4,2] ⚠️
20ms        Activates finger 2
25ms        Sends EXECUTE_BUZZ(2) ───────────────────────────►
30ms                                                           TherapyEngine activates finger 3 ⚠️
40ms                                                           Receives EXECUTE_BUZZ(2)
45ms                                                           Activates finger 2 (DOUBLE!) ⚠️
```

---

## Recommendations

### Critical Fix #1: Disable SECONDARY's Local TherapyEngine

**Change `src/app.py:893-945`:**

```python
def _run_secondary_loop(self):
    """
    Main loop for SECONDARY device.

    SECONDARY is FOLLOWER ONLY - responds to PRIMARY commands.
    Does NOT run local pattern generation.
    """
    print(f"{DEVICE_TAG} Entering SECONDARY main loop (follower mode)")
    print(f"{DEVICE_TAG} Waiting for PRIMARY sync commands")

    while self.running:
        # DO NOT update local therapy engine on SECONDARY!
        # SECONDARY only responds to EXECUTE_BUZZ commands from PRIMARY
        # if self.therapy_engine:
        #     self.therapy_engine.update()  # REMOVED!

        # Process incoming BLE messages from PRIMARY
        self._process_incoming_ble_messages()

        # Update LED based on current state
        self._update_led_state()

        # Monitor battery
        self._check_battery()

        # Memory monitoring (keep existing code)
        ...

        time.sleep(0.01)  # Faster polling for command responsiveness
```

### Critical Fix #2: Simplify SECONDARY START_SESSION Handler

**Change `src/app.py:1197-1229`:**

```python
elif command_type == "START_SESSION":
    # SECONDARY enters running state but does NOT start local engine
    # SECONDARY only responds to EXECUTE_BUZZ commands from PRIMARY
    print(f"{DEVICE_TAG} [SYNC] Received START_SESSION from PRIMARY")

    # Update state machine to match PRIMARY
    self.state_machine.transition(StateTrigger.START_SESSION)

    # Update LED to show therapy active
    self.therapy_led_controller.set_therapy_state(TherapyState.RUNNING)

    print(f"{DEVICE_TAG} SECONDARY: Entered therapy mode (follower)")
    # DO NOT start therapy_engine.start_session()!
```

### Critical Fix #3: Fix Multi-Message EOT Handling

**Change `src/ble.py:386-408`:**

```python
for i in range(bytes_read):
    byte = rx_buf[i]
    if byte == 0x04:  # EOT terminator
        # Message complete - decode and return
        message = msg_buf[:msg_len].decode('utf-8')

        # Check for remaining data after EOT
        remaining_bytes = bytes_read - i - 1
        if remaining_bytes > 0:
            # Save remaining bytes for next call
            for j in range(remaining_bytes):
                msg_buf[j] = rx_buf[i + 1 + j]
            conn._msg_len = remaining_bytes
        else:
            conn._msg_len = 0

        conn.update_last_seen()
        return message
```

### High Priority Fix #4: Add I2C Timeout

**Change `src/hardware.py:112-124`:**

```python
def select_channel(self, channel):
    if not (0 <= channel <= 7):
        raise ValueError(f"Channel {channel} out of range (0-7)")

    try:
        channel_mask = 1 << channel

        # Acquire I2C lock with timeout
        timeout_start = time.monotonic()
        while not self.i2c.try_lock():
            if time.monotonic() - timeout_start > 0.1:  # 100ms timeout
                raise RuntimeError(f"I2C lock timeout on channel {channel}")
            time.sleep(0.001)  # Brief yield

        try:
            self.i2c.writeto(self.address, bytes([channel_mask]))
            self.active_channel = channel
        finally:
            self.i2c.unlock()
    except Exception as e:
        raise RuntimeError(f"Failed to select channel {channel}: {e}")
```

### Medium Priority Fix #5: Add Periodic Time Sync

**Add to `src/app.py` PRIMARY main loop:**

```python
# In _run_primary_loop(), add periodic sync:
self._last_time_sync = time.monotonic()
TIME_SYNC_INTERVAL = 60  # seconds

# In the while loop:
if time.monotonic() - self._last_time_sync > TIME_SYNC_INTERVAL:
    self._send_time_sync()
    self._last_time_sync = time.monotonic()

def _send_time_sync(self):
    """Send time synchronization to SECONDARY."""
    if self.secondary_connection:
        current_time = int(time.monotonic_ns() // 1000)  # microseconds
        self.ble.send(
            self.secondary_connection,
            f"SYNC:TIME_SYNC:timestamp|{current_time}"
        )
```

---

## Architecture Decision: Leader/Follower vs Synchronized Generation

### Current Mixed Model (Problematic)

```
PRIMARY: Generates patterns + sends EXECUTE_BUZZ
SECONDARY: Generates OWN patterns + receives EXECUTE_BUZZ
Result: Conflict!
```

### Recommended Model A: Pure Leader/Follower

```
PRIMARY (Leader):
  - Runs TherapyEngine
  - Generates all patterns
  - Sends EXECUTE_BUZZ for every activation
  - Handles all timing

SECONDARY (Follower):
  - NO local TherapyEngine running
  - Only responds to EXECUTE_BUZZ commands
  - Mirrors PRIMARY activations exactly

Pros:
  ✅ Guaranteed synchronization
  ✅ Single source of truth
  ✅ Simpler state management

Cons:
  ❌ Higher BLE traffic (command per activation)
  ❌ Latency-sensitive (BLE delay affects sync)
  ❌ Single point of failure
```

### Alternative Model B: Synchronized Generation (Future)

```
PRIMARY:
  - Generates seed at session start
  - Sends seed to SECONDARY
  - Both seed RNG identically
  - Both generate identical patterns locally
  - Only time sync needed (no per-activation commands)

SECONDARY:
  - Receives seed from PRIMARY
  - Seeds local RNG identically
  - Generates same patterns as PRIMARY
  - Runs independently (only time sync)

Pros:
  ✅ Lower BLE traffic
  ✅ More resilient to BLE latency
  ✅ True bilateral independence

Cons:
  ❌ More complex implementation
  ❌ Requires deterministic RNG
  ❌ Drift requires periodic re-sync
```

### Recommendation

**Implement Model A (Leader/Follower) first** as it requires minimal changes and guarantees sync. Model B can be explored later for improved resilience.

---

## Testing Checklist

### After Implementing Fixes

#### Test 1: Single Motor Activation
- [ ] Start session on PRIMARY
- [ ] Verify SECONDARY receives START_SESSION
- [ ] Verify SECONDARY state = RUNNING
- [ ] Verify SECONDARY TherapyEngine is NOT running updates
- [ ] Verify EXECUTE_BUZZ activates motor exactly once

#### Test 2: Pattern Verification
- [ ] Run 10 complete cycles
- [ ] Log finger activations on both devices
- [ ] Verify identical sequences on PRIMARY and SECONDARY
- [ ] Verify no double-activations

#### Test 3: Timing Accuracy
- [ ] Enable sync_stats on SECONDARY
- [ ] Run session for 5 minutes
- [ ] Check P95 latency < 10ms
- [ ] Check P99 latency < 15ms

#### Test 4: Long Session Drift
- [ ] Run session for 30 minutes
- [ ] Measure time difference between devices
- [ ] Verify drift < 50ms total

#### Test 5: Stress Test
- [ ] Run at maximum pattern speed (time_on=50ms, time_off=30ms)
- [ ] Verify no dropped commands
- [ ] Verify no BLE disconnection

---

## Files Requiring Changes

| File | Changes Required | Priority |
|------|------------------|----------|
| `src/app.py` | Disable SECONDARY TherapyEngine.update() | CRITICAL |
| `src/app.py` | Simplify START_SESSION handler | CRITICAL |
| `src/ble.py` | Fix multi-message EOT handling | HIGH |
| `src/hardware.py` | Add I2C lock timeout | MEDIUM |
| `src/app.py` | Add periodic time sync | MEDIUM |

---

## Conclusion

The BlueBuzzah firmware has a solid foundation with good memory optimization and recent BLE communication fixes. However, the **critical issue of dual TherapyEngine execution** must be resolved before bilateral therapy can work correctly.

The simplest fix is to adopt a **pure Leader/Follower model** where SECONDARY does not run its own pattern generation, only responding to EXECUTE_BUZZ commands from PRIMARY. This requires minimal code changes and guarantees synchronization.

**Priority Actions:**
1. 🔴 Disable SECONDARY's `TherapyEngine.update()` call
2. 🔴 Simplify SECONDARY's START_SESSION handler
3. 🟠 Fix multi-message EOT handling
4. 🟡 Add I2C timeout
5. 🟡 Implement periodic time sync

---

## Appendix A: Key File Locations

```
src/
├── app.py                          # Main application orchestrator
│   ├── _run_secondary_loop()       # Line 893 - SECONDARY main loop
│   ├── _handle_sync_command()      # Line 1097 - SYNC command handler
│   └── send_sync_to_secondary()    # Line 441 - Sync callback
├── application/
│   └── session/
│       └── manager.py              # Session lifecycle management
│           └── start_session()     # Line 189 - Session start logic
├── ble.py                          # BLE communication
│   ├── send()                      # Line 273 - Message transmission
│   └── receive()                   # Line 334 - Message reception with EOT
├── therapy.py                      # Therapy engine and patterns
│   ├── TherapyEngine               # Line 326 - Pattern execution
│   └── generate_random_permutation # Line 109 - RNDP generation
├── sync.py                         # Synchronization protocol
│   ├── SyncCommand                 # Line 85 - Command serialization
│   └── SimpleSyncProtocol          # Line 274 - Time offset calculation
├── hardware.py                     # Hardware abstractions
│   ├── I2CMultiplexer              # Line 68 - TCA9548A control
│   └── DRV2605Controller           # Line 144 - Haptic motor control
└── state.py                        # State machine
    └── TherapyStateMachine         # Line 57 - State transitions
```

---

## Appendix B: SYNC Command Reference

| Command | Direction | Purpose | Data Fields |
|---------|-----------|---------|-------------|
| `START_SESSION` | PRIMARY→SECONDARY | Begin therapy | duration_sec, pattern_type, time_on_ms, time_off_ms, jitter_percent, num_fingers, mirror_pattern |
| `PAUSE_SESSION` | PRIMARY→SECONDARY | Pause therapy | (none) |
| `RESUME_SESSION` | PRIMARY→SECONDARY | Resume therapy | (none) |
| `STOP_SESSION` | PRIMARY→SECONDARY | End therapy | reason |
| `EXECUTE_BUZZ` | PRIMARY→SECONDARY | Activate motors | left_finger, right_finger, amplitude |
| `DEACTIVATE` | PRIMARY→SECONDARY | Stop motors | left_finger, right_finger |
| `TIME_SYNC` | PRIMARY→SECONDARY | Clock sync | timestamp |

---

**Report Generated:** 2025-11-24
**Author:** Claude (Anthropic AI)
**Review Type:** Architecture & Synchronization Analysis
