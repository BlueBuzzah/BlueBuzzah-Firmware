# BlueBuzzah Firmware v2.0 - Remediation Action Plan

**Document Version:** 1.0
**Created:** 2025-01-23
**Status:** IN PROGRESS
**Source:** V2_FIRMWARE_DISCREPANCY_ANALYSIS.md
**Target:** Production-ready firmware with full feature parity

---

## Progress Dashboard

### Overall Status

| Phase | Items | Completed | In Progress | Not Started | % Complete |
|-------|-------|-----------|-------------|-------------|------------|
| PHASE 1: CRITICAL | 10 | 0 | 0 | 10 | 0% |
| PHASE 2: HIGH | 8 | 0 | 0 | 8 | 0% |
| PHASE 3: MEDIUM | 6 | 0 | 0 | 6 | 0% |
| PHASE 4: LOW | 2 | 0 | 0 | 2 | 0% |
| **TOTAL** | **26** | **0** | **0** | **26** | **0%** |

### Priority Breakdown

```
CRITICAL (10):  ████████████████████████████████████████  38%
HIGH (8):       ███████████████████████████████           31%
MEDIUM (6):     ███████████████████                       23%
LOW (2):        ████                                       8%
```

### Status Legend

- **[TODO]** - Not started
- **[IN PROGRESS]** - Currently being worked on
- **[TESTING]** - Implementation complete, under test
- **[COMPLETE]** - Tested and verified
- **[BLOCKED]** - Waiting on dependency or decision

---

## PHASE 1: CRITICAL ISSUES (Must Fix Before Production)

**Goal:** Eliminate runtime failures, connection instability, and data corruption risks
**Estimated Total Effort:** 12-16 hours
**Target Completion:** Week 1

---

### CRITICAL-001: Memory Budget Analysis and Feature Re-enablement

**Status:** [TODO]
**Priority:** CRITICAL
**Effort:** 4-6 hours
**Category:** Memory Management

**Location:**
- `src/app.py` lines 41-59 (APPLICATION_LAYER_AVAILABLE flag)
- `src/application/session/manager.py` (SessionManager)
- `src/application/calibration/controller.py` (CalibrationController)

**Problem:**
Memory budget was calculated incorrectly (claimed 52KB available, actual ~90KB with BLE active). SessionManager and CalibrationController were disabled unnecessarily.

**Root Cause:**
Misunderstanding of CircuitPython memory allocation on nRF52840.

**Solution:**
Re-enable both managers and verify memory stability during long sessions.

**Action Steps:**
1. [ ] Measure baseline memory usage
   - Add memory monitoring to boot sequence
   - Log `gc.mem_free()` at startup
   - Expected: ~130KB free without BLE, ~90KB with BLE active

2. [ ] Test SessionManager memory cost
   ```python
   # Add to app.py for testing:
   gc.collect()
   mem_before = gc.mem_free()
   from application.session.manager import SessionManager
   session_mgr = SessionManager()
   gc.collect()
   mem_after = gc.mem_free()
   print(f"[MEMORY] SessionManager cost: {mem_before - mem_after} bytes")
   ```
   - Expected cost: ~1.5KB
   - Acceptable if <2KB

3. [ ] Test CalibrationController memory cost
   - Import and instantiate CalibrationController
   - Measure memory impact
   - Expected cost: ~700 bytes
   - Acceptable if <1KB

4. [ ] Re-enable APPLICATION_LAYER_AVAILABLE flag
   - Set `APPLICATION_LAYER_AVAILABLE = True` in app.py
   - Uncomment SessionManager and CalibrationController imports
   - Wire up callbacks properly

5. [ ] Run 2-hour stress test
   - Monitor `gc.mem_free()` every 60 seconds
   - Log memory stats to file
   - Verify memory stays above 10KB free throughout session
   - Check for memory leaks (gradual decline)

6. [ ] Document actual memory budget
   - Update comments in app.py with correct values
   - Add memory monitoring to INFO command response

**Acceptance Criteria:**
- [ ] Baseline memory measurements documented
- [ ] SessionManager + CalibrationController total cost <2.5KB
- [ ] 2-hour session maintains >10KB free memory
- [ ] No memory leaks detected
- [ ] All SESSION_* commands functional
- [ ] All CALIBRATE_* commands functional

**Dependencies:** None

**Notes:**
- This is the highest priority item
- Unlocks 8 BLE commands (SESSION_* and CALIBRATE_*)
- If memory is tight, apply CRITICAL-002 (const() optimization) first

---

### CRITICAL-002: Add const() Usage to Constants

**Status:** [TODO]
**Priority:** CRITICAL
**Effort:** 1 hour
**Category:** Memory Optimization

**Location:**
- `src/core/constants.py` (entire file)

**Problem:**
~40 numeric constants stored in RAM unnecessarily. CircuitPython's `const()` stores constants in flash memory, freeing ~160 bytes RAM.

**Root Cause:**
Missing `from micropython import const` and const() wrapper usage.

**Solution:**
Convert all integer constants to use const(). Convert float timing values to milliseconds as integers where possible.

**Action Steps:**
1. [ ] Add const import
   ```python
   from micropython import const
   ```

2. [ ] Convert integer constants
   ```python
   # Before:
   DEFAULT_INTENSITY = 75
   DEFAULT_BURST_COUNT = 4

   # After:
   DEFAULT_INTENSITY = const(75)
   DEFAULT_BURST_COUNT = const(4)
   ```

3. [ ] Convert float timing to milliseconds
   ```python
   # Before:
   DEFAULT_BUZZ_DURATION = 0.050  # 50ms
   DEFAULT_INTER_BUZZ_DELAY = 0.050  # 50ms
   DEFAULT_INTER_BURST_DELAY = 2.0  # 2 seconds
   BLE_INTERVAL = 0.0075  # 7.5ms
   BLE_TIMEOUT = 0.1  # 100ms (NOTE: will change to 4000ms in CRITICAL-003)

   # After:
   DEFAULT_BUZZ_DURATION_MS = const(50)
   DEFAULT_INTER_BUZZ_DELAY_MS = const(50)
   DEFAULT_INTER_BURST_DELAY_MS = const(2000)
   BLE_INTERVAL_MS = const(7)  # 7.5ms -> 7ms (acceptable)
   BLE_TIMEOUT_MS = const(4000)  # 4 seconds (fixing timeout issue)
   ```

4. [ ] Update code using these constants
   - Search for all references to changed constant names
   - Update calculations to use milliseconds (divide by 1000 where needed)
   - Test timing-critical code paths

5. [ ] Document float constants that must remain floats
   - Add comments for any constants requiring exact float precision
   - Example: `DEFAULT_FREQUENCY = 2.0  # Must be float for DRV2605 calculation`

6. [ ] Measure memory savings
   - Check `gc.mem_free()` before and after changes
   - Expected savings: ~160 bytes

**Acceptance Criteria:**
- [ ] All integer constants use const()
- [ ] Timing constants converted to milliseconds
- [ ] All code using constants updated and tested
- [ ] Memory savings verified (~160 bytes)
- [ ] Therapy timing still accurate (verify buzz durations)

**Dependencies:** None

**Notes:**
- Quick win with significant RAM savings
- Should be done before CRITICAL-001 if memory is tight
- Be careful with timing precision - test thoroughly

---

### CRITICAL-003: Fix BLE Timeout Value

**Status:** [TODO]
**Priority:** CRITICAL
**Effort:** 30 minutes
**Category:** BLE Stability

**Location:**
- `src/core/constants.py` (BLE_TIMEOUT definition)
- Any code using BLE_TIMEOUT for connection checks

**Problem:**
BLE_TIMEOUT = 0.1 seconds (100ms) is dangerously short. Will cause false disconnects due to brief RF interference, distance, or obstacles.

**Root Cause:**
Timeout value not aligned with industry standards (4 seconds).

**Solution:**
Increase BLE_TIMEOUT to 4.0 seconds.

**Action Steps:**
1. [ ] Update BLE_TIMEOUT constant
   ```python
   # In constants.py:
   BLE_TIMEOUT_MS = const(4000)  # 4 seconds (if using const from CRITICAL-002)
   # OR
   BLE_TIMEOUT = 4.0  # 4 seconds (if not using const yet)
   ```

2. [ ] Find all uses of BLE_TIMEOUT
   - Search codebase for BLE_TIMEOUT references
   - Verify timeout logic is correct (milliseconds vs seconds)

3. [ ] Update timeout calculations if needed
   - If code expects seconds, use `BLE_TIMEOUT_MS / 1000`
   - If code expects milliseconds, use `BLE_TIMEOUT_MS` directly

4. [ ] Test connection stability
   - Test with device at 5m distance
   - Test with RF interference (microwave, WiFi traffic)
   - Test with temporary disconnection (cover antenna briefly)
   - Verify no false disconnects

5. [ ] Document timeout rationale
   - Add comment explaining 4-second industry standard
   - Reference iOS Core Bluetooth and Android BLE defaults

**Acceptance Criteria:**
- [ ] BLE_TIMEOUT set to 4000ms (4 seconds)
- [ ] No false disconnects during 1-hour test session
- [ ] Connection recovers properly after brief interference
- [ ] Timeout still catches actual disconnections within 5 seconds

**Dependencies:**
- Should be done as part of CRITICAL-002 (const() conversion)

**Notes:**
- Industry standard: iOS/Android use 4-8 second timeouts
- Nordic nRF52 SDK recommends 4 seconds minimum
- Critical for therapy session continuity

---

### CRITICAL-004: Implement EOT Message Framing

**Status:** [TODO]
**Priority:** CRITICAL
**Effort:** 2-3 hours
**Category:** BLE Protocol

**Location:**
- `src/ble.py` (message sending and receiving)
- `src/menu.py` (command processing)

**Problem:**
BLE UART is a byte stream without message boundaries. Messages can be split across reads or interleaved, causing corruption.

**Root Cause:**
No message terminator (EOT) implemented in protocol.

**Solution:**
Add EOT (0x04) terminator to all messages. Implement buffered message parsing with EOT detection.

**Action Steps:**
1. [ ] Add EOT constant
   ```python
   # In constants.py:
   EOT = const(0x04)  # End of Transmission character
   ```

2. [ ] Implement buffered message reader
   ```python
   # In ble.py or new ble_parser.py:
   class BLEMessageParser:
       def __init__(self):
           self._buffer = bytearray(256)  # Pre-allocated
           self._pos = 0

       def read_message(self, uart):
           while uart.in_waiting:
               byte = uart.read(1)[0]
               if byte == EOT:
                   # Complete message
                   msg = self._buffer[:self._pos].decode('utf-8')
                   self._pos = 0
                   return msg
               else:
                   self._buffer[self._pos] = byte
                   self._pos += 1
                   if self._pos >= 255:  # Overflow
                       self._pos = 0
                       return None  # Discard corrupted message
           return None  # Incomplete message
   ```

3. [ ] Update message sending to add EOT
   ```python
   # Wherever messages are sent:
   def send_message(self, message):
       self.uart.write(message.encode('utf-8'))
       self.uart.write(bytes([EOT]))  # Add terminator
   ```

4. [ ] Update all message receiving code
   - Replace direct `uart.read()` calls with parser
   - Handle None return (incomplete message)
   - Add overflow detection logging

5. [ ] Update BLE_PROTOCOL.md documentation
   - Document EOT requirement
   - Add examples showing EOT terminator
   - Update message format specification

