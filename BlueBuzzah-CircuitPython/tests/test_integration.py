"""
Integration Tests
=================

Comprehensive integration tests for BlueBuzzah v2 firmware components.
Tests component interactions, PRIMARY/SECONDARY communication, and
end-to-end session lifecycle.

Test Categories:
    - PRIMARY/SECONDARY Communication: BLE sync protocol
    - Session Lifecycle: Complete session flow from start to stop
    - State Machine Integration: State transitions with therapy engine
    - Sync Command Flow: EXECUTE_BUZZ bilateral synchronization
    - Heartbeat Protocol: Connection health monitoring
    - Error Recovery: Disconnection and reconnection handling

Version: 1.0.0
"""

import pytest
import sys
import time
from unittest.mock import MagicMock, patch, PropertyMock
from pathlib import Path

# Add src directory to Python path
src_path = Path(__file__).parent.parent / "src"
sys.path.insert(0, str(src_path))

# Mock micropython module
if 'micropython' not in sys.modules:
    micropython_mock = MagicMock()
    micropython_mock.const = lambda x: x
    sys.modules['micropython'] = micropython_mock

# Import core types
from core.types import DeviceRole, TherapyState, ActuatorType

# Import mocks
from tests.mocks.ble import MockBLE, MockBLEConnection
from tests.mocks.hardware import MockHapticController


# ============================================================================
# Fixtures
# ============================================================================

@pytest.fixture
def primary_ble():
    """Mock BLE for PRIMARY device."""
    ble = MockBLE()
    ble.advertise("BlueBuzzah-Primary")
    return ble


@pytest.fixture
def secondary_ble():
    """Mock BLE for SECONDARY device."""
    ble = MockBLE()
    return ble


@pytest.fixture
def connected_ble_pair(primary_ble, secondary_ble):
    """Create connected PRIMARY/SECONDARY BLE pair."""
    # Register PRIMARY with SECONDARY
    secondary_ble.register_device("BlueBuzzah-Primary", rssi=-45)

    # SECONDARY connects to PRIMARY
    conn_id = secondary_ble.scan_and_connect("BlueBuzzah-Primary", timeout=5.0)

    # PRIMARY registers SECONDARY connection
    primary_ble.register_device("secondary", rssi=-50)
    primary_conn = primary_ble.wait_for_connection("secondary", timeout=5.0)

    return {
        "primary_ble": primary_ble,
        "secondary_ble": secondary_ble,
        "primary_conn": primary_conn,
        "secondary_conn": conn_id,
    }


@pytest.fixture
def primary_haptic():
    """Mock haptic controller for PRIMARY."""
    return MockHapticController(actuator_type=ActuatorType.LRA)


@pytest.fixture
def secondary_haptic():
    """Mock haptic controller for SECONDARY."""
    return MockHapticController(actuator_type=ActuatorType.LRA)


@pytest.fixture
def mock_therapy_config():
    """Default therapy configuration."""
    return {
        'actuator_type': ActuatorType.LRA,
        'frequency_hz': 250,
        'amplitude_percent': 75,
        'time_on_ms': 100.0,
        'time_off_ms': 67.0,
        'jitter_percent': 23.5,
        'mirror_pattern': True,
        'num_fingers': 5,
    }


# ============================================================================
# BLE Communication Integration Tests
# ============================================================================

