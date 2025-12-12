---
paths: "src/hardware.{cpp,h}"
description: Hardware abstraction patterns for nRF52840
---
# Hardware Module Guidelines

## I2C Architecture

```
nRF52840 MCU
└── I2C Bus @ 400kHz
    └── TCA9548A Multiplexer (0x70)
        ├── Ch0: DRV2605 - Index finger
        ├── Ch1: DRV2605 - Middle finger
        ├── Ch2: DRV2605 - Ring finger
        └── Ch3: DRV2605 - Pinky finger
```

## I2C Multiplexer Rules

1. **Always close channels** after operations to prevent bus conflicts
2. **Select channel** before each DRV2605 operation
3. Motors share address 0x5A, differentiated by mux channel

```cpp
Result activate(uint8_t finger, uint8_t amplitude) {
    if (!selectChannel(finger)) return Result::ERROR_HARDWARE;
    _drv[finger].setRealtimeValue(amplitudeToRTP(amplitude));
    closeChannels();  // CRITICAL: Always close
    return Result::OK;
}
```

## Adaptive I2C Timing

Different I2C paths need different settling times:
- Standard channels: 5ms initialization delay
- Channel 4 (longer PCB trace): 10ms initialization delay

## Retry Logic

I2C operations can fail transiently. Use retry with backoff:
- 3 retry attempts
- Brief delay between retries
- Return failure only after all attempts exhausted
