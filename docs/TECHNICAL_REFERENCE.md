# BlueBuzzah Technical Reference
**Version:** 1.0.0
**Hardware Platform:** nRF52840 + CircuitPython 9.x

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
| **RAM** | 256 KB | ~200 KB available to CircuitPython |
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
| **I2C Speed** | 100 kHz (standard) | Up to 400 kHz supported |
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
| **I2C Speed** | 100 kHz nominal | Up to 400 kHz |
| **Channel ON Resistance** | 5Ω typical | Minimal signal loss |

#### LRA Actuators

| Specification | Value | Notes |
|---------------|-------|-------|
| **Quantity** | 8 total (4 per glove) | Linear Resonant Actuators (firmware supports up to 5 per device) |
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
| **ADC Pin** | board.VOLTAGE_MONITOR | nRF52840 ADC input |
| **ADC Reference** | 3.3V | Internal reference |
| **Scaling Factor** | 2.0 | Voltage divider ratio |
| **Resolution** | 12-bit (4096 levels) | 0.8 mV per step |

**Voltage Calculation**:
```python
adc_value = analogio.AnalogIn(board.VOLTAGE_MONITOR).value  # 0-65535
voltage = (adc_value / 65535) * 3.3 * 2.0  # Scale to battery voltage
```

---

## Software Configuration

### CircuitPython

| Specification | Value | Notes |
|---------------|-------|-------|
| **Version** | 9.x.x | Latest stable release |
| **Platform** | nRF52840 | Adafruit CircuitPython build |
| **Filesystem** | FAT32 | CIRCUITPY drive |
| **Available Storage** | ~2 MB | For code and libraries |
| **Module Support** | Limited subset | No asyncio, threading |
| **Garbage Collection** | Manual `gc.collect()` | No auto-compaction |

### Required Libraries

| Library | Version | Size | Purpose |
|---------|---------|------|---------|
| `adafruit_ble` | Latest | ~100 KB | Bluetooth stack |
| `adafruit_bluefruit_connect` | Latest | ~20 KB | BLE utilities |
| `adafruit_drv2605` | Latest | ~15 KB | Motor driver control |
| `adafruit_tca9548a` | Latest | ~10 KB | I2C multiplexer |
| `neopixel` | Built-in | - | LED control |
| `adafruit_itertools` | Latest | ~5 KB | Permutations |
| `adafruit_datetime` | Latest | ~10 KB | Time utilities |
| `adafruit_ticks` | Latest | ~5 KB | Timing functions |

**Total Library Size**: ~165 KB

### Configuration Files

| File | Size | Purpose |
|------|------|---------|
| `config.py` | 1 KB | Device role and BLE name |
| `auth_token.py` | 0.5 KB | Security validation |
| `profiles/defaults.py` | 1 KB | Active therapy profile |
| `profiles/reg_vcr.py` | 1 KB | Regular VCR preset |
| `profiles/noisy_vcr.py` | 1 KB | Noisy VCR preset (DEFAULT) |
| `profiles/hybrid_vcr.py` | 1 KB | Hybrid VCR preset |

---

## Parameter Reference

### Therapy Parameters

#### Timing Parameters

| Parameter | Range | Default | Unit | Description |
|-----------|-------|---------|------|-------------|
| `ON` | 0.050-0.500 | 0.100 | seconds | Buzz duration per finger |
| `OFF` | 0.020-0.200 | 0.067 | seconds | Pause between buzzes |
| `SESSION` | 1-180 | 120 | minutes | Total session duration |
| `TIME_RELAX` | calculated | 0.668 | seconds | 4 × (ON + OFF) |
| `TIME_JITTER` | calculated | ±0.0196 | seconds | JITTER% of (ON+OFF)/2 |
| `STARTUP_WINDOW` | 1-300 | 30 | seconds | BLE command window |

**Timing Formulas**:
```python
TIME_RELAX = 4 * (TIME_ON + TIME_OFF)  # Two relax periods per macrocycle
TIME_JITTER = (TIME_ON + TIME_OFF) * (JITTER / 100) / 2  # ±Jitter range
TIME_END = time.time() + 60 * TIME_SESSION  # Session end timestamp
```

#### Actuator Parameters

