"""
Pytest Configuration and Fixtures
===================================

Comprehensive pytest configuration and fixtures for BlueBuzzah v2 testing.
Provides common test objects, mock components, and test configurations.

Fixtures:
    - state_machine: Therapy state machine for testing
    - mock_haptic_controller: Mock haptic controller
    - mock_ble_service: Mock BLE service
    - mock_battery_monitor: Mock battery monitor
    - mock_led: Mock NeoPixel LED
    - mock_multiplexer: Mock I2C multiplexer
    - test_therapy_config: Default therapy configuration
    - test_base_config: Default base configuration
    - therapy_engine: Therapy engine with mocks
    - session_manager: Session manager with mocks

Version: 2.0.0
"""

import pytest
import asyncio
import sys
from pathlib import Path
from typing import Optional
from unittest.mock import MagicMock

# Add src directory to Python path
src_path = Path(__file__).parent.parent / "src"
sys.path.insert(0, str(src_path))

# Mock micropython module before importing code that requires it
if 'micropython' not in sys.modules:
    micropython_mock = MagicMock()
    micropython_mock.const = lambda x: x
    sys.modules['micropython'] = micropython_mock

# Import core components
from core.types import DeviceRole, ActuatorType, TherapyState
from state import TherapyStateMachine, StateTrigger

# Import mocks
from tests.mocks.hardware import (
    MockHapticController,
    MockBatteryMonitor,
    MockI2CMultiplexer,
    MockNeoPixelLED,
)
from tests.mocks.ble import (
    MockBLE,
)


# ============================================================================
# Pytest Configuration
# ============================================================================

def pytest_configure(config):
    """Configure pytest with custom markers."""
    config.addinivalue_line(
        "markers", "unit: Unit tests for individual components"
    )
    config.addinivalue_line(
        "markers", "integration: Integration tests for component interactions"
    )
    config.addinivalue_line(
        "markers", "slow: Tests that take significant time to run"
    )
    config.addinivalue_line(
        "markers", "hardware: Tests requiring hardware simulation"
    )
    config.addinivalue_line(
        "markers", "ble: Tests involving BLE communication"
    )


# ============================================================================
# State Machine Fixtures
# ============================================================================

@pytest.fixture
def state_machine():
    """
    Provide TherapyStateMachine instance for testing.

    Returns:
        TherapyStateMachine: Fresh state machine instance

    Usage:
        def test_state_transitions(state_machine):
            assert state_machine.get_current_state() == TherapyState.IDLE
            state_machine.transition(StateTrigger.START_SESSION)
            assert state_machine.get_current_state() == TherapyState.RUNNING
    """
    machine = TherapyStateMachine()
    yield machine


# ============================================================================
# Mock Hardware Fixtures
# ============================================================================

@pytest.fixture
def mock_haptic_controller():
    """
    Provide MockHapticController for testing.

    Returns:
        MockHapticController: Mock haptic controller with tracking

    Usage:
        def test_haptic_activation(mock_haptic_controller):
            mock_haptic_controller.activate(finger=0, amplitude=75)
            assert mock_haptic_controller.is_active(0)
            assert mock_haptic_controller.get_activation_count() == 1
    """
    controller = MockHapticController(actuator_type=ActuatorType.LRA)
    yield controller
    controller.reset()


@pytest.fixture
def mock_battery_monitor():
    """
    Provide MockBatteryMonitor for testing.

    Returns:
        MockBatteryMonitor: Mock battery with configurable voltage

    Usage:
        def test_battery_status(mock_battery_monitor):
            mock_battery_monitor.set_voltage(3.85)
            status = mock_battery_monitor.get_status()
            assert status.voltage == 3.85
            assert not status.is_low
    """
    monitor = MockBatteryMonitor(voltage=3.85)
    yield monitor


@pytest.fixture
def mock_multiplexer():
    """
    Provide MockI2CMultiplexer for testing.

    Returns:
        MockI2CMultiplexer: Mock I2C multiplexer

    Usage:
        def test_channel_selection(mock_multiplexer):
            mock_multiplexer.select_channel(0)
            assert mock_multiplexer.get_active_channel() == 0
    """
    mux = MockI2CMultiplexer()

    # Add simulated DRV2605 devices on channels 0-4
    for channel in range(5):
        mux.add_device(channel, 0x5A)

    yield mux
    mux.reset()


@pytest.fixture
def mock_led():
    """
    Provide MockNeoPixelLED for testing.

    Returns:
        MockNeoPixelLED: Mock LED with state tracking

    Usage:
        def test_led_control(mock_led):
            mock_led.set_color(0, 255, 0)
            assert mock_led.current_color == (0, 255, 0)
            assert mock_led.is_on()
    """
    led = MockNeoPixelLED()
    yield led
    led.reset()


