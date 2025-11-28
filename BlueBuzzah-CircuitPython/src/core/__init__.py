"""
Core Module
===========
Core type definitions and constants for the BlueBuzzah v2 system.

This module provides the foundational types, enums, and constants used
throughout the BlueBuzzah bilateral haptic therapy system.

Exports:
    Types:
        - DeviceRole: PRIMARY or SECONDARY device role
        - TherapyState: Session state machine states
        - BootResult: Boot sequence outcomes
        - ActuatorType: Haptic actuator types
        - SyncCommandType: Synchronization command types
        - BatteryStatus: Battery status information
        - DeviceConfig: Device configuration data
        - SessionInfo: Therapy session information

    Constants:
        - FIRMWARE_VERSION: Current firmware version
        - Timing constants, CONNECTION_TIMEOUT, etc.
        - Hardware constants: I2C addresses, pin assignments
        - LED colors: RGB tuples for visual feedback
        - Battery thresholds: Voltage levels for warnings
        - Memory settings: Garbage collection configuration

Module: core
Version: 2.0.0
"""

from core.types import (
    DeviceRole,
    TherapyState,
    BootResult,
    ActuatorType,
    SyncCommandType,
    BatteryStatus,
    DeviceConfig,
    SessionInfo,
)

from core.constants import (
    # Firmware
    FIRMWARE_VERSION,

    # Timing
    STARTUP_WINDOW,
    CONNECTION_TIMEOUT,
    BLE_INTERVAL,
    BLE_LATENCY,
    BLE_TIMEOUT,
    BLE_ADV_INTERVAL,
    SYNC_INTERVAL,
    SYNC_TIMEOUT,
    COMMAND_TIMEOUT,

    # Hardware - I2C
    I2C_MULTIPLEXER_ADDR,
    DRV2605_DEFAULT_ADDR,
    I2C_FREQUENCY,

    # Hardware - Pins
    NEOPIXEL_PIN,
    BATTERY_PIN,
    I2C_SDA_PIN,
    I2C_SCL_PIN,

    # LED Colors
    LED_BLUE,
    LED_GREEN,
    LED_RED,
    LED_WHITE,
    LED_YELLOW,
    LED_ORANGE,
    LED_OFF,

    # Battery
    CRITICAL_VOLTAGE,
    LOW_VOLTAGE,
    BATTERY_CHECK_INTERVAL,

    # Memory Management
    GC_THRESHOLD,
    GC_ENABLED,

    # BLE Protocol
    CHUNK_SIZE,
    AUTH_TOKEN,
    MAX_MESSAGE_SIZE,
)

__all__ = [
    # Types
    "DeviceRole",
    "TherapyState",
    "BootResult",
    "ActuatorType",
    "SyncCommandType",
    "BatteryStatus",
    "Connection",

    # Constants - Firmware
    "FIRMWARE_VERSION",

    # Constants - Timing
    "STARTUP_WINDOW",
    "CONNECTION_TIMEOUT",
    "BLE_INTERVAL",
    "BLE_LATENCY",
    "BLE_TIMEOUT",
    "BLE_ADV_INTERVAL",
    "SYNC_INTERVAL",
    "SYNC_TIMEOUT",
    "COMMAND_TIMEOUT",

    # Constants - Hardware I2C
    "I2C_MULTIPLEXER_ADDR",
    "DRV2605_DEFAULT_ADDR",
    "I2C_FREQUENCY",

    # Constants - Hardware Pins
    "NEOPIXEL_PIN",
    "BATTERY_PIN",
    "I2C_SDA_PIN",
    "I2C_SCL_PIN",

    # Constants - LED Colors
    "LED_BLUE",
    "LED_GREEN",
    "LED_RED",
    "LED_WHITE",
    "LED_YELLOW",
    "LED_ORANGE",
    "LED_OFF",

    # Constants - Battery
    "CRITICAL_VOLTAGE",
    "LOW_VOLTAGE",
    "BATTERY_CHECK_INTERVAL",

    # Constants - Memory
    "GC_THRESHOLD",
    "GC_ENABLED",

    # Constants - BLE Protocol
    "CHUNK_SIZE",
    "AUTH_TOKEN",
    "MAX_MESSAGE_SIZE",
]

__version__ = "2.0.0"