| Parameter | Range | Default | Unit | Description |
|-----------|-------|---------|------|-------------|
| `TYPE` | "LRA", "ERM" | "LRA" | - | Motor type |
| `VOLT` | 1.0-3.3 | 2.50 | volts | Peak voltage (RMS) |
| `FREQ` | 150-300 | 250 | Hz | Driving frequency |
| `ACTUATOR_FREQL` | 150-300 | 210 | Hz | Low freq (RANDOM_FREQ) |
| `ACTUATOR_FREQH` | 150-300 | 260 | Hz | High freq (RANDOM_FREQ) |
| `RANDOM_FREQ` | True, False | False | - | Randomize freq per cycle |

**Voltage Formula** (DRV2605 register):
```python
register_value = int(ACTUATOR_VOLTAGE / 0.02122)  # 0-255
# Example: 2.50V → 2.50 / 0.02122 ≈ 118
```

**Frequency Formula** (DRV2605 register):
```python
register_value = int(1 / (ACTUATOR_FREQUENCY * 0.00009849))  # LRA period
# Example: 250 Hz → 1 / (250 * 0.00009849) ≈ 41
```

#### Amplitude Parameters

| Parameter | Range | Default | Unit | Description |
|-----------|-------|---------|------|-------------|
| `AMPMIN` | 0-100 | 100 | % | Minimum intensity |
| `AMPMAX` | 0-100 | 100 | % | Maximum intensity |

**DRV2605 Conversion**:
```python
drv_amplitude = random.randint(AMPLITUDE_MIN, AMPLITUDE_MAX)  # 0-100
drv_value = int((drv_amplitude / 100.0) * 127)  # 0-127 (7-bit)
driver.realtime_value = drv_value
```

**Fixed vs Variable Intensity**:
```python
# Fixed intensity (default)
AMPLITUDE_MIN = 100
AMPLITUDE_MAX = 100  # Always 100%

# Variable intensity (±10% randomization)
AMPLITUDE_MIN = 90
AMPLITUDE_MAX = 110  # Actually clamped to 100
```

#### Pattern Parameters

| Parameter | Values | Default | Description |
|-----------|--------|---------|-------------|
| `PATTERN` | "RNDP" | "RNDP" | Always Random Permutation |
| `MIRROR` | True, False | True | Symmetric L/R patterns |
| `JITTER` | 0-50 | 23.5 | % temporal randomization |
| `SYNC_LED` | True, False | True | Flash LED at end of cycle |

**Pattern Generation**:
```python
# Non-mirrored (independent L/R)
MIRROR = False
pattern = zip(random.choice(L_RNDP), random.choice(R_RNDP))

# Mirrored (symmetric L/R)
MIRROR = True
L_pattern = random.choice(L_RNDP)
R_pattern = [x + 4 for x in L_pattern]  # Mirror mapping
pattern = zip(L_pattern, R_pattern)
```

### Profile Presets

#### Profile Comparison Table

| Parameter | Regular VCR | Noisy VCR (DEFAULT) | Hybrid VCR |
|-----------|-------------|---------------------|------------|
| `ON` | 0.100s | 0.100s | 0.100s |
| `OFF` | 0.067s | 0.067s | 0.067s |
| `JITTER` | 0% | 23.5% | 0% |
| `MIRROR` | False | True | False |
| `RANDOM_FREQ` | No | No | Yes |
| `VOLT` | 2.50V | 2.50V | 2.50V |
| `FREQ` | 250 Hz | 250 Hz | 210-260 Hz |
| `AMPMIN` | 100% | 100% | 100% |
| `AMPMAX` | 100% | 100% | 100% |
| `SESSION` | 120 min | 120 min | 120 min |

**Clinical Use**:
- **Regular VCR**: Standard protocol, published research baseline
- **Noisy VCR**: Default, prevents habituation via temporal jitter
- **Hybrid VCR**: Experimental, mixed-frequency stimulation

---

## DRV2605 Register Map

### Essential Registers

