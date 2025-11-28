# Utils Module

Common utility functions for BlueBuzzah v2.

## Overview

The utils module provides reusable validation utilities for input checking and error handling.

All utilities are CircuitPython-compatible.

## Module Structure

```
utils/
├── __init__.py          # Package initialization and exports
├── validation.py        # Validation helpers
└── README.md            # This file
```

## Quick Start

```python
from utils.validation import validate_voltage, validate_range
from core.constants import DEVICE_TAG

print(f"{DEVICE_TAG} Starting application")

# Validation (raises ValueError if invalid)
voltage = validate_voltage(3.7)
amplitude = validate_range(75, 0, 100, "amplitude")
```

## validation.py

Common validation functions with descriptive error messages:
- `validate_range(value, min, max, name)`: Numeric range validation
- `validate_voltage(voltage)`: Battery voltage validation (3.0V - 4.2V)
- `validate_pin(pin, name)`: GPIO pin validation
- `validate_i2c_address(address, name)`: I2C address validation (0x00 - 0x7F)

**Example:**
```python
# All validators raise ValueError on failure
amplitude = validate_range(75, 0, 100, "amplitude")
voltage = validate_voltage(3.7)
pin = validate_pin("D13", "LED pin")
addr = validate_i2c_address(0x5A, "DRV2605")
```

## Usage in BlueBuzzah

The utils module is used throughout the BlueBuzzah codebase for:
- Input validation for BLE commands
- Configuration loading validation
- Hardware parameter checking

## Version

Version: 2.0.0
