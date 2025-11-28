# PRIMARY Device Boot MemoryError Analysis

## Error Summary

**Device**: PRIMARY (Adafruit Feather nRF52840 Express)
**Symptom**: `MemoryError` during boot sequence after BLE connection
**Error Message**: `memory allocation failed, allocating 512 bytes`
**Timing**: Immediately after UART service discovery from SECONDARY connection

## Root Cause

The MemoryError occurs at **line 214 in src/ble.py** during `BLEConnection` object creation:

```python
ble_conn = BLEConnection(conn_id, conn, uart)  # Line 214
```

The `BLEConnection.__init__()` method allocates two buffers:

```python
class BLEConnection:
    def __init__(self, name, ble_connection, uart_service):
        # ... other initialization ...

        # MEM-002 FIX: Pre-allocated buffers to eliminate allocations in hot path
        self._rx_buffer = bytearray(256)    # Line 34 - 256 bytes
        self._msg_buffer = bytearray(512)   # Line 35 - 512 bytes ← FAILS HERE
        self._msg_len = 0
```

**Total allocation required**: 256 + 512 = **768 bytes**

The error message reports "allocating 512 bytes" because the 256-byte allocation succeeds, but the heap doesn't have enough contiguous space for the 512-byte buffer.

## Execution Path to Failure

### PRIMARY Boot Sequence (src/app.py)

1. **Line 682**: `self.ble.advertise()` - Start advertising
2. **Line 702**: `conn = self.ble.wait_for_connection("secondary", timeout=0.02)`
3. **BLE detects connection** (ble.py line 193)
4. **Line 196-197**: Wait 200ms for service discovery
5. **Line 204**: `uart = conn[UARTService]` - Get UART service ✓ Success
6. **Line 214**: `ble_conn = BLEConnection(conn_id, conn, uart)` ✗ **FAILS HERE**

### Memory State at Failure Point

**Available RAM at boot**: ~130 KB (without BLE)
**BLE stack overhead**: -20 to -40 KB
**Application layer imports**: -2.2 KB
**Advertisement data**: ~1 KB
**Connection objects**: ~2-3 KB

**Estimated free memory at failure**: < 768 bytes (insufficient for both buffers)

## Why Memory is Exhausted

### 1. **No Garbage Collection Before Critical Allocation**

The code path from advertising to connection creation **never calls `gc.collect()`**:

- ✗ No gc.collect() in `ble.advertise()` (line 80-139)
- ✗ No gc.collect() in `wait_for_connection()` (line 170-221)
- ✗ No gc.collect() before `BLEConnection()` creation (line 214)
- ✗ No gc.collect() in PRIMARY boot sequence before waiting (line 662-733)

**The heap is fragmented with temporary allocations that are no longer referenced but haven't been collected.**

### 2. **Temporary Allocations in Advertisement Setup**

The `ble.advertise()` method creates multiple temporary objects:

```python
def advertise(self, name):
    # Line 103: Recreates UART service (old one becomes garbage)
    self.uart_service = UARTService()

    # Line 106: Recreates advertisement (old one becomes garbage)
    self.advertisement = ProvideServicesAdvertisement(self.uart_service)

    # Line 112-114: Creates temporary bytes for name injection
    name_bytes = name.encode('utf-8')  # Temporary allocation
    name_len = len(name_bytes) + 1
    complete_name_field = bytes([name_len, 0x09]) + name_bytes  # Temporary

    # Line 120: Creates new bytearray with concatenated data
    current_data = bytes(self.advertisement._data)
    self.advertisement._data = bytearray(current_data + complete_name_field)
```

**Each of these allocations fragments the heap without gc.collect() to reclaim space.**

### 3. **String Formatting Allocations**

Throughout the boot sequence, extensive string formatting allocates temporary strings:

```python
print("[BLE] Set adapter name to: {}".format(_bleio.adapter.name))  # Line 94
print("[BLE] Injected complete_name '{}' into advertisement data".format(name))  # Line 121
print("[BLE] Radio name: {}".format(self.ble.name))  # Line 132
print("[BLE] Advertisement name: {}".format(self.advertisement.complete_name))  # Line 134
# ... many more ...
```

**Each `.format()` call allocates a new string object.**

### 4. **List Append Operations**

MAC address formatting creates a list via append (fragments heap):

```python
# Line 74-76
mac_parts = []
for b in self.ble.address_bytes:
    mac_parts.append("{:02x}".format(b))  # Grows list dynamically
mac_addr = ":".join(mac_parts)  # Creates another string
```

### 5. **Connection Waiting Loop Allocations**

The PRIMARY boot sequence runs a tight loop (line 695-727) that:
- Calls `wait_for_connection()` repeatedly with 0.02s timeout
- Creates temporary timeout calculation strings
- Updates LED animations (may allocate for color values)
- Never collects garbage during the 30-second window