| Address | Name | Function | R/W | Default | Notes |
|---------|------|----------|-----|---------|-------|
| 0x00 | STATUS | Status/Go bit | R/W | 0x00 | Bit 0: Trigger playback |
| 0x01 | MODE | Operating mode | R/W | 0x00 | 0x05 = RTP mode |
| 0x02 | RTP_INPUT | RTP amplitude | W | 0x00 | 0-127 (7-bit) |
| 0x03 | LIBRARY_SEL | Waveform library | R/W | 0x00 | Not used in RTP |
| 0x04-0x0B | WAVESEQ[1-8] | Waveform sequence | R/W | 0x00 | Not used in RTP |
| 0x0C | GO | Execute command | W | 0x00 | Trigger waveform |
| 0x0D | OVERDRIVE_OFF | Overdrive time | R/W | 0x00 | Auto-braking |
| 0x0E | SUSTAIN_POS_OFF | Sustain time | R/W | 0x00 | Positive drive |
| 0x0F | SUSTAIN_NEG_OFF | Sustain time | R/W | 0x00 | Negative drive |
| 0x10 | BRAKE_OFF | Brake time | R/W | 0x00 | Braking period |
| 0x11 | AUDIO2VIBE_CTRL | Audio-to-vibe | R/W | 0x00 | Not used |
| 0x12 | AUDIO2VIBE_MIN | Min input level | R/W | 0x00 | Not used |
| 0x13 | AUDIO2VIBE_MAX | Max input level | R/W | 0xFF | Not used |
| 0x14 | AUDIO2VIBE_PEAK | Peak time | R/W | 0x00 | Not used |
| 0x15 | AUDIO2VIBE_FILTER | Filter select | R/W | 0x00 | Not used |
| 0x16 | RATED_VOLTAGE | Rated voltage | R/W | 0x3E | For auto-cal |
| 0x17 | OD_CLAMP | Overdrive clamp | R/W | 0x8C | **Peak voltage limit** |
| 0x18 | AUTO_CAL_COMP | Compensation | R | 0x0C | Auto-cal result |
| 0x19 | AUTO_CAL_BEMF | Back-EMF | R | 0x6C | Auto-cal result |
| 0x1A | FEEDBACK_CTRL | Feedback control | R/W | 0x36 | **N_ERM/LRA select** |
| 0x1B | CONTROL1 | Drive time | R/W | 0x93 | Not used |
| 0x1C | CONTROL2 | Sample time | R/W | 0xF5 | Not used |
| 0x1D | CONTROL3 | Advanced | R/W | 0xA0 | **Open-loop enable** |
| 0x1E | CONTROL4 | Auto-cal | R/W | 0x20 | Not used |
| 0x1F | VBAT | Battery voltage | R | varies | Read-only |
| 0x20 | LRA_RESONANCE_PERIOD | **LRA period** | R/W | 0x33 | **Frequency setting** |

### Configuration Sequence

