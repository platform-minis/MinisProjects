"""
simulator.py - Python software simulator of the Pico 2 stimulus firmware.

Implements the same ASCII protocol as the C++ firmware so integration tests
run without hardware.  All timing is simulated (no real delays).

Usage:
    sim = Simulator()
    response = sim.command("SET GP0 HIGH")   # "OK\n"
    response = sim.command("READ GP0")       # "OK:HIGH\n"
"""

from __future__ import annotations

import re
import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional

# ---------------------------------------------------------------------------
# Constants matching hal.hpp / protocol.hpp
# ---------------------------------------------------------------------------
NUM_PINS           = 30
MAX_PIN_ID         = 29
PULSE_US_MIN       = 1
PULSE_US_MAX       = 1_000_000
MAX_CAPTURE_SAMPLES = 4096
MAX_TIMEOUT_MS     = 60_000
MAX_PWM_FREQ_HZ    = 100_000_000.0
MAX_SEQ_STEPS      = 64
FIRMWARE_VERSION   = "1.0.0"
DEVICE_NAME        = "McuTestPlatform"


class PinState(Enum):
    LOW       = "LOW"
    HIGH      = "HIGH"
    UNDEFINED = "UNDEFINED"

    @classmethod
    def from_str(cls, s: str) -> "PinState":
        s = s.upper().strip()
        aliases = {"1": "HIGH", "H": "HIGH", "TRUE": "HIGH",
                   "0": "LOW",  "L": "LOW",  "FALSE": "LOW"}
        s = aliases.get(s, s)
        return cls[s]  # KeyError -> ValueError propagated

    def inverted(self) -> "PinState":
        if self == PinState.HIGH: return PinState.LOW
        if self == PinState.LOW:  return PinState.HIGH
        return self

    def is_logic(self) -> bool:
        return self in (PinState.LOW, PinState.HIGH)


class EdgeType(Enum):
    RISING  = "RISING"
    FALLING = "FALLING"
    BOTH    = "BOTH"

    @classmethod
    def from_str(cls, s: str) -> "EdgeType":
        return cls[s.upper().strip()]


@dataclass
class CaptureEvent:
    timestamp_us: int
    new_state: PinState


@dataclass
class SimulatorState:
    """All mutable state of the simulator."""
    pin_states: list[PinState]   = field(default_factory=lambda: [PinState.UNDEFINED] * NUM_PINS)
    pwm_active: list[bool]       = field(default_factory=lambda: [False] * NUM_PINS)
    pwm_freq:   list[float]      = field(default_factory=lambda: [0.0] * NUM_PINS)
    pwm_duty:   list[float]      = field(default_factory=lambda: [0.0] * NUM_PINS)
    # Capture injection queue: list of (pin, state, delay_us)
    capture_queue: list[tuple[int, PinState, int]] = field(default_factory=list)
    mock_time_us: int = 0


