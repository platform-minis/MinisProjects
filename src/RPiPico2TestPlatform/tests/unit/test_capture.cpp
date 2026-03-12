// test_capture.cpp
// Unit tests for CaptureController.
// The MockCaptureHAL is pre-loaded with SignalEvents; the controller
// must filter them correctly and respect sample count and timeout.
// 30 tests.

#include "unity.h"
#include "mock_hal.hpp"
#include "capture.hpp"

static MockCaptureHAL* mock_cap;
static capture_ctrl::CaptureController* ctrl;

void setUp(void) {
    mock_cap = new MockCaptureHAL();
    ctrl     = new capture_ctrl::CaptureController(*mock_cap);
}
void tearDown(void) {
    delete ctrl;
    delete mock_cap;
}

// ---------------------------------------------------------------------------
// Helpers to inject transitions
// ---------------------------------------------------------------------------
// Injects: UNDEFINED->HIGH (rising)
static void inj_rising(uint8_t pin, uint64_t t_us = 1) {
    mock_cap->inject(pin, PinState::HIGH, t_us);
}
// Injects: HIGH->LOW (falling)
static void inj_falling(uint8_t pin, uint64_t t_us = 2) {
    mock_cap->inject(pin, PinState::LOW, t_us);
}

// ===========================================================================
// Validation errors
// ===========================================================================
void test_capture_invalid_pin(void) {
    auto r = ctrl->capture(30, EdgeType::BOTH, 1, 100);
    TEST_ASSERT_EQUAL(capture_ctrl::CaptureError::INVALID_PIN, r.error);
}
void test_capture_zero_samples_invalid(void) {
    auto r = ctrl->capture(0, EdgeType::BOTH, 0, 100);
    TEST_ASSERT_EQUAL(capture_ctrl::CaptureError::INVALID_PARAM, r.error);
}
void test_capture_samples_over_max_invalid(void) {
    auto r = ctrl->capture(0, EdgeType::BOTH, capture_ctrl::MAX_CAPTURE_SAMPLES + 1, 100);
    TEST_ASSERT_EQUAL(capture_ctrl::CaptureError::INVALID_PARAM, r.error);
}
void test_capture_zero_timeout_invalid(void) {
    auto r = ctrl->capture(0, EdgeType::BOTH, 1, 0);
    TEST_ASSERT_EQUAL(capture_ctrl::CaptureError::INVALID_PARAM, r.error);
}
void test_capture_timeout_over_max_invalid(void) {
    auto r = ctrl->capture(0, EdgeType::BOTH, 1, capture_ctrl::MAX_CAPTURE_TIMEOUT_MS + 1);
    TEST_ASSERT_EQUAL(capture_ctrl::CaptureError::INVALID_PARAM, r.error);
}

// ===========================================================================
// HAL lifecycle
// ===========================================================================
void test_capture_calls_configure_on_hal(void) {
    ctrl->capture(5, EdgeType::RISING, 1, 100);
    TEST_ASSERT_EQUAL_UINT8(5, mock_cap->configured_pin);
    TEST_ASSERT_EQUAL(EdgeType::RISING, mock_cap->configured_edge);
}
void test_capture_calls_start_on_hal(void) {
    ctrl->capture(0, EdgeType::BOTH, 1, 100);
    TEST_ASSERT_TRUE(mock_cap->started);
}
void test_capture_calls_stop_on_hal(void) {
    ctrl->capture(0, EdgeType::BOTH, 1, 100);
    TEST_ASSERT_TRUE(mock_cap->stopped);
}

// ===========================================================================
// Timeout with no events
// ===========================================================================
void test_capture_timeout_no_events(void) {
    // No injected events -> HAL returns false -> timeout
    auto r = ctrl->capture(0, EdgeType::BOTH, 1, 10);
    TEST_ASSERT_EQUAL(capture_ctrl::CaptureError::TIMEOUT, r.error);
    TEST_ASSERT_TRUE(r.timed_out);
    TEST_ASSERT_EQUAL_size_t(0, r.samples_captured);
}
void test_capture_samples_requested_correct_on_timeout(void) {
    auto r = ctrl->capture(0, EdgeType::BOTH, 5, 10);
    TEST_ASSERT_EQUAL_size_t(5, r.samples_requested);
}

