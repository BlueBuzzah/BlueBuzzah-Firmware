---
paths: "src/hardware.{cpp,h}"
description: Hardware abstraction patterns (nRF52840 + ESP32-S3)
---
# Hardware Module Guidelines

## I2C Architecture

```
MCU (nRF52840 default pins / ESP32-S3 SDA=GPIO5, SCL=GPIO6)
└── I2C Bus @ 400kHz
    └── TCA9548A Multiplexer (0x70)
        ├── Ch0: DRV2605 - Index finger
        ├── Ch1: DRV2605 - Middle finger
        ├── Ch2: DRV2605 - Ring finger
        ├── Ch3: DRV2605 - Pinky finger
        └── Ch4: DRV2605 - Thumb (PentaBuzzer only, MAX_ACTUATORS == 5)
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

## PentaBuzzer Power / Assembly Facts

- DRV2605s + WS2812 LED are powered **directly from VBat** (no switched rail).
  Motors need a battery: on USB alone, any LRA drive browns the drivers out
  to POR defaults (standby, silent) while I2C keeps working.
- GPIO1 = shared DRV2605 EN + TCA9548A reset. LOW→HIGH **wipes all DRV2605
  registers** — re-run haptic configuration after any toggle.
- Motor JST silk labels 1–5 are **reversed** vs firmware channels:
  finger N ↔ port `5−N` (`MOTOR_SILK_PORT()` in `board_config.h`).
- QA over serial: `MOTOR_DIAG` (buzz all channels + brownout canary),
  `MOTOR_TEST:<n>` (one channel, 2 s), `MOTOR_PRESENT` (per-port open-load
  probe). DRV2605 built-in load diagnostics are NOT valid with our open-loop
  LRA config — the sole exception is `MOTOR_PRESENT`, which flips each chip
  to ERM mode just for the probe (where open-load detect IS specified) and
  restores LRA config after.

## Retry Logic

I2C operations can fail transiently. Use retry with backoff:
- 3 retry attempts
- Brief delay between retries
- Return failure only after all attempts exhausted
