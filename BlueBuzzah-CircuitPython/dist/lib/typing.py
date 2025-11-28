"""
Dummy typing module for CircuitPython compatibility.
Type hints are ignored at runtime, so we just provide no-op stubs.
"""

def _noop(*args, **kwargs):
    """No-op function that returns the first argument or None."""
    return args[0] if args else None

# Common typing constructs used in the codebase
Optional = _noop
Dict = _noop
List = _noop
Tuple = _noop
Any = _noop
Callable = _noop
Union = _noop