```python
# 1. Set actuator type (FEEDBACK_CTRL)
driver._write_u8(0x1A, 0x00)  # ERM mode (bit 7 = 0)
# OR
driver._write_u8(0x1A, 0x80)  # LRA mode (bit 7 = 1)

# 2. Enable open-loop mode (CONTROL3)
control3 = driver._read_u8(0x1D)
driver._write_u8(0x1D, control3 | 0x21)  # Set bits 5 and 0

# 3. Set peak voltage (OD_CLAMP)
voltage_val = int(2.50 / 0.02122)  # 118 for 2.50V
driver._write_u8(0x17, voltage_val)

# 4. Set LRA frequency (LRA_RESONANCE_PERIOD)
period_val = int(1 / (250 * 0.00009849))  # 41 for 250 Hz
driver._write_u8(0x20, period_val)

# 5. Set RTP mode (MODE)
driver._write_u8(0x01, 0x05)  # Real-time playback

# 6. Set amplitude (RTP_INPUT)
driver._write_u8(0x02, 127)  # Max intensity

# 7. Motor activates automatically in RTP mode
# No need to write GO register
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

**I2C Speed**: 100 kHz standard mode

**Pull-up Resistors**: 2.2kΩ - 10kΩ (typically 4.7kΩ)

### I2C Scan Results

**Expected Output**:
```
[VL] I2C Primary addresses found: ['0x70']
[VL] Secondary i2c Channel 0: ['0x5a']
[VL] Secondary i2c Channel 1: ['0x5a']
[VL] Secondary i2c Channel 2: ['0x5a']
[VL] Secondary i2c Channel 3: ['0x5a']
```

**If Missing Addresses**:
- `0x70` missing: Multiplexer not powered or disconnected
- `0x5a` missing on one channel: DRV2605 not powered or disconnected

---

## Pin Assignments

### nRF52840 Standard Pins (Adafruit Feather/ItsyBitsy)

| Function | Pin Name | Physical Pin | Notes |
|----------|----------|--------------|-------|
| **I2C SDA** | board.SDA | 26 (varies) | I2C data line |
| **I2C SCL** | board.SCL | 27 (varies) | I2C clock line |
| **Battery Monitor** | board.VOLTAGE_MONITOR | A6 (varies) | ADC input via divider |
| **NeoPixel** | board.NEOPIXEL | Varies | Onboard RGB LED |
| **USB D+** | board.USB_DP | USB | USB data + |
| **USB D-** | board.USB_DM | USB | USB data - |
| **Reset** | board.RESET | RST | Reset button |
| **3.3V Out** | board.VOLTAGE_3V3 | 3V3 | Regulated 3.3V |
| **Battery In** | board.VBAT | VBAT | LiPo input (~3.7V) |
| **Ground** | board.GND | GND | Common ground |

**Note**: Exact pin numbers vary by board manufacturer (Adafruit Feather vs ItsyBitsy nRF52840)

### I2C Wiring

```
nRF52840
  SDA (Pin 26) ──┬── 4.7kΩ pull-up to 3.3V
                 └── TCA9548A SDA
  SCL (Pin 27) ──┬── 4.7kΩ pull-up to 3.3V
                 └── TCA9548A SCL

TCA9548A
  Port 0 SDA/SCL ── DRV2605 #0 (Thumb)
  Port 1 SDA/SCL ── DRV2605 #1 (Index)
  Port 2 SDA/SCL ── DRV2605 #2 (Middle)
  Port 3 SDA/SCL ── DRV2605 #3 (Ring)

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

| Parameter | VL Value | VR Value | Unit | Notes |
|-----------|----------|----------|------|-------|
| **Connection Interval** | 7.5 | 7.5 | ms | Min latency |
| **Slave Latency** | 0 | 0 | intervals | No skipping |
| **Supervision Timeout** | 100 | 100 | ms | Disconnect detection |
| **MTU Size** | 20 | 20 | bytes | Default GATT MTU |
| **Max Data Length** | 27 | 27 | bytes | BLE 4.2+ |

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
| **TX (Write)** | 6E400002-B5A3-F393-E0A9-E50E24DCCA9E | Write, Write No Response | 20 bytes | Phone → VL |
| **RX (Notify)** | 6E400003-B5A3-F393-E0A9-E50E24DCCA9E | Notify | 20 bytes | VL → Phone |

**Message Fragmentation**:
- Max 20 bytes per BLE packet
- Long messages automatically fragmented by CircuitPython
- Reassembled on receiver side

**Throughput**:
```
Max throughput = (20 bytes / 7.5 ms) = 2.67 KB/s
Typical: ~1-2 KB/s (with protocol overhead)
```

---

## Timing Constants

### BLE Timing

| Constant | Value | Location | Notes |
|----------|-------|----------|-------|
| `CONNECTION_TIMEOUT` | 15s | ble_connection.py:60 | Max time to find VL/VR |
| `READY_TIMEOUT` | 8s | ble_connection.py:358 | Max time for READY signal |
| `SYNC_TIMEOUT` | 2s | sync_protocol.py:76 | SYNC_ADJ ACK timeout |
| `EXECUTE_BUZZ_TIMEOUT` | 10s | sync_protocol.py:219 | CRITICAL: VR safety timeout |
| `BUZZ_COMPLETE_TIMEOUT` | 3s | sync_protocol.py:336 | VL waits for VR ACK |
| `BATTERY_QUERY_TIMEOUT` | 1s | ble_connection.py:869 | VR battery query |
| `CONNECTION_TYPE_TIMEOUT` | 3s | ble_connection.py:102 | Phone vs VR detection |
| `BLE_LATENCY_COMPENSATION` | 21ms | ble_connection.py:492 | Time sync adjustment |

