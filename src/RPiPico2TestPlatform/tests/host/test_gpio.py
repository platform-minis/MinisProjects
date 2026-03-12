"""
test_gpio.py - Integration tests for SET and READ commands.
Run against the Simulator or real hardware (via conftest.py fixture).
"""

import pytest
import json
from simulator import PinState, Simulator


def parse_ok_data(response: str) -> str:
    """Extract value from 'OK:<value>\\n'."""
    assert response.startswith("OK:"), f"Expected OK: prefix, got: {response!r}"
    return response[3:].strip()


def parse_data(response: str) -> dict:
    """Extract dict from 'DATA:{...}\\n'."""
    assert response.startswith("DATA:"), f"Expected DATA: prefix, got: {response!r}"
    return json.loads(response[5:].strip())


# ===========================================================================
# SET command
# ===========================================================================

def test_set_pin_0_high(device):
    r = device.command("SET GP0 HIGH")
    assert r == "OK\n", f"Unexpected response: {r!r}"


def test_set_pin_0_low(device):
    device.command("SET GP0 HIGH")
    r = device.command("SET GP0 LOW")
    assert r == "OK\n"


def test_set_all_pins_sequential(device):
    """Set every valid pin HIGH then LOW without error."""
    for pin in range(30):
        r = device.command(f"SET GP{pin} HIGH")
        assert r == "OK\n", f"GP{pin} HIGH failed: {r!r}"
        r = device.command(f"SET GP{pin} LOW")
        assert r == "OK\n", f"GP{pin} LOW failed: {r!r}"


def test_set_pin_with_numeric_id(device):
    """SET with plain number (no 'GP' prefix) must work."""
    r = device.command("SET 5 HIGH")
    assert r == "OK\n"


def test_set_pin_with_lowercase_command(device):
    r = device.command("set GP0 HIGH")
    assert r == "OK\n"


def test_set_invalid_pin_30_returns_err(device):
    r = device.command("SET GP30 HIGH")
    assert r.startswith("ERR:INVALID_PIN:")


def test_set_invalid_pin_255_returns_err(device):
    r = device.command("SET GP255 HIGH")
    assert r.startswith("ERR:INVALID_PIN:")


def test_set_invalid_state_returns_err(device):
    r = device.command("SET GP0 MAYBE")
    assert r.startswith("ERR:INVALID_STATE:")


def test_set_missing_state_arg_returns_err(device):
    r = device.command("SET GP0")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_set_empty_returns_err(device):
    r = device.command("SET")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_set_shorthand_H(device):
    r = device.command("SET GP1 H")
    assert r == "OK\n"


def test_set_shorthand_L(device):
    r = device.command("SET GP1 L")
    assert r == "OK\n"


def test_set_shorthand_1(device):
    r = device.command("SET GP1 1")
    assert r == "OK\n"


def test_set_shorthand_0(device):
    r = device.command("SET GP1 0")
    assert r == "OK\n"


# ===========================================================================
# READ command
# ===========================================================================

def test_read_after_set_high_returns_high(device):
    device.command("SET GP2 HIGH")
    r = device.command("READ GP2")
    assert parse_ok_data(r) == "HIGH"


def test_read_after_set_low_returns_low(device):
    device.command("SET GP2 HIGH")
    device.command("SET GP2 LOW")
    r = device.command("READ GP2")
    assert parse_ok_data(r) == "LOW"


def test_read_pin_0(device):
    device.command("SET GP0 HIGH")
    r = device.command("READ GP0")
    assert parse_ok_data(r) == "HIGH"


def test_read_pin_29(device):
    device.command("SET GP29 LOW")
    r = device.command("READ GP29")
    assert parse_ok_data(r) == "LOW"


def test_read_invalid_pin_returns_err(device):
    r = device.command("READ GP30")
    assert r.startswith("ERR:INVALID_PIN:")


def test_read_missing_pin_returns_err(device):
    r = device.command("READ")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_read_multiple_pins_independent(device):
    """Verify that pins do not influence each other."""
    device.command("SET GP0 HIGH")
    device.command("SET GP1 LOW")
    device.command("SET GP2 HIGH")
    assert parse_ok_data(device.command("READ GP0")) == "HIGH"
    assert parse_ok_data(device.command("READ GP1")) == "LOW"
    assert parse_ok_data(device.command("READ GP2")) == "HIGH"


# ===========================================================================
# RESET interaction with GPIO
# ===========================================================================

def test_reset_clears_pin_state(sim: Simulator):
    sim.command("SET GP0 HIGH")
    sim.command("RESET")
    # After reset, pin should be LOW (reset drives all pins LOW)
    r = sim.command("READ GP0")
    assert parse_ok_data(r) == "LOW"


def test_set_after_reset_works(sim: Simulator):
    sim.command("RESET")
    r = sim.command("SET GP5 HIGH")
    assert r == "OK\n"
