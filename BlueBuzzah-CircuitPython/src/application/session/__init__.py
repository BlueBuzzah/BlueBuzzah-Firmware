"""
Session Management Module
=========================

This module provides session management functionality for therapy sessions.

Components:
    - SessionManager: Manages session lifecycle and tracking (tracker consolidated)
    - SessionRecord: Record of completed session
    - SessionStatistics: Aggregate statistics

Note:
    SessionTracker functionality has been consolidated into SessionManager.
"""

from application.session.manager import SessionManager, SessionRecord, SessionStatistics

__all__ = ["SessionManager", "SessionRecord", "SessionStatistics"]