6. [ ] Test message parsing
   - Send rapid messages (simulate high traffic)
   - Send partial messages (simulate split reads)
   - Verify no message corruption
   - Verify overflow protection works

**Acceptance Criteria:**
- [ ] All messages end with EOT (0x04)
- [ ] Parser handles partial messages correctly
- [ ] Parser handles message overflow (>256 bytes)
- [ ] No message corruption during rapid-fire test (100 msgs/sec)
- [ ] Protocol documentation updated

**Dependencies:** None

**Notes:**
- Essential for reliable BLE communication
- Prevents subtle bugs during high-traffic scenarios
- Should coordinate with mobile app team to update their implementation

---

### CRITICAL-005: Add 5ms Delay in DRV2605 Initialization

**Status:** [TODO]
**Priority:** CRITICAL
**Effort:** 15 minutes
**Category:** Hardware Initialization

**Location:**
- `src/hardware.py` lines 193-228 (DRV2605Controller.__init__)

**Problem:**
DRV2605 datasheet requires 5ms delay after exiting standby mode before configuring registers. Missing delay causes inconsistent motor behavior.

**Root Cause:**
Datasheet requirement not followed.

**Solution:**
Add `time.sleep(0.005)` after MODE_REG write.

**Action Steps:**
1. [ ] Locate DRV2605 initialization code
   ```python
   # Find this sequence in hardware.py:
   self._write_register(MODE_REG, 0x00)  # Exit standby
   self._write_register(LIBRARY_REG, library)  # Configuring too soon!
   ```

2. [ ] Add 5ms delay
   ```python
   self._write_register(MODE_REG, 0x00)  # Exit standby
   time.sleep(0.005)  # 5ms delay required by datasheet
   self._write_register(LIBRARY_REG, library)  # Now safe to configure
   ```

3. [ ] Add datasheet reference comment
   ```python
   # DRV2605 datasheet section 7.4.2:
   # "Wait 5ms after exiting standby before writing to registers"
   time.sleep(0.005)
   ```

4. [ ] Test motor consistency
   - Test all 8 motors (both PRIMARY and SECONDARY)
   - Run calibration sequence 10 times
   - Verify consistent motor response every time
   - No silent failures or weak vibrations

5. [ ] Test on cold boot vs warm boot
   - Test immediately after power-on
   - Test after soft reset
   - Verify consistent behavior in both cases

**Acceptance Criteria:**
- [ ] 5ms delay added after MODE_REG write
- [ ] Comment references datasheet requirement
- [ ] All 8 motors respond consistently
- [ ] 10/10 calibration sequences successful
- [ ] Works on cold boot and warm boot

**Dependencies:** None

**Notes:**
- Quick fix, high impact
- Prevents intermittent hardware issues
- May explain any "motors not working sometimes" bugs

---

### CRITICAL-006: Validate SimpleSyncProtocol Accuracy

**Status:** [TODO]
**Priority:** CRITICAL
**Effort:** 3-4 hours
**Category:** Therapy Effectiveness

**Location:**
- `src/sync.py` (SimpleSyncProtocol implementation)
- Documentation claiming <10ms sync accuracy

**Problem:**
Documentation claims <10ms bilateral sync accuracy, but this is likely unachievable. Reality is probably ~20-30ms due to BLE latency and time.monotonic() millisecond precision.

**Root Cause:**
1. `time.monotonic()` has millisecond precision (not microsecond)
2. BLE message round-trip latency is 15-25ms minimum
3. Sync accuracy never measured or validated

**Solution:**
Measure actual sync accuracy and update documentation with realistic claims.

**Action Steps:**
1. [ ] Add sync accuracy measurement logging
   ```python
   # In sync.py or therapy.py:
   def log_sync_accuracy(primary_timestamp, secondary_execution_time):
       latency_ms = secondary_execution_time - primary_timestamp
       # Log to file or circular buffer
       print(f"[SYNC] Latency: {latency_ms}ms")
   ```

2. [ ] Run 2-hour measurement session
   - Enable sync logging
   - Collect latency measurements for entire session
   - Store in file for analysis: `sync_latency_log.txt`

3. [ ] Analyze latency statistics
   ```python
   # Post-processing script:
   latencies = [...]  # Load from log file
   mean_latency = sum(latencies) / len(latencies)
   median_latency = sorted(latencies)[len(latencies) // 2]
   p95_latency = sorted(latencies)[int(len(latencies) * 0.95)]
   p99_latency = sorted(latencies)[int(len(latencies) * 0.99)]
   max_latency = max(latencies)

   print(f"Mean: {mean_latency}ms")
   print(f"Median: {median_latency}ms")
   print(f"P95: {p95_latency}ms")
   print(f"P99: {p99_latency}ms")
   print(f"Max: {max_latency}ms")
   ```

4. [ ] Validate clinical acceptability
   - Check if measured latency meets therapy requirements
   - Consult literature: Is 25-30ms bilateral sync acceptable for vCR?
   - Document any therapy effectiveness implications

5. [ ] Update documentation with measured values
   - Replace "<10ms sync accuracy" with actual measurements
   - Example: "Typical sync accuracy: 25ms (P95: 35ms)"
   - Add note about BLE latency limitations

6. [ ] Consider improvements if needed
   - If >50ms is unacceptable, document alternative approaches:
     - Hardware GPIO sync trigger
     - Faster BLE connection interval (trade-off: power)
     - Pre-synchronized clocks with drift compensation

**Acceptance Criteria:**
- [ ] Sync latency measured over 2-hour session
- [ ] Statistics calculated (mean, median, P95, P99, max)
- [ ] Clinical acceptability validated
- [ ] Documentation updated with accurate claims
- [ ] Alternative solutions documented if accuracy insufficient

**Dependencies:** None (but easier after CRITICAL-001 if SessionManager tracks sync stats)

**Notes:**
- Critical for therapy effectiveness claims
- May affect clinical trial results
- Be prepared to implement hardware sync if BLE proves insufficient

---

### CRITICAL-007: Test SESSION Commands Functionality

**Status:** [TODO]
**Priority:** CRITICAL
**Effort:** 1 hour
**Category:** Functional Testing

**Location:**
- `src/menu.py` (SESSION_* command handlers)
- `src/application/session/manager.py` (SessionManager - currently disabled)

**Problem:**
SessionManager is disabled but SESSION_* commands are documented as working. Need to verify actual behavior.

**Dependencies:**
- Should be done AFTER CRITICAL-001 (SessionManager re-enablement)
- If SessionManager cannot be re-enabled, need fallback implementation

**Solution:**
Test all 5 SESSION commands and verify functionality.

**Action Steps:**
1. [ ] Connect BLE test client to PRIMARY
   - Use Nordic UART app or custom test script
   - Verify connection established

2. [ ] Test SESSION_START command
   ```
   Send: SESSION_START
   Expected: Session begins, therapy starts
   Actual: [Record response and behavior]
   ```

3. [ ] Test SESSION_PAUSE command
   ```
   Send: SESSION_PAUSE
   Expected: Therapy pauses, motors stop, can be resumed
   Actual: [Record response and behavior]
   ```

4. [ ] Test SESSION_RESUME command
   ```
   Send: SESSION_RESUME
   Expected: Therapy resumes from paused state
   Actual: [Record response and behavior]
   ```

5. [ ] Test SESSION_STOP command
   ```
   Send: SESSION_STOP
   Expected: Session ends, stats reported, returns to IDLE
   Actual: [Record response and behavior]
   ```

6. [ ] Test SESSION_STATUS command
   ```
   Send: SESSION_STATUS
   Expected: Current session info (elapsed time, cycle count, etc.)
   Actual: [Record response and behavior]
   ```

7. [ ] Document findings
   - Which commands work as expected?
   - Which commands return errors?
   - What is actual behavior vs documented behavior?

8. [ ] Choose resolution path
   - If all work: Mark CRITICAL-007 as complete
   - If some fail: Implement fixes or document limitations
   - If all fail: Re-enable SessionManager (CRITICAL-001) or implement stub

**Acceptance Criteria:**
- [ ] All 5 SESSION commands tested
- [ ] Behavior documented
- [ ] Commands either work correctly OR limitations documented
- [ ] Mobile app team notified of any behavior differences

**Dependencies:**
- CRITICAL-001 (SessionManager re-enablement) recommended first

**Notes:**
- May discover SessionManager is not actually needed (commands work anyway)
- May need stub implementation if SessionManager doesn't fit
- Coordinate findings with mobile app development

---

### CRITICAL-008: Test CALIBRATE Commands Functionality

**Status:** [TODO]
**Priority:** CRITICAL
**Effort:** 1 hour
**Category:** Functional Testing

**Location:**
- `src/menu.py` (CALIBRATE_* command handlers)
- `src/application/calibration/controller.py` (CalibrationController - currently disabled)

**Problem:**
CalibrationController is disabled but CALIBRATE_* commands are documented as working. Need to verify actual behavior.

**Dependencies:**
- Should be done AFTER CRITICAL-001 (CalibrationController re-enablement)
- If CalibrationController cannot be re-enabled, need fallback implementation

**Solution:**
Test all 3 CALIBRATE commands and verify functionality.

**Action Steps:**
1. [ ] Connect BLE test client to device
   - Ensure device is in IDLE or READY state

2. [ ] Test CALIBRATE_START command
   ```
   Send: CALIBRATE_START
   Expected: Enters calibration mode
   Actual: [Record response]
   ```

3. [ ] Test CALIBRATE_BUZZ command for each motor
   ```
   Send: CALIBRATE_BUZZ:0:50:500  (motor 0, intensity 50, duration 500ms)
   Expected: Motor 0 vibrates at intensity 50 for 500ms
   Actual: [Record response and motor behavior]

   Repeat for motors 1-7
   ```

4. [ ] Test CALIBRATE_BUZZ with different intensities
   ```
   Test: intensity 25, 50, 75, 100
   Verify: Motor response scales appropriately
   ```

5. [ ] Test CALIBRATE_STOP command
   ```
   Send: CALIBRATE_STOP
   Expected: Exits calibration mode, returns to IDLE
   Actual: [Record response]
   ```

6. [ ] Test error handling
   ```
   Test: CALIBRATE_BUZZ with invalid motor index (>7)
   Test: CALIBRATE_BUZZ with invalid intensity (>100)
   Test: CALIBRATE_BUZZ while therapy is running
   Verify: Appropriate error messages
   ```

7. [ ] Document findings
   - Which commands work as expected?
   - Motor response quality and consistency
   - Any errors or limitations

8. [ ] Choose resolution path
   - If all work: Mark CRITICAL-008 as complete
   - If some fail: Implement fixes or document limitations
   - If all fail: Re-enable CalibrationController (CRITICAL-001) or implement stub

**Acceptance Criteria:**
- [ ] All 3 CALIBRATE commands tested
- [ ] All 8 motors verified working
- [ ] Intensity scaling verified
- [ ] Commands either work correctly OR limitations documented
- [ ] Clinical calibration workflow documented

**Dependencies:**
- CRITICAL-001 (CalibrationController re-enablement) recommended first

**Notes:**
- Critical for clinical tuning and patient-specific intensity mapping
- May need manual calibration procedure if controller doesn't fit
- Document workarounds for clinical use

---

### CRITICAL-009: Test Phone Integration End-to-End

