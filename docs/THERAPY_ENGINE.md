# BlueBuzzah Therapy Engine
**Version:** 1.0.0
**Date:** 2025-01-23
**Therapy Protocol:** Vibrotactile Coordinated Reset (vCR)

---

## Table of Contents

1. [Therapy Overview](#therapy-overview)
2. [VCR Engine Architecture](#vcr-engine-architecture)
3. [Pattern Generation](#pattern-generation)
4. [Haptic Control](#haptic-control)
5. [Session Execution](#session-execution)
6. [Therapy Profiles](#therapy-profiles)
7. [DRV2605 Motor Control](#drv2605-motor-control)
8. [Performance Optimization](#performance-optimization)

---

## Terminology Note

This document uses the following device role terminology:
- **PRIMARY** (also known as VL, left glove): Generates patterns, controls therapy timing
- **SECONDARY** (also known as VR, right glove): Receives synchronization commands

Code examples use `role = "PRIMARY"` or `"SECONDARY"` for device role parameters.

---

## Therapy Overview

### Vibrotactile Coordinated Reset (vCR)

**Clinical Foundation**: [PMC8055937](https://pmc.ncbi.nlm.nih.gov/articles/PMC8055937/)

**Mechanism**: Desynchronization of pathological neural oscillations in Parkinson's disease through bilateral vibrotactile stimulation.

**Key Parameters**:
- **Pattern**: Random permutations (RNDP) of 4 finger activations
- **Timing**: 100ms ON, 67ms OFF per finger (configurable)
- **Bilaterality**: Synchronized left/right glove stimulation
- **Jitter**: Temporal randomization to prevent habituation (0-50%)
- **Mirror Mode**: Symmetric vs independent L/R patterns
- **Session Duration**: 1-180 minutes (default: 120 minutes)

**Therapeutic Goal**: Reduce motor symptoms through coordinated reset stimulation

---

## VCR Engine Architecture

### Module Structure

**File**: `src/modules/vcr_engine.py` (491 lines)

**Two Main Functions**:

1. **`run_vcr(uart, role, config)`** - Legacy standalone therapy execution
2. **`run_vcr_session(uart, role, config, session_manager, callback)`** - Session-aware with command polling

### Function Signatures

```python
def run_vcr(uart_service, role, config):
    """
    Execute VCR therapy session (legacy standalone mode).

    Args:
        uart_service: BLE UART for VL↔VR synchronization
        role: "PRIMARY" or "SECONDARY"
        config: Therapy configuration module (e.g., profiles.defaults)

    Returns:
        None (runs until TIME_END reached)
    """

def run_vcr_session(uart_service, role, config, session_manager, command_handler_callback=None):
    """
    Execute VCR therapy with session state management.

    Args:
        uart_service: BLE UART for synchronization
        role: "PRIMARY" or "SECONDARY"
        config: Therapy configuration (dict or module)
        session_manager: SessionManager instance
        command_handler_callback: Optional callback for BLE command processing

    Returns:
        str: "COMPLETED" | "STOPPED" | "ERROR"
    """
```

### Execution Flow

```mermaid
flowchart TD
    A[Start] --> B[Display Battery Status]
    B --> C[Initialize Pattern Lists]
    C --> D{Jitter Enabled?}
    D -->|Yes| E[PRIMARY: Generate Seed]
    D -->|No| F[Use Default Seed]
    E --> G[Broadcast Seed to VR]
    G --> H[VR: Wait for Seed]
    H --> I[Both: random.seed(SHARED_SEED)]
    F --> I
    I --> J[Initialize Haptic Controller]
    J --> K[Flash Blue LED 5x]
    K --> L[Main Therapy Loop]
    L --> M{Session Active?}
    M -->|No| N[Session Complete]
    M -->|Yes| O{Paused?}
    O -->|Yes| P[Motors Off, Flash Yellow]
    O -->|No| Q{Random Freq?}
    Q -->|Yes| R[Reconfigure Drivers]
    Q -->|No| S[Execute 3 Buzz Sequences]
    R --> S
    S --> T[Relax Breaks x2]
    T --> U[Garbage Collection]
    U --> V{Sync LED?}
    V -->|Yes| W[Flash Dark Blue]
    V -->|No| L
    W --> L
    P --> M
    N --> X[Display Final Battery]
    X --> Y[All Motors Off]
    Y --> Z[End]
```

---

## Pattern Generation

### Permutation Strategy (RNDP)

**Algorithm** (vcr_engine.py:78-131):

```python
# 1. Pre-generate all permutations (one-time allocation)
from adafruit_itertools import permutations

L_RNDP = list(permutations(range(0, 4)))  # 24 patterns for LEFT (fingers 0-3)
R_RNDP = list(permutations(range(4, 8)))  # 24 patterns for RIGHT (fingers 4-7)

# 2. Seed random number generator (synchronized VL↔VR)
SHARED_SEED = int(time.monotonic() * 1000) % 1000000  # PRIMARY generates
random.seed(SHARED_SEED)  # Both gloves apply same seed

# 3. Generate buzz sequence for each macrocycle
def rndp_sequence():
    """Generate random permutation sequence"""
    pattern_left_rndp = random.choice(L_RNDP)   # e.g., (2, 0, 3, 1)

    if config.MIRROR:
        # Symmetric bilateral pattern: RIGHT mirrors LEFT (same indices 0-4)
        pattern_right_rndp = pattern_left_rndp.copy()
        # LEFT: (2, 0, 3, 1) → RIGHT: (2, 0, 3, 1) - identical sequence
    else:
        # Independent bilateral pattern: RIGHT randomly chosen
        pattern_right_rndp = random.choice(R_RNDP)  # e.g., (5, 7, 4, 6)

    # 4. Zip left and right patterns for simultaneous execution
    return zip(pattern_left_rndp, pattern_right_rndp)
    # Returns: [(2, 5), (0, 7), (3, 4), (1, 6)] for 4 fingers
```

**Pattern Space**:
- LEFT permutations: 4! = 24 patterns
- RIGHT permutations: 4! = 24 patterns (independent when non-mirrored)
- Total combinations (mirrored): 24 patterns (L determines R)
- Total combinations (non-mirrored): 24 × 24 = 576 unique patterns

**Bilateral Mirroring** (src/therapy.py:160-169):

Mirroring is controlled by the `mirror_pattern` parameter based on vCR type:

| vCR Type | `mirror_pattern` | Behavior | Rationale |
|----------|------------------|----------|-----------|
| Noisy vCR | `True` | Same finger on both hands | Avoids bilateral masking interference |
| Regular vCR | `False` | Independent sequences per hand | Increases spatial randomization for synaptic decoupling |

```python
# Generate left hand sequence (random permutation)
left_sequence = list(range(num_fingers))
_shuffle_in_place(left_sequence)

# Generate right hand sequence based on mirror setting
if mirror_pattern:
    # Mirrored: same finger on both hands (for noisy vCR)
    right_sequence = left_sequence.copy()
else:
    # Non-mirrored: independent random sequence (for regular vCR)
    right_sequence = list(range(num_fingers))
    _shuffle_in_place(right_sequence)
```

Both hands use finger indices 0-3, with the hardware wiring ensuring each channel maps to the same anatomical finger on both gloves (channel 0 = pinky on both, etc.).

### Seed Synchronization

**Why Needed?** Both gloves must generate identical random patterns for bilateral symmetry.

**PRIMARY Sequence** (vcr_engine.py:82-102):

```python
if config.JITTER != 0 and role == "PRIMARY":
    # 1. Generate seed from current time
    SHARED_SEED = int(time.monotonic() * 1000) % 1000000  # 0-999999

    # 2. Apply locally
    random.seed(SHARED_SEED)

    # 3. Broadcast to VR
    uart_service.write(f"SEED:{SHARED_SEED}\n")

    # 4. Wait for acknowledgment (2 second timeout)
    seed_ack_received = False
    ack_start = time.monotonic()
    while not seed_ack_received and (time.monotonic() - ack_start) < 2.0:
        if uart_service.in_waiting:
            response = uart_service.readline().decode().strip()
            if response == "SEED_ACK":
                seed_ack_received = True
        time.sleep(0.01)

    if not seed_ack_received:
        print(f"[VL] [WARNING] No SEED_ACK - patterns may desync!")
```

**SECONDARY Sequence** (vcr_engine.py:103-122):

```python
elif config.JITTER != 0 and role == "SECONDARY":
    # 1. Wait for seed from PRIMARY (5 second timeout)
    seed_received = False
    seed_start = time.monotonic()
    while not seed_received and (time.monotonic() - seed_start) < 5.0:
        if uart_service.in_waiting:
            message = uart_service.readline().decode().strip()
            if message.startswith("SEED:"):
                # 2. Extract and apply seed
                SHARED_SEED = int(message.split(":")[1])
                random.seed(SHARED_SEED)

                # 3. Send acknowledgment
                uart_service.write("SEED_ACK\n")
                seed_received = True
        time.sleep(0.01)

    if not seed_received:
        print(f"[VR] [WARNING] No seed received! Using default {SHARED_SEED}")
        random.seed(123456)  # Fallback to default
```

**Why Only When JITTER != 0?**
- Without jitter: Pattern randomness from permutations only (deterministic per macrocycle)
- With jitter: TIME_OFF varies randomly, requiring synchronized RNG state

### Finger Mapping

**Physical Layout**:
```
LEFT GLOVE (VL):          RIGHT GLOVE (VR):
0: Thumb                  4: Thumb
1: Index                  5: Index
2: Middle                 6: Middle
3: Ring                   7: Ring
```

**Pattern Example** (non-mirrored):
```python
# Macrocycle 1
pattern = [(2, 5), (0, 7), (3, 4), (1, 6)]

# Execution sequence:
# Buzz 1: LEFT Middle  (2) + RIGHT Index  (5)
# Buzz 2: LEFT Thumb   (0) + RIGHT Ring   (7)
# Buzz 3: LEFT Ring    (3) + RIGHT Thumb  (4)
# Buzz 4: LEFT Index   (1) + RIGHT Middle (6)
```

---

## Haptic Control

### HapticController Class

**File**: `src/modules/haptic_controller.py` (306 lines)

**Initialization** (lines 54-182):

```python
class HapticController:
    def __init__(self, config):
        self.config = config
        self.drivers = []  # List of 4 DRV2605 instances
        self.i2c = None
        self.mux = None

        # Initialize I2C with aggressive retry logic
        self._setup_i2c_and_drivers()
```

**I2C Hardware Setup** (lines 69-181):

```python
def _setup_i2c_and_drivers(self):
    # 1. Initialize I2C bus (3 retries at 100kHz)
    for attempt in range(3):
        try:
            self.i2c = board.I2C()
            break
        except RuntimeError:
            time.sleep(0.5)

    # 2. Fallback: Manual init with frequency stepping (100kHz → 50kHz → 10kHz)
    if not initialized:
        frequencies = [100000, 50000, 10000]
        for freq in frequencies:
            try:
                self.i2c = busio.I2C(board.SCL, board.SDA, frequency=freq)
                if self.i2c.try_lock():
                    self.i2c.unlock()
                    break
            except RuntimeError:
                continue

    # 3. Initialize TCA9548A multiplexer
    self.mux = adafruit_tca9548a.PCA9546A(self.i2c)

    # 4. Scan I2C buses (diagnostic)
    addresses = self.i2c.scan()
    for channel in range(4):
        addresses = self.mux[channel].scan()

    # 5. Initialize DRV2605 drivers on each mux port
    for port in self.mux:
        try:
            driver = adafruit_drv2605.DRV2605(port)
            self.drivers.append(driver)
        except ValueError:
            self.drivers.append(False)  # No driver on this port

    # 6. Configure each driver
    for driver in self.drivers:
        if driver:
            self._configure_driver(driver, self.config)
```

### Motor Control Methods

**Activate Fingers** (lines 257-271):

```python
def fingers_on(self, fingers):
    """
    Activate specified fingers with random amplitude.

    Args:
        fingers: Iterable of finger indices or (finger, ...) tuples
    """
    for finger in fingers:
        actual_finger = finger[0] if isinstance(finger, tuple) else finger

        if 0 <= actual_finger < len(self.drivers) and self.drivers[actual_finger]:
            # Random amplitude within configured range
            amplitude = random.randint(
                self.config.AMPLITUDE_MIN,
                self.config.AMPLITUDE_MAX
            )
            self.drivers[actual_finger].realtime_value = amplitude
```

**Deactivate Fingers** (lines 273-283):

```python
def fingers_off(self, fingers):
    """Deactivate specified fingers."""
    for finger in fingers:
        actual_finger = finger[0] if isinstance(finger, tuple) else finger

        if 0 <= actual_finger < len(self.drivers) and self.drivers[actual_finger]:
            self.drivers[actual_finger].realtime_value = 0
```

**Buzz Sequence** (lines 285-299):

```python
def buzz_sequence(self, sequence):
    """
    Execute a buzz sequence (pattern of finger activations).

    Args:
        sequence: Iterable of finger combinations
                  e.g., [(2, 5), (0, 7), (3, 4), (1, 6)]
    """
    for fingers in sequence:
        # 1. Activate fingers
        self.fingers_on(fingers)

        # 2. Hold for TIME_ON
        time.sleep(self.config.TIME_ON)

        # 3. Deactivate fingers
        self.fingers_off(fingers)

        # 4. Wait TIME_OFF + jitter
        time.sleep(
            self.config.TIME_OFF +
            random.uniform(-self.config.TIME_JITTER, self.config.TIME_JITTER)
        )
```

**Emergency Stop** (lines 301-305):

```python
def all_off(self):
    """Turn off all haptic motors"""
    for driver in self.drivers:
        if driver:
            driver.realtime_value = 0
```

### Random Frequency Reconfiguration

**Feature**: Optionally randomize actuator frequency each macrocycle

**Method** (lines 211-255):

```python
def reconfigure_for_random_frequency(self, config):
    """
    Reconfigure drivers with randomized frequency (if enabled).
    """
    if not config.RANDOM_FREQ:
        return

    # Reinitialize drivers
    self.drivers = []
    for port in self.mux:
        try:
            self.drivers.append(adafruit_drv2605.DRV2605(port))
        except ValueError:
            self.drivers.append(False)

    # Configure each driver with random frequency
    for driver in self.drivers:
        if driver:
            # Set actuator type
            if config.ACTUATOR_TYPE == "LRA":
                driver.use_LRA()
            else:
                driver.use_ERM()

            # Set open-loop mode
            control3 = driver._read_u8(0x1D)
            driver._write_u8(0x1D, control3 | 0x21)

            # Set peak voltage
            driver._write_u8(0x17, int(config.ACTUATOR_VOLTAGE / 0.02122))

            # Set driving frequency (randomized)
            freq = random.randrange(
                config.ACTUATOR_FREQL,   # Low bound (e.g., 210 Hz)
                config.ACTUATOR_FREQH,   # High bound (e.g., 260 Hz)
                5                         # Step size
            )
            driver._write_u8(0x20, int(1 / (freq * 0.00009849)))

            # Activate RTP mode
            driver.realtime_value = 0
            driver.mode = adafruit_drv2605.MODE_REALTIME
```

**Usage** (vcr_engine.py:167-168):

```python
if config.RANDOM_FREQ:
    haptic.reconfigure_for_random_frequency(config)
```

**Performance Note**: Reinitializing drivers is expensive (~100-200ms). Only use when therapeutic benefit outweighs timing cost.

---

## Session Execution

### Main Therapy Loop

**Legacy Mode** (vcr_engine.py:164-251):

```python
# Main therapy loop - COMMAND-BASED SYNCHRONIZATION
while time.time() < session_end_time:

    # Reconfigure for random frequency if enabled
    if config.RANDOM_FREQ:
        haptic.reconfigure_for_random_frequency(config)

    buzz_cycle_count += 1

    if buzz_cycle_count == 1:
        # Indicate start of FIRST buzz cycle with white LED
        pixels.fill((255, 255, 255))
        time.sleep(0.1)
        pixels.fill((0, 0, 0))

    # Optional: Periodic sync for monitoring (every 10 cycles)
    if role == "PRIMARY" and buzz_cycle_count % 10 == 0:
        send_periodic_sync(uart_service, buzz_cycle_count)

    # Execute 3 buzz sequences with COMMAND-BASED SYNCHRONIZATION
    for sequence_idx in range(3):

        if role == "PRIMARY":
            # PRIMARY: Send EXECUTE_BUZZ command
            send_execute_buzz(uart_service, sequence_idx)

            # PRIMARY executes its own buzz sequence
            haptic.buzz_sequence(rndp_sequence())

            # PRIMARY: Wait for SECONDARY's BUZZ_COMPLETE acknowledgment
            ack_received = receive_buzz_complete(uart_service, sequence_idx, timeout_sec=3.0)
            if not ack_received:
                print(f"[VL] [WARNING] No BUZZ_COMPLETE from VR for sequence {sequence_idx}")

        else:  # SECONDARY
            # SECONDARY: Wait for EXECUTE_BUZZ command (BLOCKING)
            received_idx = receive_execute_buzz(uart_service, timeout_sec=10.0)

            if received_idx is None:
                # Timeout - PRIMARY disconnected
                print(f"[VR] [ERROR] EXECUTE_BUZZ timeout! PRIMARY disconnected.")
                haptic.all_off()
                pixels.fill((255, 0, 0))
                # Infinite loop to prevent restart
                while True:
                    pixels.fill((255, 0, 0))
                    time.sleep(0.5)
                    pixels.fill((0, 0, 0))
                    time.sleep(0.5)

            if received_idx != sequence_idx:
                print(f"[VR] [WARNING] Sequence mismatch: expected {sequence_idx}, got {received_idx}")

            # SECONDARY executes buzz sequence
            haptic.buzz_sequence(rndp_sequence())

            # SECONDARY: Send BUZZ_COMPLETE acknowledgment
            send_buzz_complete(uart_service, sequence_idx)

    # After all 3 sequences, take relax breaks
    time.sleep(config.TIME_RELAX)
    time.sleep(config.TIME_RELAX)

    # Garbage collection
    gc.collect()

    # Optional SYNC_LED flash at end of buzz macrocycle
    if config.SYNC_LED:
        pixels.fill((0, 0, 128))  # Dark blue flash
        time.sleep(0.0125)
        pixels.fill((0, 0, 0))
        time.sleep(0.0125)

# Session complete
display_battery_status(DEVICE_TAG)
haptic.all_off()
```

### Session-Aware Mode

**Enhancements** (vcr_engine.py:254-490):

1. **Interruptible Execution**: Check session state every cycle
2. **Pause Support**: Motors off, flash yellow LED
3. **Command Polling**: Process BLE commands during therapy
4. **Progress Tracking**: SessionManager integration
5. **Graceful Termination**: Return status code

**Key Differences**:

```python
# 1. Loop condition: Session manager instead of time
while session_manager.is_active():

    # 2. Pause handling
    if session_manager.is_paused():
        haptic.all_off()
        pixels.fill((255, 255, 0))  # Yellow
        time.sleep(0.1)
        pixels.fill((0, 0, 0))

        # Poll for commands during pause
        if command_handler_callback:
            command = session_manager.check_for_commands(uart_service)
            if command:
                command_handler_callback(command)

        time.sleep(0.1)
        continue

    # 3. Check session completion
    if session_manager.is_session_complete():
        break

    # ... normal therapy execution ...

    # 4. Poll for commands during execution
    if command_handler_callback:
        command = session_manager.check_for_commands(uart_service)
        if command:
            command_handler_callback(command)

# 5. Return termination reason
if session_manager.is_idle():
    return "STOPPED"  # User stopped manually
else:
    return "COMPLETED"  # Duration reached
```

### Timing Breakdown Per Macrocycle

**Configuration** (default Noisy VCR):
- TIME_ON = 0.100s (100ms)
- TIME_OFF = 0.067s (67ms)
- JITTER = 23.5% = 0.235 * (100 + 67) / 2 = ±19.6ms
- TIME_RELAX = 4 * (TIME_ON + TIME_OFF) = 668ms

**One Macrocycle**:

```
Buzz Sequence 1:
  Finger 1: 100ms ON + 67ms OFF ±19.6ms jitter = 147-186ms
  Finger 2: 100ms ON + 67ms OFF ±19.6ms jitter = 147-186ms
  Finger 3: 100ms ON + 67ms OFF ±19.6ms jitter = 147-186ms
  Finger 4: 100ms ON + 67ms OFF ±19.6ms jitter = 147-186ms
  Total: 588-744ms (avg 666ms)

Buzz Sequence 2: 588-744ms
Buzz Sequence 3: 588-744ms

Relax Period 1: 668ms
Relax Period 2: 668ms

Total per macrocycle: 3100-3568ms (avg 3334ms ≈ 3.3 seconds)
```

**Per Session** (120 minutes):

```
Macrocycles per session: (120 * 60) / 3.334 ≈ 2,160 macrocycles
Total buzzes per glove: 2,160 * 3 * 4 = 25,920 finger activations
Total therapy time: ~108 minutes (12 minutes spent in relax periods)
```

---

## Therapy Profiles

### Profile Structure

**Location**: `src/profiles/*.py`

**Available Profiles**:

1. **reg_vcr.py** - Regular VCR (no jitter)
2. **noisy_vcr.py** - Noisy VCR (23.5% jitter, mirrored) - **DEFAULT**
3. **hybrid_vcr.py** - Hybrid VCR (mixed frequency)
4. **custom_vcr.py** - User-defined parameters
5. **defaults.py** - Active profile (loaded at boot)

### Parameter Reference

**Complete Parameter Set** (defaults.py):

```python
# Actuator Configuration
ACTUATOR_TYPE = "LRA"              # "LRA" or "ERM"
ACTUATOR_VOLTAGE = 2.50            # Peak voltage (1.0-3.3V)
ACTUATOR_FREQUENCY = 250           # Nominal frequency (150-300 Hz)
RANDOM_FREQ = False                # Randomize frequency per cycle
ACTUATOR_FREQL = 210               # Low frequency bound (if RANDOM_FREQ)
ACTUATOR_FREQNOM = 250             # Nominal frequency
ACTUATOR_FREQH = 260               # High frequency bound (if RANDOM_FREQ)

# Amplitude Control
AMPLITUDE_MIN = 100                # Minimum intensity (0-100%)
AMPLITUDE_MAX = 100                # Maximum intensity (0-100%)

# Timing Parameters
TIME_SESSION = 120                 # Session duration (1-180 minutes)
TIME_ON = 0.100                    # Buzz duration (0.050-0.500 seconds)
TIME_OFF = 0.067                   # Pause duration (0.020-0.200 seconds)
TIME_RELAX = 4 * (TIME_ON + TIME_OFF)  # Relax between sequences
JITTER = 23.5                      # Temporal jitter (0-50%)
TIME_JITTER = (TIME_ON + TIME_OFF) * (JITTER / 100) / 2  # Calculated

# Pattern Configuration
PATTERN_TYPE = "RNDP"              # Always "RNDP" (Random Permutation)
MIRROR = True                      # Symmetric L/R patterns

# Visual Feedback
SYNC_LED = True                    # Flash LED at end of macrocycle

# System Configuration
STARTUP_WINDOW = 30                # BLE command window (seconds)
TIME_END = time.time() + 60 * TIME_SESSION  # Session end timestamp
```

### Profile Comparison

| Profile | TIME_ON | TIME_OFF | JITTER | MIRROR | RANDOM_FREQ | Use Case |
|---------|---------|----------|--------|--------|-------------|----------|
| **Regular VCR** | 100ms | 67ms | 0% | False | No | Standard protocol, independent L/R |
| **Noisy VCR** | 100ms | 67ms | 23.5% | True | No | Default, prevents habituation |
| **Hybrid VCR** | 100ms | 67ms | 0% | False | Yes | Mixed frequency stimulation |
| **Custom VCR** | Variable | Variable | Variable | Variable | Variable | Research/experimental |

### Profile Switching

**Runtime Switching** (menu_controller.py:608-641):

```python
def _cmd_profile_load(self, profile_id):
    """Load therapy profile by ID (1-3)"""

    # 1. Check if session is active
    if self.session_manager.is_running():
        send_error(self.phone_uart, "Cannot modify parameters during active session")
        return

    # 2. Validate profile ID
    is_valid, error, pid = validate_profile_id(profile_id)
    if not is_valid:
        send_error(self.phone_uart, error)
        return

    # 3. Load profile via ProfileManager
    success, message, params = self.profile_manager.load_profile(pid)

    if success:
        # 4. Respond to phone
        send_response(self.phone_uart, {
            "STATUS": "LOADED",
            "PROFILE": self.profile_manager.current_profile_name
        })

        # 5. Sync to VR if PRIMARY
        if self.role == "PRIMARY":
            all_params = self.profile_manager.get_current_profile()
            self._broadcast_param_update(all_params)
```

**Note**: Profile changes only affect **next session**. Active sessions cannot be modified.

---

## DRV2605 Motor Control

### Register Configuration

**DRV2605 Key Registers**:

| Register | Address | Function | Value | Notes |
|----------|---------|----------|-------|-------|
| STATUS | 0x00 | Status/Go bit | 0x01 | Trigger playback |
| MODE | 0x01 | Operating mode | 0x05 | Real-time playback (RTP) |
| RTP_INPUT | 0x02 | RTP amplitude | 0-127 | 7-bit value |
| FEEDBACK_CTRL | 0x1A | N_ERM/LRA select | 0x00=ERM, 0x80=LRA | Actuator type |
| CONTROL3 | 0x1D | Advanced control | 0x21 | Open-loop enable |
| OD_CLAMP | 0x17 | Overdrive clamp | 0-255 | Peak voltage limit |
| LRA_RESONANCE_PERIOD | 0x20 | LRA period | Calculated | Driving frequency |

### Driver Configuration Sequence

**Method** (haptic_controller.py:183-210):

```python
def _configure_driver(driver, config):
    # 1. Set actuator type (LRA vs ERM)
    if config.ACTUATOR_TYPE == "LRA":
        driver.use_LRA()  # Sets FEEDBACK_CTRL[7] = 1
    else:
        driver.use_ERM()  # Sets FEEDBACK_CTRL[7] = 0

    # 2. Enable open-loop mode (disable auto-resonance)
    control3 = driver._read_u8(0x1D)
    driver._write_u8(0x1D, control3 | 0x21)  # Set bits 5 and 0
    # Bit 5: N_PWM_ANALOG (open-loop)
    # Bit 0: LRA_OPEN_LOOP

    # 3. Set peak voltage (overdrive clamp)
    # Formula: Voltage = Register * 0.02122V
    # Range: 0-255 (0-5.6V)
    voltage_value = int(config.ACTUATOR_VOLTAGE / 0.02122)
    driver._write_u8(0x17, voltage_value)
    # Example: 2.50V → 2.50 / 0.02122 ≈ 118

    # 4. Set driving frequency (LRA resonance period)
    # Formula: Period = 1 / (Frequency * 0.00009849)
    # Range: 150-300 Hz typical for LRAs
    period_value = int(1 / (config.ACTUATOR_FREQUENCY * 0.00009849))
    driver._write_u8(0x20, period_value)
    # Example: 250 Hz → 1 / (250 * 0.00009849) ≈ 41

    # 5. Set initial amplitude and activate RTP mode
    driver.realtime_value = 0  # Start silent
    driver.mode = adafruit_drv2605.MODE_REALTIME  # 0x05
```

### Real-Time Playback (RTP) Mode

**Why RTP?**
- Direct amplitude control (0-127)
- No waveform library needed
- Minimal latency (~1ms)
- Precise timing control

**Amplitude Mapping**:

```python
# User intensity (0-100%) → DRV2605 value (0-127)
drv_intensity = int((intensity / 100.0) * 127)

# Examples:
# 0%   → 0   (motor off)
# 50%  → 63  (half power)
# 100% → 127 (full power)
```

**RTP Update Sequence**:

```python
# 1. Set amplitude
driver.realtime_value = 63  # 50% intensity

# 2. Trigger playback (automatically done by library)
# driver.go = True  (handled internally)

# 3. Motor responds within ~1ms

# 4. Stop motor
driver.realtime_value = 0
```

### I2C Multiplexer Routing

**Hardware Setup**:

```
nRF52840 I2C Bus (SCL/SDA)
  |
  └── TCA9548A Multiplexer (0x70)
        ├── Port 0: DRV2605 #1 (Thumb)   @ 0x5A
        ├── Port 1: DRV2605 #2 (Index)   @ 0x5A
        ├── Port 2: DRV2605 #3 (Middle)  @ 0x5A
        └── Port 3: DRV2605 #4 (Ring)    @ 0x5A
```

**Why Multiplexer?** All DRV2605 chips have fixed address 0x5A. Multiplexer allows 4 chips on same bus.

**Channel Selection**:

```python
# Access motor #2 (Index finger)
motor_channel = 1
self.mux.select_channel(motor_channel)  # Route I2C to port 1
self.drivers[motor_channel].realtime_value = 100  # Set amplitude

# Library handles this automatically via port iteration:
for port in self.mux:
    driver = adafruit_drv2605.DRV2605(port)
```

---

## Performance Optimization

### Memory Efficiency

**Pre-allocation Strategy** (vcr_engine.py:78-79):

```python
# ONE-TIME allocation at session start
L_RNDP = list(permutations(range(0, 4)))  # 24 tuples
R_RNDP = list(permutations(range(4, 8)))  # 24 tuples

# Memory usage: 24 * 4 * 2 bytes * 2 lists ≈ 384 bytes
```

**Bad Alternative** (creates garbage):

```python
# DON'T DO THIS - allocates new lists every macrocycle
def rndp_sequence():
    L_RNDP = list(permutations(range(0, 4)))  # ❌ Allocates 384 bytes
    R_RNDP = list(permutations(range(4, 8)))  # ❌ Allocates 384 bytes
    # Over 2,000 macrocycles: 768KB allocated → OOM crash
```

### Garbage Collection Strategy

**Periodic Collection** (vcr_engine.py:234):

```python
# After each macrocycle (~3.3 seconds)
gc.collect()
```

**Why After Macrocycles?**
- Pattern generation creates temporary tuples
- BLE message parsing allocates strings
- ~20-30 collections per minute (minimal overhead)

**Memory Pressure**:

```python
# Typical memory usage:
# - Pattern lists: 384 bytes (persistent)
# - Driver objects: ~2KB (persistent)
# - BLE buffers: ~512 bytes (recycled)
# - Temporary strings: ~200 bytes/macrocycle (garbage collected)
#
# Total footprint: ~5KB stable, ~200 bytes/cycle transient
# Available RAM: ~256KB → Plenty of headroom
```

### Timing Optimization

**Critical Path** (minimize jitter):

```python
# 1. Pattern generation: 0-5ms
pattern = rndp_sequence()

# 2. Motor activation: 1-2ms
haptic.fingers_on(fingers)

# 3. Delay (non-critical): 100ms
time.sleep(TIME_ON)

# 4. Motor deactivation: 1-2ms
haptic.fingers_off(fingers)

# Total critical time: 2-9ms (well within 100ms budget)
```

**Avoiding Blocking Operations**:

```python
# BAD: Blocking I2C read in critical path
if uart.in_waiting:
    msg = uart.readline()  # Blocks until \n received
    haptic.buzz_sequence(pattern)  # Delayed

# GOOD: Non-blocking poll
if uart.in_waiting:
    msg = uart.readline()  # Only called if data ready
haptic.buzz_sequence(pattern)  # No delay
```

### I2C Bus Optimization

**Frequency Selection** (haptic_controller.py:80-134):

```python
# Try standard 100kHz first (most reliable)
self.i2c = board.I2C()  # Default 100kHz

# Fallback to lower frequencies if needed
# 100kHz → 50kHz → 10kHz
# Lower frequency = more reliable, slightly slower
```

**Why Not 400kHz Fast Mode?**
- Longer wires (glove to fingers) require slower speeds
- 100kHz provides sufficient bandwidth (~12KB/s)
- Lower frequency reduces electrical noise sensitivity

---

## Clinical Considerations

### Therapeutic Parameters

**Optimal Settings** (based on research):
- **Frequency**: 250 Hz (matches LRA resonance)
- **Amplitude**: 100% (maximum sensation without pain)
- **ON/OFF ratio**: 100ms/67ms (3:2 ratio)
- **Jitter**: 23.5% (prevents habituation)
- **Session duration**: 120 minutes (2 hours)
- **Pattern**: Random permutations (non-predictable)

### Safety Features

1. **Battery monitoring**: Alert at <3.3V
2. **Connection timeout**: Halt therapy if VL disconnects (10s)
3. **Emergency stop**: `haptic.all_off()` immediately stops all motors
4. **Visual feedback**: LED indicators for errors
5. **Authorization token**: Prevents unauthorized device use

### Bilateral Synchronization

**Importance**: Both hands must stimulate simultaneously for optimal neural desynchronization.

**Achieved Synchronization**: ±7.5-20ms (well within therapeutic tolerance)

**Verification**:

```python
# Enable SYNC_LED in profile
SYNC_LED = True  # Flash LED at end of each macrocycle

# Observe LED timing:
# - Both gloves should flash within 20ms of each other
# - If >50ms lag, check BLE connection quality
```

---

## Troubleshooting

### Motor Not Activating

**Diagnostic Checklist**:

1. **Check I2C connection**:
   ```python
   # Serial output should show:
   # "I2C Primary addresses found: ['0x70']"
   # "Secondary i2c Channel 0: ['0x5a']"
   ```

2. **Check driver initialization**:
   ```python
   # Serial output should show:
   # "Begin haptic driver config..."
   # "Haptic driver configuration complete!"
   ```

3. **Check voltage setting**:
   ```python
   # Ensure ACTUATOR_VOLTAGE >= 2.0V
   # Too low = weak/no sensation
   ```

4. **Check frequency**:
   ```python
   # Ensure ACTUATOR_FREQUENCY matches LRA resonance (~250 Hz)
   # Off-resonance = weak sensation
   ```

### Synchronization Drift

**Symptoms**: VL and VR buzz at noticeably different times

**Causes**:
1. BLE connection interval >7.5ms (check `connection.connection_interval`)
2. EXECUTE_BUZZ timeout (check VR serial logs for timeouts)
3. Pattern desync (check SEED_ACK received)

**Fix**:
```python
# Restart both gloves
# Monitor serial output for:
# "[VL] Sent EXECUTE_BUZZ:0"
# "[VR] Received EXECUTE_BUZZ:0"
# Time delta should be <20ms
```

### Session Not Completing

**Symptoms**: Therapy runs indefinitely

**Causes**:
1. `TIME_END` calculation error
2. `session_manager` not initialized
3. Clock rollover (after ~50 days uptime)

**Fix**:
```python
# Check TIME_SESSION setting
# Ensure session_manager.is_session_complete() called in loop
```

---

## Future Enhancements

### Potential Improvements

1. **Adaptive Jitter**: Adjust jitter based on therapy progress
2. **Multi-Frequency Stimulation**: Simultaneously drive different fingers at different frequencies
3. **Closed-Loop Control**: Adjust amplitude based on measured motor response
4. **Session Logging**: Record buzz patterns to flash storage
5. **Resume After Disconnect**: Save session state for recovery

### Research Extensions

1. **Frequency Sweeps**: Gradual frequency changes during session
2. **Phase-Shifted Patterns**: Introduce phase delays between fingers
3. **Intensity Ramps**: Gradual amplitude increase/decrease
4. **Personalized Profiles**: Machine learning-optimized parameters

---

**Document Maintenance:**
Update this document when:
- Modifying therapy algorithms or pattern generation
- Changing DRV2605 configuration parameters
- Adding new therapy profiles
- Updating timing constants
- Modifying haptic control logic

**Last Updated:** 2025-01-23
**Therapy Protocol Version:** vCR 1.0
**Reviewed By:** Clinical Engineering Team