class Simulator:
    """
    Pure-Python simulator of the Pico 2 stimulus firmware.
    Thread-safe for single-threaded sequential test usage.
    """

    def __init__(self) -> None:
        self._s = SimulatorState()

    # -----------------------------------------------------------------------
    # Public API
    # -----------------------------------------------------------------------
    def command(self, line: str) -> str:
        """Process one command line, return response string (ends with \\n)."""
        line = line.strip()
        if not line:
            return "ERR:INVALID_CMD:empty line\n"
        parts = line.split()
        name  = parts[0].upper()
        args  = parts[1:]

        dispatch = {
            "SET":      self._cmd_set,
            "READ":     self._cmd_read,
            "PULSE":    self._cmd_pulse,
            "CAPTURE":  self._cmd_capture,
            "SEQUENCE": self._cmd_sequence,
            "PWM":      self._cmd_pwm,
            "PWM_STOP": self._cmd_pwm_stop,
            "RESET":    self._cmd_reset,
            "STATUS":   self._cmd_status,
            "VERSION":  self._cmd_version,
            "PINS":     self._cmd_pins,
            "HELP":     self._cmd_help,
        }
        fn = dispatch.get(name)
        if fn is None:
            return f"ERR:INVALID_CMD:unknown command '{name}'\n"
        try:
            return fn(args)
        except Exception as exc:
            return f"ERR:INTERNAL:{exc}\n"

    def inject_capture_event(self, pin: int, state: PinState, delay_us: int = 0) -> None:
        """Pre-load a capture event (called from tests before CAPTURE command)."""
        self._s.capture_queue.append((pin, state, delay_us))

    def inject_square_wave(self, pin: int, count: int, period_us: int) -> None:
        """Inject alternating HIGH/LOW transitions with uniform spacing."""
        state = PinState.HIGH
        for i in range(count):
            self._s.capture_queue.append((pin, state, i * (period_us // 2)))
            state = state.inverted()

    def get_pin_state(self, pin: int) -> PinState:
        return self._s.pin_states[pin]

    def is_pwm_active(self, pin: int) -> bool:
        return self._s.pwm_active[pin]

    # -----------------------------------------------------------------------
    # Private helpers
    # -----------------------------------------------------------------------
    @staticmethod
    def _parse_pin(s: str) -> int:
        s = s.upper()
        if s.startswith("GP"):
            s = s[2:]
        n = int(s)
        if not (0 <= n <= MAX_PIN_ID):
            raise ValueError(f"pin {n} out of range")
        return n

    @staticmethod
    def _parse_state(s: str) -> PinState:
        return PinState.from_str(s)

    @staticmethod
    def _parse_edge(s: str) -> EdgeType:
        return EdgeType.from_str(s)

    def _check_pin(self, pin: int) -> Optional[str]:
        """Returns error response or None."""
        if not (0 <= pin <= MAX_PIN_ID):
            return f"ERR:INVALID_PIN:pin {pin} out of range\n"
        return None

    def _check_not_busy(self, pin: int) -> Optional[str]:
        if self._s.pwm_active[pin]:
            return f"ERR:PIN_BUSY:pin GP{pin} has active PWM\n"
        return None

    # -----------------------------------------------------------------------
    # Command handlers
    # -----------------------------------------------------------------------
    def _cmd_set(self, args: list[str]) -> str:
        if len(args) < 2:
            return "ERR:INVALID_PARAM:usage: SET GP<n> HIGH|LOW\n"
        try:
            pin = self._parse_pin(args[0])
        except (ValueError, IndexError):
            return "ERR:INVALID_PIN:bad pin argument\n"
        try:
            state = self._parse_state(args[1])
        except (ValueError, KeyError):
            return "ERR:INVALID_STATE:expected HIGH or LOW\n"
        if err := self._check_not_busy(pin):
            return err
        self._s.pin_states[pin] = state
        return "OK\n"

    def _cmd_read(self, args: list[str]) -> str:
        if len(args) < 1:
            return "ERR:INVALID_PARAM:usage: READ GP<n>\n"
        try:
            pin = self._parse_pin(args[0])
        except (ValueError, IndexError):
            return "ERR:INVALID_PIN:bad pin argument\n"
        state = self._s.pin_states[pin]
        return f"OK:{state.value}\n"

    def _cmd_pulse(self, args: list[str]) -> str:
        if len(args) < 2:
            return "ERR:INVALID_PARAM:usage: PULSE GP<n> <us>\n"
        try:
            pin = self._parse_pin(args[0])
        except (ValueError, IndexError):
            return "ERR:INVALID_PIN:bad pin argument\n"
        try:
            dur = int(args[1])
        except ValueError:
            return "ERR:INVALID_PARAM:duration_us must be integer\n"
        if not (PULSE_US_MIN <= dur <= PULSE_US_MAX):
            return f"ERR:INVALID_PARAM:duration_us out of range [{PULSE_US_MIN},{PULSE_US_MAX}]\n"
        if err := self._check_not_busy(pin):
            return err

        start = self._s.pin_states[pin]
        if not start.is_logic():
            start = PinState.LOW

        # Simulate: flip -> wait (mock, no real sleep) -> flip back
        self._s.pin_states[pin] = start.inverted()
        self._s.mock_time_us += dur
        self._s.pin_states[pin] = start

        import json
        data = {"pin": pin, "requested_us": dur, "actual_us": dur, "ok": True}
        return f"DATA:{json.dumps(data)}\n"

    def _cmd_capture(self, args: list[str]) -> str:
        if len(args) < 3:
            return "ERR:INVALID_PARAM:usage: CAPTURE GP<n> RISING|FALLING|BOTH <samples> [timeout_ms]\n"
        try:
            pin = self._parse_pin(args[0])
        except (ValueError, IndexError):
            return "ERR:INVALID_PIN:bad pin argument\n"
        try:
            edge = self._parse_edge(args[1])
        except (ValueError, KeyError):
            return "ERR:INVALID_EDGE:expected RISING, FALLING or BOTH\n"
        try:
            samples = int(args[2])
        except ValueError:
            return "ERR:INVALID_PARAM:samples must be integer\n"
        if not (1 <= samples <= MAX_CAPTURE_SAMPLES):
            return "ERR:INVALID_PARAM:samples out of range\n"

        timeout_ms = 1000
        if len(args) >= 4:
            try:
                timeout_ms = int(args[3])
            except ValueError:
                return "ERR:INVALID_PARAM:timeout_ms must be integer\n"
        if not (1 <= timeout_ms <= MAX_TIMEOUT_MS):
            return "ERR:INVALID_PARAM:timeout_ms out of range\n"

        # Process injected events
        events: list[CaptureEvent] = []
        prev_state = PinState.UNDEFINED
        queue = [(p, s, t) for p, s, t in self._s.capture_queue if p == pin]
        self._s.capture_queue = [(p, s, t) for p, s, t in self._s.capture_queue if p != pin]

        timed_out = False
        for (ev_pin, ev_state, ev_delay) in queue:
            if len(events) >= samples:
                break
            if ev_delay > timeout_ms * 1000:
                timed_out = True
                break
            # Edge filter
            is_transition = (ev_state != prev_state and ev_state.is_logic())
            is_rising  = is_transition and prev_state != PinState.HIGH and ev_state == PinState.HIGH
            is_falling = is_transition and prev_state != PinState.LOW  and ev_state == PinState.LOW
            matches = (
                (edge == EdgeType.BOTH    and is_transition) or
                (edge == EdgeType.RISING  and is_rising)     or
                (edge == EdgeType.FALLING and is_falling)
            )
            if matches:
                events.append(CaptureEvent(timestamp_us=ev_delay, new_state=ev_state))
            prev_state = ev_state

        if len(events) < samples:
            timed_out = True

        duration_us = events[-1].timestamp_us if events else 0

        import json
        data = {
            "pin": pin,
            "count": len(events),
            "timed_out": timed_out,
            "duration_us": duration_us,
        }
        return f"DATA:{json.dumps(data)}\n"

    def _cmd_sequence(self, args: list[str]) -> str:
        if not args:
            return "ERR:INVALID_PARAM:usage: SEQUENCE GP<n>:HIGH|LOW[:<us>] ...\n"
        if len(args) > MAX_SEQ_STEPS:
            return "ERR:OVERFLOW:too many steps\n"

        steps = []
        for i, arg in enumerate(args):
            parts = arg.split(":")
            if len(parts) < 2:
                return f"ERR:INVALID_PARAM:bad step {i+1}: '{arg}'\n"
            try:
                pin   = self._parse_pin(parts[0])
                state = self._parse_state(parts[1])
                delay = int(parts[2]) if len(parts) > 2 else 0
            except (ValueError, KeyError):
                return f"ERR:INVALID_PARAM:bad step {i+1}: '{arg}'\n"
            steps.append((pin, state, delay))

        # Validate ALL before executing
        for pin, state, delay in steps:
            if self._s.pwm_active[pin]:
                return f"ERR:PIN_BUSY:pin GP{pin} has active PWM\n"

        # Execute
        total_delay = 0
        for i, (pin, state, delay) in enumerate(steps):
            self._s.pin_states[pin] = state
            self._s.mock_time_us += delay
            total_delay += delay

        import json
        data = {
            "executed":   len(steps),
            "total":      len(steps),
            "completed":  True,
            "duration_us": total_delay,
        }
        return f"DATA:{json.dumps(data)}\n"

    def _cmd_pwm(self, args: list[str]) -> str:
        if len(args) < 3:
            return "ERR:INVALID_PARAM:usage: PWM GP<n> <freq_hz> <duty%>\n"
        try:
            pin = self._parse_pin(args[0])
        except (ValueError, IndexError):
            return "ERR:INVALID_PIN:bad pin argument\n"
        try:
            freq = float(args[1])
            duty = float(args[2])
        except ValueError:
            return "ERR:INVALID_PARAM:freq and duty must be numeric\n"
        if not (1.0 <= freq <= MAX_PWM_FREQ_HZ):
            return "ERR:INVALID_PARAM:freq_hz out of range\n"
        if not (0.0 <= duty <= 100.0):
            return "ERR:INVALID_PARAM:duty_pct out of range [0,100]\n"
        self._s.pwm_active[pin] = True
        self._s.pwm_freq[pin]   = freq
        self._s.pwm_duty[pin]   = duty
        return "OK\n"

    def _cmd_pwm_stop(self, args: list[str]) -> str:
        if len(args) < 1:
            return "ERR:INVALID_PARAM:usage: PWM_STOP GP<n>\n"
        try:
            pin = self._parse_pin(args[0])
        except (ValueError, IndexError):
            return "ERR:INVALID_PIN:bad pin argument\n"
        self._s.pwm_active[pin]  = False
        self._s.pin_states[pin]  = PinState.LOW
        return "OK\n"

    def _cmd_reset(self, args: list[str]) -> str:
        self._s = SimulatorState()
        return "OK\n"

    def _cmd_status(self, args: list[str]) -> str:
        import json
        data = {"device": DEVICE_NAME, "version": FIRMWARE_VERSION}
        return f"DATA:{json.dumps(data)}\n"

    def _cmd_version(self, args: list[str]) -> str:
        return f"OK:{DEVICE_NAME}/{FIRMWARE_VERSION}\n"

    def _cmd_pins(self, args: list[str]) -> str:
        import json
        data = {"num_pins": NUM_PINS, "max_pin": MAX_PIN_ID}
        return f"DATA:{json.dumps(data)}\n"

    def _cmd_help(self, args: list[str]) -> str:
        return (
            "OK:SET GP<n> H|L | READ GP<n> | PULSE GP<n> <us> | "
            "CAPTURE GP<n> RISING|FALLING|BOTH <n> [ms] | "
            "SEQUENCE GP<n>:H|L[:<us>] ... | "
            "PWM GP<n> <hz> <duty%> | PWM_STOP GP<n> | "
            "RESET | STATUS | VERSION | PINS | HELP\n"
        )