### Therapy Timing

| Constant | Value | Source | Notes |
|----------|-------|--------|-------|
| `TIME_ON` | 100ms | config (default) | Buzz duration |
| `TIME_OFF` | 67ms | config (default) | Inter-buzz pause |
| `TIME_JITTER` | ±19.6ms | config (23.5%) | Random variation |
| `TIME_RELAX` | 668ms | calculated | 4 × (ON + OFF) |
| `MACROCYCLE_DURATION` | ~3.3s | measured | 3 buzzes + 2 relax |
| `SESSION_DURATION` | 7200s | config (120 min) | Total therapy time |

**Macrocycle Breakdown**:
```
Buzz Sequence 1: (100 + 67 ± 19.6) × 4 fingers = 588-744ms
Buzz Sequence 2: 588-744ms
Buzz Sequence 3: 588-744ms
Relax Period 1: 668ms
Relax Period 2: 668ms
--------------------------------------------------------------
Total: 3100-3568ms (average 3334ms)
```

### System Timing

| Constant | Value | Location | Notes |
|----------|-------|----------|-------|
| `STARTUP_WINDOW` | 30s | config (default) | BLE command window |
| `I2C_RETRY_DELAY` | 0.5s | haptic_controller.py:93 | Between I2C init attempts |
| `GC_INTERVAL` | per macrocycle | vcr_engine.py:234 | Garbage collection |
| `POLLING_INTERVAL` | 1-10ms | various | BLE message polling |

---

## Synchronization Statistics

### SyncStats Module (sync_stats.py)

The `SyncStats` module tracks timing metrics for bilateral synchronization validation. It ensures the SimpleSyncProtocol meets the <10ms latency requirement for vCR therapy.

**Metrics Tracked**:

| Metric | Description | Unit |
|--------|-------------|------|
| `network_latency` | BLE transmission time (PRIMARY → SECONDARY) | µs |
| `execution_time` | Motor activation processing on SECONDARY | µs |
| `total_latency` | End-to-end synchronization accuracy | µs |

**Usage** (SECONDARY device):
```python
from sync_stats import SyncStats

# Initialize with circular buffer
stats = SyncStats(max_samples=100)

# During EXECUTE_BUZZ processing
def handle_execute_buzz(primary_timestamp_us):
    receive_time_us = time.monotonic_ns() // 1000
    network_latency = receive_time_us - primary_timestamp_us

    # Execute motor activation
    activate_motors()

    # Calculate execution time
    execution_time = (time.monotonic_ns() // 1000) - receive_time_us
    total_latency = network_latency + execution_time

    # Record sample
    stats.add_sample(network_latency, execution_time, total_latency)

# Print report periodically
stats.print_report()
```

**Statistics Output Example**:
```
============================================================
SYNCHRONIZATION TIMING STATISTICS
============================================================
Total samples: 1234
Buffer size: 100/100
Last sample: 0.5s ago

NETWORK LATENCY:
  Mean:    2847.32 µs ( 2.847 ms)
  Median:  2654.00 µs ( 2.654 ms)
  P95:     4123.00 µs ( 4.123 ms)
  P99:     5892.00 µs ( 5.892 ms)

TOTAL LATENCY:
  Mean:    4521.18 µs ( 4.521 ms)
  Median:  4312.00 µs ( 4.312 ms)
  P95:     6789.00 µs ( 6.789 ms)
  P99:     8456.00 µs ( 8.456 ms)

vCR THERAPY REQUIREMENTS (< 10ms total latency):
  Mean:   ✅ PASS (4.521 ms)
  P95:    ✅ PASS (6.789 ms)
  P99:    ✅ PASS (8.456 ms)
============================================================
```

**vCR Requirement**: Total latency must be <10ms for bilateral coordination.

---

## Memory Budget

### Flash Storage (~2 MB available)

