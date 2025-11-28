"""
Validation Utilities
====================
Common validation functions for BlueBuzzah v2.

This module provides validation utilities for common operations:
- Numeric range validation
- Battery voltage validation
- GPIO pin validation
- I2C address validation

All validators raise ValueError with descriptive messages on failure.

Usage:
    from utils.validation import (
        validate_range,
        validate_voltage,
        validate_pin,
        validate_i2c_address
    )

    # Validate numeric range
    amplitude = validate_range(value, 0, 100, "amplitude")

    # Validate battery voltage
    voltage = validate_voltage(battery_reading)

    # Validate GPIO pin
    validate_pin(13, "LED pin")

    # Validate I2C address
    validate_i2c_address(0x5A, "DRV2605")

Module: utils.validation
Version: 2.0.0
"""


def validate_range(
    value, min_value, max_value, name= "value"
):
    """
    Validate that a numeric value is within specified range.

    Args:
        value: Value to validate
        min_value: Minimum allowed value (inclusive)
        max_value: Maximum allowed value (inclusive)
        name: Name of value for error message

    Returns:
        The validated value (unchanged)

    Raises:
        ValueError: If value is outside range

    Example:
        amplitude = validate_range(75, 0, 100, "amplitude")
        # amplitude = 75

        amplitude = validate_range(150, 0, 100, "amplitude")
        # Raises: ValueError: amplitude must be between 0 and 100, got 150
    """
    if not (min_value <= value <= max_value):
        raise ValueError(
            "{} must be between {} and {}, got {}".format(name, min_value, max_value, value)
        )
    return value


def validate_voltage(
    voltage,
    min_voltage= 3.0,
    max_voltage= 4.2,
    name= "voltage",
):
    """
    Validate battery voltage is within safe LiPo range.

    Default range is for standard LiPo batteries:
    - 3.0V: Safe discharge minimum
    - 4.2V: Fully charged maximum

    Args:
        voltage: Voltage to validate
        min_voltage: Minimum safe voltage (default: 3.0V)
        max_voltage: Maximum safe voltage (default: 4.2V)
        name: Name of voltage for error message

    Returns:
        The validated voltage (unchanged)

    Raises:
        ValueError: If voltage is outside safe range

    Example:
        battery_v = validate_voltage(3.7)
        # battery_v = 3.7

        battery_v = validate_voltage(5.0)
        # Raises: ValueError: voltage must be between 3.0V and 4.2V, got 5.0V
    """
    if not (min_voltage <= voltage <= max_voltage):
        raise ValueError(
            "{} must be between {}V and {}V, got {}V".format(name, min_voltage, max_voltage, voltage)
        )
    return voltage


def validate_pin(
    pin, name= "pin", valid_pins= None
):
    """
    Validate GPIO pin identifier.

    Checks that pin is either:
    - A non-negative integer (pin number)
    - A valid string identifier (pin name)
    - In the valid_pins list if provided

    Args:
        pin: Pin identifier (number or name)
        name: Name of pin for error message
        valid_pins: Optional list of valid pin identifiers

    Returns:
        The validated pin identifier (unchanged)

    Raises:
        ValueError: If pin is invalid

    Example:
        # Validate pin number
        pin = validate_pin(13, "LED pin")
        # pin = 13

        # Validate pin name
        pin = validate_pin("D13", "LED pin")
        # pin = "D13"

        # Validate against whitelist
        pin = validate_pin("A6", "Battery pin", valid_pins=["A6", "A7"])
        # pin = "A6"

        pin = validate_pin("A5", "Battery pin", valid_pins=["A6", "A7"])
        # Raises: ValueError: Battery pin must be one of ['A6', 'A7'], got A5
    """
    # Check type
    if not isinstance(pin, (int, str)):
        raise ValueError("{} must be int or str, got {}".format(name, type(pin).__name__))

    # Validate integer pins are non-negative
    if isinstance(pin, int) and pin < 0:
        raise ValueError("{} must be non-negative, got {}".format(name, pin))

    # Validate string pins are non-empty
    if isinstance(pin, str) and not pin.strip():
        raise ValueError("{} cannot be empty string".format(name))

    # Check against valid_pins list if provided
    if valid_pins is not None:
        if pin not in valid_pins:
            raise ValueError("{} must be one of {}, got {}".format(name, valid_pins, pin))

    return pin