**Status:** [TODO]
**Priority:** CRITICAL
**Effort:** 4 hours
**Category:** Integration Testing

**Location:**
- All BLE command handling in `src/menu.py`
- `src/ble.py` (phone connection management)
- Mobile app (external dependency)

**Problem:**
BLE infrastructure exists but end-to-end command flow from phone not fully integration tested.

**Dependencies:**
- CRITICAL-004 (EOT message framing) should be done first
- Mobile app must support EOT terminators
- CRITICAL-007 and CRITICAL-008 (command testing) should be done first

**Solution:**
Comprehensive end-to-end testing with actual mobile app.

**Action Steps:**
1. [ ] Prepare test environment
   - Deploy firmware to PRIMARY device
   - Install mobile app on phone
   - Prepare test data collection (logs, notes)

2. [ ] Test basic connectivity
   - Connect phone to PRIMARY via BLE
   - Verify connection established
   - Verify reconnection after disconnection

3. [ ] Test all 18 BLE commands from phone
   ```
   1. INFO - Get device information
   2. BATTERY - Get battery status
   3. PING - Test connection
   4. PROFILE_LIST - List available profiles
   5. PROFILE_LOAD - Load a profile
   6. PROFILE_GET - Get current profile
   7. PROFILE_CUSTOM - Set custom parameters
   8. SESSION_START - Start therapy session
   9. SESSION_PAUSE - Pause session
   10. SESSION_RESUME - Resume session
   11. SESSION_STOP - Stop session
   12. SESSION_STATUS - Get session status
   13. PARAM_SET - Update parameters
   14. CALIBRATE_START - Enter calibration mode
   15. CALIBRATE_BUZZ - Test motor
   16. CALIBRATE_STOP - Exit calibration mode
   17. HELP - Get command list
   18. RESTART - Restart device
   ```

4. [ ] Test response format verification
   - Verify all responses end with EOT
   - Verify response format matches BLE_PROTOCOL.md
   - Check for any protocol violations

5. [ ] Test during active therapy
   - Start therapy session
   - Send commands while therapy is running
   - Verify: Commands don't interrupt therapy
   - Verify: Responses still arrive correctly
   - Test: Message interleaving (rapid commands)

6. [ ] Test error handling
   - Send invalid commands
   - Send malformed parameters
   - Send commands in wrong state (e.g., SESSION_PAUSE when not running)
   - Verify: Appropriate error messages
   - Verify: System remains stable

7. [ ] Test edge cases
   - Very long command strings (>256 bytes)
   - Rapid-fire commands (100+ commands/second)
   - Commands during BLE reconnection
   - Commands during low battery

8. [ ] Test simultaneous connections
   - Connect phone to PRIMARY
   - Connect SECONDARY to PRIMARY
   - Verify: Both connections maintained
   - Verify: Internal messages (EXECUTE_BUZZ, etc.) not sent to phone
   - Verify: Phone commands don't interfere with sync messages

9. [ ] Document any issues found
   - List bugs discovered
   - List protocol violations
   - List areas needing improvement

10. [ ] Create test report
    - Summary of test results
    - Pass/fail for each command
    - Issues found and severity
    - Recommendations for fixes

**Acceptance Criteria:**
- [ ] All 18 commands tested from mobile app
- [ ] All responses received correctly with EOT
- [ ] Commands work during active therapy
- [ ] No message corruption detected
- [ ] Error handling verified
- [ ] Simultaneous connections (phone + SECONDARY) verified
- [ ] Test report completed

**Dependencies:**
- CRITICAL-004 (EOT framing)
- CRITICAL-007 (SESSION commands)
- CRITICAL-008 (CALIBRATE commands)
- Mobile app with EOT support

**Notes:**
- Most comprehensive test of entire system
- May uncover issues not found in unit testing
- Coordinate with mobile app team for any changes needed
- Critical for production readiness

---

### CRITICAL-010: Reduce F-String Usage in Hot Paths

**Status:** [TODO]
**Priority:** CRITICAL
**Effort:** 2 hours
**Category:** Memory Optimization

**Location:**
- `src/app.py` (95 f-string instances)
- Main event loops
- Callback functions

**Problem:**
F-strings allocate new memory on every execution. In hot paths (main loops, callbacks), this causes heap fragmentation and eventual MemoryError.

**Root Cause:**
Convenient f-string syntax used without considering memory implications.

**Solution:**
Replace f-strings in hot paths with pre-allocated strings or eliminate verbose logging.

**Action Steps:**
1. [ ] Identify hot path f-strings
   - Main event loops (_run_primary_loop, _run_secondary_loop)
   - Callbacks called frequently (on_therapy_cycle_complete, etc.)
   - Message processing functions

2. [ ] Create pre-allocated format strings
   ```python
   # At module level in app.py:
   MSG_RECEIVED = "[BLE] Message received: "
   CYCLE_COMPLETE = "[THERAPY] Cycle "
   CYCLE_COMPLETE_END = " complete"
   BATTERY_LEVEL = "[BATTERY] Level: "
   MEMORY_FREE = "[MEMORY] Free: "
   MEMORY_FREE_BYTES = " bytes"
   ```

3. [ ] Replace f-strings in hot paths
   ```python
   # Before (allocates new string each time):
   print(f"[BLE] Message received: {message}")
   print(f"[THERAPY] Cycle {cycle_num} complete")
   print(f"[BATTERY] Level: {battery_level}%")

   # After (reuses strings):
   print(MSG_RECEIVED, message)
   print(CYCLE_COMPLETE, cycle_num, CYCLE_COMPLETE_END)
   print(BATTERY_LEVEL, battery_level, "%")
   ```

4. [ ] Add DEBUG flag for verbose logging
   ```python
   # In constants.py:
   DEBUG = const(0)  # Set to 1 for debug builds

   # In code:
   if DEBUG:
       print(MSG_RECEIVED, message)
   # Production builds skip logging entirely
   ```

5. [ ] Profile memory usage before/after
   - Run 1-hour session with old code
   - Monitor memory fragmentation
   - Run 1-hour session with new code
   - Compare memory stability

6. [ ] Keep f-strings in non-critical paths
   - One-time initialization messages: OK
   - Error messages: OK (rare events)
   - Boot sequence: OK (only once)
   - Commands: OK (not in tight loops)

**Acceptance Criteria:**
- [ ] All f-strings removed from main loops
- [ ] All f-strings removed from frequent callbacks
- [ ] Pre-allocated format strings created
- [ ] DEBUG flag implemented
- [ ] Memory stability improved in 1-hour test
- [ ] Cold path f-strings (initialization, errors) can remain

**Dependencies:** None

**Notes:**
- Important for long-term memory stability
- Trade-off: Slightly less readable code
- Keep DEBUG logging for development, disable for production
- Focus on loops and callbacks, not one-time events

---

## PHASE 2: HIGH PRIORITY (Fix Soon)

**Goal:** Fix issues causing significant confusion or functional gaps
**Estimated Total Effort:** 16-20 hours
**Target Completion:** Week 2

---

### HIGH-001: Implement BLE Connection Recovery Logic

**Status:** [TODO]
**Priority:** HIGH
**Effort:** 2-3 hours
**Category:** BLE Reliability

**Location:**
- `src/app.py` (main loops: _run_primary_loop, _run_secondary_loop)

**Problem:**
If BLE connection is lost during therapy, there's no recovery mechanism. Therapy continues unsynchronized or stops.

**Solution:**
Add connection lost detection and recovery logic.

**Action Steps:**
1. [ ] Add connection timeout detection
   ```python
   CONNECTION_LOST_THRESHOLD = const(5)  # 5 seconds
   last_message_time = time.monotonic()

   while True:
       if ble.connected:
           message = ble.receive()
           if message:
               last_message_time = time.monotonic()
               process_message(message)

       # Timeout detection
       if time.monotonic() - last_message_time > CONNECTION_LOST_THRESHOLD:
           print("[BLE] Connection timeout detected")
           handle_connection_lost()
   ```

2. [ ] Implement connection recovery
   ```python
   def handle_connection_lost(self):
       print("[BLE] Connection lost - attempting recovery")

       # Stop therapy as fail-safe
       self.therapy_engine.stop()
       self.led_controller.set_error_pattern()

       # Attempt reconnection
       self.ble.disconnect()  # Clean disconnect
       gc.collect()

       if self.device_role == "PRIMARY":
           self.ble.start_advertising()
           # Wait for SECONDARY and/or phone to reconnect
       else:  # SECONDARY
           self.ble.start_scanning()
           # Attempt to reconnect to PRIMARY
   ```

3. [ ] Add reconnection attempts limit
   ```python
   MAX_RECONNECT_ATTEMPTS = const(3)
   reconnect_count = 0

   while reconnect_count < MAX_RECONNECT_ATTEMPTS:
       if attempt_reconnection():
           print("[BLE] Reconnection successful")
           break
       reconnect_count += 1
       time.sleep(2)  # Wait 2 seconds between attempts

   if reconnect_count >= MAX_RECONNECT_ATTEMPTS:
       print("[BLE] Reconnection failed - entering error state")
       self.state_machine.transition_to_error()
   ```

4. [ ] Add LED feedback for connection state
   - Connected: Normal operation LEDs
   - Connection lost: Red blinking LED
   - Reconnecting: Yellow pulsing LED
   - Reconnection failed: Red solid LED

5. [ ] Test recovery scenarios
   - Force disconnection during therapy (cover antenna)
   - Verify therapy stops immediately
   - Verify reconnection attempts
   - Verify therapy can resume after reconnection
   - Test reconnection failure path (device out of range)

**Acceptance Criteria:**
- [ ] Connection timeout detection implemented (5 second threshold)
- [ ] Therapy stops immediately on connection loss (fail-safe)
- [ ] Automatic reconnection attempted (up to 3 times)
- [ ] LED feedback indicates connection state
- [ ] Recovery tested in various scenarios
- [ ] System stable after reconnection

**Dependencies:**
- CRITICAL-003 (BLE timeout) should be done first

**Notes:**
- Critical for patient safety (therapy must stop if coordination lost)
- Improves user experience (automatic reconnection)
- Document expected behavior in user manual

---

### HIGH-002: Remove JSON Import from sync.py

**Status:** [TODO]
**Priority:** HIGH
**Effort:** 1-2 hours
**Category:** Memory Optimization

**Location:**
- `src/sync.py` line 17 (import json)
- SimpleSyncProtocol message serialization

**Problem:**
JSON import costs ~2KB RAM. Sync messages are simple enough to serialize manually.

**Solution:**
Replace JSON serialization with pipe-delimited format.

**Action Steps:**
1. [ ] Remove JSON import
   ```python
   # Delete this line:
   import json
   ```

2. [ ] Implement manual serialization
   ```python
   def serialize_buzz_cmd(timestamp, finger_index, intensity):
       return f"EXECUTE_BUZZ|{timestamp}|{finger_index}|{intensity}"

   def serialize_buzz_complete(timestamp, finger_index):
       return f"BUZZ_COMPLETE|{timestamp}|{finger_index}"

   def serialize_param_update(param_name, value):
       return f"PARAM_UPDATE|{param_name}|{value}"

   def serialize_seed(seed_value):
       return f"SEED|{seed_value}"

   def serialize_seed_ack():
       return "SEED_ACK"
   ```