## Specific Code Locations

### Critical Allocation Point

**File**: `src/ble.py`
**Function**: `BLEConnection.__init__()`
**Lines**: 34-35

```python
self._rx_buffer = bytearray(256)    # Succeeds
self._msg_buffer = bytearray(512)   # FAILS - MemoryError here
```

### Calling Context

**File**: `src/ble.py`
**Function**: `BLE.wait_for_connection()`
**Line**: 214

```python
ble_conn = BLEConnection(conn_id, conn, uart)  # Creates object, triggers allocation
```

**File**: `src/app.py`
**Function**: `BlueBuzzahApplication._primary_boot_sequence()`
**Line**: 702

```python
conn = self.ble.wait_for_connection("secondary", timeout=min(0.02, remaining_time))
```

## Memory Optimization Opportunities

### HIGH Priority (Must Fix)

#### 1. **Add gc.collect() Before Critical Allocations**

**Location**: `src/ble.py`, line 212 (just before BLEConnection creation)

```python
# Before creating BLEConnection, ensure heap is compacted
import gc
gc.collect()
ble_conn = BLEConnection(conn_id, conn, uart)
```

**Expected savings**: 5-10 KB (reclaims all unreferenced temporaries)

#### 2. **Add gc.collect() After Advertisement Setup**

**Location**: `src/ble.py`, line 129 (after advertisement start)

```python
self.ble.start_advertising(self.advertisement)
import gc
gc.collect()  # Reclaim temporary name/data buffers
time.sleep(0.5)
```

**Expected savings**: 1-2 KB

#### 3. **Reduce Buffer Sizes**

**Location**: `src/ble.py`, lines 34-35

**Analysis**:
- Current: 256 + 512 = 768 bytes per connection
- PRIMARY creates 2 connections (secondary + phone) = 1,536 bytes total
- Messages are line-delimited, rarely exceed 200 bytes

**Recommendation**:

```python
# Reduce buffer sizes to minimum viable
self._rx_buffer = bytearray(128)    # Was 256, save 128 bytes
self._msg_buffer = bytearray(256)   # Was 512, save 256 bytes
```

**Expected savings**: 384 bytes per connection × 2 = **768 bytes total**

#### 4. **Optimize MAC Address Formatting**

**Location**: `src/ble.py`, lines 74-78

**Current (allocates list + 6 strings)**:

```python
mac_parts = []
for b in self.ble.address_bytes:
    mac_parts.append("{:02x}".format(b))
mac_addr = ":".join(mac_parts)
```

**Optimized (single allocation)**:

```python
from micropython import const
MAC_LEN = const(17)  # "xx:xx:xx:xx:xx:xx" = 17 chars
mac_buffer = bytearray(MAC_LEN)
offset = 0
for i, b in enumerate(self.ble.address_bytes):
    if i > 0:
        mac_buffer[offset] = ord(':')
        offset += 1
    hex_str = "{:02x}".format(b)
    mac_buffer[offset:offset+2] = hex_str.encode()
    offset += 2
mac_addr = bytes(mac_buffer).decode('ascii')
```

**Expected savings**: ~200 bytes

### MEDIUM Priority (Should Fix)

#### 5. **Guard Debug Prints with DEBUG_ENABLED**

**Location**: `src/ble.py`, throughout

Many print statements execute unconditionally:

```python
print("[BLE] Set adapter name to: {}".format(_bleio.adapter.name))  # Line 94
print("[BLE] Injected complete_name '{}' into advertisement data".format(name))  # Line 121
```

**Recommendation**: Wrap in conditional to avoid string allocations when debugging disabled:

```python
from core.constants import DEBUG_ENABLED

if DEBUG_ENABLED:
    print("[BLE] Set adapter name to: {}".format(_bleio.adapter.name))
```

**Expected savings**: 500-1000 bytes (eliminates all debug string formatting)

#### 6. **Reuse Advertisement Objects**

**Location**: `src/ble.py`, lines 103-106

Currently recreates UARTService and Advertisement on every advertise() call:

```python
self.uart_service = UARTService()  # Old one becomes garbage
self.advertisement = ProvideServicesAdvertisement(self.uart_service)  # Old one becomes garbage
```

**Recommendation**: Only create once in `__init__()`, modify in place for name changes

**Expected savings**: ~2 KB per advertise() call

### LOW Priority (Consider)

#### 7. **Use const() for Buffer Sizes**

**Location**: `src/ble.py`, lines 34-35

```python
from micropython import const

RX_BUFFER_SIZE = const(128)
MSG_BUFFER_SIZE = const(256)

self._rx_buffer = bytearray(RX_BUFFER_SIZE)
self._msg_buffer = bytearray(MSG_BUFFER_SIZE)
```