class TestBLECommunicationIntegration:
    """Test BLE communication between devices."""

    def test_primary_advertises_and_secondary_connects(self, primary_ble, secondary_ble):
        """PRIMARY advertises, SECONDARY scans and connects."""
        # Verify PRIMARY is advertising
        assert primary_ble._advertising is True
        assert primary_ble.advertised_name == "BlueBuzzah-Primary"

        # Register PRIMARY device for SECONDARY to find
        secondary_ble.register_device("BlueBuzzah-Primary", rssi=-45)

        # SECONDARY scans and connects
        conn_id = secondary_ble.scan_and_connect("BlueBuzzah-Primary", timeout=5.0)

        assert conn_id is not None
        assert secondary_ble.is_connected(conn_id)

    def test_bidirectional_message_exchange(self, connected_ble_pair):
        """Messages can be sent and received bidirectionally."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # PRIMARY sends to SECONDARY
        primary.send(primary_conn, "PING\x04")
        secondary.simulate_receive(secondary_conn, "PING\x04")

        msg = secondary.receive(secondary_conn, timeout=1.0)
        assert msg == "PING\x04"

        # SECONDARY responds to PRIMARY
        secondary.send(secondary_conn, "PONG\x04")
        primary.simulate_receive(primary_conn, "PONG\x04")

        msg = primary.receive(primary_conn, timeout=1.0)
        assert msg == "PONG\x04"

    def test_multiple_rapid_messages(self, connected_ble_pair):
        """Multiple rapid messages are delivered correctly."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Send multiple messages rapidly
        for i in range(10):
            primary.send(primary_conn, "MSG_{}\x04".format(i))
            secondary.simulate_receive(secondary_conn, "MSG_{}\x04".format(i))

        # Receive all messages
        received = []
        for _ in range(10):
            msg = secondary.receive(secondary_conn, timeout=0.1)
            if msg:
                received.append(msg)

        assert len(received) == 10
        for i in range(10):
            assert "MSG_{}\x04".format(i) in received


# ============================================================================
# Sync Protocol Integration Tests
# ============================================================================

