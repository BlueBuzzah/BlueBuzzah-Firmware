# BlueBuzzah Technical Reference
**Version:** 2.0.0
**Hardware Platform:** nRF52840 + Arduino C++ (PlatformIO)

---

## Table of Contents

1. [Hardware Specifications](#hardware-specifications)
2. [Software Configuration](#software-configuration)
3. [Parameter Reference](#parameter-reference)
4. [DRV2605 Register Map](#drv2605-register-map)
5. [I2C Address Map](#i2c-address-map)
6. [Pin Assignments](#pin-assignments)
7. [BLE Specifications](#ble-specifications)
8. [Timing Constants](#timing-constants)
9. [Memory Budget](#memory-budget)
10. [File Structure](#file-structure)
11. [Quick Reference Tables](#quick-reference-tables)
12. [Testing & Validation](#testing--validation)

---

## Terminology Note

This document uses the following device role terminology:
- **PRIMARY** (also known as VL, left glove): Main controller, phone connection point
- **SECONDARY** (also known as VR, right glove): Receives commands via PRIMARY

Hardware specifications apply equally to both gloves.

---

## Hardware Specifications

### Microcontroller (nRF52840)

| Specification | Value | Notes |
|---------------|-------|-------|
| **CPU** | ARM Cortex-M4F @ 64 MHz | 32-bit with FPU |
| **Flash** | 1 MB | ~800 KB available after bootloader |
| **RAM** | 256 KB | ~240 KB available to application |
| **Bluetooth** | BLE 5.0 (5.3 compatible) | 2.4 GHz radio |
| **I2C** | 2x hardware I2C | Using primary I2C bus |
| **ADC** | 12-bit, 8 channels | For battery monitoring |
| **GPIO** | 48 pins total | Limited by board design |
| **USB** | USB 2.0 Full Speed | For programming/debugging |
| **NeoPixel** | 1x onboard RGB LED | Status indicator |
| **RTC** | Yes | Time tracking |
| **Floating Point** | Hardware FPU | Fast math operations |

**Power Consumption**:
- Active (BLE + I2C): ~15 mA @ 3.3V
- Therapy (motors active): ~150-300 mA (depends on intensity)
- Sleep (future): <5 µA

### Haptic Components

#### DRV2605 Motor Driver

| Specification | Value | Notes |
|---------------|-------|-------|
| **Quantity** | 8 total (4 per glove) | One per finger |
| **Supply Voltage** | 2.5-5.2V | Operating from LiPo |
| **I2C Address** | 0x5A (fixed) | Requires multiplexer |
| **I2C Speed** | 400 kHz | Fast mode |
| **Output Voltage** | 0-5.6V peak | Programmable via OD_CLAMP |
| **Output Current** | 250 mA max | Per driver |
| **Waveform Library** | 123 effects | Using RTP mode instead |
| **RTP Mode** | 0-127 amplitude | Real-time playback |
| **Update Rate** | 1 kHz | Amplitude changes |
| **LRA Support** | Yes | Recommended for therapy |
| **ERM Support** | Yes | Alternative actuator type |

#### TCA9548A I2C Multiplexer

| Specification | Value | Notes |
|---------------|-------|-------|
| **Quantity** | 2 total (1 per glove) | PCA9546A variant (4 channels) |
| **I2C Address** | 0x70 (default) | Configurable via pins |
| **Channels** | 4 | One per finger |
| **Supply Voltage** | 2.3-5.5V | Compatible with nRF52840 |
| **I2C Speed** | 400 kHz | Fast mode |
| **Channel ON Resistance** | 5Ω typical | Minimal signal loss |

#### LRA Actuators

| Specification | Value | Notes |
|---------------|-------|-------|
| **Quantity** | 8 total (4 per glove) | Linear Resonant Actuators |
| **Resonance Frequency** | 250 Hz nominal | 200-300 Hz typical |
| **Operating Voltage** | 2.0-3.0V RMS | 2.5V default |
| **Peak Voltage** | 3.0-4.0V | During resonance |
| **Current Draw** | 50-100 mA per motor | At full intensity |
| **Force Output** | ~1.0 G peak | At resonance |
| **Response Time** | 15-25 ms rise time | To 90% amplitude |
| **Mechanical Q** | 100-150 | High efficiency at resonance |
| **Lifetime** | >100 million cycles | Typical LRA spec |

### Power System

#### LiPo Battery

| Specification | Value | Notes |
|---------------|-------|-------|
| **Chemistry** | LiPo (Lithium Polymer) | Single cell |
| **Nominal Voltage** | 3.7V | Range 3.0-4.2V |
| **Capacity** | 500-2000 mAh | Typical 850 mAh |
| **Discharge Rate** | 1C nominal | 500 mA continuous |
| **Cutoff Voltage** | 3.0V | Firmware warning at 3.3V |
| **Charge Voltage** | 4.2V | Standard LiPo charging |
| **Protection** | Built-in PCM | Overcharge/discharge/short |

**Battery Life Estimate** (850 mAh battery):
```
Idle (BLE only):          ~50 hours
Light therapy (25%):      ~4 hours
Moderate therapy (50%):   ~3 hours
Full therapy (100%):      ~2 hours
```

#### Voltage Monitor Circuit

| Component | Value | Notes |
|-----------|-------|-------|
| **Divider R1** | 100kΩ | Upper resistor |
| **Divider R2** | 100kΩ | Lower resistor (to GND) |
| **ADC Pin** | A6 | nRF52840 ADC input |
| **ADC Reference** | 3.3V | Internal reference |
| **Scaling Factor** | 2.0 | Voltage divider ratio |
| **Resolution** | 12-bit (4096 levels) | 0.8 mV per step |

**Voltage Calculation**:
```cpp
uint16_t adcValue = analogRead(BATTERY_PIN);  // 0-1023 (10-bit)
float voltage = (adcValue / 1023.0f) * 3.3f * 2.0f;  // Scale to battery voltage
```

---

## Software Configuration

### Arduino/PlatformIO

| Specification | Value | Notes |
|---------------|-------|-------|
| **Framework** | Arduino | Via PlatformIO |
| **Platform** | nordicnrf52 | Adafruit nRF52 BSP |
| **Board** | adafruit_feather_nrf52840 | Feather nRF52840 Express |
| **Filesystem** | LittleFS | Internal flash storage |
| **Available Storage** | ~1 MB | For code and data |
| **Module Support** | Full C++ | Standard library available |
| **Memory Management** | Manual/RAII | No garbage collection |

### Required Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| `Bluefruit` | Built-in | BLE stack (Adafruit nRF52) |
| `Adafruit_DRV2605` | Latest | Motor driver control |
| `TCA9548A` | Latest | I2C multiplexer |
| `Adafruit_NeoPixel` | Latest | LED control |
| `Adafruit_LittleFS` | Built-in | Filesystem |
| `ArduinoJson` | 7.x | JSON parsing |
| `Wire` | Built-in | I2C communication |

### Configuration Files

| File | Location | Purpose |
|------|----------|---------|
| `platformio.ini` | Project root | Build configuration |
| `settings.json` | LittleFS `/` | Device role and settings |
| `profiles/*.json` | LittleFS `/profiles/` | Therapy profiles |

---

## Parameter Reference

### Therapy Parameters

#### Timing Parameters

| Parameter | Range | Default | Unit | Description |
|-----------|-------|---------|------|-------------|
| `burstDurationMs` | 50-500 | 100 | ms | Buzz duration per finger |
| `interBurstIntervalMs` | 20-200 | 67 | ms | Pause between buzzes |
| `durationSec` | 60-10800 | 7200 | seconds | Total session duration |
| `jitterPercent` | 0-50 | 23.5 | % | Temporal randomization |
| `STARTUP_WINDOW_SEC` | 1-300 | 30 | seconds | BLE command window |

**Timing Formulas**:
```cpp
uint32_t timeRelaxMs = 4 * (burstDurationMs + interBurstIntervalMs);  // Two relax periods per macrocycle
uint16_t timeJitterMs = ((burstDurationMs + interBurstIntervalMs) * jitterPercent) / 200;  // ±Jitter range
uint32_t sessionEndMs = millis() + (durationSec * 1000UL);  // Session end timestamp
```

#### Actuator Parameters

| Parameter | Range | Default | Unit | Description |
|-----------|-------|---------|------|-------------|
| `actuatorType` | LRA, ERM | LRA | - | Motor type |
| `voltage` | 1.0-3.3 | 2.50 | volts | Peak voltage (RMS) |
| `frequencyHz` | 150-300 | 250 | Hz | Driving frequency |
| `amplitudePercent` | 0-100 | 100 | % | Intensity level |

**Voltage Formula** (DRV2605 register):
```cpp
uint8_t registerValue = (uint8_t)(voltage / 0.02122f);  // 0-255
// Example: 2.50V → 2.50 / 0.02122 ≈ 118
```

**Frequency Formula** (DRV2605 register):
```cpp
uint8_t periodValue = (uint8_t)(1.0f / (frequencyHz * 0.00009849f));  // LRA period
// Example: 250 Hz → 1 / (250 * 0.00009849) ≈ 41
```

#### Amplitude Parameters

| Parameter | Range | Default | Unit | Description |
|-----------|-------|---------|------|-------------|
| `amplitudePercent` | 0-100 | 100 | % | Motor intensity |

**DRV2605 Conversion**:
```cpp
uint8_t amplitude = config.amplitudePercent;  // 0-100
uint8_t drvValue = (amplitude * 127) / 100;   // 0-127 (7-bit)
driver.setRealtimeValue(drvValue);
```

#### Pattern Parameters

| Parameter | Values | Default | Description |
|-----------|--------|---------|-------------|
| `patternType` | "random", "sequential" | "random" | Pattern algorithm |
| `mirrorPattern` | true, false | true | Symmetric L/R patterns |
| `jitterPercent` | 0-50 | 23.5 | % temporal randomization |
| `burstsPerCycle` | 3-4 | 4 | Bursts per macrocycle |

**Pattern Generation**:
```cpp
// Non-mirrored (independent L/R)
if (!config.mirrorPattern) {
    shuffleArray(leftSequence, 4);
    shuffleArray(rightSequence, 4);
}

// Mirrored (symmetric L/R)
if (config.mirrorPattern) {
    shuffleArray(leftSequence, 4);
    memcpy(rightSequence, leftSequence, sizeof(leftSequence));
}
```

### Profile Presets

#### Profile Comparison Table

| Parameter | Regular vCR | Noisy vCR (DEFAULT) | Hybrid vCR |
|-----------|-------------|---------------------|------------|
| `burstDurationMs` | 100 | 100 | 100 |
| `interBurstIntervalMs` | 67 | 67 | 67 |
| `jitterPercent` | 0 | 23.5 | 0 |
| `mirrorPattern` | false | true | false |
| `frequencyHz` | 250 | 250 | 210-260 |
| `amplitudePercent` | 100 | 100 | 100 |
| `durationSec` | 7200 | 7200 | 7200 |

**Clinical Use**:
- **Regular vCR**: Standard protocol, published research baseline
- **Noisy vCR**: Default, prevents habituation via temporal jitter
- **Hybrid vCR**: Experimental, mixed-frequency stimulation

---

## DRV2605 Register Map

### Essential Registers

| Address | Name | Function | R/W | Default | Notes |
|---------|------|----------|-----|---------|-------|
| 0x00 | STATUS | Status/Go bit | R/W | 0x00 | Bit 0: Trigger playback |
| 0x01 | MODE | Operating mode | R/W | 0x00 | 0x05 = RTP mode |
| 0x02 | RTP_INPUT | RTP amplitude | W | 0x00 | 0-127 (7-bit) |
| 0x03 | LIBRARY_SEL | Waveform library | R/W | 0x00 | Not used in RTP |
| 0x16 | RATED_VOLTAGE | Rated voltage | R/W | 0x3E | For auto-cal |
| 0x17 | OD_CLAMP | Overdrive clamp | R/W | 0x8C | **Peak voltage limit** |
| 0x1A | FEEDBACK_CTRL | Feedback control | R/W | 0x36 | **N_ERM/LRA select** |
| 0x1D | CONTROL3 | Advanced | R/W | 0xA0 | **Open-loop enable** |
| 0x20 | LRA_RESONANCE_PERIOD | **LRA period** | R/W | 0x33 | **Frequency setting** |

### Configuration Sequence

```cpp
// 1. Set actuator type (FEEDBACK_CTRL)
driver.writeRegister8(0x1A, 0x80);  // LRA mode (bit 7 = 1)

// 2. Enable open-loop mode (CONTROL3)
uint8_t control3 = driver.readRegister8(0x1D);
driver.writeRegister8(0x1D, control3 | 0x21);  // Set bits 5 and 0

// 3. Set peak voltage (OD_CLAMP)
uint8_t voltageVal = (uint8_t)(2.50f / 0.02122f);  // 118 for 2.50V
driver.writeRegister8(0x17, voltageVal);

// 4. Set LRA frequency (LRA_RESONANCE_PERIOD)
uint8_t periodVal = (uint8_t)(1.0f / (250.0f * 0.00009849f));  // 41 for 250 Hz
driver.writeRegister8(0x20, periodVal);

// 5. Set RTP mode (MODE)
driver.setMode(DRV2605_MODE_REALTIME);

// 6. Set amplitude (RTP_INPUT)
driver.setRealtimeValue(127);  // Max intensity
```

### Mode Register (0x01) Values

| Value | Mode | Description |
|-------|------|-------------|
| 0x00 | Internal Trigger | Use waveform library |
| 0x01 | External Trigger (Edge) | Rising edge trigger |
| 0x02 | External Trigger (Level) | High level trigger |
| 0x03 | PWM/Analog Input | Direct PWM control |
| 0x04 | Audio-to-Vibe | Audio input mode |
| 0x05 | **Real-Time Playback** | **RTP mode (used)** |
| 0x06 | Diagnostics | Test mode |
| 0x07 | Auto Calibration | Run auto-cal |

---

## I2C Address Map

### Primary I2C Bus (nRF52840)

| Device | Address | Hex | Binary | Notes |
|--------|---------|-----|--------|-------|
| **TCA9548A Mux** | 112 | 0x70 | 0b1110000 | Default address |
| DRV2605 #0 (via mux) | 90 | 0x5A | 0b1011010 | Multiplexer port 0 |
| DRV2605 #1 (via mux) | 90 | 0x5A | 0b1011010 | Multiplexer port 1 |
| DRV2605 #2 (via mux) | 90 | 0x5A | 0b1011010 | Multiplexer port 2 |
| DRV2605 #3 (via mux) | 90 | 0x5A | 0b1011010 | Multiplexer port 3 |

**I2C Speed**: 400 kHz Fast Mode

**Pull-up Resistors**: 2.2kΩ - 10kΩ (typically 4.7kΩ)

### I2C Scan Results

**Expected Output**:
```
[PRIMARY] I2C scan - addresses found: 0x70
[PRIMARY] Mux Channel 0: 0x5A (DRV2605)
[PRIMARY] Mux Channel 1: 0x5A (DRV2605)
[PRIMARY] Mux Channel 2: 0x5A (DRV2605)
[PRIMARY] Mux Channel 3: 0x5A (DRV2605)
```

**If Missing Addresses**:
- `0x70` missing: Multiplexer not powered or disconnected
- `0x5A` missing on one channel: DRV2605 not powered or disconnected

---

## Pin Assignments

### nRF52840 Standard Pins (Adafruit Feather)

| Function | Pin Name | Physical Pin | Notes |
|----------|----------|--------------|-------|
| **I2C SDA** | SDA | 26 | I2C data line |
| **I2C SCL** | SCL | 27 | I2C clock line |
| **Battery Monitor** | A6 | A6 | ADC input via divider |
| **NeoPixel** | D8 | 8 | Onboard RGB LED |
| **USB D+** | USB_DP | USB | USB data + |
| **USB D-** | USB_DM | USB | USB data - |
| **Reset** | RESET | RST | Reset button |
| **3.3V Out** | 3V3 | 3V3 | Regulated 3.3V |
| **Battery In** | VBAT | VBAT | LiPo input (~3.7V) |
| **Ground** | GND | GND | Common ground |

### I2C Wiring

```
nRF52840
  SDA (Pin 26) ──┬── 4.7kΩ pull-up to 3.3V
                 └── TCA9548A SDA
  SCL (Pin 27) ──┬── 4.7kΩ pull-up to 3.3V
                 └── TCA9548A SCL

TCA9548A
  Port 0 SDA/SCL ── DRV2605 #0 (Pinky)
  Port 1 SDA/SCL ── DRV2605 #1 (Ring)
  Port 2 SDA/SCL ── DRV2605 #2 (Middle)
  Port 3 SDA/SCL ── DRV2605 #3 (Index)

DRV2605 #X
  IN+ ── LRA motor +
  IN- ── LRA motor -
  VDD ── 3.3V (from nRF52840)
  GND ── Common ground
```

---

## BLE Specifications

### BLE Radio Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| **BLE Version** | 5.0 (5.3 compatible) | nRF52840 hardware |
| **Frequency** | 2.400-2.4835 GHz | ISM band |
| **Channels** | 40 (3 advertising, 37 data) | Standard BLE |
| **TX Power** | +4 dBm nominal | Up to +8 dBm available |
| **RX Sensitivity** | -95 dBm | Typical |
| **Range** | 10-30 meters | Line of sight, indoor |
| **Data Rate** | 1 Mbps (LE 1M PHY) | Standard mode |

### Connection Parameters

| Parameter | PRIMARY | SECONDARY | Unit | Notes |
|-----------|---------|-----------|------|-------|
| **Connection Interval** | 7.5 | 7.5 | ms | Min latency |
| **Slave Latency** | 0 | 0 | intervals | No skipping |
| **Supervision Timeout** | 100 | 100 | ms | Disconnect detection |
| **MTU Size** | 247 | 247 | bytes | Extended MTU |
| **Max Data Length** | 251 | 251 | bytes | BLE 5.0 |

**Latency Calculation**:
```
Min latency = Connection Interval = 7.5 ms
Typical latency = 7.5-15 ms (one to two intervals)
Max latency (timeout) = 100 ms
```

### UART Service (Nordic UART Service - NUS)

| Characteristic | UUID | Properties | Max Size | Notes |
|----------------|------|------------|----------|-------|
| **Service** | 6E400001-B5A3-F393-E0A9-E50E24DCCA9E | - | - | Nordic UART |
| **TX (Write)** | 6E400002-B5A3-F393-E0A9-E50E24DCCA9E | Write, Write No Response | 247 bytes | Phone → PRIMARY |
| **RX (Notify)** | 6E400003-B5A3-F393-E0A9-E50E24DCCA9E | Notify | 247 bytes | PRIMARY → Phone |

**Throughput**:
```
Max throughput = (247 bytes / 7.5 ms) = 32.9 KB/s
Typical: ~10-20 KB/s (with protocol overhead)
```

---

## Timing Constants

### BLE Timing

| Constant | Value | Location | Notes |
|----------|-------|----------|-------|
| `CONNECTION_TIMEOUT_SEC` | 30s | config.h | Max time to find device |
| `HEARTBEAT_INTERVAL_MS` | 2000ms | config.h | Heartbeat send interval |
| `HEARTBEAT_TIMEOUT_MS` | 6000ms | config.h | 3 missed = connection lost |
| `COMMAND_TIMEOUT_MS` | 5000ms | config.h | BLE command timeout |

### Therapy Timing

| Constant | Value | Source | Notes |
|----------|-------|--------|-------|
| `burstDurationMs` | 100ms | config (default) | Buzz duration |
| `interBurstIntervalMs` | 67ms | config (default) | Inter-buzz pause |
| `jitterPercent` | 23.5% | config (default) | Random variation |
| `timeRelaxMs` | 668ms | calculated | 4 × (ON + OFF) |
| `durationSec` | 7200s | config (120 min) | Total therapy time |

**Macrocycle Breakdown**:
```
Burst Sequence 1: (100 + 67 ± 19.6) × 4 fingers = 588-744ms
Burst Sequence 2: 588-744ms
Burst Sequence 3: 588-744ms
Relax Period 1: 668ms
Relax Period 2: 668ms
--------------------------------------------------------------
Total: 3100-3568ms (average 3334ms)
```

### System Timing

| Constant | Value | Location | Notes |
|----------|-------|----------|-------|
| `STARTUP_WINDOW_SEC` | 30s | config.h | BLE command window |
| `I2C_FREQUENCY` | 400000 | config.h | I2C clock speed |
| `BATTERY_CHECK_INTERVAL_MS` | 60000ms | config.h | Battery polling |

---

## Memory Budget

### Flash Storage (~1 MB available)

| Category | Size | % | Files |
|----------|------|---|-------|
| **Bootloader** | ~200 KB | 20% | Adafruit nRF52 bootloader |
| **Firmware Code** | ~150 KB | 15% | src/*.cpp + libraries |
| **LittleFS** | ~100 KB | 10% | Settings, profiles |
| **Free Space** | ~550 KB | 55% | Available |

**Total Used**: ~450 KB / 1 MB (45%)

### RAM Usage (~256 KB available)

| Category | Size | % | Notes |
|----------|------|---|-------|
| **Bluefruit Stack** | ~40 KB | 16% | BLE radio buffers |
| **Application Heap** | ~80 KB | 31% | Global variables, objects |
| **Stack** | ~32 KB | 12.5% | Function call stack |
| **Pattern Buffers** | ~1 KB | 0.4% | Sequence arrays |
| **Driver Objects** | ~5 KB | 2% | DRV2605 instances |
| **Free Heap** | ~98 KB | 38% | Dynamic allocation |

**Total Used**: ~158 KB / 256 KB (62%)

**Memory Safety**:
- No garbage collection - use RAII patterns
- Pre-allocate buffers at startup
- Avoid heap fragmentation with static allocation

---

## File Structure

### Complete Source Tree

```
BlueBuzzah-Firmware/
├── platformio.ini                 # Build configuration
├── CLAUDE.md                      # Development guide
├── README.md                      # Project overview
│
├── include/                       # Header files
│   ├── config.h                   # Pin definitions, constants
│   ├── types.h                    # Enums, structs, device roles
│   ├── hardware.h                 # Motor, battery, I2C mux
│   ├── ble_manager.h              # BLE stack, connections
│   ├── therapy_engine.h           # Pattern generation
│   ├── sync_protocol.h            # PRIMARY-SECONDARY messaging
│   ├── state_machine.h            # Therapy FSM
│   ├── menu_controller.h          # Command routing
│   ├── profile_manager.h          # Profile loading/saving
│   └── led_controller.h           # NeoPixel control
│
├── src/                           # C++ source files
│   ├── main.cpp                   # setup() / loop() entry point
│   ├── hardware.cpp               # DRV2605, NeoPixel, battery, I2C mux
│   ├── ble_manager.cpp            # BLE advertising, connections, UART
│   ├── therapy_engine.cpp         # Pattern algorithms, motor scheduling
│   ├── sync_protocol.cpp          # Serialize/deserialize sync commands
│   ├── state_machine.cpp          # 11-state therapy FSM
│   ├── menu_controller.cpp        # Phone command parsing/routing
│   └── profile_manager.cpp        # Therapy profile management
│
├── test/                          # PlatformIO tests
│   ├── test_state_machine/
│   ├── test_sync_protocol/
│   ├── test_therapy_engine/
│   └── test_types/
│
└── docs/                          # Documentation
    ├── ARDUINO_FIRMWARE_ARCHITECTURE.md
    ├── ARDUINO_MIGRATION.md
    ├── API_REFERENCE.md
    ├── ARCHITECTURE.md
    ├── BLE_PROTOCOL.md
    ├── BOOT_SEQUENCE.md
    ├── CALIBRATION_GUIDE.md
    ├── COMMAND_REFERENCE.md
    ├── STATE_DIAGRAM.md
    ├── SYNCHRONIZATION_PROTOCOL.md
    ├── TECHNICAL_REFERENCE.md      # This document
    ├── TESTING.md
    └── THERAPY_ENGINE.md
```

**Total Lines of Code**:
- Firmware: ~3,000 lines (src/ + include/)
- Documentation: ~15,000 lines (docs/)
- Tests: ~500 lines (test/)

---

## Quick Reference Tables

### Command Summary

| Command | Args | Response Time | Use Case |
|---------|------|---------------|----------|
| INFO | none | 100-500ms | Device status |
| BATTERY | none | 100-500ms | Battery check |
| PING | none | <50ms | Latency test |
| PROFILE_LIST | none | <50ms | List profiles |
| PROFILE_LOAD | name | 50-250ms | Load preset |
| PROFILE_GET | none | <50ms | View settings |
| PROFILE_CUSTOM | json | 50-250ms | Custom profile |
| SESSION_START | profile:duration | 100-500ms | Start therapy |
| SESSION_PAUSE | none | <50ms | Pause therapy |
| SESSION_RESUME | none | <50ms | Resume therapy |
| SESSION_STOP | none | <50ms | Stop therapy |
| SESSION_STATUS | none | <50ms | Progress check |
| CALIBRATE_START | none | <50ms | Enter calibration |
| CALIBRATE_BUZZ | finger:amplitude:duration | 50-2050ms | Test motor |
| CALIBRATE_STOP | none | <50ms | Exit calibration |
| HELP | none | <50ms | List commands |
| RESTART | none | 500ms + reboot | Reboot device |
| SET_ROLE | PRIMARY\|SECONDARY | 100ms | Set device role |

### Error Code Reference

| Error | Typical Cause | Fix |
|-------|--------------|-----|
| Unknown command | Typo or unsupported command | Check HELP |
| Invalid profile | Profile not found | Use PROFILE_LIST |
| SECONDARY not connected | SECONDARY glove offline | Connect SECONDARY first |
| Battery too low | Voltage <3.3V | Charge battery |
| No active session | Session not started | Send SESSION_START |
| Cannot modify during session | Profile change blocked | Stop session first |
| Invalid parameter | Unknown key or out of range | Check documentation |
| Not in calibration mode | Calibration not active | Send CALIBRATE_START |
| Invalid finger index | Finger not 0-3 | Valid range: 0-3 |

### Voltage Thresholds

| Voltage | Status | LED Color | Action |
|---------|--------|-----------|--------|
| >3.6V | Good | Green | Full operation |
| 3.4-3.6V | Low | Orange | Warning |
| <3.4V | Critical | Red | Therapy blocked |
| <3.0V | Dead | Off | Charge immediately |

### Finger Index Map

| Index | Glove | Finger | Motor Channel |
|-------|-------|--------|---------------|
| 0 | PRIMARY | Pinky | 0 |
| 1 | PRIMARY | Ring | 1 |
| 2 | PRIMARY | Middle | 2 |
| 3 | PRIMARY | Index | 3 |
| 0 | SECONDARY | Pinky | 0 |
| 1 | SECONDARY | Ring | 1 |
| 2 | SECONDARY | Middle | 2 |
| 3 | SECONDARY | Index | 3 |

---

## Testing & Validation

### PlatformIO Test Framework

BlueBuzzah v2 uses **PlatformIO's native test framework** for unit and integration testing.

**Test Commands:**
```bash
pio test                        # Run all tests
pio test -e native              # Run native tests (no hardware)
pio test -e adafruit_feather_nrf52840  # Run on-device tests
```

**Test Categories:**

| Category | Location | Purpose |
|----------|----------|---------|
| State Machine | `test/test_state_machine/` | FSM transitions |
| Sync Protocol | `test/test_sync_protocol/` | Message parsing |
| Therapy Engine | `test/test_therapy_engine/` | Pattern generation |
| Types | `test/test_types/` | Struct/enum validation |

**Test Coverage:**
- State machine transitions
- Pattern generation algorithms
- SYNC protocol parsing
- BLE command handling
- Battery status calculation

**Testing Requirements:**
- PlatformIO Core CLI or IDE
- For hardware tests: Feather nRF52840 connected via USB

**See:** [Testing Guide](TESTING.md) for complete test procedures.

---

**Document Maintenance:**
Update this document when:
- Changing hardware specifications
- Modifying parameter ranges
- Adding new configuration options
- Updating pin assignments
- Changing memory budget
- Updating test coverage statistics

---

**Platform**: Arduino C++ with PlatformIO
**Last Updated**: 2025-01-11