// ===========================================================================
// BOTH edge filter
// ===========================================================================
void test_capture_both_records_rising(void) {
    // prev=UNDEFINED, inject HIGH -> first event is treated as initial state
    // To get a real edge, inject LOW then HIGH
    mock_cap->inject(0, PinState::LOW,  1);
    mock_cap->inject(0, PinState::HIGH, 2);
    auto r = ctrl->capture(0, EdgeType::BOTH, 1, 100);
    TEST_ASSERT_EQUAL(capture_ctrl::CaptureError::OK, r.error);
    TEST_ASSERT_EQUAL_size_t(1, r.samples_captured);
    TEST_ASSERT_EQUAL(PinState::HIGH, r.events[0].new_state);
}
void test_capture_both_records_falling(void) {
    mock_cap->inject(0, PinState::HIGH, 1);
    mock_cap->inject(0, PinState::LOW,  2);
    auto r = ctrl->capture(0, EdgeType::BOTH, 1, 100);
    TEST_ASSERT_EQUAL(capture_ctrl::CaptureError::OK, r.error);
    TEST_ASSERT_EQUAL(PinState::LOW, r.events[0].new_state);
}
void test_capture_both_records_alternating(void) {
    mock_cap->inject_square_wave(0, 4, 10);
    auto r = ctrl->capture(0, EdgeType::BOTH, 4, 1000);
    TEST_ASSERT_EQUAL(capture_ctrl::CaptureError::OK, r.error);
    TEST_ASSERT_EQUAL_size_t(4, r.samples_captured);
}

// ===========================================================================
// RISING edge filter
// ===========================================================================
void test_capture_rising_only_records_low_to_high(void) {
    mock_cap->inject(0, PinState::HIGH, 1);   // rising (UNDEFINED->HIGH)
    mock_cap->inject(0, PinState::LOW,  2);   // falling - must be filtered
    mock_cap->inject(0, PinState::HIGH, 3);   // rising - must be recorded
    auto r = ctrl->capture(0, EdgeType::RISING, 2, 1000);
    TEST_ASSERT_EQUAL_size_t(2, r.samples_captured);
    TEST_ASSERT_EQUAL(PinState::HIGH, r.events[0].new_state);
    TEST_ASSERT_EQUAL(PinState::HIGH, r.events[1].new_state);
}
void test_capture_rising_does_not_record_falling(void) {
    mock_cap->inject(0, PinState::HIGH, 1);  // first: rising
    mock_cap->inject(0, PinState::LOW,  2);  // second: falling -> filtered
    // Only 1 sample requested; we get the rising one
    auto r = ctrl->capture(0, EdgeType::RISING, 1, 100);
    TEST_ASSERT_EQUAL_size_t(1, r.samples_captured);
    TEST_ASSERT_EQUAL(PinState::HIGH, r.events[0].new_state);
}

// ===========================================================================
// FALLING edge filter
// ===========================================================================
void test_capture_falling_only_records_high_to_low(void) {
    mock_cap->inject(0, PinState::LOW,  1);   // falling (UNDEFINED->LOW)
    mock_cap->inject(0, PinState::HIGH, 2);   // rising  - must be filtered
    mock_cap->inject(0, PinState::LOW,  3);   // falling - must be recorded
    auto r = ctrl->capture(0, EdgeType::FALLING, 2, 1000);
    TEST_ASSERT_EQUAL_size_t(2, r.samples_captured);
    TEST_ASSERT_EQUAL(PinState::LOW, r.events[0].new_state);
    TEST_ASSERT_EQUAL(PinState::LOW, r.events[1].new_state);
}
void test_capture_falling_does_not_record_rising(void) {
    mock_cap->inject(0, PinState::LOW,  1);   // falling
    mock_cap->inject(0, PinState::HIGH, 2);   // rising - filtered
    auto r = ctrl->capture(0, EdgeType::FALLING, 1, 100);
    TEST_ASSERT_EQUAL_size_t(1, r.samples_captured);
    TEST_ASSERT_EQUAL(PinState::LOW, r.events[0].new_state);
}

// ===========================================================================
// Duplicate-state filtering
// ===========================================================================
void test_capture_ignores_same_state_twice(void) {
    // HAL fires two HIGH events in a row - only the first counts as an edge
    mock_cap->inject(0, PinState::HIGH, 1);
    mock_cap->inject(0, PinState::HIGH, 2);  // same state, not an edge
    mock_cap->inject(0, PinState::LOW,  3);  // true falling edge
    auto r = ctrl->capture(0, EdgeType::BOTH, 2, 1000);
    TEST_ASSERT_EQUAL_size_t(2, r.samples_captured);
    TEST_ASSERT_EQUAL(PinState::HIGH, r.events[0].new_state);
    TEST_ASSERT_EQUAL(PinState::LOW,  r.events[1].new_state);
}

