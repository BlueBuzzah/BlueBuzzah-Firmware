"""
BlueBuzzah v2 Main Application
===============================

Main application orchestrator that wires all components together following
clean architecture principles with clear layer boundaries and dependency injection.

This module implements the BlueBuzzahApplication class which:
- Initializes all architectural layers (hardware, infrastructure, domain, application, presentation)
- Sets up dependency injection
- Wires event bus subscriptions
- Initializes state machine
- Orchestrates the main application loop
- Handles graceful shutdown and error recovery

Classes:
    - BlueBuzzahApplication: Main application orchestrator

Module: main
Version: 2.0.0
"""

import time
import gc

# Simplified configuration (dict-based)
import config
from core.types import DeviceRole, BootResult, TherapyState
from core.constants import FIRMWARE_VERSION, SKIP_BOOT_SEQUENCE
from state import TherapyStateMachine, StateTrigger

# Consolidated modules (Phase 2 & 3)
from hardware import BoardConfig, DRV2605Controller, BatteryMonitor, I2CMultiplexer
from ble import BLE
from menu import MenuController
from led import LEDController
from therapy import TherapyEngine
from profiles import ProfileManager, create_default_therapy_config
import sync

# CRITICAL-004: Sync accuracy validation
from sync_stats import SyncStats

# Application layer - RE-ENABLED after memory optimizations
# Memory freed: ~160 bytes (const) + ~2KB (JSON removal) = ~2.16KB
# SessionManager + CalibrationController estimated cost: ~2.2KB (acceptable)
try:
    import gc
    gc.collect()
    mem_before_app_layer = gc.mem_free()
    print(f"[MEMORY] Before application layer: {mem_before_app_layer} bytes")

    from application.session.manager import SessionManager
    from application.calibration.controller import CalibrationController

    gc.collect()
    mem_after_app_layer = gc.mem_free()
    app_layer_cost = mem_before_app_layer - mem_after_app_layer

    print(f"[MEMORY] After application layer: {mem_after_app_layer} bytes")
    print(f"[MEMORY] Application layer cost: {app_layer_cost} bytes ({app_layer_cost / 1024:.2f} KB)")

    APPLICATION_LAYER_AVAILABLE = True
    print("[INFO] Application layer (SessionManager, CalibrationController) enabled successfully")

except (ImportError, MemoryError) as e:
    print(f"[WARNING] Application layer disabled due to: {e}")
    SessionManager = None
    CalibrationController = None
    APPLICATION_LAYER_AVAILABLE = False

# Utilities
from core.constants import DEBUG_ENABLED, DEVICE_TAG


# ============================================================================
# Temporary stubs for missing/consolidated classes
# ============================================================================

# Use SimpleSyncProtocol as SyncProtocol (aliasing)
SyncProtocol = sync.SimpleSyncProtocol

# Stub SyncCoordinator - minimal implementation for consolidated architecture
class SyncCoordinator:
    """Stub synchronization coordinator."""
    def __init__(self, role, sync_protocol, ble_service):
        self.role = role
        self.sync_protocol = sync_protocol
        self.ble_service = ble_service

    def update(self):
        """Stub update method."""
        pass

# Stub PatternGeneratorFactory - not used in consolidated architecture
class PatternGeneratorFactory:
    """Stub pattern generator factory."""
    @staticmethod
    def create(pattern_type, config):
        """Return None - consolidated TherapyEngine handles patterns internally."""
        return None

# Use LEDController for both boot and therapy modes (aliasing)
BootSequenceLEDController = LEDController
TherapyLEDController = LEDController


