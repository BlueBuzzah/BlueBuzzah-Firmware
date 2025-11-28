"""
Application Layer for BlueBuzzah v2
====================================

This package contains the application layer components that coordinate between
the domain logic and presentation/infrastructure layers.

Components:
    - session: Session management (tracker consolidated into SessionManager)
    - calibration: Calibration mode controller
    - commands: Command processing and handling

Note (Phase 3 consolidation):
    - ProfileManager: src/profiles.py
    - TherapyEngine: src/therapy.py
    - LEDController: src/led.py
    - SessionTracker merged into SessionManager
    - BootSequenceManager inlined into main.py
    - PresentationCoordinator inlined into main.py

The application layer is responsible for:
    - Orchestrating use cases and workflows
    - Managing session lifecycle
    - Coordinating between domain and infrastructure
    - Processing and routing commands

Example:
    from application.session import SessionManager
    from profiles import ProfileManager
    from application.commands import CommandProcessor

    # Initialize managers
    session_mgr = SessionManager(therapy_engine)
    profile_mgr = ProfileManager()
    cmd_processor = CommandProcessor(session_mgr, profile_mgr)

    # Process commands
    response = cmd_processor.process_command(command)
"""

__version__ = "2.0.0"

# Re-export main application components
# Lazy imports to avoid memory issues on CircuitPython
# Import only when actually needed

__all__ = [
    "SessionManager",
    "SessionRecord",
    "SessionStatistics",
    "CalibrationController",
]

# Don't eagerly import - let importing modules handle it
# This reduces memory footprint during initial load

# To use these components:
#   from application.session.manager import SessionManager
#   from application.calibration.controller import CalibrationController

# TODO: Commands module not yet implemented
# from .commands.processor import CommandProcessor
# from .commands.handlers import (...)