3. [ ] Implement manual deserialization
   ```python
   def deserialize_message(message):
       parts = message.split("|")
       cmd = parts[0]

       if cmd == "EXECUTE_BUZZ":
           return {
               "cmd": cmd,
               "ts": int(parts[1]),
               "finger": int(parts[2]),
               "intensity": int(parts[3])
           }
       elif cmd == "BUZZ_COMPLETE":
           return {
               "cmd": cmd,
               "ts": int(parts[1]),
               "finger": int(parts[2])
           }
       elif cmd == "PARAM_UPDATE":
           return {
               "cmd": cmd,
               "param": parts[1],
               "value": parts[2]  # Type conversion depends on param
           }
       elif cmd == "SEED":
           return {
               "cmd": cmd,
               "seed": int(parts[1])
           }
       elif cmd == "SEED_ACK":
           return {"cmd": cmd}
       else:
           return None  # Unknown command
   ```

4. [ ] Update all JSON usage in sync.py
   - Replace `json.dumps()` with serialize_* functions
   - Replace `json.loads()` with deserialize_message()

5. [ ] Add error handling
   ```python
   def deserialize_message(message):
       try:
           parts = message.split("|")
           # ... parsing logic ...
       except (IndexError, ValueError) as e:
           print(f"[SYNC] Parse error: {e}")
           return None
   ```

6. [ ] Test sync protocol
   - Test EXECUTE_BUZZ messages
   - Test BUZZ_COMPLETE messages
   - Test PARAM_UPDATE messages
   - Test SEED and SEED_ACK
   - Verify bilateral sync still works
   - Test with malformed messages

7. [ ] Measure memory savings
   - Check gc.mem_free() before and after
   - Expected savings: ~2KB (2048 bytes)

**Acceptance Criteria:**
- [ ] JSON import removed
- [ ] Manual serialization implemented for all 5 message types
- [ ] Bilateral sync still functional
- [ ] Error handling for malformed messages
- [ ] Memory savings verified (~2KB)

**Dependencies:** None

**Notes:**
- Easy optimization with significant savings
- Pipe-delimited format is simpler and faster than JSON
- No loss of functionality
- Compatible with existing protocol (just different encoding)

---

### HIGH-003: Implement uart.readinto() Buffer Reuse

**Status:** [TODO]
**Priority:** HIGH
**Effort:** 1 hour
**Category:** Memory Optimization

**Location:**
- `src/ble.py` line ~262 (BLE message reading)

**Problem:**
`uart.read()` allocates a new buffer on each call. Over 2-hour session with 10msg/sec, this is 72,000 allocations causing heap fragmentation.

**Solution:**
Pre-allocate buffer once and use `uart.readinto()` to reuse it.

**Action Steps:**
1. [ ] Add pre-allocated buffer to BLEController.__init__
   ```python
   class BLEController:
       def __init__(self):
           # ... existing init ...
           self._read_buffer = bytearray(256)  # Pre-allocate once
           self._read_pos = 0
   ```

2. [ ] Replace uart.read() with uart.readinto()
   ```python
   # Before:
   data = uart.read()  # Allocates new buffer
   if data:
       message = data.decode('utf-8')

   # After:
   num_bytes = uart.readinto(self._read_buffer)
   if num_bytes:
       message = self._read_buffer[:num_bytes].decode('utf-8')
   ```

3. [ ] Integrate with EOT message parser (from CRITICAL-004)
   ```python
   def read_message(self):
       while self.uart.in_waiting:
           num_bytes = self.uart.readinto(self._read_buffer, 1)  # Read 1 byte
           if num_bytes:
               byte = self._read_buffer[0]
               if byte == EOT:
                   # Complete message
                   msg = self._message_buffer[:self._msg_pos].decode('utf-8')
                   self._msg_pos = 0
                   return msg
               else:
                   self._message_buffer[self._msg_pos] = byte
                   self._msg_pos += 1
       return None
   ```

4. [ ] Test BLE communication
   - Verify messages still received correctly
   - Test with rapid messages
   - Test with long messages (>100 bytes)
   - Verify no buffer corruption

5. [ ] Monitor heap fragmentation
   - Run 2-hour session
   - Monitor gc.mem_free() over time
   - Compare with old implementation (if possible)
   - Verify less fragmentation (more stable free memory)

**Acceptance Criteria:**
- [ ] Pre-allocated buffer implemented
- [ ] uart.readinto() used instead of uart.read()
- [ ] BLE communication still functional
- [ ] Heap fragmentation reduced (verified over 2-hour test)

**Dependencies:**
- Coordinate with CRITICAL-004 (EOT message framing)

**Notes:**
- Significant reduction in allocations
- Improves long-term memory stability
- May need to adjust buffer size based on max message length

---

### HIGH-004: Document Dependency Injection Architecture

**Status:** [TODO]
**Priority:** HIGH
**Effort:** 3 hours
**Category:** Documentation

**Location:**
- Create new section in `docs/FIRMWARE_ARCHITECTURE.md`
- Reference `src/app.py` lines 206-433

**Problem:**
BlueBuzzahApplication uses sophisticated dependency injection across 5 initialization methods (230+ lines), but this critical architectural pattern is not documented.

**Solution:**
Add comprehensive documentation explaining the DI pattern, initialization order, and extension points.

**Action Steps:**
1. [ ] Add "Dependency Injection Architecture" section to FIRMWARE_ARCHITECTURE.md

2. [ ] Document the 5 initialization layers
   ```markdown
   ## Dependency Injection Architecture

   BlueBuzzah v2.0 uses a layered dependency injection pattern to initialize
   components in the correct order and manage dependencies explicitly.

   ### Initialization Layers

   The application initializes components in 5 sequential phases:

   1. **Core Systems** (_initialize_core_systems)
      - State machine
      - Device role detection
      - Logging setup

   2. **Hardware Layer** (_initialize_hardware)
      - Board configuration
      - LED controller
      - DRV2605 haptic drivers
      - Battery monitor
      - I2C multiplexer

   3. **Infrastructure Layer** (_initialize_infrastructure)
      - BLE service initialization
      - Connection management
      - Protocol handlers

   4. **Domain Layer** (_initialize_domain)
      - Therapy engine
      - Sync protocol
      - Pattern generators
      - Profile management

   5. **Application Layer** (_initialize_application)
      - Session manager (if enabled)
      - Calibration controller (if enabled)
      - Command handlers
      - Menu controller

   6. **Presentation Layer** (_initialize_presentation)
      - LED feedback controller
      - BLE interface orchestration
   ```

3. [ ] Document initialization order diagram
   ```markdown
   ### Initialization Flow

   ```
   main.py
     ├─> load_device_configuration()
     └─> BlueBuzzahApplication.__init__()
           ├─> _initialize_core_systems()
           ├─> _initialize_hardware()
           ├─> _initialize_infrastructure()
           ├─> _initialize_domain()
           ├─> _initialize_application()
           └─> _initialize_presentation()
   ```

4. [ ] Document dependency relationships
   ```markdown
   ### Component Dependencies

   | Component | Depends On | Used By |
   |-----------|-----------|---------|
   | TherapyEngine | DRV2605Controller, PatternGenerators | SessionManager |
   | SyncProtocol | BLEController | TherapyEngine |
   | SessionManager | TherapyEngine, ProfileManager | MenuController |
   | CalibrationController | DRV2605Controller | MenuController |
   | MenuController | SessionManager, CalibrationController, ProfileManager | BLEController |
   ```

5. [ ] Document extension points
   ```markdown
   ### Extending the Application

   To add a new component:

   1. Determine which layer it belongs to
   2. Add initialization in the appropriate _initialize_* method
   3. Inject dependencies via constructor parameters
   4. Wire up callbacks if needed
   5. Update this documentation

   Example: Adding a new sensor
   ```python
   # In _initialize_hardware():
   self.new_sensor = NewSensorController(
       i2c_bus=self.i2c_bus,
       multiplexer=self.i2c_multiplexer
   )

   # In _initialize_domain():
   self.therapy_engine = TherapyEngine(
       haptic_controller=self.haptic_controller,
       new_sensor=self.new_sensor  # Inject dependency
   )
   ```

6. [ ] Document why DI pattern was chosen
   ```markdown
   ### Design Rationale

   Dependency injection was chosen for:

   - **Testability**: Components can be tested in isolation with mock dependencies
   - **Memory efficiency**: Components created once, referenced multiple times
   - **Clear dependencies**: No hidden global state or circular imports
   - **Initialization order**: Explicit control over component creation sequence
   - **Flexibility**: Easy to swap implementations (e.g., mock BLE for testing)
   ```

7. [ ] Add testing guidance
   ```markdown
   ### Testing with Dependency Injection

   Components can be tested with mock dependencies:

   ```python
   # test_therapy_engine.py
   class MockHapticController:
       def activate_motor(self, motor, intensity, duration):
           self.last_activation = (motor, intensity, duration)

   # Test with mock
   mock_haptic = MockHapticController()
   engine = TherapyEngine(haptic_controller=mock_haptic)
   engine.execute_buzz(motor=0, intensity=50)

   assert mock_haptic.last_activation == (0, 50, DEFAULT_BUZZ_DURATION)
   ```

**Acceptance Criteria:**
- [ ] "Dependency Injection Architecture" section added to FIRMWARE_ARCHITECTURE.md
- [ ] All 5 initialization layers documented
- [ ] Initialization flow diagram included
- [ ] Component dependency table created
- [ ] Extension points explained with example
- [ ] Design rationale documented
- [ ] Testing guidance provided

**Dependencies:** None

**Notes:**
- Important for maintainability
- Helps new developers understand the architecture
- Documents design decisions
- Makes testing strategy clear

---

### HIGH-005: Document Callback-Based Event System

**Status:** [TODO]
**Priority:** HIGH
**Effort:** 2 hours
**Category:** Documentation

**Location:**
- Create new section in `docs/FIRMWARE_ARCHITECTURE.md`
- Reference `src/app.py` lines 436-487

**Problem:**
Inter-component communication uses direct callbacks (11+ callback methods), but event flow is not documented.

**Solution:**
Add comprehensive documentation of callback system and event flow.

**Action Steps:**
1. [ ] Add "Event System Architecture" section to FIRMWARE_ARCHITECTURE.md

2. [ ] Document all callback methods
   ```markdown
   ## Event System Architecture

   BlueBuzzah uses a callback-based event system for inter-component communication.

   ### Callback Registry

   | Callback | Parameters | Triggered By | Purpose |
   |----------|-----------|--------------|---------|
   | _on_session_started | session_id, profile_name | SessionManager | Notify components of session start |
   | _on_session_paused | None | SessionManager | Handle therapy pause |
   | _on_session_resumed | None | SessionManager | Handle therapy resume |
   | _on_session_stopped | reason | SessionManager | Clean up after session end |
   | _on_therapy_cycle_complete | cycle_stats | TherapyEngine | Log cycle completion, update stats |
   | on_battery_low | None | BatteryMonitor | Warn user, reduce intensity |
   | on_battery_critical | None | BatteryMonitor | Stop therapy, force shutdown |
   | on_connection_lost | None | BLEController | Attempt reconnection |
   | on_state_changed | old_state, new_state, trigger | StateMachine | Update LEDs, log transitions |
   ```

3. [ ] Create event flow diagrams for key operations
   ```markdown
   ### Event Flow: Starting a Therapy Session

   ```
   User sends SESSION_START command
     └─> MenuController.handle_session_start()
           └─> SessionManager.start_session()
                 ├─> Loads profile from ProfileManager
                 ├─> Configures TherapyEngine
                 ├─> Calls _on_session_started(session_id, profile_name)
                 │     ├─> LEDController.set_running_pattern()
                 │     ├─> BLEController.send_response("SESSION_STARTED")
                 │     └─> Logs session start event
                 └─> TherapyEngine.start()
                       └─> Begins therapy cycles
                             └─> Calls _on_therapy_cycle_complete() after each cycle
   ```