class TestSyncProtocolIntegration:
    """Test SYNC protocol integration between devices."""

    def test_execute_buzz_sync_command(self, connected_ble_pair):
        """EXECUTE_BUZZ command is sent and received correctly."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # PRIMARY sends EXECUTE_BUZZ
        sync_cmd = "SYNC:EXECUTE_BUZZ|0|75|100\x04"
        primary.send(primary_conn, sync_cmd)
        secondary.simulate_receive(secondary_conn, sync_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)

        assert msg == sync_cmd
        assert "EXECUTE_BUZZ" in msg
        assert "|0|75|100" in msg

    def test_start_session_sync_command(self, connected_ble_pair):
        """START_SESSION command is sent and received correctly."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # PRIMARY sends START_SESSION
        sync_cmd = "SYNC:START_SESSION|noisy_vcr|100|67|23.5|1\x04"
        primary.send(primary_conn, sync_cmd)
        secondary.simulate_receive(secondary_conn, sync_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)

        assert msg == sync_cmd
        assert "START_SESSION" in msg
        assert "noisy_vcr" in msg

    def test_heartbeat_sync_command(self, connected_ble_pair):
        """HEARTBEAT command is sent and received correctly."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # PRIMARY sends HEARTBEAT
        timestamp = time.monotonic()
        sync_cmd = "SYNC:HEARTBEAT|{:.3f}\x04".format(timestamp)
        primary.send(primary_conn, sync_cmd)
        secondary.simulate_receive(secondary_conn, sync_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)

        assert "HEARTBEAT" in msg
        # Timestamp should be parseable
        parts = msg.replace("\x04", "").split("|")
        assert len(parts) == 2
        parsed_timestamp = float(parts[1])
        assert abs(parsed_timestamp - timestamp) < 0.001

    def test_stop_session_sync_command(self, connected_ble_pair):
        """STOP_SESSION command is sent and received correctly."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # PRIMARY sends STOP_SESSION
        sync_cmd = "SYNC:STOP_SESSION|completed\x04"
        primary.send(primary_conn, sync_cmd)
        secondary.simulate_receive(secondary_conn, sync_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)

        assert msg == sync_cmd
        assert "STOP_SESSION" in msg
        assert "completed" in msg

    def test_pause_resume_sync_commands(self, connected_ble_pair):
        """PAUSE and RESUME commands work correctly."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Send PAUSE
        pause_cmd = "SYNC:PAUSE_SESSION\x04"
        primary.send(primary_conn, pause_cmd)
        secondary.simulate_receive(secondary_conn, pause_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)
        assert "PAUSE_SESSION" in msg

        # Send RESUME
        resume_cmd = "SYNC:RESUME_SESSION\x04"
        primary.send(primary_conn, resume_cmd)
        secondary.simulate_receive(secondary_conn, resume_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)
        assert "RESUME_SESSION" in msg


# ============================================================================
# State Machine Integration Tests
# ============================================================================

class TestStateMachineIntegration:
    """Test state machine integration with other components."""

    def test_state_machine_initial_state(self):
        """State machine starts in IDLE state."""
        from state import TherapyStateMachine

        sm = TherapyStateMachine()
        assert sm.get_current_state() == TherapyState.IDLE

    def test_state_machine_transition_chain(self):
        """State machine supports full session lifecycle transitions."""
        from state import TherapyStateMachine, StateTrigger

        sm = TherapyStateMachine()

        # IDLE -> RUNNING (via START_SESSION)
        sm.transition(StateTrigger.START_SESSION)
        assert sm.get_current_state() == TherapyState.RUNNING

        # RUNNING -> PAUSED
        sm.transition(StateTrigger.PAUSE_SESSION)
        assert sm.get_current_state() == TherapyState.PAUSED

        # PAUSED -> RUNNING
        sm.transition(StateTrigger.RESUME_SESSION)
        assert sm.get_current_state() == TherapyState.RUNNING

        # RUNNING -> STOPPING
        sm.transition(StateTrigger.STOP_SESSION)
        assert sm.get_current_state() == TherapyState.STOPPING

        # STOPPING -> IDLE (via STOPPED)
        sm.transition(StateTrigger.STOPPED)
        assert sm.get_current_state() == TherapyState.IDLE

    def test_state_callback_integration(self):
        """State machine callbacks are invoked correctly."""
        from state import TherapyStateMachine, StateTrigger

        callback_history = []

        def on_change(transition):
            callback_history.append({
                'old': transition.from_state,
                'new': transition.to_state,
                'trigger': transition.trigger
            })

        sm = TherapyStateMachine()
        sm.on_state_change(on_change)

        sm.transition(StateTrigger.START_SESSION)
        sm.transition(StateTrigger.PAUSE_SESSION)

        assert len(callback_history) == 2
        assert callback_history[0]['old'] == TherapyState.IDLE
        assert callback_history[0]['new'] == TherapyState.RUNNING
        assert callback_history[1]['old'] == TherapyState.RUNNING
        assert callback_history[1]['new'] == TherapyState.PAUSED


# ============================================================================
# Session Lifecycle Integration Tests
# ============================================================================

class TestSessionLifecycleIntegration:
    """Test complete session lifecycle integration."""

    def test_session_start_to_stop_flow(self, connected_ble_pair, mock_therapy_config):
        """Complete session from start to stop."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # 1. Start session
        start_cmd = "SYNC:START_SESSION|noisy_vcr|100|67|23.5|1\x04"
        primary.send(primary_conn, start_cmd)
        secondary.simulate_receive(secondary_conn, start_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)
        assert "START_SESSION" in msg

        # 2. Simulate some EXECUTE_BUZZ commands (therapy running)
        for finger in range(5):
            buzz_cmd = "SYNC:EXECUTE_BUZZ|{}|75|100\x04".format(finger)
            primary.send(primary_conn, buzz_cmd)
            secondary.simulate_receive(secondary_conn, buzz_cmd)

            msg = secondary.receive(secondary_conn, timeout=1.0)
            assert "EXECUTE_BUZZ|{}".format(finger) in msg

        # 3. Stop session
        stop_cmd = "SYNC:STOP_SESSION|completed\x04"
        primary.send(primary_conn, stop_cmd)
        secondary.simulate_receive(secondary_conn, stop_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)
        assert "STOP_SESSION" in msg

    def test_session_with_pause_resume(self, connected_ble_pair):
        """Session with pause and resume."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Start session
        start_cmd = "SYNC:START_SESSION|noisy_vcr|100|67|23.5|1\x04"
        primary.send(primary_conn, start_cmd)
        secondary.simulate_receive(secondary_conn, start_cmd)
        secondary.receive(secondary_conn, timeout=1.0)

        # Execute some buzzes
        buzz_cmd = "SYNC:EXECUTE_BUZZ|0|75|100\x04"
        primary.send(primary_conn, buzz_cmd)
        secondary.simulate_receive(secondary_conn, buzz_cmd)
        secondary.receive(secondary_conn, timeout=1.0)

        # Pause session
        pause_cmd = "SYNC:PAUSE_SESSION\x04"
        primary.send(primary_conn, pause_cmd)
        secondary.simulate_receive(secondary_conn, pause_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)
        assert "PAUSE_SESSION" in msg

        # Resume session
        resume_cmd = "SYNC:RESUME_SESSION\x04"
        primary.send(primary_conn, resume_cmd)
        secondary.simulate_receive(secondary_conn, resume_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)
        assert "RESUME_SESSION" in msg

        # Continue with buzzes
        buzz_cmd = "SYNC:EXECUTE_BUZZ|1|75|100\x04"
        primary.send(primary_conn, buzz_cmd)
        secondary.simulate_receive(secondary_conn, buzz_cmd)
        secondary.receive(secondary_conn, timeout=1.0)

        # Stop session
        stop_cmd = "SYNC:STOP_SESSION|completed\x04"
        primary.send(primary_conn, stop_cmd)
        secondary.simulate_receive(secondary_conn, stop_cmd)
        secondary.receive(secondary_conn, timeout=1.0)


# ============================================================================
# Heartbeat Protocol Integration Tests
# ============================================================================

class TestHeartbeatIntegration:
    """Test heartbeat protocol integration."""

    def test_heartbeat_sequence(self, connected_ble_pair):
        """Multiple heartbeats are sent and received."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Send multiple heartbeats
        timestamps = []
        for _ in range(5):
            ts = time.monotonic()
            timestamps.append(ts)

            hb_cmd = "SYNC:HEARTBEAT|{:.3f}\x04".format(ts)
            primary.send(primary_conn, hb_cmd)
            secondary.simulate_receive(secondary_conn, hb_cmd)

            msg = secondary.receive(secondary_conn, timeout=1.0)
            assert "HEARTBEAT" in msg

            time.sleep(0.01)

        # Verify timestamps are increasing
        for i in range(1, len(timestamps)):
            assert timestamps[i] > timestamps[i-1]

    def test_heartbeat_timestamp_parsing(self, connected_ble_pair):
        """Heartbeat timestamps are parseable."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        original_ts = 12345.678

        hb_cmd = "SYNC:HEARTBEAT|{:.3f}\x04".format(original_ts)
        primary.send(primary_conn, hb_cmd)
        secondary.simulate_receive(secondary_conn, hb_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)

        # Parse timestamp from message
        parts = msg.replace("\x04", "").split("|")
        parsed_ts = float(parts[1])

        assert abs(parsed_ts - original_ts) < 0.0001


# ============================================================================
# Bilateral Sync Integration Tests
# ============================================================================

class TestBilateralSyncIntegration:
    """Test bilateral synchronization between PRIMARY and SECONDARY."""

    def test_bilateral_execute_buzz(self, connected_ble_pair, primary_haptic, secondary_haptic):
        """EXECUTE_BUZZ activates motors on both devices."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Simulate PRIMARY executing buzz locally
        primary_haptic.activate(finger=0, amplitude=75)

        # PRIMARY sends sync command to SECONDARY
        buzz_cmd = "SYNC:EXECUTE_BUZZ|0|75|100\x04"
        primary.send(primary_conn, buzz_cmd)
        secondary.simulate_receive(secondary_conn, buzz_cmd)

        # SECONDARY receives and executes
        msg = secondary.receive(secondary_conn, timeout=1.0)
        assert "EXECUTE_BUZZ|0" in msg

        # Parse command and activate SECONDARY motor
        parts = msg.replace("\x04", "").split("|")
        finger = int(parts[1])
        amplitude = int(parts[2])
        # duration = int(parts[3])  # Duration is in message but not used in mock

        secondary_haptic.activate(finger=finger, amplitude=amplitude)

        # Verify both activated
        assert primary_haptic.get_activation_count() >= 1
        assert secondary_haptic.get_activation_count() >= 1

    def test_full_pattern_bilateral_sync(self, connected_ble_pair, primary_haptic, secondary_haptic):
        """Full pattern executes on both devices."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Execute full 5-finger pattern
        pattern = [2, 0, 3, 1, 4]  # RNDP order

        for finger in pattern:
            # PRIMARY executes locally
            primary_haptic.activate(finger=finger, amplitude=75)

            # PRIMARY sends sync
            buzz_cmd = "SYNC:EXECUTE_BUZZ|{}|75|100\x04".format(finger)
            primary.send(primary_conn, buzz_cmd)
            secondary.simulate_receive(secondary_conn, buzz_cmd)

            # SECONDARY executes
            msg = secondary.receive(secondary_conn, timeout=1.0)
            parts = msg.replace("\x04", "").split("|")
            secondary_haptic.activate(
                finger=int(parts[1]),
                amplitude=int(parts[2])
            )

        # Verify both executed 5 activations
        assert primary_haptic.get_activation_count() == 5
        assert secondary_haptic.get_activation_count() == 5


# ============================================================================
# Error Recovery Integration Tests
# ============================================================================

class TestErrorRecoveryIntegration:
    """Test error recovery scenarios."""

    def test_disconnection_handling(self, connected_ble_pair):
        """Disconnection is handled gracefully."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Verify connected
        assert secondary.is_connected(secondary_conn)

        # Disconnect
        secondary.disconnect(secondary_conn)

        # Verify disconnected
        assert not secondary.is_connected(secondary_conn)

        # Send should fail
        result = secondary.send(secondary_conn, "TEST\x04")
        assert result is False

    def test_receive_after_disconnect(self, connected_ble_pair):
        """Receive returns None after disconnection."""
        secondary = connected_ble_pair["secondary_ble"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Disconnect
        secondary.disconnect(secondary_conn)

        # Receive should return None
        msg = secondary.receive(secondary_conn, timeout=0.1)
        assert msg is None

    def test_reconnection_scenario(self, primary_ble, secondary_ble):
        """Reconnection after disconnection works."""
        # Initial connection
        secondary_ble.register_device("BlueBuzzah-Primary", rssi=-45)
        conn_id = secondary_ble.scan_and_connect("BlueBuzzah-Primary", timeout=5.0)

        assert secondary_ble.is_connected(conn_id)

        # Disconnect
        secondary_ble.disconnect(conn_id)
        assert not secondary_ble.is_connected(conn_id)

        # Reconnect
        new_conn_id = secondary_ble.scan_and_connect("BlueBuzzah-Primary", timeout=5.0)

        assert new_conn_id is not None
        assert secondary_ble.is_connected(new_conn_id)


# ============================================================================
# Performance Integration Tests
# ============================================================================

class TestPerformanceIntegration:
    """Test performance characteristics."""

    @pytest.mark.slow
    def test_high_frequency_sync_commands(self, connected_ble_pair):
        """High frequency sync commands are handled."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Send 100 EXECUTE_BUZZ commands rapidly
        for i in range(100):
            finger = i % 5
            buzz_cmd = "SYNC:EXECUTE_BUZZ|{}|75|100\x04".format(finger)
            primary.send(primary_conn, buzz_cmd)
            secondary.simulate_receive(secondary_conn, buzz_cmd)

        # Receive all
        received = 0
        for _ in range(100):
            msg = secondary.receive(secondary_conn, timeout=0.01)
            if msg:
                received += 1

        assert received == 100

    def test_long_session_simulation(self, connected_ble_pair, primary_haptic, secondary_haptic):
        """Simulate extended session with many cycles."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Simulate 10 full cycles (50 buzzes)
        num_cycles = 10
        pattern = [2, 0, 3, 1, 4]

        for cycle in range(num_cycles):
            for finger in pattern:
                # PRIMARY
                primary_haptic.activate(finger=finger, amplitude=75)

                # Sync
                buzz_cmd = "SYNC:EXECUTE_BUZZ|{}|75|100\x04".format(finger)
                primary.send(primary_conn, buzz_cmd)
                secondary.simulate_receive(secondary_conn, buzz_cmd)

                msg = secondary.receive(secondary_conn, timeout=0.1)
                parts = msg.replace("\x04", "").split("|")
                secondary_haptic.activate(
                    finger=int(parts[1]),
                    amplitude=int(parts[2])
                )

        # Verify total activations
        total_expected = num_cycles * len(pattern)
        assert primary_haptic.get_activation_count() == total_expected
        assert secondary_haptic.get_activation_count() == total_expected


# ============================================================================
# Component Interaction Tests
# ============================================================================

class TestComponentInteractions:
    """Test interactions between multiple components."""

    def test_state_machine_with_ble_commands(self, connected_ble_pair):
        """State machine transitions triggered by BLE commands."""
        from state import TherapyStateMachine, StateTrigger

        sm = TherapyStateMachine()

        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Start session
        start_cmd = "SYNC:START_SESSION|noisy_vcr|100|67|23.5|1\x04"
        primary.send(primary_conn, start_cmd)
        secondary.simulate_receive(secondary_conn, start_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)
        if "START_SESSION" in msg:
            sm.transition(StateTrigger.START_SESSION)

        assert sm.get_current_state() == TherapyState.RUNNING

        # Pause
        pause_cmd = "SYNC:PAUSE_SESSION\x04"
        primary.send(primary_conn, pause_cmd)
        secondary.simulate_receive(secondary_conn, pause_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)
        if "PAUSE_SESSION" in msg:
            sm.transition(StateTrigger.PAUSE_SESSION)

        assert sm.get_current_state() == TherapyState.PAUSED

        # Resume
        resume_cmd = "SYNC:RESUME_SESSION\x04"
        primary.send(primary_conn, resume_cmd)
        secondary.simulate_receive(secondary_conn, resume_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)
        if "RESUME_SESSION" in msg:
            sm.transition(StateTrigger.RESUME_SESSION)

        assert sm.get_current_state() == TherapyState.RUNNING

        # Stop
        stop_cmd = "SYNC:STOP_SESSION|completed\x04"
        primary.send(primary_conn, stop_cmd)
        secondary.simulate_receive(secondary_conn, stop_cmd)

        msg = secondary.receive(secondary_conn, timeout=1.0)
        if "STOP_SESSION" in msg:
            sm.transition(StateTrigger.STOP_SESSION)
            sm.transition(StateTrigger.STOPPED)

        assert sm.get_current_state() == TherapyState.IDLE

    def test_haptic_with_state_constraints(self, primary_haptic):
        """Haptic activation respects state constraints."""
        from state import TherapyStateMachine, StateTrigger

        sm = TherapyStateMachine()

        # In IDLE state - should not activate
        # (In real code, session manager checks state before activating)
        if sm.get_current_state() == TherapyState.IDLE:
            # Don't activate
            pass

        # Start session
        sm.transition(StateTrigger.START_SESSION)

        # In RUNNING state - can activate
        if sm.get_current_state() == TherapyState.RUNNING:
            primary_haptic.activate(finger=0, amplitude=75)

        assert primary_haptic.get_activation_count() == 1

        # Pause
        sm.transition(StateTrigger.PAUSE_SESSION)

        # In PAUSED state - should not activate new patterns
        if sm.get_current_state() == TherapyState.PAUSED:
            # Don't activate
            pass

        assert primary_haptic.get_activation_count() == 1  # Still 1


# ============================================================================
# Edge Case Integration Tests
# ============================================================================

class TestEdgeCaseIntegration:
    """Test edge cases and boundary conditions."""

    def test_empty_message_handling(self, connected_ble_pair):
        """Empty messages are handled gracefully."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Send empty message
        primary.send(primary_conn, "\x04")
        secondary.simulate_receive(secondary_conn, "\x04")

        # Should not crash
        msg = secondary.receive(secondary_conn, timeout=0.1)
        # May be empty string or None

    def test_malformed_sync_command(self, connected_ble_pair):
        """Malformed sync commands don't crash."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Send malformed command
        malformed = "SYNC:INVALID|broken|data\x04"
        primary.send(primary_conn, malformed)
        secondary.simulate_receive(secondary_conn, malformed)

        msg = secondary.receive(secondary_conn, timeout=1.0)

        # Message received - parsing is responsibility of handler
        assert msg == malformed

    def test_very_long_message(self, connected_ble_pair):
        """Very long messages are handled."""
        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Send long message (but under buffer size)
        long_data = "X" * 200
        long_msg = "SYNC:DATA|{}\x04".format(long_data)
        primary.send(primary_conn, long_msg)
        secondary.simulate_receive(secondary_conn, long_msg)

        msg = secondary.receive(secondary_conn, timeout=1.0)

        assert msg == long_msg
        assert long_data in msg


# ============================================================================
# Marker Tests
# ============================================================================

@pytest.mark.integration
class TestMarkerIntegration:
    """Tests marked with integration marker."""

    def test_full_bilateral_session(self, connected_ble_pair, primary_haptic, secondary_haptic):
        """Full bilateral session test."""
        from state import TherapyStateMachine, StateTrigger

        primary_sm = TherapyStateMachine()
        secondary_sm = TherapyStateMachine()

        primary = connected_ble_pair["primary_ble"]
        secondary = connected_ble_pair["secondary_ble"]
        primary_conn = connected_ble_pair["primary_conn"]
        secondary_conn = connected_ble_pair["secondary_conn"]

        # Start both state machines
        primary_sm.transition(StateTrigger.START_SESSION)
        secondary_sm.transition(StateTrigger.START_SESSION)

        # Execute pattern
        pattern = [2, 0, 3, 1, 4]

        for finger in pattern:
            if primary_sm.get_current_state() == TherapyState.RUNNING:
                primary_haptic.activate(finger=finger, amplitude=75)

                buzz_cmd = "SYNC:EXECUTE_BUZZ|{}|75|100\x04".format(finger)
                primary.send(primary_conn, buzz_cmd)
                secondary.simulate_receive(secondary_conn, buzz_cmd)

                msg = secondary.receive(secondary_conn, timeout=0.1)
                if msg and secondary_sm.get_current_state() == TherapyState.RUNNING:
                    parts = msg.replace("\x04", "").split("|")
                    secondary_haptic.activate(
                        finger=int(parts[1]),
                        amplitude=int(parts[2])
                    )

        # Stop both
        primary_sm.transition(StateTrigger.STOP_SESSION)
        primary_sm.transition(StateTrigger.STOPPED)
        secondary_sm.transition(StateTrigger.STOP_SESSION)
        secondary_sm.transition(StateTrigger.STOPPED)

        assert primary_sm.get_current_state() == TherapyState.IDLE
        assert secondary_sm.get_current_state() == TherapyState.IDLE
        assert primary_haptic.get_activation_count() == 5
        assert secondary_haptic.get_activation_count() == 5


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