// ===========================================================================
// Sample count
// ===========================================================================
void test_capture_stops_at_requested_count(void) {
    mock_cap->inject_square_wave(0, 10, 5);  // inject 10 events
    auto r = ctrl->capture(0, EdgeType::BOTH, 3, 1000);
    TEST_ASSERT_EQUAL_size_t(3, r.samples_captured);
    TEST_ASSERT_FALSE(r.timed_out);
}
void test_capture_max_samples_valid(void) {
    // Inject exactly MAX_CAPTURE_SAMPLES events (alternating)
    for (size_t i = 0; i < capture_ctrl::MAX_CAPTURE_SAMPLES; ++i) {
        PinState s = (i % 2 == 0) ? PinState::HIGH : PinState::LOW;
        mock_cap->inject(0, s, (uint64_t)(i + 1));
    }
    auto r = ctrl->capture(0, EdgeType::BOTH, capture_ctrl::MAX_CAPTURE_SAMPLES, 60000);
    TEST_ASSERT_EQUAL_size_t(capture_ctrl::MAX_CAPTURE_SAMPLES, r.samples_captured);
}

// ===========================================================================
// Timestamps
// ===========================================================================
void test_capture_timestamps_non_zero(void) {
    mock_cap->inject(0, PinState::HIGH, 42);
    auto r = ctrl->capture(0, EdgeType::BOTH, 1, 100);
    TEST_ASSERT_EQUAL(42ULL, r.events[0].timestamp_us);
}
void test_capture_timestamps_monotonic(void) {
    mock_cap->inject(0, PinState::HIGH, 10);
    mock_cap->inject(0, PinState::LOW,  20);
    mock_cap->inject(0, PinState::HIGH, 30);
    auto r = ctrl->capture(0, EdgeType::BOTH, 3, 1000);
    TEST_ASSERT_EQUAL_size_t(3, r.samples_captured);
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(r.events[1].timestamp_us, r.events[2].timestamp_us);
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(r.events[0].timestamp_us, r.events[1].timestamp_us);
}

// ===========================================================================
// Partial capture on timeout
// ===========================================================================
void test_capture_partial_result_on_timeout(void) {
    mock_cap->inject(0, PinState::HIGH, 1);
    // Only 1 event available, we requested 5
    auto r = ctrl->capture(0, EdgeType::BOTH, 5, 10);
    TEST_ASSERT_TRUE(r.timed_out);
    TEST_ASSERT_EQUAL_size_t(5, r.samples_requested);
    TEST_ASSERT_LESS_THAN_size_t(5, r.samples_captured + 1);  // captured < requested
}

// ===========================================================================
// Result fields
// ===========================================================================
void test_capture_result_samples_requested_matches_arg(void) {
    auto r = ctrl->capture(0, EdgeType::BOTH, 7, 100);
    TEST_ASSERT_EQUAL_size_t(7, r.samples_requested);
}
void test_capture_result_ok_when_all_samples_received(void) {
    mock_cap->inject(0, PinState::HIGH, 1);
    auto r = ctrl->capture(0, EdgeType::BOTH, 1, 100);
    TEST_ASSERT_EQUAL(capture_ctrl::CaptureError::OK, r.error);
    TEST_ASSERT_FALSE(r.timed_out);
}
void test_capture_pin_id_reflects_argument(void) {
    mock_cap->inject(9, PinState::HIGH, 1);
    auto r = ctrl->capture(9, EdgeType::BOTH, 1, 100);
    TEST_ASSERT_EQUAL_UINT8(9, mock_cap->configured_pin);
}

// ---------------------------------------------------------------------------
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_capture_invalid_pin);
    RUN_TEST(test_capture_zero_samples_invalid);
    RUN_TEST(test_capture_samples_over_max_invalid);
    RUN_TEST(test_capture_zero_timeout_invalid);
    RUN_TEST(test_capture_timeout_over_max_invalid);

    RUN_TEST(test_capture_calls_configure_on_hal);
    RUN_TEST(test_capture_calls_start_on_hal);
    RUN_TEST(test_capture_calls_stop_on_hal);

    RUN_TEST(test_capture_timeout_no_events);
    RUN_TEST(test_capture_samples_requested_correct_on_timeout);

    RUN_TEST(test_capture_both_records_rising);
    RUN_TEST(test_capture_both_records_falling);
    RUN_TEST(test_capture_both_records_alternating);

    RUN_TEST(test_capture_rising_only_records_low_to_high);
    RUN_TEST(test_capture_rising_does_not_record_falling);

    RUN_TEST(test_capture_falling_only_records_high_to_low);
    RUN_TEST(test_capture_falling_does_not_record_rising);

    RUN_TEST(test_capture_ignores_same_state_twice);

    RUN_TEST(test_capture_stops_at_requested_count);
    RUN_TEST(test_capture_max_samples_valid);

    RUN_TEST(test_capture_timestamps_non_zero);
    RUN_TEST(test_capture_timestamps_monotonic);

    RUN_TEST(test_capture_partial_result_on_timeout);
    RUN_TEST(test_capture_result_samples_requested_matches_arg);
    RUN_TEST(test_capture_result_ok_when_all_samples_received);
    RUN_TEST(test_capture_pin_id_reflects_argument);

    return UNITY_END();
}
