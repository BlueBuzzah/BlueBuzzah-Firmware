"""
Mock Implementations for Testing
=================================

Comprehensive mock implementations of hardware and communication components
for isolated unit testing without physical hardware dependencies.

Classes:
    MockHapticController: Mock haptic controller with call tracking
    MockBatteryMonitor: Mock battery with configurable voltage
    MockI2CMultiplexer: Mock I2C multiplexer
    MockNeoPixelLED: Mock LED with state recording
    MockBLE: Mock BLE service with simulated connections
    MockBLEConnection: Mock BLE connection

Version: 2.0.0
"""

from .hardware import (
    MockHapticController,
    MockBatteryMonitor,
    MockI2CMultiplexer,
    MockNeoPixelLED,
)
from .ble import (
    MockBLE,
    MockBLEConnection,
)

__all__ = [
    "MockHapticController",
    "MockBatteryMonitor",
    "MockI2CMultiplexer",
    "MockNeoPixelLED",
    "MockBLE",
    "MockBLEConnection",
]
