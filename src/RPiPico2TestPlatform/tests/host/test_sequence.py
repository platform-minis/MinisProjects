"""
test_sequence.py - Integration tests for the SEQUENCE command.
"""

import pytest
import json
from simulator import PinState, Simulator


def parse_data(r: str) -> dict:
    assert r.startswith("DATA:"), f"Expected DATA:, got: {r!r}"
    return json.loads(r[5:].strip())


# ===========================================================================
# Basic execution
# ===========================================================================

def test_sequence_single_step_high(sim: Simulator):
    r = parse_data(sim.command("SEQUENCE GP0:HIGH"))
    assert r["completed"] is True
    assert r["executed"] == 1
    assert "HIGH" in sim.command("READ GP0")


def test_sequence_single_step_low(sim: Simulator):
    sim.command("SET GP0 HIGH")
    r = parse_data(sim.command("SEQUENCE GP0:LOW"))
    assert r["completed"] is True
    assert "LOW" in sim.command("READ GP0")


def test_sequence_two_pins(sim: Simulator):
    r = parse_data(sim.command("SEQUENCE GP0:HIGH GP1:LOW"))
    assert r["completed"] is True
    assert r["executed"] == 2
    assert "HIGH" in sim.command("READ GP0")
    assert "LOW"  in sim.command("READ GP1")


def test_sequence_toggle_same_pin(sim: Simulator):
    r = parse_data(sim.command("SEQUENCE GP0:HIGH GP0:LOW GP0:HIGH"))
    assert r["completed"] is True
    assert r["executed"] == 3
    assert "HIGH" in sim.command("READ GP0")


def test_sequence_all_steps_correct_total(sim: Simulator):
    r = parse_data(sim.command("SEQUENCE GP0:HIGH GP1:HIGH GP2:LOW"))
    assert r["total"] == 3
    assert r["executed"] == 3


# ===========================================================================
# With delays
# ===========================================================================

def test_sequence_with_delays_completes(sim: Simulator):
    r = parse_data(sim.command("SEQUENCE GP0:HIGH:100 GP1:LOW:200"))
    assert r["completed"] is True
    assert r["duration_us"] >= 300


def test_sequence_zero_delay_does_not_advance_time(sim: Simulator):
    r = parse_data(sim.command("SEQUENCE GP0:HIGH:0 GP1:LOW:0"))
    assert r["completed"] is True
    assert r["duration_us"] == 0


# ===========================================================================
# Error cases
# ===========================================================================

def test_sequence_empty_returns_err(device):
    r = device.command("SEQUENCE")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_sequence_bad_step_format_returns_err(device):
    r = device.command("SEQUENCE GP0ONLY")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_sequence_bad_state_returns_err(device):
    r = device.command("SEQUENCE GP0:MAYBE")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_sequence_invalid_pin_returns_err(device):
    r = device.command("SEQUENCE GP30:HIGH")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_sequence_busy_pin_returns_err(sim: Simulator):
    sim.command("PWM GP2 1000 50")
    r = sim.command("SEQUENCE GP0:HIGH GP2:LOW")
    assert r.startswith("ERR:PIN_BUSY:")


# ===========================================================================
# Atomic validation guarantee
# ===========================================================================

def test_sequence_no_gpio_change_on_validation_failure(sim: Simulator):
    """If any step is invalid, NO pins must change."""
    sim.command("SET GP0 LOW")
    sim.command("PWM GP1 1000 50")  # GP1 busy
    # Sequence: GP0:HIGH (ok) + GP1:LOW (busy) - validation must fail before any GPIO
    sim.command("SEQUENCE GP0:HIGH GP1:LOW")
    # GP0 must still be LOW
    r = sim.command("READ GP0")
    assert "LOW" in r


# ===========================================================================
# Numeric pin format
# ===========================================================================

def test_sequence_numeric_pin_without_gp_prefix(device):
    r = device.command("SEQUENCE 5:HIGH")
    assert r.startswith("DATA:")