4. [ ] Document callback wiring
   ```markdown
   ### Callback Wiring

   Callbacks are wired during initialization:

   ```python
   # In _initialize_application():
   self.session_manager = SessionManager(
       on_session_started=self._on_session_started,
       on_session_paused=self._on_session_paused,
       on_session_resumed=self._on_session_resumed,
       on_session_stopped=self._on_session_stopped
   )

   self.therapy_engine = TherapyEngine(
       on_cycle_complete=self._on_therapy_cycle_complete
   )

   self.battery_monitor = BatteryMonitor(
       on_low_battery=self.on_battery_low,
       on_critical_battery=self.on_battery_critical
   )
   ```

5. [ ] Explain design choice
   ```markdown
   ### Why Callbacks Instead of Event Bus?

   Callbacks were chosen over a centralized event bus for:

   - **Memory efficiency**: No event queue to manage (saves RAM)
   - **Simplicity**: Direct function calls, easy to debug
   - **Performance**: No event dispatch overhead
   - **Type safety**: Callback signatures enforce correct parameter types

   Trade-offs:

   - **Tight coupling**: Components must know about each other's callbacks
   - **No event replay**: Events are immediate, not queued
   - **No filtering**: All callbacks receive all events

   For a memory-constrained embedded system, these trade-offs are acceptable.
   ```

6. [ ] Add debugging guidance
   ```markdown
   ### Debugging Event Flow

   To trace event propagation:

   1. Add logging to callback methods:
      ```python
      def _on_session_started(self, session_id, profile_name):
          print(f"[EVENT] Session started: {session_id}, profile: {profile_name}")
          # ... rest of callback ...
      ```

   2. Use stack traces to find callback callers:
      ```python
      import traceback
      print("[EVENT] Callback stack:")
      traceback.print_stack()
      ```

   3. Common issues:
      - Callback not called: Check if callback was wired in initialization
      - Callback called twice: Check for duplicate wiring
      - Wrong parameters: Check callback signature matches caller
   ```

**Acceptance Criteria:**
- [ ] "Event System Architecture" section added
- [ ] All callbacks documented in table
- [ ] Event flow diagrams for key operations
- [ ] Callback wiring explained
- [ ] Design rationale documented
- [ ] Debugging guidance provided

**Dependencies:** None

**Notes:**
- Complements HIGH-004 (DI documentation)
- Critical for understanding component interactions
- Helps debug mysterious behavior
- Documents design trade-offs

---

### HIGH-006: Create v1.0 to v2.0 Migration Guide

**Status:** [TODO]
**Priority:** HIGH
**Effort:** 2 hours
**Category:** Documentation

**Location:**
- Create new file: `docs/V1_TO_V2_MIGRATION.md`

**Problem:**
Module organization completely different from v1.0. Developers following old docs will encounter import errors.

**Solution:**
Create comprehensive migration guide with file mapping table.

**Action Steps:**
1. [ ] Create V1_TO_V2_MIGRATION.md file

2. [ ] Add file mapping table
   ```markdown
   # BlueBuzzah v1.0 to v2.0 Migration Guide

   ## File Structure Changes

   | v1.0 Path | v2.0 Path | Change Type |
   |-----------|-----------|-------------|
   | `src/code.py` | `src/main.py` | [RENAMED] Entry point renamed |
   | N/A | `src/app.py` | [NEW] Application orchestrator |
   | `modules/ble_connection.py` | `src/ble.py` | [CHANGED] Consolidated BLE module |
   | `modules/vcr_engine.py` | `src/therapy.py` | [RENAMED] Therapy engine |
   | `modules/haptic_controller.py` | `src/hardware.py` | [CHANGED] Hardware abstraction |
   | `modules/menu_controller.py` | `src/menu.py` | [RENAMED] Command handler |
   | `modules/sync_protocol.py` | `src/sync.py` | [CHANGED] Simplified sync |
   | `modules/profile_manager.py` | `src/profiles.py` | [RENAMED] Profile management |
   | `modules/session_manager.py` | `src/application/session/manager.py` | [MOVED] Layered architecture |
   | `modules/calibration_mode.py` | `src/application/calibration/controller.py` | [MOVED] Layered architecture |
   | `modules/utils.py` | `src/utils/validation.py` | [CHANGED] Utils reorganized |
   | N/A | `src/led.py` | [NEW] LED controller |
   | N/A | `src/state.py` | [NEW] State machine |
   | N/A | `src/core/types.py` | [NEW] Type definitions |
   | N/A | `src/core/constants.py` | [NEW] Centralized constants |
   ```

3. [ ] Document import changes
   ```markdown
   ## Import Statement Migration

   ### v1.0 Imports
   ```python
   from modules.ble_connection import BLEController
   from modules.vcr_engine import TherapyEngine
   from modules.haptic_controller import HapticDriver
   from modules.profile_manager import ProfileManager
   ```

   ### v2.0 Imports
   ```python
   from ble import BLEController
   from therapy import TherapyEngine
   from hardware import DRV2605Controller  # Renamed from HapticDriver
   from profiles import ProfileManager
   ```

4. [ ] Document API changes
   ```markdown
   ## API Changes

   ### Pattern Generation

   v1.0 used factory pattern:
   ```python
   from modules.pattern_factory import PatternGeneratorFactory
   factory = PatternGeneratorFactory()
   pattern = factory.generate("RNDP", num_fingers=8)
   ```

   v2.0 uses simple functions:
   ```python
   from therapy import generate_random_permutation
   pattern = generate_random_permutation(num_fingers=8, randomize=True)
   ```

   ### Sync Protocol

   v1.0 used full NTP-like protocol (not actually implemented)
   v2.0 uses SimpleSyncProtocol (basic timestamp sync)

   ```python
   # v2.0:
   from sync import SimpleSyncProtocol
   sync = SimpleSyncProtocol(ble_controller)
   ```

5. [ ] Document behavioral changes
   ```markdown
   ## Behavioral Changes

   ### Memory Constraints

   - v2.0 has stricter memory management
   - SessionManager and CalibrationController may be disabled
   - Check APPLICATION_LAYER_AVAILABLE flag before using

   ### BLE Protocol

   - v2.0 uses shorthand command format (e.g., "BATP", "FREQ")
   - v2.0 adds EOT (0x04) message terminator
   - Update mobile apps to add EOT to all messages

   ### Architecture

   - v2.0 uses 5-layer architecture
   - Dependency injection pattern
   - Callback-based event system
   ```

6. [ ] Add step-by-step migration guide
   ```markdown
   ## Migration Steps

   1. Update imports to use new paths
   2. Change pattern generation from factory to functions
   3. Update BLE message handling to expect EOT terminators
   4. Check if SESSION and CALIBRATE features are available
   5. Update constants to use new centralized constants.py
   6. Test thoroughly on actual hardware
   ```

**Acceptance Criteria:**
- [ ] V1_TO_V2_MIGRATION.md created
- [ ] File mapping table complete
- [ ] Import changes documented
- [ ] API changes explained
- [ ] Behavioral changes noted
- [ ] Step-by-step migration guide provided

**Dependencies:** None

**Notes:**
- Essential for developers migrating from v1.0
- Reduces confusion and support burden
- Reference from main documentation

---

### HIGH-007: Document Boot Sequence Architecture

**Status:** [TODO]
**Priority:** HIGH
**Effort:** 2 hours
**Category:** Documentation

**Location:**
- Add section to `docs/FIRMWARE_ARCHITECTURE.md`
- Reference `src/app.py` lines 493-659

**Problem:**
Sophisticated boot sequence (PRIMARY vs SECONDARY paths, timeouts, LED feedback) is not documented.

**Solution:**
Document boot flow, timeout values, LED patterns, and failure modes.

**Action Steps:**
1. [ ] Add "Boot Sequence" section to FIRMWARE_ARCHITECTURE.md

2. [ ] Document boot flow diagram
   ```markdown
   ## Boot Sequence

   ### Overview

   The boot sequence establishes BLE connections and prepares the device for therapy.

   ### Flow Diagram

   ```
   main.py:main()
     ├─> load_device_configuration()
     │     └─> Reads device.json (role, name, etc.)
     ├─> BlueBuzzahApplication.__init__()
     │     └─> Initializes all components
     └─> app.run()
           ├─> _execute_boot_sequence()
           │     ├─> PRIMARY path:
           │     │     ├─> Start BLE advertising
           │     │     ├─> Wait for SECONDARY (timeout: 30s)
           │     │     └─> Wait for phone (timeout: 60s)
           │     └─> SECONDARY path:
           │           ├─> Start BLE scanning
           │           └─> Connect to PRIMARY (timeout: 30s)
           ├─> Boot result: SUCCESS, TIMEOUT, or ERROR
           ├─> _run_primary_loop() OR _run_secondary_loop()
           └─> _shutdown() on exit
   ```

3. [ ] Document boot timeout values
   ```markdown
   ### Boot Timeouts

   | Device Role | Connection | Timeout | Behavior on Timeout |
   |-------------|-----------|---------|---------------------|
   | PRIMARY | Waiting for SECONDARY | 30s | Continue (can operate without SECONDARY) |
   | PRIMARY | Waiting for phone | 60s | Continue (phone is optional) |
   | SECONDARY | Connecting to PRIMARY | 30s | Retry or enter error state |
   ```

4. [ ] Document LED patterns during boot
   ```markdown
   ### LED Feedback During Boot

   | Stage | LED Pattern | Meaning |
   |-------|------------|---------|
   | Initialization | Blue pulse | Starting up |
   | PRIMARY advertising | Green pulse | Waiting for connections |
   | SECONDARY scanning | Yellow pulse | Searching for PRIMARY |
   | SECONDARY connected | Green solid | Connected to PRIMARY |
   | Boot timeout | Orange blink | Timeout occurred |
   | Boot error | Red blink | Fatal error |
   | Boot success | Green fade | Ready for therapy |
   ```

5. [ ] Document boot result enum
   ```markdown
   ### Boot Results

   ```python
   class BootResult:
       SUCCESS = 0  # All connections established
       TIMEOUT = 1  # One or more timeouts (device still operational)
       ERROR = 2    # Fatal error (device cannot operate)
   ```

   | Result | PRIMARY Behavior | SECONDARY Behavior |
   |--------|------------------|-------------------|
   | SUCCESS | Ready for therapy | Ready for therapy |
   | TIMEOUT | Can operate with partial connections | Retry connection or error state |
   | ERROR | Enter error state, wait for reset | Enter error state, wait for reset |
   ```

