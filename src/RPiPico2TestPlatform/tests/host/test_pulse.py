"""
test_pulse.py - Integration tests for the PULSE command.
Verifies that:
  - Pin returns to original state after pulse
  - Duration is reported correctly
  - Boundary conditions are enforced
  - Pin busy with PWM blocks PULSE
"""

import pytest
import json
from simulator import PinState, Simulator


def parse_data(r: str) -> dict:
    assert r.startswith("DATA:"), f"Expected DATA:, got: {r!r}"
    return json.loads(r[5:].strip())


# ===========================================================================
# Basic correctness
# ===========================================================================

def test_pulse_from_low_returns_to_low(sim: Simulator):
    sim.command("SET GP0 LOW")
    r = parse_data(sim.command("PULSE GP0 10"))
    assert r["ok"] is True
    # Pin must be LOW after pulse
    after = sim.command("READ GP0")
    assert "LOW" in after


def test_pulse_from_high_returns_to_high(sim: Simulator):
    sim.command("SET GP0 HIGH")
    r = parse_data(sim.command("PULSE GP0 10"))
    assert r["ok"] is True
    after = sim.command("READ GP0")
    assert "HIGH" in after


def test_pulse_from_undefined_defaults_and_returns_low(sim: Simulator):
    """Undefined pin defaults to LOW before pulse."""
    # No prior SET - pin is UNDEFINED
    r = parse_data(sim.command("PULSE GP0 10"))
    assert r["ok"] is True
    after = sim.command("READ GP0")
    assert "LOW" in after


def test_pulse_reports_requested_duration(sim: Simulator):
    r = parse_data(sim.command("PULSE GP5 250"))
    assert r["requested_us"] == 250


def test_pulse_reports_actual_duration(sim: Simulator):
    r = parse_data(sim.command("PULSE GP5 250"))
    # Simulator reports same value; real firmware may differ slightly
    assert "actual_us" in r
    assert r["actual_us"] > 0


def test_pulse_pin_field_correct(sim: Simulator):
    r = parse_data(sim.command("PULSE GP7 100"))
    assert r["pin"] == 7


# ===========================================================================
# State independence
# ===========================================================================

def test_pulse_does_not_affect_other_pins(sim: Simulator):
    sim.command("SET GP0 HIGH")
    sim.command("SET GP1 LOW")
    sim.command("PULSE GP0 10")
    # GP1 must be unchanged
    r = sim.command("READ GP1")
    assert "LOW" in r


def test_multiple_pulses_same_pin(sim: Simulator):
    sim.command("SET GP3 LOW")
    for _ in range(5):
        r = parse_data(sim.command("PULSE GP3 5"))
        assert r["ok"] is True
    after = sim.command("READ GP3")
    assert "LOW" in after


# ===========================================================================
# Boundary values
# ===========================================================================

def test_pulse_minimum_duration_1us(device):
    r = device.command("PULSE GP0 1")
    assert r.startswith("DATA:")


def test_pulse_maximum_duration_1s(device):
    r = device.command("PULSE GP0 1000000")
    assert r.startswith("DATA:")


def test_pulse_zero_duration_rejected(device):
    r = device.command("PULSE GP0 0")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_pulse_above_max_rejected(device):
    r = device.command("PULSE GP0 1000001")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_pulse_negative_duration_rejected(device):
    r = device.command("PULSE GP0 -1")
    assert r.startswith("ERR:INVALID_PARAM:")


# ===========================================================================
# Error cases
# ===========================================================================

def test_pulse_invalid_pin_30_returns_err(device):
    r = device.command("PULSE GP30 10")
    assert r.startswith("ERR:INVALID_PIN:")


def test_pulse_missing_pin_returns_err(device):
    r = device.command("PULSE")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_pulse_missing_duration_returns_err(device):
    r = device.command("PULSE GP0")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_pulse_pin_busy_with_pwm_returns_err(sim: Simulator):
    sim.command("PWM GP2 1000 50")
    r = sim.command("PULSE GP2 10")
    assert r.startswith("ERR:PIN_BUSY:")


def test_pulse_after_pwm_stop_works(sim: Simulator):
    sim.command("PWM GP2 1000 50")
    sim.command("PWM_STOP GP2")
    r = sim.command("PULSE GP2 10")
    assert r.startswith("DATA:")
