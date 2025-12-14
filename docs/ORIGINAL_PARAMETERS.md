# Original V1 Firmware Parameters Reference

This document provides a complete reference of the original CircuitPython firmware behavior, pattern implementation, and timing parameters. Use this as the source of truth when aligning new C++ firmware behavior.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Pattern Types](#pattern-types)
3. [Configuration Presets](#configuration-presets)
4. [Timing Parameters](#timing-parameters)
5. [Motor Control Parameters](#motor-control-parameters)
6. [Pattern Sequencing Logic](#pattern-sequencing-logic)
7. [Randomization Behavior](#randomization-behavior)
8. [Synchronization Protocol](#synchronization-protocol)
9. [Parameter Comparison Table](#parameter-comparison-table)

---

## Architecture Overview

The original v1 firmware was written in CircuitPython for Adafruit Feather nRF52840 boards. It uses a master/slave architecture:

- **VL (Left Glove)**: Master role - initiates therapy, sends sync commands
- **VR (Right Glove)**: Slave role - receives sync, applies timing corrections

### Source Files

| File | Location | Purpose |
|------|----------|---------|
| `main_program_VCR.py` | VL, VR | Therapy engine, pattern generation |
| `code_master.py` | VL | BLE setup, command menu, sync initiation |
| `code_slave.py` | VR | BLE connection, sync reception |
| `defaults.py` | VL, VR | Runtime configuration (modified dynamically) |
| `defaults_*.py` | VL | Preset configurations |
| `menu_controller.py` | VL | BLE command interface |

---

## Pattern Types

### RNDP (Random Permutation)

The **only** pattern type implemented. Uses factorial permutations of 4 fingers.

```python
L_RNDP = list(permutations(range(0, 4)))   # 24 patterns for LEFT (fingers 0-3)
R_RNDP = list(permutations(range(4, 8)))   # 24 patterns for RIGHT (fingers 4-7)
```

**Permutation Count**: 4! = 24 unique sequences per hand

**Example Sequences**:
- `(0, 1, 2, 3)` → Index, Middle, Ring, Pinky
- `(3, 2, 1, 0)` → Pinky, Ring, Middle, Index
- `(1, 3, 0, 2)` → Middle, Pinky, Index, Ring

### Finger Mapping

| Index | VL (Left) | VR (Right) |
|-------|-----------|------------|
| 0 / 4 | Index | Index |
| 1 / 5 | Middle | Middle |
| 2 / 6 | Ring | Ring |
| 3 / 7 | Pinky | Pinky |

---

## Configuration Presets

Four VCR (Vibration Coordinated Reset) presets are defined:

### 1. Regular VCR (`defaults_RegVCR.py`)

Standard therapy mode with fixed timing, no variation.

```python
PATTERN_TYPE = "RNDP"
TIME_ON = 0.100        # 100 ms
TIME_OFF = 0.067       # 67 ms
JITTER = 0             # No timing variation
MIRROR = False         # Independent L/R patterns
RANDOM_FREQ = False    # Fixed 250 Hz
AMPLITUDE_MIN = 100
AMPLITUDE_MAX = 100    # Fixed 100% amplitude
```

### 2. Noisy VCR (`defaults_NoisyVCR.py`)

Adds timing jitter and mirrors patterns between hands.

```python
PATTERN_TYPE = "RNDP"
TIME_ON = 0.100        # 100 ms
TIME_OFF = 0.067       # 67 ms
JITTER = 23.5          # ±19.6 ms jitter
MIRROR = True          # L/R patterns synchronized
RANDOM_FREQ = False    # Fixed 250 Hz
AMPLITUDE_MIN = 100
AMPLITUDE_MAX = 100    # Fixed 100% amplitude
```

### 3. Hybrid VCR (`defaults_HybridVCR.py`)

Jitter enabled but patterns not mirrored.

```python
PATTERN_TYPE = "RNDP"
TIME_ON = 0.100        # 100 ms
TIME_OFF = 0.067       # 67 ms
JITTER = 23.5          # ±19.6 ms jitter
MIRROR = False         # Independent L/R patterns
RANDOM_FREQ = False    # Fixed 250 Hz
AMPLITUDE_MIN = 100
AMPLITUDE_MAX = 100    # Fixed 100% amplitude
```

### 4. Custom VCR (`defaults_CustomVCR.py`)

Full variation: jitter, random frequency, variable amplitude.

```python
PATTERN_TYPE = "RNDP"
TIME_ON = 0.100        # 100 ms
TIME_OFF = 0.067       # 67 ms
JITTER = 23.5          # ±19.6 ms jitter
MIRROR = False         # Independent L/R patterns
RANDOM_FREQ = True     # 210-260 Hz range
ACTUATOR_FREQL = 210   # Minimum frequency
ACTUATOR_FREQH = 260   # Maximum frequency
AMPLITUDE_MIN = 70     # Variable 70-100%
AMPLITUDE_MAX = 100
```

---

## Timing Parameters

### Core Timing Values

| Parameter | Value | Description |
|-----------|-------|-------------|
| `TIME_ON` | **100 ms** | Vibration duration per finger |
| `TIME_OFF` | **67 ms** | Pause between finger activations |
| `TIME_RELAX` | **668 ms** | `4 × (TIME_ON + TIME_OFF)` - Relaxation between pattern sets |
| `TIME_SESSION` | **120 min** | Total therapy session duration |
| `STARTUP_WINDOW` | **6 s** | BLE command window before therapy starts |

### Jitter Calculation

```python
TIME_JITTER = (TIME_ON + TIME_OFF) * (JITTER / 100) / 2
```

| Preset | JITTER | TIME_JITTER |
|--------|--------|-------------|
| Regular | 0% | 0 ms |
| Noisy | 23.5% | ±19.6 ms |
| Hybrid | 23.5% | ±19.6 ms |
| Custom | 23.5% | ±19.6 ms |

**Application**: Jitter is applied to `TIME_OFF` only, not `TIME_ON`:

```python
time.sleep(TIME_OFF + random.uniform(-TIME_JITTER, +TIME_JITTER))
```

### Macrocycle Structure

One macrocycle consists of:

```
┌─────────────────────────────────────────────────────────────┐
│ Pattern 1 (4 fingers) │ Pattern 2 (4 fingers) │ Pattern 3 (4 fingers) │ Relax × 2 │
└─────────────────────────────────────────────────────────────┘
```

**Timing Breakdown**:

| Component | Duration | Calculation |
|-----------|----------|-------------|
| Single finger activation | 167 ms | TIME_ON + TIME_OFF |
| One pattern (4 fingers) | 668 ms | 4 × 167 ms |
| Three patterns | 2004 ms | 3 × 668 ms |
| Double relaxation | 1336 ms | 2 × TIME_RELAX |
| **Total macrocycle** | **~3340 ms** | 2004 + 1336 ms |

**Session Statistics**:
- Macrocycles per 120-min session: ~2155
- Total finger activations: ~25,860 per hand

---

## Motor Control Parameters

### DRV2605 Configuration

| Parameter | Value | Register | Notes |
|-----------|-------|----------|-------|
| Actuator Type | LRA | Mode select | Linear Resonant Actuator |
| Peak Voltage | 2.50 V | 0x17 | `int(2.50 / 0.02122)` ≈ 118 |
| Mode | Real-time Playback | - | `MODE_REALTIME` |
| Open Loop | Enabled | 0x1D | `control3 \| 0x21` |

### Frequency Control

**Fixed Frequency** (Regular, Noisy, Hybrid):
```python
ACTUATOR_FREQUENCY = 250  # Hz
# Register 0x20: int(1 / (250 * 0.00009849)) = 40
```

**Random Frequency** (Custom only):
```python
ACTUATOR_FREQUENCY = random.randrange(210, 260, 5)  # 210, 215, 220, ..., 255, 260 Hz
# 11 possible values in 5 Hz increments
```

### Amplitude Control

| Preset | Min | Max | Behavior |
|--------|-----|-----|----------|
| Regular | 100 | 100 | Fixed at 100% |
| Noisy | 100 | 100 | Fixed at 100% |
| Hybrid | 100 | 100 | Fixed at 100% |
| Custom | 70 | 100 | Random per finger |

**Application**:
```python
drivers[finger].realtime_value = random.randint(AMPLITUDE_MIN, AMPLITUDE_MAX)
```

---

## Pattern Sequencing Logic

### Buzz Sequence Function

```python
def buzz_sequence(sequence):
    """Execute one 4-finger pattern sequence."""
    for finger in sequence:
        fingers_on(finger)
        time.sleep(TIME_ON)           # 100 ms vibration
        fingers_off(finger)
        time.sleep(
            TIME_OFF                   # 67 ms base
            + random.uniform(-TIME_JITTER, +TIME_JITTER)  # Optional jitter
        )
```

### Macrocycle Execution

```python
def run_macrocycle():
    """Execute one complete macrocycle (3 patterns + 2 relaxation periods)."""

    # Execute 3 random permutation patterns
    buzz_sequence(rndp_sequence())    # Pattern 1
    buzz_sequence(rndp_sequence())    # Pattern 2
    buzz_sequence(rndp_sequence())    # Pattern 3

    # Double relaxation period
    time.sleep(TIME_RELAX)            # 668 ms
    time.sleep(TIME_RELAX)            # 668 ms
```

### Pattern Selection

```python
def rndp_sequence():
    """Select a random permutation pattern."""
    if MIRROR:
        # Synchronized: left pattern determines right
        pattern = random.choice(L_RNDP)
        return pattern  # Same for both gloves
    else:
        # Independent: each glove picks randomly
        return random.choice(L_RNDP)  # VL uses L_RNDP
        # return random.choice(R_RNDP)  # VR uses R_RNDP
```

---

## Randomization Behavior

### Shared Seed Synchronization

Both gloves must produce identical random sequences. This is achieved via shared seed:

```python
# VL (Master) - Generate and share seed
if JITTER != 0:
    SHARED_SEED = int(time.monotonic() * 1000) % 1000000
    random.seed(SHARED_SEED)
    uart_service.write(f"SEED:{SHARED_SEED}")

# VR (Slave) - Receive and apply seed
if response.startswith("SEED:"):
    SHARED_SEED = int(response.split(":")[1])
    random.seed(SHARED_SEED)
```

### Randomization Elements

| Element | Application | Preset |
|---------|-------------|--------|
| Pattern selection | `random.choice(RNDP)` | All |
| Jitter offset | `random.uniform(-J, +J)` | Noisy, Hybrid, Custom |
| Amplitude | `random.randint(70, 100)` | Custom only |
| Frequency | `random.randrange(210, 260, 5)` | Custom only |

### Random Function Calls Per Macrocycle

| Function | Count | Total/Session |
|----------|-------|---------------|
| Pattern selection | 3 | ~6,465 |
| Jitter offset | 12 (4×3) | ~25,860 |
| Amplitude (Custom) | 12 | ~25,860 |
| Frequency (Custom) | 1 | ~2,155 |

---

## Synchronization Protocol

### Initial Handshake

```
┌──────────────────┐                    ┌──────────────────┐
│   VL (Master)    │                    │   VR (Slave)     │
└────────┬─────────┘                    └────────┬─────────┘
         │                                       │
         │──── FIRST_SYNC:{timestamp} ──────────>│
         │                                       │
         │<──────────── ACK ─────────────────────│
         │                                       │
         │──── VCR_START ───────────────────────>│
         │                                       │
         ▼                                       ▼
    Start Therapy                          Start Therapy
```

### Per-Macrocycle Synchronization

```
┌──────────────────┐                    ┌──────────────────┐
│   VL (Master)    │                    │   VR (Slave)     │
└────────┬─────────┘                    └────────┬─────────┘
         │                                       │
         │──── SYNC_ADJ:{timestamp} ────────────>│
         │                                       │ (Calculate offset)
         │<──── ACK_SYNC_ADJ ────────────────────│
         │                                       │
         │──── SYNC_ADJ_START ──────────────────>│
         │                                       │
         ▼                                       ▼
    Execute Macrocycle                   Execute Macrocycle
         │                                       │
    SYNC_LED Flash                        SYNC_LED Flash
```

### Timing Compensation

VR applies a fixed compensation for BLE latency:

```python
# VR (Slave) - Apply 21 ms compensation
adjusted_sync_time = received_timestamp + 21  # BLE overhead compensation

# Offset calculation with smoothing (ALPHA = 1.0 = no smoothing)
new_offset = sync_adj_timestamp - received_timestamp
timebase_adjustment = (ALPHA * new_offset) + ((1 - ALPHA) * timebase_adjustment)
```

### LED Sync Flash

Both gloves flash at end of each macrocycle:

```python
def sync_led_flash():
    """Deep blue flash indicating sync point."""
    neopixel[0] = (0, 0, 128)  # Deep blue
    time.sleep(0.0125)          # 12.5 ms ON
    neopixel[0] = (0, 0, 0)    # OFF
    time.sleep(0.0125)          # 12.5 ms OFF
```

---

## Parameter Comparison Table

Complete reference for all presets:

| Parameter | Regular | Noisy | Hybrid | Custom | Units |
|-----------|---------|-------|--------|--------|-------|
| **Timing** |
| TIME_ON | 100 | 100 | 100 | 100 | ms |
| TIME_OFF | 67 | 67 | 67 | 67 | ms |
| TIME_RELAX | 668 | 668 | 668 | 668 | ms |
| TIME_SESSION | 120 | 120 | 120 | 120 | min |
| JITTER | 0 | 23.5 | 23.5 | 23.5 | % |
| TIME_JITTER | 0 | ±19.6 | ±19.6 | ±19.6 | ms |
| **Motor** |
| ACTUATOR_TYPE | LRA | LRA | LRA | LRA | - |
| ACTUATOR_VOLTAGE | 2.50 | 2.50 | 2.50 | 2.50 | V |
| ACTUATOR_FREQUENCY | 250 | 250 | 250 | 210-260 | Hz |
| AMPLITUDE_MIN | 100 | 100 | 100 | 70 | % |
| AMPLITUDE_MAX | 100 | 100 | 100 | 100 | % |
| **Pattern** |
| PATTERN_TYPE | RNDP | RNDP | RNDP | RNDP | - |
| MIRROR | false | true | false | false | - |
| RANDOM_FREQ | false | false | false | true | - |
| **Macrocycle** |
| Patterns per cycle | 3 | 3 | 3 | 3 | - |
| Fingers per pattern | 4 | 4 | 4 | 4 | - |
| Relaxation periods | 2 | 2 | 2 | 2 | - |
| Total duration | ~3340 | ~3340 | ~3340 | ~3340 | ms |

---

## Implementation Notes for New Firmware

### Critical Timing Accuracy

1. **TIME_ON must be exactly 100 ms** - Vibration duration
2. **TIME_OFF must be exactly 67 ms** - Base pause (before jitter)
3. **Jitter range is ±19.6 ms** when JITTER = 23.5%
4. **Double relaxation** - Two consecutive `TIME_RELAX` periods (1336 ms total)

### Pattern Sequencing

1. Execute **3 patterns per macrocycle**, not 1 or 2
2. Each pattern is a **complete 4-finger sequence**
3. Randomization uses **permutation selection**, not random finger picking
4. Mirroring means **both gloves execute same pattern sequence**

### Motor Control

1. Use **real-time playback mode** for amplitude control
2. **Open-loop mode** must be enabled for LRA
3. Frequency set via register 0x20, not waveform library
4. Amplitude range 0-127 maps to `realtime_value`

### Synchronization

1. Master sends `SYNC_ADJ` **every macrocycle**
2. Slave applies **21 ms BLE latency compensation**
3. Both gloves must use **shared random seed** for identical patterns
4. LED flash indicates successful sync point

---

## Version History

| Version | Date | Description |
|---------|------|-------------|
| 1.0 | Original | CircuitPython implementation in `temp/v1/` |

---

*This document extracted from analysis of original CircuitPython firmware in `/temp/v1/VL/` and `/temp/v1/VR/` directories.*
