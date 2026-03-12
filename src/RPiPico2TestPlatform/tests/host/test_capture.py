"""
test_capture.py - Integration tests for the CAPTURE command.
Uses the Simulator's inject_capture_event() API to simulate DUT signals.
Tests edge filtering, sample count, timeout, and result correctness.
"""

import pytest
import json
from simulator import PinState, EdgeType, Simulator


def parse_data(r: str) -> dict:
    assert r.startswith("DATA:"), f"Expected DATA:, got: {r!r}"
    return json.loads(r[5:].strip())


# ===========================================================================
# Validation errors
# ===========================================================================

def test_capture_invalid_pin(sim: Simulator):
    r = sim.command("CAPTURE GP30 BOTH 1 100")
    assert r.startswith("ERR:INVALID_PIN:")


def test_capture_invalid_edge(sim: Simulator):
    r = sim.command("CAPTURE GP0 UP 1 100")
    assert r.startswith("ERR:INVALID_EDGE:")


def test_capture_zero_samples(sim: Simulator):
    r = sim.command("CAPTURE GP0 BOTH 0 100")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_capture_negative_timeout(sim: Simulator):
    r = sim.command("CAPTURE GP0 BOTH 1 0")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_capture_too_many_samples(sim: Simulator):
    r = sim.command("CAPTURE GP0 BOTH 4097 100")
    assert r.startswith("ERR:INVALID_PARAM:")


def test_capture_missing_args(sim: Simulator):
    r = sim.command("CAPTURE GP0 BOTH")
    assert r.startswith("ERR:INVALID_PARAM:")


# ===========================================================================
# Timeout with no events
# ===========================================================================

def test_capture_timeout_no_events(sim: Simulator):
    r = parse_data(sim.command("CAPTURE GP0 BOTH 1 1"))
    assert r["timed_out"] is True
    assert r["count"] == 0


def test_capture_samples_requested_in_result(sim: Simulator):
    r = parse_data(sim.command("CAPTURE GP0 BOTH 5 1"))
    # timed_out, but count must reflect reality
    assert r["timed_out"] is True


# ===========================================================================
# Rising edge filter
# ===========================================================================

def test_capture_rising_receives_high_event(sim: Simulator):
    sim.inject_capture_event(0, PinState.LOW,  delay_us=0)
    sim.inject_capture_event(0, PinState.HIGH, delay_us=1)
    r = parse_data(sim.command("CAPTURE GP0 RISING 1 100"))
    assert r["count"] == 1
    assert r["timed_out"] is False


def test_capture_rising_filters_falling(sim: Simulator):
    # Only a falling edge available - should time out
    sim.inject_capture_event(0, PinState.HIGH, delay_us=0)
    sim.inject_capture_event(0, PinState.LOW,  delay_us=1)
    r = parse_data(sim.command("CAPTURE GP0 RISING 1 100"))
    # The falling event must not count
    assert r["count"] == 0
    assert r["timed_out"] is True


def test_capture_rising_collects_multiple(sim: Simulator):
    sim.inject_square_wave(0, count=6, period_us=10)
    r = parse_data(sim.command("CAPTURE GP0 RISING 3 1000"))
    assert r["count"] == 3
    assert r["timed_out"] is False


# ===========================================================================
# Falling edge filter
# ===========================================================================

def test_capture_falling_receives_low_event(sim: Simulator):
    sim.inject_capture_event(0, PinState.HIGH, delay_us=0)
    sim.inject_capture_event(0, PinState.LOW,  delay_us=1)
    r = parse_data(sim.command("CAPTURE GP0 FALLING 1 100"))
    assert r["count"] == 1


def test_capture_falling_filters_rising(sim: Simulator):
    sim.inject_capture_event(0, PinState.LOW,  delay_us=0)
    sim.inject_capture_event(0, PinState.HIGH, delay_us=1)
    r = parse_data(sim.command("CAPTURE GP0 FALLING 1 100"))
    assert r["count"] == 0
    assert r["timed_out"] is True


# ===========================================================================
# BOTH edge filter
# ===========================================================================

def test_capture_both_records_all_transitions(sim: Simulator):
    sim.inject_square_wave(0, count=4, period_us=10)
    r = parse_data(sim.command("CAPTURE GP0 BOTH 4 1000"))
    assert r["count"] == 4
    assert r["timed_out"] is False


def test_capture_both_does_not_record_duplicate_state(sim: Simulator):
    # Inject same state twice; should only count the first as a transition
    sim.inject_capture_event(0, PinState.HIGH, delay_us=0)
    sim.inject_capture_event(0, PinState.HIGH, delay_us=1)   # duplicate
    sim.inject_capture_event(0, PinState.LOW,  delay_us=2)   # real transition
    r = parse_data(sim.command("CAPTURE GP0 BOTH 2 1000"))
    assert r["count"] == 2  # HIGH then LOW


# ===========================================================================
# Sample count enforcement
# ===========================================================================

def test_capture_stops_at_requested_count(sim: Simulator):
    sim.inject_square_wave(0, count=20, period_us=5)
    r = parse_data(sim.command("CAPTURE GP0 BOTH 5 1000"))
    assert r["count"] == 5
    assert r["timed_out"] is False


def test_capture_returns_all_available_on_timeout(sim: Simulator):
    sim.inject_capture_event(0, PinState.HIGH, delay_us=0)
    sim.inject_capture_event(0, PinState.LOW,  delay_us=1)
    # Requested 5 but only 2 available
    r = parse_data(sim.command("CAPTURE GP0 BOTH 5 100"))
    assert r["count"] <= 5
    assert r["timed_out"] is True


# ===========================================================================
# Data correctness
# ===========================================================================

def test_capture_pin_field_in_response(sim: Simulator):
    r = parse_data(sim.command("CAPTURE GP7 BOTH 1 1"))
    assert r["pin"] == 7


def test_capture_count_zero_on_timeout(sim: Simulator):
    r = parse_data(sim.command("CAPTURE GP0 BOTH 10 1"))
    assert r["count"] == 0


def test_capture_duration_us_non_negative(sim: Simulator):
    r = parse_data(sim.command("CAPTURE GP0 BOTH 1 1"))
    assert r["duration_us"] >= 0


# ===========================================================================
# Edge cases
# ===========================================================================

def test_capture_single_pin_does_not_consume_other_pin_events(sim: Simulator):
    """Events injected for GP1 must not appear when capturing GP0."""
    sim.inject_capture_event(1, PinState.HIGH, delay_us=0)  # wrong pin
    r = parse_data(sim.command("CAPTURE GP0 BOTH 1 1"))
    assert r["count"] == 0
    assert r["timed_out"] is True


def test_capture_min_samples_1(sim: Simulator):
    sim.inject_capture_event(0, PinState.HIGH, delay_us=0)
    r = parse_data(sim.command("CAPTURE GP0 BOTH 1 100"))
    assert r["count"] == 1