def validate_i2c_address(
    address, name= "I2C address", min_addr= 0x08, max_addr= 0x77
):
    """
    Validate I2C device address.

    Standard 7-bit I2C addresses range from 0x08 to 0x77.
    Addresses below 0x08 and above 0x77 are reserved.

    Reserved addresses:
    - 0x00-0x07: Reserved for special purposes
    - 0x78-0x7F: Reserved for 10-bit addressing and special purposes

    Args:
        address: I2C address to validate (7-bit)
        name: Name of device for error message
        min_addr: Minimum valid address (default: 0x08)
        max_addr: Maximum valid address (default: 0x77)

    Returns:
        The validated address (unchanged)

    Raises:
        ValueError: If address is outside valid range

    Example:
        addr = validate_i2c_address(0x5A, "DRV2605")
        # addr = 0x5A (90 decimal)

        addr = validate_i2c_address(0x70, "TCA9548A")
        # addr = 0x70 (112 decimal)

        addr = validate_i2c_address(0x05, "Device")
        # Raises: ValueError: I2C address must be between 0x08 and 0x77, got 0x05
    """
    if not (min_addr <= address <= max_addr):
        raise ValueError(
            "{} must be between 0x{:02X} and 0x{:02X}, got 0x{:02X}".format(name, min_addr, max_addr, address)
        )
    return address


def validate_actuator_index(
    index, max_actuators= 5, name= "actuator index"
):
    """
    Validate actuator/finger index.

    BlueBuzzah supports up to 5 actuators (one per finger):
    - 0: Thumb
    - 1: Index finger
    - 2: Middle finger
    - 3: Ring finger
    - 4: Pinky finger

    Args:
        index: Actuator index to validate
        max_actuators: Maximum number of actuators (default: 5)
        name: Name for error message

    Returns:
        The validated index (unchanged)

    Raises:
        ValueError: If index is outside valid range

    Example:
        finger = validate_actuator_index(2)
        # finger = 2 (middle finger)

        finger = validate_actuator_index(5)
        # Raises: ValueError: actuator index must be between 0 and 4, got 5
    """
    if not (0 <= index < max_actuators):
        raise ValueError(
            "{} must be between 0 and {}, got {}".format(name, max_actuators - 1, index)
        )
    return index


def validate_amplitude(
    amplitude,
    min_amplitude= 0,
    max_amplitude= 100,
    name= "amplitude",
):
    """
    Validate haptic amplitude percentage.

    Amplitude must be between 0 and 100 percent.

    Args:
        amplitude: Amplitude to validate
        min_amplitude: Minimum amplitude (default: 0)
        max_amplitude: Maximum amplitude (default: 100)
        name: Name for error message

    Returns:
        The validated amplitude (unchanged)

    Raises:
        ValueError: If amplitude is outside valid range

    Example:
        amp = validate_amplitude(75)
        # amp = 75

        amp = validate_amplitude(150)
        # Raises: ValueError: amplitude must be between 0 and 100, got 150
    """
    return validate_range(amplitude, min_amplitude, max_amplitude, name)


def validate_frequency(
    frequency,
    min_freq= 150,
    max_freq= 250,
    name= "frequency",
):
    """
    Validate LRA resonant frequency.

    Typical LRA resonant frequencies range from 150-250 Hz.

    Args:
        frequency: Frequency to validate in Hz
        min_freq: Minimum frequency (default: 150 Hz)
        max_freq: Maximum frequency (default: 250 Hz)
        name: Name for error message

    Returns:
        The validated frequency (unchanged)

    Raises:
        ValueError: If frequency is outside valid range

    Example:
        freq = validate_frequency(175)
        # freq = 175

        freq = validate_frequency(300)
        # Raises: ValueError: frequency must be between 150 and 250, got 300
    """
    return validate_range(frequency, min_freq, max_freq, name)