6. [ ] Document failure recovery
   ```markdown
   ### Boot Failure Recovery

   #### PRIMARY Timeout (No SECONDARY)

   - PRIMARY can still:
     - Accept phone connection
     - Run calibration mode
     - Provide device info
   - PRIMARY cannot:
     - Run bilateral therapy

   #### SECONDARY Timeout (Cannot Find PRIMARY)

   - Options:
     1. Retry connection (up to 3 attempts)
     2. Enter error state (LED red blink)
     3. User must restart SECONDARY when PRIMARY is ready

   #### Fatal Errors

   - Hardware initialization failure (I2C, DRV2605)
   - Memory allocation failure
   - Invalid configuration
   - Action: Enter error state, require restart
   ```

7. [ ] Add troubleshooting guide
   ```markdown
   ### Boot Sequence Troubleshooting

   | Symptom | Likely Cause | Solution |
   |---------|-------------|----------|
   | PRIMARY green pulse forever | SECONDARY not powered or out of range | Power on SECONDARY, move closer |
   | SECONDARY yellow pulse forever | PRIMARY not advertising or out of range | Check PRIMARY is powered, move closer |
   | Both devices red blink | Hardware failure | Check I2C connections, restart both |
   | Boot takes >2 minutes | BLE interference | Move away from WiFi/microwave, restart |
   ```

**Acceptance Criteria:**
- [ ] "Boot Sequence" section added to FIRMWARE_ARCHITECTURE.md
- [ ] Boot flow diagram included
- [ ] Timeout values documented
- [ ] LED patterns explained
- [ ] Boot results documented
- [ ] Failure recovery explained
- [ ] Troubleshooting guide provided

**Dependencies:** None

**Notes:**
- Important for clinical setup
- Helps troubleshoot connection issues
- Documents expected behavior
- Useful for training materials

---

### HIGH-008: Document Hardware Initialization Order

**Status:** [TODO]
**Priority:** HIGH
**Effort:** 2 hours
**Category:** Documentation

**Location:**
- Add section to `docs/TECHNICAL_REFERENCE.md`
- Reference `src/hardware.py`

**Problem:**
DRV2605 and I2C multiplexer initialization has specific ordering requirements that are not documented.

**Solution:**
Document hardware initialization sequence, timing requirements, and troubleshooting.

**Action Steps:**
1. [ ] Add "Hardware Initialization" section to TECHNICAL_REFERENCE.md

2. [ ] Document initialization sequence
   ```markdown
   ## Hardware Initialization

   ### Initialization Order

   Hardware components must be initialized in this exact order:

   1. **I2C Bus** (board.I2C())
      - Clock: 100kHz (standard mode)
      - No pull-up resistors needed (internal pull-ups)

   2. **I2C Multiplexer** (TCA9548A)
      - Address: 0x70
      - **CRITICAL**: Call `deselect_all()` before any other I2C operation
      - Ensures no channel is active during initialization

   3. **DRV2605 Controllers** (one per channel)
      - For each of 8 channels:
         a. Select multiplexer channel
         b. Initialize DRV2605 at address 0x5A
         c. Exit standby mode (MODE_REG = 0x00)
         d. **CRITICAL**: Wait 5ms (time.sleep(0.005))
         e. Configure registers (library, mode, etc.)
         f. Deselect multiplexer channel

   4. **Battery Monitor**
      - Initialize ADC
      - Set voltage divider ratio
      - Calibrate if needed

   5. **LED Controller**
      - Initialize NeoPixel
      - Set default brightness
      - Test LED (brief flash)
   ```

3. [ ] Document critical timing requirements
   ```markdown
   ### Timing Requirements

   | Component | Requirement | Source | Consequence if Violated |
   |-----------|-------------|--------|------------------------|
   | DRV2605 | 5ms delay after exit standby | Datasheet 7.4.2 | Register writes ignored, motors don't respond |
   | I2C Multiplexer | deselect_all() before first use | TCA9548A datasheet | Multiple channels active, I2C bus conflict |
   | I2C Bus | 10ms settling time after init | General I2C spec | Bus errors, devices not detected |
   ```

4. [ ] Document why deselect_all() is critical
   ```markdown
   ### Why deselect_all() is Critical

   The TCA9548A multiplexer powers on with **undefined channel state**. This means
   multiple channels might be active simultaneously, causing:

   - I2C address conflicts (all DRV2605 have same address 0x5A)
   - Bus contention (multiple devices responding)
   - Corrupted I2C transactions
   - Intermittent failures

   **ALWAYS call `multiplexer.deselect_all()` before any I2C operation.**

   ```python
   # Correct:
   multiplexer = I2CMultiplexer(i2c_bus, address=0x70)
   multiplexer.deselect_all()  # Clear undefined state

   # Then safe to use:
   multiplexer.select_channel(0)
   drv2605 = DRV2605Controller(i2c_bus, address=0x5A)
   ```

5. [ ] Add initialization code example
   ```python
   ### Initialization Code Example

   ```python
   import board
   import busio
   import time
   import gc

   # 1. Initialize I2C bus
   i2c = busio.I2C(board.SCL, board.SDA, frequency=100000)
   time.sleep(0.01)  # 10ms settling time
   gc.collect()

   # 2. Initialize multiplexer
   multiplexer = I2CMultiplexer(i2c, address=0x70)
   multiplexer.deselect_all()  # CRITICAL
   gc.collect()

   # 3. Initialize DRV2605 controllers
   drv2605_controllers = []
   for channel in range(8):
       multiplexer.select_channel(channel)

       drv2605 = DRV2605Controller(i2c, address=0x5A)
       drv2605.exit_standby()  # MODE_REG = 0x00
       time.sleep(0.005)  # CRITICAL: 5ms delay
       drv2605.configure(library=6, mode=RTP_MODE)

       drv2605_controllers.append(drv2605)
       multiplexer.deselect_channel(channel)
       gc.collect()

   # 4. Initialize battery monitor
   battery = BatteryMonitor(adc_pin=board.A0)
   gc.collect()

   # 5. Initialize LED
   led = LEDController(pin=board.NEOPIXEL)
   led.test_pattern()  # Brief flash
   gc.collect()
   ```

6. [ ] Add troubleshooting guide
   ```markdown
   ### Hardware Initialization Troubleshooting

   | Symptom | Likely Cause | Fix |
   |---------|-------------|-----|
   | I2C devices not detected | Bus not initialized, wrong pins | Check SCL/SDA pins, verify wiring |
   | Some motors work, others don't | Missing deselect_all() | Add multiplexer.deselect_all() at start |
   | Motors intermittent | Missing 5ms delay | Add time.sleep(0.005) after exit standby |
   | All motors weak or no response | Wrong library or mode | Verify library=6, mode=RTP_MODE |
   | Random I2C errors | Bus capacitance too high | Add external pull-up resistors (2.2kΩ) |
   | MemoryError during init | Too many allocations | Add gc.collect() after each component |

   #### I2C Bus Scanner

   Use this code to scan for I2C devices:

   ```python
   import board
   import busio

   i2c = busio.I2C(board.SCL, board.SDA)
   while not i2c.try_lock():
       pass

   print("I2C addresses found:", [hex(device) for device in i2c.scan()])
   i2c.unlock()
   ```

   Expected addresses:
   - 0x70: TCA9548A multiplexer
   - 0x5A: DRV2605 (on selected channel)
   ```

**Acceptance Criteria:**
- [ ] "Hardware Initialization" section added to TECHNICAL_REFERENCE.md
- [ ] Initialization sequence documented
- [ ] Critical timing requirements explained
- [ ] deselect_all() importance documented
- [ ] Code example provided
- [ ] Troubleshooting guide included

**Dependencies:** None

**Notes:**
- Critical for hardware reliability
- Helps diagnose intermittent issues
- Useful for hardware team
- Reference for custom implementations

---

## PHASE 3: MEDIUM PRIORITY (Should Fix)

**Goal:** Improve code quality, add best practices, document features
**Estimated Total Effort:** 10-12 hours
**Target Completion:** Week 3

---

### MEDIUM-001: Add Proactive gc.collect() Calls

**Status:** [TODO]
**Priority:** MEDIUM
**Effort:** 1 hour
**Category:** Memory Management

**Location:**
- Main event loops in `src/app.py`
- After large operations throughout codebase

**Problem:**
CircuitPython doesn't auto-collect garbage as aggressively as desktop Python. Memory can accumulate unnecessarily.

**Solution:**
Add proactive gc.collect() calls at strategic points.

**Action Steps:**
1. [ ] Add gc.collect() to main loops
   ```python
   # In _run_primary_loop() and _run_secondary_loop():
   loop_iteration = 0
   while True:
       process_messages()
       update_therapy()
       update_leds()

       loop_iteration += 1
       if loop_iteration % 100 == 0:  # Every 100 iterations
           gc.collect()

       time.sleep(0.01)
   ```

2. [ ] Add gc.collect() after large allocations
   ```python
   # After loading profiles:
   profile = ProfileManager.load_profile(profile_name)
   gc.collect()

   # After pattern generation:
   pattern = generate_random_permutation(num_fingers=8)
   gc.collect()

   # After boot sequence:
   boot_result = self._execute_boot_sequence()
   gc.collect()
   ```

3. [ ] Add gc.collect() in SessionManager
   ```python
   def start_session(self, profile_name):
       # ... session startup ...
       gc.collect()  # Clean up before therapy starts

   def stop_session(self):
       # ... session cleanup ...
       gc.collect()  # Free session memory
   ```

4. [ ] Monitor effectiveness
   - Log gc.mem_free() before and after gc.collect()
   - Verify memory is actually being freed
   - If little memory freed, gc.collect() is unnecessary at that location

**Acceptance Criteria:**
- [ ] gc.collect() added to main loops (every 100 iterations)
- [ ] gc.collect() added after large allocations
- [ ] gc.collect() added in SessionManager start/stop
- [ ] Effectiveness verified (memory freed)

**Dependencies:** None

**Notes:**
- Low-risk optimization
- May improve long-term stability
- Can be removed if profiling shows no benefit

---

### MEDIUM-002: Add gc.mem_free() Monitoring

**Status:** [TODO]
**Priority:** MEDIUM
**Effort:** 1 hour
**Category:** Memory Management

**Location:**
- Boot sequence in `src/app.py`
- Main loops
- SessionManager

**Problem:**
No visibility into memory usage during operation. Memory leaks hard to detect.

**Solution:**
Add periodic memory monitoring with warning thresholds.

**Action Steps:**
1. [ ] Add memory monitoring to boot
   ```python
   # In main.py or app.py __init__:
   import gc
   gc.collect()
   initial_free = gc.mem_free()
   print(f"[BOOT] Free RAM: {initial_free} bytes ({initial_free / 1024:.1f} KB)")
   ```

2. [ ] Add periodic monitoring to main loop
   ```python
   # In main loops:
   last_memory_check = time.monotonic()
   MEMORY_CHECK_INTERVAL = const(60)  # Every 60 seconds

   while True:
       # ... main loop ...

       if time.monotonic() - last_memory_check > MEMORY_CHECK_INTERVAL:
           gc.collect()
           free = gc.mem_free()
           print(f"[MEMORY] Free: {free} bytes ({free / 1024:.1f} KB)")

           if free < 10000:  # Less than 10KB
               print("[WARNING] Low memory!")
           elif free < 5000:  # Less than 5KB
               print("[CRITICAL] Very low memory!")

           last_memory_check = time.monotonic()
   ```

3. [ ] Add memory stats to INFO command
   ```python
   # In menu.py handle_info():
   gc.collect()
   free_mem = gc.mem_free()
   response += f"Memory: {free_mem} bytes free\n"
   ```

