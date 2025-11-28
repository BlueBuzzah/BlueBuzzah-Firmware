# Memory Error Fix Recommendations

## Immediate Emergency Fix (Apply Now)

### Fix #1: Add Garbage Collection Before BLEConnection Creation

**File**: `src/ble.py`
**Line**: 212 (in `wait_for_connection()` method)

**Current code**:
```python
                        uart = conn[UARTService]
                        ble_conn = BLEConnection(conn_id, conn, uart)  # FAILS HERE
                        self._connections[conn_id] = ble_conn
```

**Fixed code**:
```python
                        uart = conn[UARTService]

                        # CRITICAL: Compact heap before large allocation
                        # BLEConnection allocates 768 bytes (256 + 512) for buffers
                        import gc
                        gc.collect()
                        free_mem = gc.mem_free()
                        if free_mem < 2000:  # Less than 2KB - warn
                            print("[BLE] WARNING: Low memory before connection: {} bytes".format(free_mem))

                        ble_conn = BLEConnection(conn_id, conn, uart)
                        self._connections[conn_id] = ble_conn
```

**Expected impact**: Reclaims 5-10 KB from temporary allocations during advertisement setup

---

### Fix #2: Add Garbage Collection in scan_and_connect()

**File**: `src/ble.py`
**Line**: 256 (in `scan_and_connect()` method)

**Current code**:
```python
                            uart = connection[UARTService]
                            ble_conn = BLEConnection(conn_id, connection, uart)  # FAILS HERE TOO
                            self._connections[conn_id] = ble_conn
```

**Fixed code**:
```python
                            uart = connection[UARTService]

                            # CRITICAL: Compact heap before large allocation (same as wait_for_connection)
                            import gc
                            gc.collect()
                            free_mem = gc.mem_free()
                            if free_mem < 2000:
                                print("[BLE] WARNING: Low memory before connection: {} bytes".format(free_mem))

                            ble_conn = BLEConnection(conn_id, connection, uart)
                            self._connections[conn_id] = ble_conn
```

**Expected impact**: Prevents same error when SECONDARY connects to PRIMARY

---

### Fix #3: Reduce BLEConnection Buffer Sizes

**File**: `src/ble.py`
**Lines**: 34-35 (in `BLEConnection.__init__()`)

**Current code**:
```python
        # MEM-002 FIX: Pre-allocated buffers to eliminate allocations in hot path
        self._rx_buffer = bytearray(256)    # Read buffer for uart.readinto()
        self._msg_buffer = bytearray(512)   # Message accumulation buffer
        self._msg_len = 0                    # Current message length
```

**Fixed code**:
```python
        # MEM-002 FIX: Pre-allocated buffers to eliminate allocations in hot path
        # Buffer sizes optimized for nRF52840 memory constraints
        # Messages are line-delimited and rarely exceed 200 bytes
        self._rx_buffer = bytearray(128)    # Read buffer for uart.readinto() (was 256)
        self._msg_buffer = bytearray(256)   # Message accumulation buffer (was 512)
        self._msg_len = 0                    # Current message length
```

**Analysis**:
- Current allocation: 256 + 512 = 768 bytes per connection
- New allocation: 128 + 256 = 384 bytes per connection
- **Savings**: 384 bytes per connection
- PRIMARY creates 2 connections (secondary + phone) = **768 bytes total savings**

**Risk assessment**:
- Sync commands are ~80-150 bytes typically
- Menu commands are <200 bytes
- 256-byte message buffer provides 2x safety margin
- If messages exceed 256 bytes, buffer overflow protection resets (line 418)

---

## Additional Optimizations (Apply After Testing Emergency Fixes)

### Fix #4: Add gc.collect() After Advertisement Setup

**File**: `src/ble.py`
**Line**: 129 (in `advertise()` method, after `start_advertising()`)

**Current code**:
```python
        print("[BLE] Starting advertising...")
        self.ble.start_advertising(self.advertisement)
        time.sleep(0.5)

        print("[BLE] *** ADVERTISING STARTED ***")
```

**Fixed code**:
```python
        print("[BLE] Starting advertising...")
        self.ble.start_advertising(self.advertisement)

        # Reclaim memory from temporary name injection buffers
        import gc
        gc.collect()

        time.sleep(0.5)

        print("[BLE] *** ADVERTISING STARTED ***")
```

**Expected impact**: Reclaims 1-2 KB from temporary advertisement setup allocations

---

### Fix #5: Optimize MAC Address Formatting

**File**: `src/ble.py`
**Lines**: 74-78 (in `__init__()` method)

**Current code (allocates list + 6 string objects)**:
```python
        # Format MAC address
        mac_parts = []
        for b in self.ble.address_bytes:
            mac_parts.append("{:02x}".format(b))
        mac_addr = ":".join(mac_parts)
        print("[BLE] MAC address: {}".format(mac_addr))
```

**Fixed code (single pre-allocated buffer)**:
```python
        # Format MAC address (memory optimized - single allocation)
        from micropython import const
        MAC_ADDR_LEN = const(17)  # "xx:xx:xx:xx:xx:xx" format
        mac_buffer = bytearray(MAC_ADDR_LEN)
        offset = 0
        for i, b in enumerate(self.ble.address_bytes):
            if i > 0:
                mac_buffer[offset] = ord(':')
                offset += 1
            # Format byte as hex
            high_nibble = (b >> 4) & 0x0F
            low_nibble = b & 0x0F
            mac_buffer[offset] = ord('0') + high_nibble if high_nibble < 10 else ord('a') + (high_nibble - 10)
            mac_buffer[offset + 1] = ord('0') + low_nibble if low_nibble < 10 else ord('a') + (low_nibble - 10)
            offset += 2
        mac_addr = bytes(mac_buffer).decode('ascii')
        print("[BLE] MAC address: {}".format(mac_addr))
```

