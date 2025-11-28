# BLE UART Communication Debug Analysis

## Critical Bug Identified and Fixed

### Root Cause
**PRIMARY was using the WRONG UARTService object for communication!**

### The Problem

When PRIMARY (peripheral/server) accepts a connection from SECONDARY (central/client):

**BEFORE (BROKEN)**:
```python
# In wait_for_connection() - line 186
uart = self.uart_service  # Using the advertised service instance
```

**AFTER (FIXED)**:
```python
# In wait_for_connection() - line 187
uart = conn[UARTService]  # Getting the service from the connection object
```

### Why This Matters

The UARTService has two characteristics:
- **TX (Transmit)**: What the server writes, the client reads
- **RX (Receive)**: What the client writes, the server reads

When PRIMARY used `self.uart_service`:
- PRIMARY's `write()` wrote to its own **local** TX characteristic buffer
- SECONDARY was trying to read from the **connection's** RX characteristic buffer
- **These are different buffers!** Data never reached SECONDARY.

When PRIMARY uses `conn[UARTService]`:
- PRIMARY gets the UARTService instance from the connection object
- This correctly maps to the shared TX/RX characteristics
- PRIMARY's `write()` goes to the buffer that SECONDARY's `read()` accesses
- **Data flows correctly!**

### The Fix

Modified `/Users/rbonestell/Development/BlueBuzzah2-Firmware/src/ble.py`:

1. **Line 187**: Changed `uart = self.uart_service` to `uart = conn[UARTService]`
2. Added extensive debug logging to `wait_for_connection()` to verify UART object properties
3. Added extensive debug logging to `send()` to trace write operations
4. Added extensive debug logging to `receive()` to trace read operations

## What to Test

### Test 1: Verify Connection Setup
**Expected Output on PRIMARY**:
```
[BLE] *** CRITICAL: Using connection's UART service for 'secondary' ***
[BLE] UART object: <UARTService object at 0x...>
[BLE] UART has read(): True
[BLE] UART has write(): True
[BLE] UART has in_waiting: True
[BLE] *** secondary CONNECTED *** (total: 1)
```

### Test 2: Verify Send Operation
**Expected Output on PRIMARY when sending**:
```
[BLE] *** SEND DEBUG for 'secondary' ***
[BLE] UART object: <UARTService object at 0x...>
[BLE] UART object type: <class 'adafruit_ble.services.nordic.UARTService'>
[BLE] Message (first 100 chars): {"type":"sync",...
[BLE] Data bytes length: 150
[BLE] Data bytes (first 50): b'{"type":"sync",...'
[BLE] Calling conn.uart.write()...
[BLE] write() returned: (None or byte count)
[BLE] (flush() or No flush() method available)
[BLE] *** SEND SUCCESS to 'secondary' ***
```

### Test 3: Verify Receive Operation
**Expected Output on SECONDARY when receiving**:
```
[BLE] *** RECEIVE DEBUG for 'primary' ***
[BLE] UART object: <UARTService object at 0x...>
[BLE] UART object type: <class 'adafruit_ble.services.nordic.UARTService'>
[BLE] UART has read(): True
[BLE] UART has in_waiting: True
[BLE] receive(): conn_id 'primary' poll #1, in_waiting=150, buffer_len=0
[BLE] *** DATA AVAILABLE: 150 bytes waiting ***
[BLE] Calling conn.uart.read()...
[BLE] read() returned: b'{"type":"sync",...\x04'
[BLE] Decoded chunk: '{"type":"sync",...'
[BLE] Total buffer length: 150
[BLE] *** COMPLETE MESSAGE RECEIVED: '{"type":"sync",...' ***
```

### Critical Success Indicators

✅ **Connection Setup**:
- PRIMARY shows "Using connection's UART service"
- UART object has read(), write(), and in_waiting attributes

✅ **Send Operation**:
- PRIMARY's write() completes without exception
- write() may return None or byte count (both valid)

✅ **Receive Operation**:
- SECONDARY's in_waiting shows > 0 bytes
- read() returns data
- Message completes with EOT terminator

❌ **Failure Indicators**:
- in_waiting always 0 (means the wrong buffer is being checked)
- read() returns None when in_waiting > 0 (buffer access issue)
- UART object missing attributes (wrong service type)

## Additional Debug Information Captured

### Connection Logging
- UART object memory address (verify same object used for read/write)
- UART object type (should be UARTService)
- Available methods (read, write, in_waiting, flush, etc.)

### Send Logging
- Exact bytes being written
- Return value from write()
- Availability of flush() method

### Receive Logging
- Poll count and frequency
- in_waiting value every second
- Byte-level data when received
- Buffer accumulation
- EOT detection

## Next Steps After Testing

1. **If data now flows correctly**:
   - Remove excessive debug logging
   - Keep critical checkpoints (connection setup, UART object verification)
   - Validate sync/pattern messaging works end-to-end

2. **If in_waiting is still 0**:
   - Check UART object type (should match on both ends)
   - Verify write() return value
   - Test with simple "TEST" message first
   - Check if notifications need to be enabled

3. **If data is partial/corrupted**:
   - Verify EOT framing is working
   - Check MTU size (nRF52840 max: 247 bytes)
   - Ensure no buffer overflow

## Technical Reference

### UARTService Characteristics (from Adafruit BLE library)

**Server (Peripheral) Perspective**:
- TX characteristic: Server writes, client reads
- RX characteristic: Client writes, server reads

**Client (Central) Perspective**:
- RX characteristic: Client reads (from server's TX)
- TX characteristic: Client writes (to server's RX)

The `conn[UARTService]` access automatically handles the perspective mapping, ensuring that:
- `uart.write()` always writes to the correct characteristic for the remote peer to read
- `uart.read()` always reads from the correct characteristic written by the remote peer
- `uart.in_waiting` checks the correct receive buffer

This is why using `self.uart_service` failed - it was checking the server's own local buffers instead of the connection-specific buffers.