4. [ ] Add memory logging to SessionManager
   ```python
   # In SessionManager:
   def start_session(self):
       gc.collect()
       mem_at_start = gc.mem_free()
       print(f"[SESSION] Memory at start: {mem_at_start} bytes")

   def stop_session(self):
       gc.collect()
       mem_at_end = gc.mem_free()
       print(f"[SESSION] Memory at end: {mem_at_end} bytes")
   ```

5. [ ] Create memory log file (optional)
   ```python
   # For long-term monitoring:
   def log_memory_stats():
       with open("/memory_log.txt", "a") as f:
           timestamp = time.monotonic()
           gc.collect()
           free = gc.mem_free()
           f.write(f"{timestamp},{free}\n")
   ```

**Acceptance Criteria:**
- [ ] Memory logged at boot
- [ ] Memory monitored every 60 seconds in main loop
- [ ] Warning at <10KB free
- [ ] Memory stats in INFO command response
- [ ] Memory logged in SessionManager start/stop

**Dependencies:** None

**Notes:**
- Essential for debugging memory issues
- Helps detect memory leaks early
- Minimal performance impact (60 second interval)

---

### MEDIUM-003: Update SimpleSyncProtocol Documentation

**Status:** [TODO]
**Priority:** MEDIUM
**Effort:** 2 hours
**Category:** Documentation

**Location:**
- Update `docs/SYNCHRONIZATION_PROTOCOL.md`
- Update `docs/TECHNICAL_REFERENCE.md`

**Problem:**
Documentation implies full NTP-like synchronization, but reality is basic timestamp-based sync.

**Dependencies:**
- CRITICAL-006 (sync accuracy validation) should be done first to get actual measurements

**Solution:**
Update documentation to describe SimpleSyncProtocol accurately.

**Action Steps:**
1. [ ] Update SYNCHRONIZATION_PROTOCOL.md title and intro
   ```markdown
   # SimpleSyncProtocol - Bilateral Synchronization

   BlueBuzzah v2.0 uses SimpleSyncProtocol for coordinating therapy between
   PRIMARY and SECONDARY devices. This is a lightweight timestamp-based protocol
   optimized for CircuitPython's memory constraints.

   ## Overview

   SimpleSyncProtocol provides:
   - Timestamp-based coordination (not full NTP clock sync)
   - RNG seed sharing for coordinated pattern generation
   - Direct BLE message passing (no coordinator abstraction)
   - Typical accuracy: ~25ms (measured), P95: ~35ms
   ```

2. [ ] Document how it works
   ```markdown
   ## Protocol Operation

   ### 1. RNG Seed Synchronization

   Before therapy starts, PRIMARY and SECONDARY synchronize their random number
   generators to ensure identical pattern generation.

   ```
   PRIMARY                          SECONDARY
      |                                  |
      |--- SEED: 12345 ----------------->|
      |                                  |--- Initializes RNG with seed 12345
      |<-- SEED_ACK --------------------|
      |
      |--- Both generate identical patterns
   ```

   ### 2. Execution Coordination

   During therapy, PRIMARY sends EXECUTE_BUZZ commands with timestamps:

   ```
   PRIMARY                          SECONDARY
      |                                  |
      |--- EXECUTE_BUZZ ---------------->|
      |    ts: 1000                      |--- Executes immediately
      |    finger: 0                     |
      |    intensity: 50                 |
      |                                  |
      |<-- BUZZ_COMPLETE ---------------|
      |    ts: 1025                      |--- Actual execution time
      |    finger: 0                     |
      |
      |--- Latency calculated: 25ms
   ```

3. [ ] Document accuracy limitations
   ```markdown
   ## Synchronization Accuracy

   ### Measured Performance

   Based on 2-hour therapy session measurements:

   | Metric | Value |
   |--------|-------|
   | Mean latency | 24.5ms |
   | Median latency | 23ms |
   | P95 latency | 34ms |
   | P99 latency | 48ms |
   | Maximum latency | 67ms |

   ### Accuracy Limitations

   SimpleSyncProtocol accuracy is limited by:

   1. **BLE latency**: 15-25ms round-trip typical
   2. **time.monotonic() precision**: ±1-2ms
   3. **Processing delay**: 2-5ms per device

   **Result**: ~25ms typical bilateral sync accuracy

   ### Clinical Implications

   Literature on vibrotactile Coordinated Reset (vCR) therapy suggests bilateral
   synchronization within 50ms is clinically acceptable. SimpleSyncProtocol
   meets this requirement with margin (P99 < 50ms).

   For tighter sync requirements (<10ms), consider hardware GPIO trigger instead
   of BLE messaging.
   ```

4. [ ] Explain why simple approach was chosen
   ```markdown
   ## Design Rationale

   ### Why Not Full NTP?

   Full NTP synchronization (clock drift compensation, multiple time samples,
   statistical filtering) was considered but rejected due to:

   - **Memory constraints**: NTP requires ~5-10KB RAM for state tracking
   - **Complexity**: NTP is overkill for 2-device coordination
   - **Adequate accuracy**: 25ms sync is clinically acceptable
   - **Power efficiency**: Fewer BLE messages = lower power consumption

   ### Trade-offs Accepted

   - No clock drift compensation (assumes monotonic() is stable)
   - No multi-sample averaging (single timestamp per command)
   - No automatic accuracy adjustment (fixed BLE interval)

   These trade-offs are acceptable for vCR therapy where:
   - Sessions last 2-4 hours (drift is minimal)
   - Accuracy requirement is relaxed (~50ms)
   - Memory is scarce (~90KB available)
   ```

5. [ ] Update message format documentation
   ```markdown
   ## Message Format

   ### After HIGH-002 (JSON removal):

   Messages use pipe-delimited format:

   ```
   EXECUTE_BUZZ|<timestamp>|<finger>|<intensity>
   BUZZ_COMPLETE|<timestamp>|<finger>
   PARAM_UPDATE|<param_name>|<value>
   SEED|<seed_value>
   SEED_ACK
   ```

   Example:
   ```
   EXECUTE_BUZZ|1000000|0|50
   ```
   Means: Execute buzz at timestamp 1000000, finger 0, intensity 50
   ```

**Acceptance Criteria:**
- [ ] SYNCHRONIZATION_PROTOCOL.md updated with accurate description
- [ ] Protocol operation explained with diagrams
- [ ] Measured accuracy documented (from CRITICAL-006)
- [ ] Accuracy limitations explained
- [ ] Design rationale documented
- [ ] Message format updated

**Dependencies:**
- CRITICAL-006 (sync accuracy validation) for actual measurements

**Notes:**
- Important for setting correct expectations
- Helps clinical team understand capabilities
- Documents design trade-offs

---

### MEDIUM-004: Document Pattern Generation API

**Status:** [TODO]
**Priority:** MEDIUM
**Effort:** 1 hour
**Category:** Documentation

**Location:**
- Update `docs/THERAPY_ENGINE.md`
- Add examples to `docs/TECHNICAL_REFERENCE.md`

**Problem:**
Documentation references PatternGeneratorFactory class, but v2.0 uses simple functions.

**Solution:**
Update documentation to reflect function-based API.

**Action Steps:**
1. [ ] Update pattern generation section in THERAPY_ENGINE.md
   ```markdown
   ## Pattern Generation

   BlueBuzzah v2.0 uses simple pattern generation functions instead of factory classes.

   ### Available Pattern Functions

   #### generate_random_permutation()

   Generates Random Permutation (RNDP) patterns for vCR therapy.

   ```python
   from therapy import generate_random_permutation

   pattern = generate_random_permutation(
       num_fingers=8,
       randomize=True,  # True for RNDP, False for fixed sequence
       seed=None  # Optional: for reproducible patterns
   )
   # Returns: [3, 7, 1, 5, 0, 6, 2, 4]  (example)
   ```

   #### generate_sequential_pattern()

   Generates sequential patterns (0, 1, 2, ...).

   ```python
   from therapy import generate_sequential_pattern

   pattern = generate_sequential_pattern(
       num_fingers=8,
       reverse=False  # True for reverse sequence (7, 6, 5, ...)
   )
   # Returns: [0, 1, 2, 3, 4, 5, 6, 7]
   ```

   #### generate_mirrored_pattern()

   Generates mirrored patterns for bilateral coordination testing.

   ```python
   from therapy import generate_mirrored_pattern

   pattern = generate_mirrored_pattern(
       num_fingers=8,
       offset=0  # Offset for pattern variation
   )
   # Returns: [0, 7, 1, 6, 2, 5, 3, 4]  (mirrors from center)
   ```