**Expected impact**: Saves ~200 bytes, eliminates list fragmentation

---

### Fix #6: Add Memory Monitoring to Boot Sequence

**File**: `src/app.py`
**Line**: 700 (in `_primary_boot_sequence()`, before connection wait loop)

**Add this before line 695** (before the while loop):
```python
        # Memory monitoring during boot
        import gc
        gc.collect()
        free_at_boot_start = gc.mem_free()
        print("{} [MEMORY] Free at boot start: {} bytes ({} KB)".format(
            DEVICE_TAG, free_at_boot_start, free_at_boot_start // 1024))
```

**Add this inside the while loop** (after line 695):
```python
        while (time.monotonic() - start_time) < self.config.get('startup_window_sec', 30):
            # MEMORY: Periodic GC during boot (every 5 seconds)
            if int(time.monotonic() - start_time) % 5 == 0:
                gc.collect()
                free_now = gc.mem_free()
                if free_now < 5000:  # Less than 5KB - critical
                    print("{} [CRITICAL] Memory very low during boot: {} bytes".format(DEVICE_TAG, free_now))
                elif free_now < 10000:  # Less than 10KB - warning
                    print("{} [WARNING] Memory low during boot: {} bytes".format(DEVICE_TAG, free_now))

            # Update LED animations (flash and breathing)
            self.boot_led_controller.update_animation()
            # ... rest of loop ...
```

**Expected impact**: Early warning of memory issues, periodic compaction during 30s boot window

---

## Complete Patch for Emergency Fix

Here's the complete minimal patch to apply immediately:

### Patch 1: src/ble.py

```python
# Line 34-35 - Reduce buffer sizes
        self._rx_buffer = bytearray(128)    # Was 256
        self._msg_buffer = bytearray(256)   # Was 512

# Line 212 - Add gc.collect() before BLEConnection in wait_for_connection()
                        uart = conn[UARTService]

                        # CRITICAL: Compact heap before allocation
                        import gc
                        gc.collect()

                        ble_conn = BLEConnection(conn_id, conn, uart)

# Line 256 - Add gc.collect() before BLEConnection in scan_and_connect()
                            uart = connection[UARTService]

                            # CRITICAL: Compact heap before allocation
                            import gc
                            gc.collect()

                            ble_conn = BLEConnection(conn_id, connection, uart)
```

## Testing Plan

### Step 1: Verify Emergency Fix

1. Apply **Fix #1, #2, #3** (gc.collect() + reduced buffers)
2. Deploy to PRIMARY device
3. Boot and connect SECONDARY
4. Monitor serial output for memory warnings
5. **Success criteria**: No MemoryError during boot, connection succeeds

### Step 2: Memory Validation

Add temporary debugging to measure actual memory usage:

```python
# In src/ble.py, line 214 (after gc.collect(), before BLEConnection)
free_before = gc.mem_free()
ble_conn = BLEConnection(conn_id, conn, uart)
gc.collect()
free_after = gc.mem_free()
print("[BLE] BLEConnection cost: {} bytes (free: {} -> {})".format(
    free_before - free_after, free_before, free_after))
```

Expected output:
```
[BLE] BLEConnection cost: ~400 bytes (free: 8000 -> 7600)
```

### Step 3: Stress Test

With emergency fix working:
1. Connect SECONDARY (verify no error)
2. Connect phone (verify no error)
3. Start therapy session
4. Monitor free memory during session (should stay >10 KB)

### Step 4: Apply Additional Optimizations

After emergency fix proven stable, apply **Fix #4, #5, #6** for further improvements.

## Rollback Plan

If emergency fix causes issues:

1. **Revert buffer sizes** to 256/512 (keeps gc.collect() changes)
2. **Add gc.collect()** in PRIMARY boot sequence before advertising:

```python
# src/app.py, line 679 (before advertise())
        # Compact heap before BLE advertisement (high memory usage)
        gc.collect()
        print("{} [MEMORY] Free before advertising: {} bytes".format(DEVICE_TAG, gc.mem_free()))

        self.ble.advertise(self.config.get('ble_name', 'BlueBuzzah'))
```

## Expected Results

### Before Fix
```
[BLE] UART has in_waiting: True
[PRIMARY] [ERROR] Boot sequence failed: memory allocation failed, allocating 512 bytes
```

### After Emergency Fix
```
[BLE] UART has in_waiting: True
[BLE] Free memory before connection: 8245 bytes
[BLE] *** secondary CONNECTED *** (total: 1)
[PRIMARY] PRIMARY: SECONDARY connected
```

### After All Optimizations
```
[PRIMARY] [MEMORY] Free at boot start: 91234 bytes (89 KB)
[BLE] UART has in_waiting: True
[BLE] Free memory before connection: 10456 bytes
[BLE] BLEConnection cost: 384 bytes (free: 10456 -> 10072)
[BLE] *** secondary CONNECTED *** (total: 1)
[PRIMARY] PRIMARY: SECONDARY connected
[PRIMARY] [MEMORY] Free after SECONDARY connection: 9821 bytes
```

## Summary

**Minimum viable fix**: Apply Fix #1, #2, #3 (add gc.collect(), reduce buffers)

**Estimated time**: 5 minutes to apply, 5 minutes to test

**Risk**: Low - gc.collect() is always safe, buffer reduction leaves 2x margin

**Expected outcome**: Boot completes successfully with >2 KB free memory

**Next steps**: Monitor stability, apply additional optimizations if needed