| Category | Size | % | Files |
|----------|------|---|-------|
| **CircuitPython Core** | ~600 KB | 30% | CircuitPython interpreter |
| **Libraries** | ~165 KB | 8% | Adafruit + dependencies |
| **Firmware Code** | ~50 KB | 2.5% | src/*.py + modules/*.py |
| **Profiles** | ~5 KB | 0.25% | profiles/*.py |
| **Documentation** | ~10 KB | 0.5% | README, comments |
| **Free Space** | ~1170 KB | 58.75% | Available |

**Total Used**: ~830 KB / 2 MB (41.5%)

### RAM Usage (~200 KB available)

| Category | Size | % | Notes |
|----------|------|---|-------|
| **CircuitPython Runtime** | ~80 KB | 40% | Interpreter overhead |
| **BLE Stack** | ~30 KB | 15% | Radio buffers |
| **Firmware Heap** | ~50 KB | 25% | Global variables, objects |
| **Pattern Lists** | ~0.4 KB | 0.2% | L_RNDP + R_RNDP |
| **Driver Objects** | ~2 KB | 1% | Haptic controller |
| **Free Heap** | ~37.6 KB | 18.8% | Dynamic allocation |

**Total Used**: ~162.4 KB / 200 KB (81.2%)

**Memory Safety**:
- Periodic `gc.collect()` every macrocycle (~3s)
- Avoid allocations in critical path
- Pre-allocate pattern lists at session start

---

## File Structure

### Complete Source Tree

```
BlueBuzzah/
├── src/                           # Firmware source (deployed to CIRCUITPY)
│   ├── code.py                    # Entry point (164 lines)
│   ├── config.py                  # Device configuration
│   ├── auth_token.py              # Security token
│   │
│   ├── modules/                   # Core functionality
│   │   ├── __init__.py
│   │   ├── ble_connection.py      # BLE radio (897 lines)
│   │   ├── vcr_engine.py          # Therapy execution (491 lines)
│   │   ├── haptic_controller.py   # Motor control (306 lines)
│   │   ├── menu_controller.py     # Commands (1057 lines)
│   │   ├── sync_protocol.py       # VL↔VR messaging (338 lines)
│   │   ├── profile_manager.py     # Parameters (274 lines)
│   │   ├── session_manager.py     # Session state (329 lines)
│   │   ├── calibration_mode.py    # Motor testing (275 lines)
│   │   ├── response_formatter.py  # BLE responses
│   │   ├── validators.py          # Parameter validation
│   │   └── utils.py               # Utilities
│   │
│   └── profiles/                  # Therapy profiles
│       ├── __init__.py
│       ├── defaults.py            # Active profile (loaded at boot)
│       ├── reg_vcr.py             # Regular VCR
│       ├── noisy_vcr.py           # Noisy VCR (DEFAULT)
│       ├── hybrid_vcr.py          # Hybrid VCR
│       └── custom_vcr.py          # User-defined
│
├── docs/                          # Documentation
│   ├── FIRMWARE_ARCHITECTURE.md  # System overview
│   ├── SYNCHRONIZATION_PROTOCOL.md # BLE protocol
│   ├── THERAPY_ENGINE.md          # VCR implementation
│   ├── COMMAND_REFERENCE.md       # BLE commands
│   ├── CALIBRATION_GUIDE.md       # Motor testing
│   ├── TECHNICAL_REFERENCE.md     # This document
│   └── BLE_PROTOCOL.md            # Protocol v2.0.0 spec
│
├── utils/                         # Development tools
│   ├── deploy.py                  # Automated deployment
│   ├── test_desktop.py            # Desktop simulation
│   └── [other utilities]
│
├── README.md                      # Project overview
├── LICENSE                        # MIT license
└── ATTRIBUTION.md                 # Third-party credits
```

**Total Lines of Code**:
- Firmware: ~4,500 lines (src/)
- Documentation: ~20,000 lines (docs/)
- Tests: ~500 lines (utils/)

---

## Quick Reference Tables

### Command Summary

| Command | Args | Response Time | Use Case |
|---------|------|---------------|----------|
| INFO | none | 100-1100ms | Device status |
| BATTERY | none | 100-1100ms | Battery check |
| PING | none | <50ms | Latency test |
| PROFILE_LIST | none | <50ms | List profiles |
| PROFILE_LOAD | id (1-3) | 50-250ms | Load preset |
| PROFILE_GET | none | <50ms | View settings |
| PROFILE_CUSTOM | key:val... | 50-250ms | Custom profile |
| SESSION_START | none | 100-500ms | Start therapy |
| SESSION_PAUSE | none | <50ms | Pause therapy |
| SESSION_RESUME | none | <50ms | Resume therapy |
| SESSION_STOP | none | <50ms | Stop therapy |
| SESSION_STATUS | none | <50ms | Progress check |
| PARAM_SET | key, value | 50-250ms | Single parameter |
| CALIBRATE_START | none | <50ms | Enter calibration |
| CALIBRATE_BUZZ | finger, intensity, duration | 50-2050ms | Test motor |
| CALIBRATE_STOP | none | <50ms | Exit calibration |
| HELP | none | <50ms | List commands |
| RESTART | none | 500ms + reboot | Reboot device |

### Error Code Reference

| Error | Typical Cause | Fix |
|-------|--------------|-----|
| Unknown command | Typo or unsupported command | Check HELP |
| Invalid profile ID | ID not 1-3 | Use PROFILE_LIST |
| VR not connected | VR glove offline | Connect VR first |
| Battery too low | Voltage <3.3V | Charge battery |
| No active session | Session not started | Send SESSION_START |
| Cannot modify during active session | Profile change blocked | Stop session first |
| Invalid parameter name | Unknown key | Check PROFILE_GET |
| Value out of range | Parameter exceeds limits | See parameter ranges |
| Not in calibration mode | Calibration not active | Send CALIBRATE_START |
| Invalid finger index | Finger not 0-7 | Valid range: 0-7 |

### Voltage Thresholds

| Voltage | Status | LED Color | Action |
|---------|--------|-----------|--------|
| >3.6V | Good | Green | Full operation |
| 3.3-3.6V | Medium | Orange | Warning |
| <3.3V | Critical | Red | Therapy blocked |
| <3.0V | Dead | Off | Charge immediately |

### Finger Index Map

| Index | Glove | Finger | Motor Channel |
|-------|-------|--------|---------------|
| 0 | VL | Thumb | 0 |
| 1 | VL | Index | 1 |
| 2 | VL | Middle | 2 |
| 3 | VL | Ring | 3 |
| 4 | VR | Thumb | 0 |
| 5 | VR | Index | 1 |
| 6 | VR | Middle | 2 |
| 7 | VR | Ring | 3 |

---

## Testing & Validation

### Hardware Integration Testing

BlueBuzzah v2 firmware is validated using **manual hardware integration tests** on actual Feather nRF52840 devices.

**Test Infrastructure:**
- Manual BLE connection setup required
- Tests run on physical hardware (no mocks/simulators)
- Python-based test scripts send BLE commands
- Response validation via UART service

**Test Coverage:**

| Category | Tested | Total | Coverage |
|----------|--------|-------|----------|
| BLE Commands | 8 | 18 | 44% |
| Functional | ~15-20% | 100% | ~15-20% |

**Available Tests:**

1. **calibrate_commands_test.py**
   - Tests: CALIBRATE_START, CALIBRATE_BUZZ, CALIBRATE_STOP
   - Validates: 8 motors at multiple intensity levels
   - Duration: 5-10 minutes

2. **session_commands_test.py**
   - Tests: SESSION_START, SESSION_PAUSE, SESSION_RESUME, SESSION_STOP, SESSION_STATUS
   - Validates: State transitions and session management
   - Duration: 3-5 minutes

3. **memory_stress_test.py**
   - Tests: Extended operation memory stability
   - Validates: No memory leaks, >10KB free minimum
   - Duration: 2+ hours

4. **sync_accuracy_test.py**
   - Tests: EXECUTE_BUZZ/BUZZ_COMPLETE latency
   - Validates: 7.5-20ms target latency
   - Duration: 2-5 minutes

**Testing Requirements:**
- Feather nRF52840 with BlueBuzzah v2 firmware
- BLE 4.0+ capable host computer
- Python with BLE libraries (adafruit_ble or bleak)
- Charged battery or USB power

**See:** [Testing Guide](TESTING.md) for complete test procedures and execution instructions.

---

**Document Maintenance:**
Update this document when:
- Changing hardware specifications
- Modifying parameter ranges
- Adding new configuration options
- Updating pin assignments
- Changing memory budget
- Updating test coverage statistics