2. [ ] Add usage examples
   ```markdown
   ### Usage Examples

   #### Basic RNDP Therapy

   ```python
   # Generate random permutation pattern
   pattern = generate_random_permutation(num_fingers=8, randomize=True)

   # Execute therapy cycle
   for finger in pattern:
       haptic_controller.activate_motor(
           motor=finger,
           intensity=50,
           duration=0.050  # 50ms
       )
       time.sleep(0.050)  # Inter-buzz delay
   ```

   #### Synchronized Bilateral Therapy

   ```python
   # PRIMARY and SECONDARY use same seed for identical patterns
   seed = 12345

   # PRIMARY:
   pattern_primary = generate_random_permutation(num_fingers=8, seed=seed)

   # SECONDARY (receives seed via SimpleSyncProtocol):
   pattern_secondary = generate_random_permutation(num_fingers=8, seed=seed)

   # Result: Both devices generate identical pattern
   assert pattern_primary == pattern_secondary
   ```

   #### Custom Pattern Testing

   ```python
   # Test specific motor sequence
   test_pattern = [0, 2, 4, 6, 1, 3, 5, 7]  # Alternate sides

   for finger in test_pattern:
       haptic_controller.activate_motor(motor=finger, intensity=75, duration=0.100)
       time.sleep(0.1)
   ```

3. [ ] Document why factory pattern was removed
   ```markdown
   ### Design Change from v1.0

   v1.0 used a PatternGeneratorFactory:

   ```python
   # v1.0 approach:
   factory = PatternGeneratorFactory()
   pattern = factory.generate("RNDP", num_fingers=8)
   ```

   v2.0 uses simple functions:

   ```python
   # v2.0 approach:
   pattern = generate_random_permutation(num_fingers=8)
   ```

   **Rationale**: Factory pattern was removed to save memory. Factory object
   and registration overhead cost ~500 bytes RAM with no functional benefit
   in this simple use case.
   ```

**Acceptance Criteria:**
- [ ] THERAPY_ENGINE.md updated with function-based API
- [ ] All 3 pattern functions documented
- [ ] Usage examples provided
- [ ] v1.0 → v2.0 change explained

**Dependencies:** None

**Notes:**
- Important for developers using therapy engine
- Reference for custom implementations
- Shows memory-conscious design decisions

---

### MEDIUM-005: Add Memory Constraints Section to Documentation

**Status:** [TODO]
**Priority:** MEDIUM
**Effort:** 1 hour
**Category:** Documentation

**Location:**
- Add section to `docs/TECHNICAL_REFERENCE.md`

**Problem:**
Memory constraints drove major architectural decisions but are not documented.

**Solution:**
Add comprehensive memory budget documentation.

**Action Steps:**
1. [ ] Add "Memory Constraints" section to TECHNICAL_REFERENCE.md
   ```markdown
   ## Memory Constraints

   ### nRF52840 Memory Budget

   The Adafruit Feather nRF52840 Express has limited SRAM:

   ```
   Total SRAM:              256 KB (hardware specification)
   CircuitPython Runtime:   ~90 KB (interpreter, built-in modules)
   BLE Stack:               ~40 KB (Nordic SoftDevice when BLE active)
   ---------------------------------------------------------------
   Available to user code:  ~130 KB (without BLE)
                            ~90 KB (with BLE active)
   ```

   ### Current Memory Usage (v2.0)

   Measured at boot with BLE active:

   | Component | Memory Cost | Notes |
   |-----------|------------|-------|
   | Core runtime | ~90 KB | CircuitPython interpreter |
   | BLE stack | ~40 KB | Nordic SoftDevice |
   | Firmware heap | ~60 KB | Application code and data |
   | **Free memory** | **~30 KB** | Available for runtime allocations |

   ### Memory-Driven Design Decisions

   Several architectural decisions were made to stay within memory constraints:

   1. **SessionManager/CalibrationController**: Initially disabled (but re-enabled in v2.1)
   2. **JSON removed**: Replaced with manual serialization (saves 2KB)
   3. **const() usage**: Stores constants in flash instead of RAM (saves ~160 bytes)
   4. **No factory patterns**: Direct functions instead of factory objects (saves ~500 bytes)
   5. **Callback system**: No event bus (saves ~1-2KB)
   6. **Buffer pre-allocation**: Reuse buffers instead of allocating per use
   7. **Minimal logging**: Production builds disable verbose logging
   ```

2. [ ] Add development guidelines
   ```markdown
   ### Memory-Efficient Development Guidelines

   When adding features to BlueBuzzah:

   #### DO:
   - Use `const()` for all integer constants
   - Pre-allocate buffers, reuse them
   - Call `gc.collect()` after large allocations
   - Monitor `gc.mem_free()` during development
   - Use direct function calls instead of factories
   - Minimize string operations in loops

   #### DON'T:
   - Import large libraries (json, re, collections)
   - Allocate memory in loops or callbacks
   - Use f-strings in hot paths
   - Create deep object hierarchies
   - Use metaclasses or complex inheritance
   - Store large data structures in RAM

   #### Example: Memory-Efficient Code

   ```python
   # BAD - Allocates in loop:
   for i in range(100):
       message = f"[DEBUG] Iteration {i}"  # New allocation each time
       print(message)

   # GOOD - Pre-allocated string:
   MSG_PREFIX = "[DEBUG] Iteration "
   for i in range(100):
       print(MSG_PREFIX, i)  # Reuses string
   ```
   ```

3. [ ] Add memory monitoring guide
   ```markdown
   ### Monitoring Memory Usage

   #### During Development

   ```python
   import gc

   # Check available memory
   gc.collect()
   print(f"Free memory: {gc.mem_free()} bytes")

   # Measure component cost
   gc.collect()
   mem_before = gc.mem_free()

   # ... create your component ...

   gc.collect()
   mem_after = gc.mem_free()
   print(f"Component cost: {mem_before - mem_after} bytes")
   ```

   #### Memory Thresholds

   | Free Memory | Status | Action |
   |------------|--------|--------|
   | >30 KB | Healthy | Normal operation |
   | 10-30 KB | Caution | Monitor closely |
   | 5-10 KB | Warning | Optimize memory usage |
   | <5 KB | Critical | Reduce features or risk MemoryError |

   #### Long-Term Stability

   Run 2-hour test sessions and log memory every 60 seconds:

   ```python
   while True:
       # ... main loop ...
       if time.monotonic() % 60 < 0.1:
           gc.collect()
           free = gc.mem_free()
           with open("/memory_log.txt", "a") as f:
               f.write(f"{time.monotonic()},{free}\n")
   ```

   Memory should remain stable. Gradual decline indicates a leak.
   ```

**Acceptance Criteria:**
- [ ] "Memory Constraints" section added to TECHNICAL_REFERENCE.md
- [ ] Memory budget breakdown documented
- [ ] Memory-driven design decisions explained
- [ ] Development guidelines provided
- [ ] Memory monitoring guide included

**Dependencies:** None

**Notes:**
- Critical for future development
- Explains why certain features missing
- Helps developers make informed decisions

---

### MEDIUM-006: Update Internal Message List Documentation

**Status:** [TODO]
**Priority:** MEDIUM
**Effort:** 30 minutes
**Category:** Documentation

**Location:**
- Update `docs/BLE_PROTOCOL.md`

**Problem:**
Documentation lists 5 internal messages, but implementation has 8.

**Solution:**
Update BLE_PROTOCOL.md to match implementation.

**Action Steps:**
1. [ ] Find internal message section in BLE_PROTOCOL.md

2. [ ] Update internal message list
   ```markdown
   ## Internal Messages

   These messages are used for device-to-device communication and are **not**
   sent to connected phones. The mobile app will never see these messages.

   ### Message List

   1. **EXECUTE_BUZZ** - PRIMARY → SECONDARY: Execute motor activation
   2. **BUZZ_COMPLETE** - SECONDARY → PRIMARY: Confirm buzz executed
   3. **PARAM_UPDATE** - Bidirectional: Synchronize parameter changes
   4. **SEED** - PRIMARY → SECONDARY: RNG seed for pattern sync
   5. **SEED_ACK** - SECONDARY → PRIMARY: Confirm seed received
   6. **GET_BATTERY** - PRIMARY → SECONDARY: Request battery status
   7. **BATRESPONSE** - SECONDARY → PRIMARY: Battery status response
   8. **ACK_PARAM_UPDATE** - Bidirectional: Acknowledge parameter update

   ### Message Filtering

   The firmware automatically filters internal messages:

   ```python
   # In menu.py:
   INTERNAL_MESSAGES = [
       "EXECUTE_BUZZ", "BUZZ_COMPLETE", "PARAM_UPDATE",
       "SEED", "SEED_ACK", "GET_BATTERY", "BATRESPONSE",
       "ACK_PARAM_UPDATE"
   ]

   def should_forward_to_phone(message):
       cmd = message.split(":")[0]
       return cmd not in INTERNAL_MESSAGES
   ```

   Phone connections only receive user-facing commands (INFO, BATTERY, etc.).
   ```

3. [ ] Add note about why certain messages are internal
   ```markdown
   ### Why These Messages Are Internal

   - **EXECUTE_BUZZ / BUZZ_COMPLETE**: High-frequency sync messages (~4-8 per second during therapy). Would overwhelm phone connection.
   - **SEED / SEED_ACK**: Technical implementation detail, no user value.
   - **PARAM_UPDATE / ACK_PARAM_UPDATE**: Duplicate of user-initiated commands.
   - **GET_BATTERY / BATRESPONSE**: Automatic background query, user sees aggregated result via BATTERY command.
   ```

**Acceptance Criteria:**
- [ ] Internal message list updated to 8 messages
- [ ] Message filtering explained
- [ ] Rationale for internal messages documented

**Dependencies:** None

**Notes:**
- Minor discrepancy but worth fixing
- Helps mobile app developers understand what to expect
- Documents protocol design decisions

---

## PHASE 4: LOW PRIORITY (Nice to Have)

**Goal:** Polish and completeness
**Estimated Total Effort:** 2 hours
**Target Completion:** Week 4

---

### LOW-001: Add LED Animation System Documentation

**Status:** [TODO]
**Priority:** LOW
**Effort:** 1 hour
**Category:** Documentation

**Location:**
- Create new section in `docs/TECHNICAL_REFERENCE.md`
- Reference `src/led.py`

**Problem:**
LED feedback system exists but is not documented.

**Solution:**
Document LED patterns and their meanings.

**Action Steps:**
1. [ ] Add "LED Feedback System" section to TECHNICAL_REFERENCE.md
2. [ ] Document LED patterns for each state
3. [ ] Create LED pattern reference table
4. [ ] Add usage examples

**Acceptance Criteria:**
- [ ] LED patterns documented for all states
- [ ] Pattern meanings explained
- [ ] Usage examples provided

**Dependencies:** None

**Notes:**
- Useful for user manual
- Helps clinical staff interpret device status

---

### LOW-002: Document State Machine Details

**Status:** [TODO]
**Priority:** LOW
**Effort:** 1 hour
**Category:** Documentation

**Location:**
- Add section to `docs/FIRMWARE_ARCHITECTURE.md`
- Reference `src/state.py`

**Problem:**
State machine exists but transitions and states not fully documented.

**Solution:**
Document state machine with state diagram and transition table.

**Action Steps:**
1. [ ] Add "State Machine" section to FIRMWARE_ARCHITECTURE.md
2. [ ] Create state diagram
3. [ ] Document state transitions and triggers
4. [ ] Add state-based behavior table

**Acceptance Criteria:**
- [ ] State machine states documented
- [ ] State diagram included
- [ ] Transition triggers explained
- [ ] State-based behaviors documented

**Dependencies:** None

**Notes:**
- Completes architecture documentation
- Useful for understanding firmware behavior

---

## Completion Tracking

### Phase Completion Checklist

#### Phase 1: CRITICAL
- [ ] CRITICAL-001: Memory budget and feature re-enablement
- [ ] CRITICAL-002: Add const() usage
- [ ] CRITICAL-003: Fix BLE timeout
- [ ] CRITICAL-004: Implement EOT message framing
- [ ] CRITICAL-005: Add 5ms DRV2605 delay
- [ ] CRITICAL-006: Validate sync accuracy
- [ ] CRITICAL-007: Test SESSION commands
- [ ] CRITICAL-008: Test CALIBRATE commands
- [ ] CRITICAL-009: Test phone integration
- [ ] CRITICAL-010: Reduce f-string usage

#### Phase 2: HIGH
- [ ] HIGH-001: BLE connection recovery
- [ ] HIGH-002: Remove JSON import
- [ ] HIGH-003: Implement uart.readinto()
- [ ] HIGH-004: Document DI architecture
- [ ] HIGH-005: Document callback system
- [ ] HIGH-006: Create migration guide
- [ ] HIGH-007: Document boot sequence
- [ ] HIGH-008: Document hardware initialization

#### Phase 3: MEDIUM
- [ ] MEDIUM-001: Add gc.collect() calls
- [ ] MEDIUM-002: Add memory monitoring
- [ ] MEDIUM-003: Update sync protocol docs
- [ ] MEDIUM-004: Document pattern generation
- [ ] MEDIUM-005: Add memory constraints docs
- [ ] MEDIUM-006: Update internal message list

#### Phase 4: LOW
- [ ] LOW-001: Document LED system
- [ ] LOW-002: Document state machine

---

## Notes and Decisions Log

### Decision Log

| Date | Item | Decision | Rationale |
|------|------|----------|-----------|
| 2025-01-23 | SessionManager re-enablement | Pending test results | Need to verify actual memory usage |
| 2025-01-23 | EOT framing | Implement with 0x04 terminator | Industry standard, simple to implement |
| 2025-01-23 | JSON removal | Replace with pipe-delimited format | Saves 2KB, adequate for simple messages |
| 2025-01-23 | Sync accuracy | Accept ~25ms typical | Clinically acceptable per literature |

### Issues and Blockers

| Date | Issue | Status | Resolution |
|------|-------|--------|-----------|
| | | | |

---

**Document Status:** ACTIVE
**Last Updated:** 2025-01-23
**Next Review:** After Phase 1 completion
**Owner:** Development Team