# ============================================================================
# Mock BLE Fixtures
# ============================================================================

@pytest.fixture
def mock_ble():
    """
    Provide MockBLE for testing.

    Returns:
        MockBLE: Mock BLE service with connection simulation

    Usage:
        def test_ble_connection(mock_ble):
            mock_ble.register_device("BlueBuzzah", rssi=-45)
            conn_id = mock_ble.connect("BlueBuzzah")
            assert conn_id is not None
    """
    ble = MockBLE()
    yield ble
    ble.disconnect_all()


# ============================================================================
# Configuration Fixtures
# ============================================================================

@pytest.fixture
def test_device_config():
    """
    Provide default device configuration for testing.

    Returns:
        dict: PRIMARY device configuration

    Usage:
        def test_device_role(test_device_config):
            assert test_device_config['role'] == DeviceRole.PRIMARY
    """
    return {
        'role': DeviceRole.PRIMARY,
        'ble_name': 'BlueBuzzah',
        'firmware_version': '2.0.0-test',
        'startup_window_sec': 30,
        'connection_timeout_sec': 30,
        'ble_interval_ms': 7.5,
        'ble_timeout_ms': 100,
    }


@pytest.fixture
def test_therapy_config():
    """
    Provide default therapy configuration for testing.

    Returns:
        dict: Noisy vCR therapy configuration

    Usage:
        def test_therapy_parameters(test_therapy_config):
            assert test_therapy_config['actuator_type'] == ActuatorType.LRA
            assert test_therapy_config['jitter_percent'] > 0
    """
    return {
        'actuator_type': ActuatorType.LRA,
        'frequency_hz': 250,
        'amplitude_percent': 100,
        'time_on_ms': 100.0,
        'time_off_ms': 67.0,
        'jitter_percent': 23.5,
        'mirror_pattern': True,
        'num_fingers': 5,
    }


# ============================================================================
# Integrated Component Fixtures
# ============================================================================


# ============================================================================
# Asyncio Event Loop Fixture
# ============================================================================

@pytest.fixture
def event_loop():
    """
    Provide event loop for async tests.

    Returns:
        asyncio.AbstractEventLoop: Event loop

    Usage:
        @pytest.mark.asyncio
        async def test_async_operation(event_loop):
            await asyncio.sleep(0.1)
            assert True
    """
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    yield loop
    loop.close()


# ============================================================================
# Test Data Fixtures
# ============================================================================

@pytest.fixture
def sample_pattern_sequence():
    """
    Provide sample pattern sequence for testing.

    Returns:
        dict: Pattern sequence data

    Usage:
        def test_pattern_generation(sample_pattern_sequence):
            left = sample_pattern_sequence["left_sequence"]
            assert len(left) == 5
    """
    return {
        "left_sequence": [2, 0, 3, 1, 4],
        "right_sequence": [2, 0, 3, 1, 4],
        "timing_ms": [167.0, 167.0, 167.0, 167.0, 167.0],
        "burst_duration_ms": 100.0,
        "inter_burst_interval_ms": 67.0,
    }


@pytest.fixture
def sample_ble_message():
    """
    Provide sample BLE protocol message.

    Returns:
        bytes: Sample message

    Usage:
        def test_message_parsing(sample_ble_message):
            # Parse and validate message
            pass
    """
    return b"START_SESSION|noisy_vcr\n"


# ============================================================================
# Temporary File Fixtures
# ============================================================================

@pytest.fixture
def temp_config_file(tmp_path):
    """
    Provide temporary configuration file for testing.

    Args:
        tmp_path: Pytest temporary directory

    Returns:
        Path: Path to temporary config file

    Usage:
        def test_config_loading(temp_config_file):
            # Load config from temp file
            pass
    """
    import json

    config_data = {
        "deviceRole": "Primary",
        "actuatorType": "LRA",
        "frequencyHz": 250,
    }

    config_file = tmp_path / "test_config.json"
    with open(config_file, "w") as f:
        json.dump(config_data, f)

    return config_file


# ============================================================================
# Cleanup Fixtures
# ============================================================================

@pytest.fixture(autouse=True)
def reset_singletons():
    """
    Auto-cleanup fixture to reset singleton instances between tests.

    This ensures test isolation by clearing any singleton state.

    Usage:
        Automatically applied to all tests
    """
    yield
    # Cleanup code here if needed for singletons
    pass