class BlueBuzzahApplication:
    """
    Main application orchestrator for BlueBuzzah v2 bilateral therapy system.

    This class implements the complete application lifecycle following clean
    architecture principles, with clear separation of concerns across layers:

    Layers (outer to inner):
        1. Presentation - LED UI, BLE command interface
        2. Application - Session management, command processing, calibration
        3. Domain - Therapy engine, synchronization protocol, patterns
        4. Infrastructure - BLE service, haptic drivers, battery monitoring
        5. Hardware - Board-specific peripherals, I/O

    Features:
        - Dependency injection for all components
        - Event-driven communication between layers
        - Explicit state machine for therapy sessions
        - Role-based behavior (PRIMARY vs SECONDARY)
        - Graceful error recovery and shutdown
        - Memory monitoring and optimization
        - Comprehensive logging

    Attributes:
        config: Base device configuration
        therapy_config: Therapy-specific configuration
        role: Device role (PRIMARY or SECONDARY)
        event_bus: Central event bus for decoupled communication
        state_machine: Therapy session state machine
        running: Application running flag

    Usage:
        >>> # Load configuration
        >>> device_config = load_device_config("settings.json")
        >>> therapy_config = load_therapy_profile("default")
        >>>
        >>> # Create and run application
        >>> app = BlueBuzzahApplication(device_config, therapy_config)
        >>> app.run()  # Synchronous - blocks until shutdown
    """

    def __init__(
        self,
        device_config,
        therapy_config):
        """
        Initialize the BlueBuzzah application.

        This constructor sets up all layers of the application architecture,
        wiring dependencies with direct callbacks.

        Args:
            device_config: Device configuration dictionary
            therapy_config: Therapy configuration dictionary

        Raises:
            RuntimeError: If initialization fails
        """
        self.config = device_config
        self.therapy_config = therapy_config
        self.role = device_config['role']
        self.running = False

        # Set device tag for print output
        global DEVICE_TAG
        DEVICE_TAG = config.get_device_tag(device_config)

        print(f"{DEVICE_TAG} Initializing BlueBuzzah v{FIRMWARE_VERSION}")
        print(f"{DEVICE_TAG} Device role: {self.role}")

        # Core systems
        self.state_machine= None

        # Hardware layer
        self.board_config= None
        self.led_pin= None
        self.haptic_controller= None
        self.battery_monitor= None
        self.i2c_multiplexer= None

        # Infrastructure layer - consolidated
        self.ble= None  # Consolidated BLE connection manager
        self.menu= None  # Consolidated menu/command handler

        # Domain layer
        self.sync_protocol= None
        self.sync_coordinator= None
        self.therapy_engine= None
        self.profile_manager= None

        # Application layer
        self.session_manager= None
        self.calibration_controller= None
        # command_processor removed - replaced by MenuController

        # Presentation layer
        self.boot_led_controller= None
        self.therapy_led_controller= None
        # PresentationCoordinator consolidated into main.py
        self.led_mode = "boot"  # "boot" or "therapy"
        self.message_queue=None

        # Boot sequence (consolidated - inlined from boot/manager.py)
        self.boot_result= None
        self.secondary_connection= None
        self.phone_connection= None
        self.primary_connection= None

        # CRITICAL-004: Sync statistics (SECONDARY only, for validation)
        if self.role == DeviceRole.SECONDARY:
            self.sync_stats = SyncStats(max_samples=200)
            self._stats_print_interval = 60  # Print every 60 seconds
            self._last_stats_print = time.monotonic()
            print(f"{DEVICE_TAG} Sync statistics enabled (validation mode)")
        else:
            self.sync_stats = None

        # Heartbeat protocol for detecting silent disconnections
        # PRIMARY sends heartbeats during therapy, SECONDARY detects timeout
        self.HEARTBEAT_INTERVAL_SEC = 2.0   # Send heartbeat every 2 seconds
        self.HEARTBEAT_TIMEOUT_SEC = 6.0    # 3 missed heartbeats = timeout
        self._last_heartbeat_sent = 0.0     # PRIMARY: timestamp of last heartbeat sent
        self._last_heartbeat_received = None  # SECONDARY: timestamp of last heartbeat received

        # Sequence tracking for command loss detection
        self._last_received_seq = -1
        self._missed_commands = 0

        # SYNC command timeout tracking (10s per architecture spec)
        # Detects stale sessions when PRIMARY stops sending commands but heartbeat continues
        self._last_sync_command_received = None
        self.SYNC_COMMAND_TIMEOUT_SEC = 10.0

        # Memory check tracking - check once per minute
        self._last_memory_check = 0

        # Initialize all layers
        self._initialize_core_systems()
        self._initialize_hardware()
        self._initialize_infrastructure()
        self._initialize_domain()
        self._initialize_application()
        self._initialize_presentation()

        print(f"{DEVICE_TAG} Initialization complete")

    def _initialize_core_systems(self):
        """
        Initialize core systems (state machine).

        This is the foundation that other layers depend on.
        """
        if DEBUG_ENABLED:
            print(f"{DEVICE_TAG} [DEBUG] Initializing core systems...")

        # State machine for therapy session management
        self.state_machine = TherapyStateMachine(initial_state=TherapyState.IDLE)
        if DEBUG_ENABLED:
            print(f"{DEVICE_TAG} [DEBUG] State machine initialized")

        # Wire state machine callback
        def on_state_change(transition):
            print(f"{DEVICE_TAG} State transition: {transition.from_state} → {transition.to_state} [{transition.trigger}]")

        self.state_machine.on_state_change(on_state_change)

    def _initialize_hardware(self):
        """
        Initialize hardware layer (board, LED, haptic, battery, I2C).

        This layer provides direct access to hardware peripherals.
        """
        if DEBUG_ENABLED:
            print(f"{DEVICE_TAG} [DEBUG] Initializing hardware layer...")

        try:
            # Board configuration
            self.board_config = BoardConfig()
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Board: {self.board_config.board_id}")

            # LED pin (store raw pin for LED controllers)
            self.led_pin = self.board_config.neopixel_pin
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] LED pin configured")

            # I2C multiplexer (for haptic controllers)
            self.i2c_multiplexer = I2CMultiplexer(
                i2c=self.board_config.i2c,
                address=self.board_config.tca9548a_address
            )
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] I2C multiplexer initialized")

            # Haptic controller
            self.haptic_controller = DRV2605Controller(
                multiplexer=self.i2c_multiplexer,
                actuator_type=self.therapy_config.get('actuator_type', 'LRA')
            )
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Haptic controller initialized")

            # Battery monitor
            self.battery_monitor = BatteryMonitor(
                adc_pin=self.board_config.battery_sense_pin,
                warning_voltage=self.therapy_config.get('battery_warning_voltage', 3.3),
                critical_voltage=self.therapy_config.get('battery_critical_voltage', 3.0)
            )
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Battery monitor initialized")

            # Log initial battery status
            battery_status = self.battery_monitor.get_status()
            print(f"{DEVICE_TAG} Battery: {battery_status}")

        except Exception as e:
            print(f"{DEVICE_TAG} [ERROR] Hardware initialization failed: {e}")
            raise RuntimeError(f"Hardware initialization failed: {e}")

    def _initialize_infrastructure(self):
        """
        Initialize infrastructure layer (BLE, protocol handler).

        This layer provides communication and storage services.
        """
        if DEBUG_ENABLED:
            print(f"{DEVICE_TAG} [DEBUG] Initializing infrastructure layer...")

        try:
            # BLE service - consolidated
            self.ble = BLE()
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] BLE initialized (consolidated)")

        except Exception as e:
            print(f"{DEVICE_TAG} [ERROR] Infrastructure initialization failed: {e}")
            raise RuntimeError(f"Infrastructure initialization failed: {e}")

    def _initialize_domain(self):
        """
        Initialize domain layer (therapy engine, sync protocol, patterns).

        This layer contains the core business logic.
        """
        if DEBUG_ENABLED:
            print(f"{DEVICE_TAG} [DEBUG] Initializing domain layer...")

        try:
            # Synchronization protocol for PRIMARY-SECONDARY coordination
            self.sync_protocol = SyncProtocol()
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Sync protocol initialized")

            # Synchronization coordinator
            self.sync_coordinator = SyncCoordinator(
                role=self.role,
                sync_protocol=self.sync_protocol,
                ble_service=self.ble  # Use consolidated BLE
            )
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Sync coordinator initialized")

            # Therapy engine (consolidated version uses callbacks, not constructor params)
            self.therapy_engine = TherapyEngine()
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Therapy engine initialized")

            # Wire therapy engine callbacks for haptic execution
            # Activate callback: finger_index -> activate_motor(finger, amplitude)
            def activate_haptic(finger_index, amplitude):
                try:
                    self.haptic_controller.activate(finger_index, amplitude)
                except Exception as e:
                    print(f"{DEVICE_TAG} [ERROR] Failed to activate finger {finger_index}: {e}")

            # Deactivate callback: finger_index -> deactivate_motor(finger)
            def deactivate_haptic(finger_index):
                try:
                    self.haptic_controller.deactivate(finger_index)
                except Exception as e:
                    print(f"{DEVICE_TAG} [ERROR] Failed to deactivate finger {finger_index}: {e}")

            # Send command callback (PRIMARY only): send sync commands via BLE to SECONDARY
            def send_sync_command(command_type, data):
                if self.role == DeviceRole.PRIMARY and self.secondary_connection:
                    try:
                        # Send sync command to SECONDARY via BLE
                        # Format: "SYNC:command_type:key1|val1|key2|val2"
                        data_str = ""
                        if data:
                            parts = []
                            for key, value in data.items():
                                parts.append(str(key))
                                parts.append(str(value))
                            data_str = "|".join(parts)

                        message = "SYNC:" + command_type + ":" + data_str
                        self.ble.send(self.secondary_connection, message)
                    except Exception as e:
                        if DEBUG_ENABLED:
                            print(f"{DEVICE_TAG} [DEBUG] Failed to send sync command: {e}")

            # Set callbacks on therapy engine
            self.therapy_engine.set_activate_callback(activate_haptic)
            self.therapy_engine.set_deactivate_callback(deactivate_haptic)
            if self.role == DeviceRole.PRIMARY:
                self.therapy_engine.set_send_command_callback(send_sync_command)

            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Therapy engine callbacks configured")

            # Initialize all DRV2605 haptic drivers (fingers 0-4)
            for finger_index in range(5):
                try:
                    self.haptic_controller.initialize_finger(finger_index)
                    if DEBUG_ENABLED:
                        print(f"{DEVICE_TAG} [DEBUG] Initialized haptic driver for finger {finger_index}")
                except Exception as e:
                    print(f"{DEVICE_TAG} [ERROR] Failed to initialize finger {finger_index}: {e}")

            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] All haptic drivers initialized")

            # CRITICAL: Turn OFF all tactors immediately after initialization
            # This ensures clean boot state for debugging and prevents residual activation
            # force_all=True ensures ALL motors are disabled regardless of tracked state
            print(f"{DEVICE_TAG} Disabling all tactors at boot...")
            self.haptic_controller.stop_all(force_all=True)
            print(f"{DEVICE_TAG} All tactors OFF")

            # Profile manager
            self.profile_manager = ProfileManager()
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Profile manager initialized")

        except Exception as e:
            print(f"{DEVICE_TAG} [ERROR] Domain initialization failed: {e}")
            raise RuntimeError(f"Domain initialization failed: {e}")

    def _initialize_application(self):
        """
        Initialize application layer (session, profile, calibration, commands).

        This layer orchestrates domain logic for specific use cases.
        """
        if DEBUG_ENABLED:
            print(f"{DEVICE_TAG} [DEBUG] Initializing application layer...")

        try:
            # Session manager with callbacks (optional - memory constrained)
            if APPLICATION_LAYER_AVAILABLE and SessionManager:
                # Create send_sync_callback for PRIMARY→SECONDARY synchronization
                def send_sync_to_secondary(command_type, data):
                    """Send sync command to SECONDARY device."""
                    # APP-003 FIX: Guard debug prints to avoid f-string allocations in hot path
                    if DEBUG_ENABLED:
                        print("{} [SYNC] send_sync_to_secondary() CALLED: type={}, data={}".format(DEVICE_TAG, command_type, data))

                    if self.role != DeviceRole.PRIMARY:
                        if DEBUG_ENABLED:
                            print("{} [SYNC] SKIPPED: Not PRIMARY (role={})".format(DEVICE_TAG, self.role))
                        return

                    if not self.secondary_connection:
                        if DEBUG_ENABLED:
                            print("{} [SYNC] SKIPPED: No SECONDARY connection".format(DEVICE_TAG))
                        return

                    try:
                        # Format: "SYNC:command_type:key1|val1|key2|val2"
                        data_str = ""
                        if data:
                            parts = []
                            for key, value in data.items():
                                parts.append(str(key))
                                parts.append(str(value))
                            data_str = "|".join(parts)

                        message = "SYNC:" + command_type + ":" + data_str
                        if DEBUG_ENABLED:
                            print("{} [SYNC] Sending: {}...".format(DEVICE_TAG, message[:80]))

                        result = self.ble.send(self.secondary_connection, message)

                        if DEBUG_ENABLED:
                            if result:
                                print("{} [SYNC] ble.send() SUCCESS".format(DEVICE_TAG))
                            else:
                                print("{} [SYNC] ble.send() FAILURE".format(DEVICE_TAG))
                    except Exception as e:
                        print("{} [SYNC] EXCEPTION: {}".format(DEVICE_TAG, e))
                        import traceback
                        traceback.print_exception(e, e, e.__traceback__)

                self.session_manager = SessionManager(
                    state_machine=self.state_machine,
                    on_session_started=self._on_session_started,
                    on_session_paused=self._on_session_paused,
                    on_session_resumed=self._on_session_resumed,
                    on_session_stopped=self._on_session_stopped,
                    therapy_engine=self.therapy_engine,
                    send_sync_callback=send_sync_to_secondary
                )
                if DEBUG_ENABLED:
                    print(f"{DEVICE_TAG} [DEBUG] Session manager initialized")
            else:
                if DEBUG_ENABLED:
                    print(f"{DEVICE_TAG} [DEBUG] Session manager disabled (memory)")

            # Calibration controller with callbacks (optional - memory constrained)
            if APPLICATION_LAYER_AVAILABLE and CalibrationController:
                self.calibration_controller = CalibrationController(
                    state_machine=self.state_machine,
                    on_calibration_started=self._on_calibration_started,
                    on_calibration_complete=self._on_calibration_complete,
                    haptic_controller=self.haptic_controller
                )
                if DEBUG_ENABLED:
                    print(f"{DEVICE_TAG} [DEBUG] Calibration controller initialized")
            else:
                if DEBUG_ENABLED:
                    print(f"{DEVICE_TAG} [DEBUG] Calibration controller disabled (memory)")

            # Menu controller - consolidated command handler
            # Set device name with -Secondary suffix for SECONDARY devices
            device_name = self.config.get('ble_name', 'BlueBuzzah')
            if self.role == DeviceRole.SECONDARY:
                device_name = device_name + "-Secondary"

            self.menu = MenuController(
                session_manager=self.session_manager,
                profile_manager=self.profile_manager,
                calibration_controller=self.calibration_controller,
                battery_monitor=self.battery_monitor,
                device_role=str(self.role),
                firmware_version=FIRMWARE_VERSION,
                device_name=device_name
            )
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Menu controller initialized (consolidated)")

        except Exception as e:
            print(f"{DEVICE_TAG} [ERROR] Application initialization failed: {e}")
            raise RuntimeError(f"Application initialization failed: {e}")

    def _initialize_presentation(self):
        """
        Initialize presentation layer (LED controllers, BLE interface coordinator).

        This layer handles user interaction and external communication.
        """
        if DEBUG_ENABLED:
            print(f"{DEVICE_TAG} [DEBUG] Initializing presentation layer...")

        try:
            # Single LED controller (used for both boot and therapy modes)
            # Since both are aliased to LEDController, we only need one instance
            led_controller = LEDController(pixel_pin=self.led_pin)
            self.boot_led_controller = led_controller
            self.therapy_led_controller = led_controller
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] LED controller initialized (shared for boot/therapy)")

            # Presentation layer (consolidated from presentation/coordinator.py)
            # Message queue for BLE commands/responses (simple list for CircuitPython)
            self.message_queue = []
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Presentation layer initialized (consolidated)")

        except Exception as e:
            print(f"{DEVICE_TAG} [ERROR] Presentation initialization failed: {e}")
            raise RuntimeError(f"Presentation initialization failed: {e}")

    # Callback handlers (called directly instead of via events)
    def _on_session_started(self, session_id, profile_name):
        """Handle session started."""
        # Debug: Track how many times this callback is executed
        import supervisor
        print(f"{DEVICE_TAG} Session started: {profile_name} (timestamp: {supervisor.ticks_ms()})")
        self.therapy_led_controller.set_therapy_state(TherapyState.RUNNING)

    def _on_session_paused(self, session_id):
        """Handle session paused."""
        print(f"{DEVICE_TAG} Session paused")
        self.therapy_led_controller.set_therapy_state(TherapyState.PAUSED)

    def _on_session_resumed(self, session_id):
        """Handle session resumed."""
        print(f"{DEVICE_TAG} Session resumed")
        self.therapy_led_controller.set_therapy_state(TherapyState.RUNNING)

    def _on_session_stopped(self, session_id, reason):
        """Handle session stopped."""
        print(f"{DEVICE_TAG} Session stopped: {reason}")
        self.therapy_led_controller.set_therapy_state(TherapyState.STOPPING)

    def _on_calibration_started(self, finger_index, glove_side):
        """Handle calibration started."""
        print(f"{DEVICE_TAG} Calibration started: {glove_side} finger {finger_index}")

    def _on_calibration_complete(self, finger_index, glove_side, passed):
        """Handle calibration complete."""
        status = "passed" if passed else "failed"
        print(f"{DEVICE_TAG} Calibration complete: {glove_side} finger {finger_index} - {status}")

    def _on_connection_established(self, connection_type, device_name):
        """Handle connection established."""
        print(f"{DEVICE_TAG} Connection established: {connection_type} - {device_name}")

    def _on_connection_lost(self, connection_type, reason):
        """Handle connection lost."""
        print(f"{DEVICE_TAG} [ERROR] Connection lost: {connection_type} - {reason}")
        if connection_type in ["primary", "secondary"]:
            # Critical connection lost - emergency shutdown
            self.therapy_led_controller.set_therapy_state(TherapyState.CONNECTION_LOST)
            self._emergency_shutdown("connection_lost")

    def on_battery_low(self, voltage):
        """Handle battery low condition (called by battery monitor)."""
        print(f"{DEVICE_TAG} [WARNING] Battery low: {voltage}V")
        self.therapy_led_controller.set_therapy_state(TherapyState.LOW_BATTERY)

    def on_battery_critical(self, voltage):
        """Handle battery critical condition (called by battery monitor)."""
        print(f"{DEVICE_TAG} [ERROR] Battery critical: {voltage}V - shutting down")
        self.therapy_led_controller.set_therapy_state(TherapyState.CRITICAL_BATTERY)
        # Emergency shutdown
        self._emergency_shutdown("battery_critical")

    # ============================================================================
    # Boot sequence methods (consolidated from boot/manager.py)
    # ============================================================================

    def _execute_boot_sequence(self):
        """
        Execute role-specific boot sequence.
        Consolidated from boot/manager.py - now part of main application.

        Returns:
            BootResult indicating success or failure
        """
        print(f"{DEVICE_TAG} Starting boot sequence")
        print(f"{DEVICE_TAG} Timeout: {self.config.get('startup_window_sec', 30)}s")

        try:
            if self.role == DeviceRole.PRIMARY:
                return self._primary_boot_sequence()
            else:
                return self._secondary_boot_sequence()
        except Exception as e:
            print(f"{DEVICE_TAG} [ERROR] Boot sequence failed: {e}")
            self.boot_led_controller.indicate_failure()
            return BootResult.FAILED

    def _primary_boot_sequence(self):
        """
        Execute PRIMARY device boot sequence.
        Consolidated from boot/manager.py.

        PRIMARY Boot Flow:
            1. Initialize BLE and advertise as 'BlueBuzzah'
            2. LED: Rapid blue flash during connection wait
            3. Wait for SECONDARY (required) + phone (optional)
            4. At timeout: Solid green if SECONDARY connected, red flash if failed

        Returns:
            BootResult (SUCCESS_WITH_PHONE, SUCCESS_NO_PHONE, or FAILED)
        """
        print(f"{DEVICE_TAG} PRIMARY: Beginning boot sequence")

        # Initialize BLE and start advertising
        self.boot_led_controller.indicate_ble_init()

        try:
            self.ble.advertise(self.config.get('ble_name', 'BlueBuzzah'))
        except Exception as e:
            print(f"{DEVICE_TAG} PRIMARY: ERROR - Failed to advertise: {e}")
            self.boot_led_controller.indicate_failure()
            return BootResult.FAILED

        print(f"{DEVICE_TAG} PRIMARY: Waiting for connections")

        # Simple polling with timeout
        start_time = time.monotonic()
        secondary_connected = False
        phone_connected = False

        while (time.monotonic() - start_time) < self.config.get('startup_window_sec', 30):
            # Update LED animations (flash and breathing)
            self.boot_led_controller.update_animation()

            # Check for SECONDARY connection
            if not secondary_connected:
                remaining_time = self.config.get('startup_window_sec', 30) - (time.monotonic() - start_time)
                conn = self.ble.wait_for_connection("secondary", timeout=min(0.02, remaining_time))
                if conn:
                    self.secondary_connection = conn
                    secondary_connected = True
                    print(f"{DEVICE_TAG} PRIMARY: SECONDARY connected")
                    # Show connection success briefly
                    self.boot_led_controller.indicate_connection_success()
                    time.sleep(0.5)
                    # Switch to waiting for phone (2Hz blue blink)
                    self.boot_led_controller.indicate_waiting_for_phone()
                    print(f"{DEVICE_TAG} PRIMARY: Waiting for phone...")

            # Check for phone connection
            if not phone_connected and secondary_connected:
                remaining_time = self.config.get('startup_window_sec', 30) - (time.monotonic() - start_time)
                conn = self.ble.wait_for_connection("phone", timeout=min(0.02, remaining_time))
                if conn:
                    self.phone_connection = conn
                    phone_connected = True
                    print(f"{DEVICE_TAG} PRIMARY: Phone connected")

            # If both connected, finish early
            if secondary_connected and phone_connected:
                break

            time.sleep(0.01)

        # Determine result
        if not secondary_connected:
            print(f"{DEVICE_TAG} PRIMARY: FAILED - SECONDARY did not connect")
            self.boot_led_controller.indicate_failure()
            return BootResult.FAILED

        # Boot complete - show solid blue
        self.boot_led_controller.indicate_ready()

        if phone_connected:
            print(f"{DEVICE_TAG} PRIMARY: SUCCESS - SECONDARY and phone connected")
            return BootResult.SUCCESS_WITH_PHONE
        else:
            print(f"{DEVICE_TAG} PRIMARY: SUCCESS - SECONDARY connected (no phone)")
            return BootResult.SUCCESS_NO_PHONE

    def _secondary_boot_sequence(self):
        """
        Execute SECONDARY device boot sequence.
        Consolidated from boot/manager.py.

        SECONDARY Boot Flow:
            1. Initialize BLE and scan for 'BlueBuzzah'
            2. LED: Rapid blue flash during scanning
            3. Connect to PRIMARY within timeout
            4. On connection: 5x green flash, solid green
            5. On timeout: Slow red flash

        Returns:
            BootResult (SUCCESS or FAILED)
        """
        print(f"{DEVICE_TAG} SECONDARY: Beginning boot sequence")

        # Initialize BLE for scanning
        self.boot_led_controller.indicate_ble_init()

        # Set SECONDARY's own identity name with suffix
        secondary_name = self.config.get('ble_name', 'BlueBuzzah') + "-Secondary"
        self.ble.set_identity_name(secondary_name)

        print(f"{DEVICE_TAG} SECONDARY: Scanning for PRIMARY")

        # Simple polling with timeout
        start_time = time.monotonic()

        while (time.monotonic() - start_time) < self.config.get('startup_window_sec', 30):
            # Update LED flash animation
            self.boot_led_controller.update_flash()

            try:
                # Attempt to scan and connect
                scan_timeout = min(0.02, self.config.get('startup_window_sec', 30) - (time.monotonic() - start_time))
                connection = self.ble.scan_and_connect(
                    self.config.get('ble_name', 'BlueBuzzah'),
                    timeout=scan_timeout
                )

                if connection:
                    # Connection successful
                    print(f"{DEVICE_TAG} SECONDARY: Connected to PRIMARY!")
                    self.primary_connection = connection

                    # Show success feedback
                    self.boot_led_controller.indicate_connection_success()
                    time.sleep(0.5)
                    self.boot_led_controller.indicate_ready()

                    print(f"{DEVICE_TAG} SECONDARY: SUCCESS")
                    return BootResult.SUCCESS

            except Exception as e:
                print(f"{DEVICE_TAG} SECONDARY: Scan error: {e}")

            time.sleep(0.01)

        # Timeout - connection failed
        print(f"{DEVICE_TAG} SECONDARY: FAILED - Timeout without PRIMARY connection")
        self.boot_led_controller.indicate_failure()
        return BootResult.FAILED

    # ============================================================================
    # Main application loop
    # ============================================================================

    def run(self):
        """
        Main application loop.

        This method:
        1. Executes boot sequence
        2. Enters main application loop
        3. Handles graceful shutdown

        Raises:
            RuntimeError: If critical error occurs during execution
        """
        print(f"{DEVICE_TAG} Starting BlueBuzzah application...")
        self.running = True

        try:
            # Execute boot sequence (consolidated from boot/manager.py)
            if SKIP_BOOT_SEQUENCE:
                print(f"{DEVICE_TAG} [DEV MODE] Skipping boot sequence")
                self.boot_result = BootResult.SUCCESS_NO_PHONE
            else:
                self.boot_result = self._execute_boot_sequence()

                if self.boot_result == BootResult.FAILED:
                    print(f"{DEVICE_TAG} [ERROR] Boot sequence failed - halting")
                    self.boot_led_controller.indicate_failure()
                    return

            print(f"{DEVICE_TAG} Boot complete: {self.boot_result}")

            # Transition to therapy LED mode (consolidated from coordinator.start())
            self._switch_to_therapy_led()

            # Subscribe to state changes for LED updates
            self.state_machine.on_state_change(self._on_state_change_led)

            # Determine post-boot behavior
            if self.role == DeviceRole.PRIMARY:
                self._run_primary_loop()
            else:
                self._run_secondary_loop()

        except KeyboardInterrupt:
            print(f"{DEVICE_TAG} Keyboard interrupt - shutting down")
        except Exception as e:
            print(f"{DEVICE_TAG} [ERROR] Fatal error: {e}")
            raise
        finally:
            self._shutdown()

    def _run_primary_loop(self):
        """
        Main loop for PRIMARY device.

        PRIMARY responsibilities:
        - Wait for phone commands (if phone connected)
        - Start default therapy profile (if no phone)
        - Coordinate with SECONDARY
        - Monitor battery and connections
        """
        print(f"{DEVICE_TAG} Entering PRIMARY main loop")

        # If phone connected, wait for commands
        if self.boot_result == BootResult.SUCCESS_WITH_PHONE:
            print(f"{DEVICE_TAG} Phone connected - waiting for commands")
            self._wait_for_commands()

        # If no phone, start default therapy
        elif self.boot_result == BootResult.SUCCESS_NO_PHONE:
            print(f"{DEVICE_TAG} No phone - starting default therapy profile")
            self._start_default_therapy()

        # Main processing loop
        while self.running:
            # Update therapy engine (execute patterns, timing, motor control)
            if self.therapy_engine:
                self.therapy_engine.update()

            # Heartbeat: Send periodic heartbeat to SECONDARY during therapy
            if self.secondary_connection and self.therapy_engine and self.therapy_engine.is_running():
                now = time.monotonic()
                if now - self._last_heartbeat_sent >= self.HEARTBEAT_INTERVAL_SEC:
                    try:
                        ts = int(time.monotonic_ns() // 1000)  # Microseconds
                        self.ble.send(self.secondary_connection, f"SYNC:HEARTBEAT:ts|{ts}")
                        self._last_heartbeat_sent = now
                    except Exception as e:
                        print(f"{DEVICE_TAG} [WARN] Failed to send heartbeat: {e}")

            # Process BLE commands (consolidated from coordinator.update())
            self._process_incoming_ble_messages()
            self._process_message_queue()

            # Update LED based on current state
            self._update_led_state()

            # Monitor battery
            self._check_battery()

            # Periodic garbage collection and memory monitoring
            now = time.monotonic()
            if now - self._last_memory_check >= 60:
                self._last_memory_check = now
                gc.collect()
                free_mem = gc.mem_free()
                print("[MEMORY] Free: {} bytes ({} KB)".format(free_mem, free_mem / 1024))

                # Memory warnings
                if free_mem < 10000:  # Less than 10KB
                    print("[WARNING] Low memory! Free < 10KB")
                elif free_mem < 5000:  # Less than 5KB
                    print("[CRITICAL] Very low memory! Free < 5KB")

            # Brief sleep to avoid busy-waiting (cooperative multitasking)
            time.sleep(0.05)  # 20Hz update rate
            # Yield to system tasks (BLE, USB)
            time.sleep(0)

    def _run_secondary_loop(self):
        """
        Main loop for SECONDARY device.

        SECONDARY responsibilities:
        - Listen for PRIMARY sync commands
        - Execute therapy in coordination with PRIMARY
        - Monitor battery and connection
        """
        print(f"{DEVICE_TAG} Entering SECONDARY main loop")

        # SECONDARY waits for PRIMARY commands
        print(f"{DEVICE_TAG} Waiting for PRIMARY sync commands")

        # Main processing loop
        while self.running:
            # CRITICAL FIX: SECONDARY is a FOLLOWER - do NOT run local TherapyEngine
            # Motor activations come exclusively from PRIMARY's EXECUTE_BUZZ commands
            # Running therapy_engine.update() here caused dual-activation conflicts:
            # - SECONDARY generated its own patterns (different from PRIMARY)
            # - SECONDARY also received EXECUTE_BUZZ and activated again
            # - Result: Motors fired twice per cycle with conflicting sequences
            # if self.therapy_engine:
            #     self.therapy_engine.update()  # DISABLED - causes sync conflict

            # CRITICAL: Process incoming BLE messages from PRIMARY
            # This is how SECONDARY receives EXECUTE_BUZZ commands
            self._process_incoming_ble_messages()

            # Heartbeat: Check for timeout during active therapy session
            if self._last_heartbeat_received is not None:
                elapsed = time.monotonic() - self._last_heartbeat_received
                if elapsed > self.HEARTBEAT_TIMEOUT_SEC:
                    print(f"{DEVICE_TAG} [ERROR] Heartbeat timeout ({elapsed:.1f}s) - PRIMARY connection lost!")
                    self._handle_heartbeat_timeout()

            # SYNC command timeout check - warns if PRIMARY stops sending commands during session
            # Heartbeat timeout (6s) handles connection loss; this detects stale sessions
            if self._last_sync_command_received is not None:
                sync_elapsed = time.monotonic() - self._last_sync_command_received
                if sync_elapsed > self.SYNC_COMMAND_TIMEOUT_SEC:
                    current_state = self.state_machine.get_current_state()
                    if current_state == TherapyState.RUNNING:
                        print("{} [WARN] No SYNC command for {:.1f}s (timeout: {}s)".format(
                            DEVICE_TAG, sync_elapsed, self.SYNC_COMMAND_TIMEOUT_SEC))
                        # Reset timestamp to avoid repeated warnings
                        self._last_sync_command_received = time.monotonic()

            # Update LED based on current state
            self._update_led_state()

            # Monitor battery
            self._check_battery()

            # Periodic garbage collection and memory monitoring
            now = time.monotonic()
            if now - self._last_memory_check >= 60:
                self._last_memory_check = now
                gc.collect()
                free_mem = gc.mem_free()
                print("[MEMORY] Free: {} bytes ({} KB)".format(free_mem, free_mem / 1024))

                # Memory warnings
                if free_mem < 10000:  # Less than 10KB
                    print("[WARNING] Low memory! Free < 10KB")
                elif free_mem < 5000:  # Less than 5KB
                    print("[CRITICAL] Very low memory! Free < 5KB")

            # CRITICAL-004: Periodic sync statistics reporting (SECONDARY only)
            if self.sync_stats:
                now = time.monotonic()
                if now - self._last_stats_print >= self._stats_print_interval:
                    self.sync_stats.print_report()
                    self._last_stats_print = now

            # Brief sleep to avoid busy-waiting (cooperative multitasking)
            time.sleep(0.05)  # 20Hz update rate
            # Yield to system tasks (BLE, USB)
            time.sleep(0)

    def _wait_for_commands(self):
        """
        Wait for commands from phone app.

        PRIMARY only - enters ready state and waits for user commands.
        """
        self.state_machine.transition(StateTrigger.CONNECTED)
        print(f"{DEVICE_TAG} Ready for commands")

    def _start_default_therapy(self):
        """
        Start default therapy profile.

        PRIMARY only - automatically starts therapy if no phone connected.
        """
        print(f"{DEVICE_TAG} Starting default therapy profile")

        # Consolidation: Load default profile directly from ProfileManager
        default_profile = self.profile_manager.get_default_profile()

        # Start session with the profile's config (if available)
        if self.session_manager:
            self.session_manager.start_session(default_profile.config)
        else:
            print(f"{DEVICE_TAG} [WARNING] Session manager not available, skipping session start")

    def _check_battery(self):
        """
        Periodic battery monitoring.

        Calls callbacks directly if thresholds crossed.
        """
        status = self.battery_monitor.get_status()

        if status.is_critical and not self.state_machine.get_current_state().is_error():
            # Call critical battery handler directly
            self.on_battery_critical(status.voltage)

        elif status.is_low and self.state_machine.get_current_state() == TherapyState.RUNNING:
            # Call low battery handler directly
            self.on_battery_low(status.voltage)

    # ============================================================================
    # Presentation layer methods (consolidated from presentation/coordinator.py)
    # ============================================================================

    def _switch_to_therapy_led(self):
        """
        Switch from boot LED to therapy LED controller.
        Consolidated from coordinator._switch_to_therapy_led()
        """
        print(f"{DEVICE_TAG} Switching to therapy LED mode")

        # Switch to therapy LED mode
        self.led_mode = "therapy"

        # Set initial therapy LED state
        # For SECONDARY: keep solid blue (READY state) after successful boot
        # For PRIMARY: use current state from state machine
        if self.role == DeviceRole.SECONDARY and self.boot_result == BootResult.SUCCESS:
            self.therapy_led_controller.set_therapy_state(TherapyState.READY)
        else:
            current_state = self.state_machine.get_current_state()
            self.therapy_led_controller.set_therapy_state(current_state)

    def _update_led_state(self):
        """
        Update LED state based on current therapy state.
        Consolidated from coordinator._update_led_state()
        """
        if self.led_mode != "therapy":
            return

        current_state = self.state_machine.get_current_state()

        # Only update if state changed or if in breathing mode
        if current_state == TherapyState.RUNNING:
            # Update breathing effect continuously
            self.therapy_led_controller.update_breathing()
        else:
            # Set static state (paused, stopping, error, etc.)
            self.therapy_led_controller.set_therapy_state(current_state)

    def _on_state_change_led(self, transition):
        """
        Handle therapy state changes for LED updates.
        Consolidated from coordinator._on_state_change()
        """
        if self.led_mode == "therapy":
            self.therapy_led_controller.set_therapy_state(transition.to_state)

    def _process_incoming_ble_messages(self):
        """
        Process incoming BLE messages from phone or PRIMARY.
        Consolidated from coordinator._process_incoming_messages()
        """
        # Check phone connection for commands
        if self.phone_connection and self.ble.is_connected(self.phone_connection):
            self._process_connection_messages(self.phone_connection, "phone")

        # Check PRIMARY connection for sync commands (SECONDARY only)
        if self.primary_connection:
            # Debug logging for SECONDARY
            if self.role == DeviceRole.SECONDARY:
                is_conn = self.ble.is_connected(self.primary_connection)
                print(f"{DEVICE_TAG} [POLL] PRIMARY connection check: conn_id='{self.primary_connection}', is_connected={is_conn}")

            if self.ble.is_connected(self.primary_connection):
                self._process_connection_messages(self.primary_connection, "primary")

        # Check SECONDARY connection for responses (PRIMARY only)
        if self.secondary_connection and self.ble.is_connected(self.secondary_connection):
            self._process_connection_messages(self.secondary_connection, "secondary")

    def _process_connection_messages(self, connection, connection_type):
        """
        Process messages from a specific BLE connection.
        Consolidated from coordinator._process_connection_messages()
        """
        try:
            # CRITICAL-003 FIX: Removed high-frequency polling logs (executed 20Hz)
            # Lines 1048-1060 removed to eliminate f-string allocations in hot path

            # Receive data with short timeout (non-blocking)
            message = self.ble.receive(connection, timeout=0.01)

            if message is None:
                return  # No data available

            # Log message receipt (only when data actually received)
            print(f"{DEVICE_TAG} [RX] Received from {connection_type}: '{message[:100]}...'")

            # CRITICAL FIX: Handle SYNC commands from PRIMARY (SECONDARY only)
            if connection_type == "primary" and message.startswith("SYNC:"):
                self._handle_sync_command(message)
                return  # Don't queue response for sync commands

            # Handle command with consolidated menu controller
            response = self.menu.handle_command(message)

            # Queue response for sending (simple list for CircuitPython)
            self.message_queue.append((connection, response))
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [RX] Response queued: '{response[:100]}...'")

        except Exception as e:
            print(f"{DEVICE_TAG} [RX] EXCEPTION processing {connection_type} message: {e}")
            import traceback
            traceback.print_exception(e, e, e.__traceback__)

    def _handle_sync_command(self, message):
        """
        Handle SYNC command from PRIMARY device (SECONDARY only).

        SYNC command format: "SYNC:command_type:key1|val1|key2|val2"

        Args:
            message: Raw SYNC message string
        """
        # CRITICAL-004: Capture receive timestamp immediately for latency measurement
        t_receive = time.monotonic_ns()

        # APP-003 FIX: Guard debug prints to avoid f-string allocations in hot path
        if DEBUG_ENABLED:
            print("{} [SYNC_RX] Received: {}".format(DEVICE_TAG, message[:60]))

        try:
            # Parse: "SYNC:command_type:data_str"
            parts = message.strip().split(':', 2)

            if len(parts) < 2:
                # Keep error print but convert to .format() to avoid f-string overhead
                print("{} [SYNC_RX] ERROR: Invalid SYNC format: {}".format(DEVICE_TAG, message[:40]))
                return

            command_type = parts[1]
            data_str = parts[2] if len(parts) == 3 else ""

            # Update SYNC command timestamp for timeout detection
            self._last_sync_command_received = time.monotonic()

            if DEBUG_ENABLED:
                print("{} [SYNC_RX] Parsed: cmd={}, data={}".format(DEVICE_TAG, command_type, data_str[:40]))

            # Parse data (pipe-delimited key|value pairs)
            data = {}
            command_timestamp = None
            if data_str:
                data_parts = data_str.split('|')
                for i in range(0, len(data_parts), 2):
                    if i + 1 < len(data_parts):
                        key = data_parts[i]
                        value = data_parts[i + 1]
                        # Try to convert to int
                        try:
                            value = int(value)
                        except ValueError:
                            pass
                        data[key] = value
                        # Capture timestamp if present
                        if key == 'timestamp':
                            command_timestamp = value

            # Handle command types
            if command_type == "EXECUTE_BUZZ":
                # Sequence gap detection for command loss monitoring
                seq = data.get('seq', -1)
                if seq >= 0:
                    if seq > self._last_received_seq + 1 and self._last_received_seq >= 0:
                        gap = seq - self._last_received_seq - 1
                        self._missed_commands += gap
                        # Keep warning for operational visibility but use .format()
                        print("{} [WARN] Sequence gap: missed {} cmds".format(DEVICE_TAG, gap))
                    self._last_received_seq = seq

                # CRITICAL-004: Calculate network latency if timestamp available
                network_latency_us = 0
                if command_timestamp and self.sync_stats:
                    t_receive_us = t_receive // 1000  # Convert ns to µs
                    network_latency_us = t_receive_us - command_timestamp
                    if DEBUG_ENABLED:
                        print("[SYNC_TIMING] Network latency: {} us".format(network_latency_us))

                # Execute haptic activation on SECONDARY
                left_finger = data.get('left_finger', 0)
                right_finger = data.get('right_finger', 0)
                amplitude = data.get('amplitude', 100)

                if DEBUG_ENABLED:
                    print(f"{DEVICE_TAG} [DEBUG] SYNC: Activate fingers L{left_finger}/R{right_finger} @ {amplitude}%")

                # CRITICAL-004: Capture execution start timestamp
                t_exec_start = time.monotonic_ns()

                # Activate local motors on SECONDARY
                try:
                    self.haptic_controller.activate(left_finger, amplitude)
                except Exception as e:
                    print(f"{DEVICE_TAG} [ERROR] Failed to activate finger {left_finger}: {e}")

                try:
                    self.haptic_controller.activate(right_finger, amplitude)
                except Exception as e:
                    print(f"{DEVICE_TAG} [ERROR] Failed to activate finger {right_finger}: {e}")

                # CRITICAL-004: Calculate timing statistics
                if self.sync_stats:
                    t_exec_complete = time.monotonic_ns()
                    exec_time_us = (t_exec_complete - t_exec_start) // 1000
                    total_latency_us = (t_exec_complete - t_receive) // 1000

                    print(f"[SYNC_TIMING] Execution time: {exec_time_us} µs ({exec_time_us / 1000:.2f} ms)")
                    print(f"[SYNC_TIMING] Total latency: {total_latency_us} µs ({total_latency_us / 1000:.2f} ms)")

                    # Add sample to statistics
                    self.sync_stats.add_sample(network_latency_us, exec_time_us, total_latency_us)

            elif command_type == "DEACTIVATE":
                # Deactivate motors
                left_finger = data.get('left_finger', 0)
                right_finger = data.get('right_finger', 0)

                try:
                    self.haptic_controller.deactivate(left_finger)
                    self.haptic_controller.deactivate(right_finger)
                except Exception as e:
                    print(f"{DEVICE_TAG} [ERROR] Failed to deactivate: {e}")

            elif command_type == "START_SESSION":
                # SECONDARY enters therapy mode as FOLLOWER
                # CRITICAL: Do NOT start local TherapyEngine - SECONDARY only responds to EXECUTE_BUZZ
                print(f"{DEVICE_TAG} [SYNC] Received START_SESSION from PRIMARY")

                # Log session config for reference (but don't use it to start local engine)
                duration_sec = data.get('duration_sec', 7200)
                pattern_type = data.get('pattern_type', 'rndp')
                jitter_int = data.get('jitter_percent', 235)
                jitter_percent = float(jitter_int) / 10.0
                print(f"{DEVICE_TAG} [DEBUG] Session config: duration={duration_sec}s, pattern={pattern_type}, jitter={jitter_percent}%")

                # Update state machine to RUNNING to match PRIMARY
                # This enables LED feedback and state tracking, but NO local pattern generation
                self.state_machine.transition(StateTrigger.START_SESSION)

                # Update LED to show therapy active
                self.therapy_led_controller.set_therapy_state(TherapyState.RUNNING)

                print(f"{DEVICE_TAG} SECONDARY: Entered therapy mode (follower - awaiting EXECUTE_BUZZ commands)")

            elif command_type == "PAUSE_SESSION":
                # Pause therapy session on SECONDARY
                if DEBUG_ENABLED:
                    print(f"{DEVICE_TAG} [DEBUG] SYNC: Pause session")

                if self.therapy_engine:
                    self.therapy_engine.pause()
                    # Update state machine to match PRIMARY
                    self.state_machine.transition(StateTrigger.PAUSE_SESSION)
                    print(f"{DEVICE_TAG} SECONDARY: Therapy session paused")

            elif command_type == "RESUME_SESSION":
                # Resume therapy session on SECONDARY
                if DEBUG_ENABLED:
                    print(f"{DEVICE_TAG} [DEBUG] SYNC: Resume session")

                if self.therapy_engine:
                    self.therapy_engine.resume()
                    # Update state machine to match PRIMARY
                    self.state_machine.transition(StateTrigger.RESUME_SESSION)
                    print(f"{DEVICE_TAG} SECONDARY: Therapy session resumed")

            elif command_type == "STOP_SESSION":
                # Stop therapy session on SECONDARY
                if DEBUG_ENABLED:
                    print(f"{DEVICE_TAG} [DEBUG] SYNC: Stop session")

                if self.therapy_engine:
                    self.therapy_engine.stop()
                    # Update state machine to match PRIMARY
                    self.state_machine.transition(StateTrigger.STOP_SESSION)
                    self.state_machine.transition(StateTrigger.STOPPED)
                    print(f"{DEVICE_TAG} SECONDARY: Therapy session stopped")

            elif command_type == "HEARTBEAT":
                # Heartbeat from PRIMARY - update last received timestamp
                self._last_heartbeat_received = time.monotonic()
                # Optionally log timestamp from PRIMARY for drift analysis
                if DEBUG_ENABLED:
                    ts = data.get('ts', 0)
                    print(f"{DEVICE_TAG} [DEBUG] Heartbeat received (PRIMARY ts: {ts})")

            else:
                if DEBUG_ENABLED:
                    print(f"{DEVICE_TAG} [DEBUG] Unknown SYNC command: {command_type}")

        except Exception as e:
            print(f"{DEVICE_TAG} [ERROR] Failed to handle SYNC command: {e}")

    def _process_message_queue(self):
        """
        Process outgoing message queue.
        Consolidated from coordinator._process_message_queue()
        """
        try:
            # Process up to 5 messages per update (avoid blocking)
            for _ in range(5):
                if not self.message_queue:  # Check if list is empty
                    break

                # Get message from queue (pop from front)
                connection, data = self.message_queue.pop(0)

                # Send message
                self.ble.send(connection, data)

        except Exception as e:
            if DEBUG_ENABLED:
                print(f"{DEVICE_TAG} [DEBUG] Error processing message queue: {e}")

    # ============================================================================
    # Shutdown and emergency methods
    # ============================================================================

    def _handle_heartbeat_timeout(self):
        """
        Handle heartbeat timeout on SECONDARY device.

        Called when SECONDARY hasn't received a heartbeat from PRIMARY within
        the timeout period. This indicates PRIMARY may have disconnected silently
        (without triggering a BLE disconnect event).

        Actions:
        1. Emergency stop all motors (safety)
        2. Update state machine to CONNECTION_LOST
        3. Update LED to indicate connection lost
        4. Reset heartbeat tracking
        5. Attempt reconnection to PRIMARY
        """
        print(f"{DEVICE_TAG} [RECOVERY] Heartbeat timeout - entering recovery mode")

        # 1. Emergency stop all motors for safety
        if self.haptic_controller:
            try:
                self.haptic_controller.emergency_stop()
                print(f"{DEVICE_TAG} [RECOVERY] Motors stopped")
            except Exception as e:
                print(f"{DEVICE_TAG} [ERROR] Failed to stop motors: {e}")

        # 2. Update state machine
        self.state_machine.force_state(TherapyState.CONNECTION_LOST, reason="heartbeat_timeout")

        # 3. Update LED
        self.therapy_led_controller.set_therapy_state(TherapyState.CONNECTION_LOST)

        # 4. Reset heartbeat tracking
        self._last_heartbeat_received = None

        # 5. Attempt reconnection
        self._attempt_reconnect_to_primary()

    def _attempt_reconnect_to_primary(self):
        """
        Attempt to reconnect to PRIMARY device after connection loss.

        Makes multiple attempts to scan and reconnect. If successful,
        returns to READY state. If all attempts fail, enters IDLE state.

        Returns:
            bool: True if reconnection successful, False otherwise
        """
        MAX_ATTEMPTS = 3
        DELAY_SEC = 2.0

        print(f"{DEVICE_TAG} [RECOVERY] Starting reconnection attempts to PRIMARY")

        for attempt in range(MAX_ATTEMPTS):
            print(f"{DEVICE_TAG} [RECOVERY] Attempt {attempt + 1}/{MAX_ATTEMPTS}")

            # Update LED to show scanning
            self.therapy_led_controller.indicate_ble_init()

            try:
                # Scan for PRIMARY device
                connection = self.ble.scan_and_connect(
                    self.config.get('ble_name', 'BlueBuzzah'),
                    timeout=10.0,
                    conn_id="primary"
                )

                if connection:
                    self.primary_connection = connection
                    self.state_machine.force_state(TherapyState.READY)
                    self.therapy_led_controller.indicate_ready()
                    print(f"{DEVICE_TAG} [RECOVERY] Reconnected to PRIMARY successfully!")
                    return True

            except Exception as e:
                print(f"{DEVICE_TAG} [RECOVERY] Attempt {attempt + 1} failed: {e}")

            # Wait before next attempt
            time.sleep(DELAY_SEC)

        # All attempts failed
        print(f"{DEVICE_TAG} [RECOVERY] Failed to reconnect after {MAX_ATTEMPTS} attempts")
        self.state_machine.force_state(TherapyState.IDLE)
        self.therapy_led_controller.indicate_failure()
        return False

    def _emergency_shutdown(self, reason):
        """
        Emergency shutdown due to critical condition.

        Args:
            reason: Reason for emergency shutdown
        """
        print(f"{DEVICE_TAG} [ERROR] EMERGENCY SHUTDOWN: {reason}")

        # Stop therapy immediately
        if self.session_manager:
            self.session_manager.emergency_stop()

        # Stop all motors
        if self.haptic_controller:
            self.haptic_controller.emergency_stop()

        # Set error state
        self.state_machine.force_state(TherapyState.ERROR, reason=reason)

        # Stop application
        self.running = False

    def _shutdown(self):
        """
        Graceful application shutdown.

        Cleans up resources and stops all components.
        """
        print(f"{DEVICE_TAG} Shutting down BlueBuzzah application...")

        self.running = False

        try:
            # Disconnect BLE connections (consolidated)
            if self.phone_connection:
                self.ble.disconnect(self.phone_connection)
                self.phone_connection = None

            if self.secondary_connection:
                self.ble.disconnect(self.secondary_connection)
                self.secondary_connection = None

            if self.primary_connection:
                self.ble.disconnect(self.primary_connection)
                self.primary_connection = None

            # Stop session if active
            if self.session_manager and self.state_machine.get_current_state().is_active():
                self.session_manager.stop_session()

            # Stop all motors
            if self.haptic_controller:
                self.haptic_controller.stop_all()

            # Turn off LED
            if self.boot_led_controller:
                self.boot_led_controller.off()

            # Disconnect BLE
            if self.ble:
                self.ble.stop_advertising()

            print(f"{DEVICE_TAG} Shutdown complete")

        except Exception as e:
            print(f"{DEVICE_TAG} [ERROR] Error during shutdown: {e}")

    def get_status(self):
        """
        Get current application status.

        Returns:
            Dictionary with comprehensive status information
        """
        return {
            "firmware_version": FIRMWARE_VERSION,
            "role": str(self.role),
            "running": self.running,
            "state": str(self.state_machine.get_current_state()),
            "boot_result": str(self.boot_result) if self.boot_result else None,
            "battery": str(self.battery_monitor.get_status()) if self.battery_monitor else None,
            "connections": {
                "secondary": self.secondary_connection is not None,
                "phone": self.phone_connection is not None,
                "primary": self.primary_connection is not None,
            },
        }

    def __repr__(self):
        """String representation for logging."""
        return f"BlueBuzzahApplication(role={self.role}, state={self.state_machine.get_current_state()})"