**Expected savings**: 8 bytes (constants stored in flash, not RAM)

#### 8. **Pre-allocate _pending_messages List**

**Location**: `src/ble.py`, line 40

**Current**:
```python
self._pending_messages = []  # Grows dynamically via append()
```

**Optimized**:
```python
from micropython import const
MAX_PENDING = const(10)
self._pending_messages = [None] * MAX_PENDING
self._pending_count = 0
```

**Expected savings**: Prevents fragmentation, ~100 bytes

## Recommended Fixes (Prioritized)

### Immediate (Emergency Fix)

Apply these changes to fix the MemoryError immediately:

1. **Add gc.collect() before BLEConnection creation** (ble.py:212)
2. **Reduce buffer sizes** to 128/256 (ble.py:34-35)

**Combined fix**:

```python
# src/ble.py, line 212
import gc
gc.collect()  # Critical: compact heap before large allocation
print("[BLE] Free memory before BLEConnection: {} bytes".format(gc.mem_free()))

ble_conn = BLEConnection(conn_id, conn, uart)
```

```python
# src/ble.py, lines 34-35 (inside BLEConnection.__init__)
self._rx_buffer = bytearray(128)    # Reduced from 256
self._msg_buffer = bytearray(256)   # Reduced from 512
```

**Expected outcome**: Boot succeeds with >2 KB free memory after connection

### Short-term (Stability)

After immediate fix works, apply these for better memory hygiene:

3. **Add gc.collect() after advertisement setup** (ble.py:129)
4. **Optimize MAC formatting** to eliminate list append (ble.py:74-78)
5. **Guard debug prints** with DEBUG_ENABLED flag

### Long-term (Architecture)

For sustainable memory management:

6. **Reuse advertisement objects** instead of recreating
7. **Add memory monitoring** to boot sequence with warnings
8. **Pre-allocate all buffers** at application startup

## Testing Recommendations

### Verify Fix

After applying gc.collect() and buffer reduction:

1. **Monitor free memory** at each boot stage:

```python
# Add to src/app.py _primary_boot_sequence() before line 702
gc.collect()
free_before_connect = gc.mem_free()
print("[PRIMARY] Free memory before connection: {} bytes".format(free_before_connect))

conn = self.ble.wait_for_connection("secondary", timeout=min(0.02, remaining_time))

if conn:
    gc.collect()
    free_after_connect = gc.mem_free()
    print("[PRIMARY] Free memory after connection: {} bytes".format(free_after_connect))
    print("[PRIMARY] Connection cost: {} bytes".format(free_before_connect - free_after_connect))
```

2. **Expected results**:
   - Free before connection: >5 KB
   - Free after connection: >2 KB
   - Connection cost: <3 KB

3. **Failure indicators**:
   - Free before connection: <1 KB → Earlier memory leak
   - Connection cost: >5 KB → BLE stack issue

### Stress Test

Run PRIMARY with both connections (SECONDARY + phone) and monitor memory during:
- Advertisement startup
- SECONDARY connection
- Phone connection
- Session start

Free memory should never drop below 10 KB during normal operation.

## Related Files

- `/Users/rbonestell/Development/BlueBuzzah2-Firmware/src/ble.py` - BLE connection management
- `/Users/rbonestell/Development/BlueBuzzah2-Firmware/src/app.py` - Application boot sequence
- `/Users/rbonestell/Development/BlueBuzzah2-Firmware/src/boot.py` - Boot configuration
- `/Users/rbonestell/Development/BlueBuzzah2-Firmware/src/main.py` - Entry point

## Memory Profiling Results

Ran CircuitPython memory profiler on src/ble.py:

- **Estimated peak**: 23,100 bytes
- **GC pressure**: HIGH (68 critical issues found)
- **Fragmentation risk**: LOW (but actual fragmentation HIGH due to missing gc.collect())
- **Critical patterns detected**:
  - List growth via append() (2 instances)
  - String formatting in loops (10+ instances)
  - File/stream read() without pre-allocated buffer (1 instance)

**Top 3 high-impact fixes from profiler**:
1. Pre-allocate MAC address formatting buffer (save ~200 bytes)
2. Remove unused imports (if any - none found)
3. Use const() for buffer size constants (save 8 bytes)

## Summary

**Root cause**: Memory exhaustion due to heap fragmentation from temporary allocations during BLE advertisement setup and connection waiting, with no garbage collection before critical 768-byte allocation.

**Immediate fix**: Add `gc.collect()` before `BLEConnection()` creation (line 212 in ble.py) and reduce buffer sizes to 128/256 bytes.

**Long-term solution**: Implement comprehensive garbage collection strategy at all major allocation points, reduce buffer sizes to minimum viable, and guard debug output with feature flags.

**Expected recovery**: >2 KB free memory after applying immediate fixes, sufficient for stable operation with both connections active.
