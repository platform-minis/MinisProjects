"""
conftest.py - pytest fixtures for host integration tests.

Provides a 'device' fixture that returns either:
  - A real hardware adapter (if PICO_PORT env var is set)
  - The software Simulator (fallback for CI without hardware)

The device adapter exposes the same interface as Simulator.command().
"""

from __future__ import annotations

import os
import pytest
from simulator import Simulator, PinState, EdgeType


# ---------------------------------------------------------------------------
# Hardware adapter (real Pico 2 over USB-CDC)
# ---------------------------------------------------------------------------
class SerialAdapter:
    """Thin wrapper around pyserial for real hardware tests."""

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 5.0) -> None:
        import serial  # type: ignore
        self._ser = serial.Serial(port, baudrate=baudrate, timeout=timeout)
        # Flush any banner / startup messages
        self._ser.read_until(b"ready\n", size=256)

    def command(self, line: str) -> str:
        if not line.endswith("\n"):
            line += "\n"
        self._ser.write(line.encode())
        return self._ser.readline().decode()

    def inject_capture_event(self, *args, **kwargs) -> None:
        raise NotImplementedError(
            "inject_capture_event() is only available with the Simulator. "
            "For hardware tests, drive pins physically."
        )

    def close(self) -> None:
        self._ser.close()


# ---------------------------------------------------------------------------
# Fixture
# ---------------------------------------------------------------------------
@pytest.fixture
def device():
    """
    Yields a device handle.
    Set PICO_PORT=/dev/ttyACM0 (or similar) to test against real hardware.
    Without PICO_PORT, the software Simulator is used.
    """
    port = os.environ.get("PICO_PORT")
    if port:
        adapter = SerialAdapter(port)
        yield adapter
        adapter.close()
    else:
        yield Simulator()


@pytest.fixture
def sim():
    """Always yields a Simulator (for unit-style integration tests)."""
    return Simulator()
