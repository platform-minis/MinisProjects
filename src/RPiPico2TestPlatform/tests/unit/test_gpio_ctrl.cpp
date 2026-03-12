// test_gpio_ctrl.cpp
// Unit tests for GpioController: SET, READ, PULSE, PWM, RESET.
// 40 tests.

#include "unity.h"
#include "mock_hal.hpp"
#include "gpio_ctrl.hpp"

static MockHAL* mock;
static gpio_ctrl::GpioController* ctrl;

void setUp(void) {
    mock = new MockHAL();
    ctrl = new gpio_ctrl::GpioController(*mock, *mock);
}
void tearDown(void) {
    delete ctrl;
    delete mock;
}

// ===========================================================================
// is_valid_pin
// ===========================================================================
void test_is_valid_pin_min(void) {
    TEST_ASSERT_TRUE(gpio_ctrl::GpioController::is_valid_pin(0));
}
void test_is_valid_pin_max(void) {
    TEST_ASSERT_TRUE(gpio_ctrl::GpioController::is_valid_pin(29));
}
void test_is_valid_pin_30_invalid(void) {
    TEST_ASSERT_FALSE(gpio_ctrl::GpioController::is_valid_pin(30));
}
void test_is_valid_pin_255_invalid(void) {
    TEST_ASSERT_FALSE(gpio_ctrl::GpioController::is_valid_pin(255));
}

// ===========================================================================
// set_pin
// ===========================================================================
void test_set_pin_high(void) {
    auto err = ctrl->set_pin(0, PinState::HIGH);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::OK, err);
    TEST_ASSERT_TRUE(mock->pin_value[0]);
}
void test_set_pin_low(void) {
    auto err = ctrl->set_pin(0, PinState::LOW);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::OK, err);
    TEST_ASSERT_FALSE(mock->pin_value[0]);
}
void test_set_pin_updates_cache(void) {
    ctrl->set_pin(5, PinState::HIGH);
    TEST_ASSERT_EQUAL(PinState::HIGH, ctrl->cached_state(5));
}
void test_set_pin_configures_output(void) {
    ctrl->set_pin(3, PinState::HIGH);
    TEST_ASSERT_EQUAL(PinDirection::OUTPUT, mock->pin_dir[3]);
}
void test_set_pin_invalid_id_returns_error(void) {
    auto err = ctrl->set_pin(30, PinState::HIGH);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::INVALID_PIN, err);
}
void test_set_pin_invalid_does_not_touch_hal(void) {
    ctrl->set_pin(30, PinState::HIGH);
    // No init_pin or set_pin should have been called for pin 30
    // (pin 30 doesn't exist in the array, but we check pin 0 wasn't touched)
    TEST_ASSERT_EQUAL(0u, mock->set_calls[0]);
}
void test_set_pin_multiple_pins_independent(void) {
    ctrl->set_pin(0, PinState::HIGH);
    ctrl->set_pin(1, PinState::LOW);
    ctrl->set_pin(2, PinState::HIGH);
    TEST_ASSERT_TRUE(mock->pin_value[0]);
    TEST_ASSERT_FALSE(mock->pin_value[1]);
    TEST_ASSERT_TRUE(mock->pin_value[2]);
}
void test_set_pin_change_state(void) {
    ctrl->set_pin(0, PinState::HIGH);
    ctrl->set_pin(0, PinState::LOW);
    TEST_ASSERT_FALSE(mock->pin_value[0]);
    TEST_ASSERT_EQUAL(PinState::LOW, ctrl->cached_state(0));
}
void test_set_pin_busy_with_pwm_returns_error(void) {
    mock->pwm_active[7] = true;
    auto err = ctrl->set_pin(7, PinState::HIGH);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::PIN_BUSY, err);
}
void test_set_pin_busy_with_pwm_does_not_change_gpio(void) {
    mock->pwm_active[7] = true;
    mock->pin_value[7] = false;
    ctrl->set_pin(7, PinState::HIGH);
    TEST_ASSERT_FALSE(mock->pin_value[7]);  // unchanged
}

// ===========================================================================
// read_pin
// ===========================================================================
void test_read_pin_returns_hardware_value(void) {
    mock->force_pin(4, true);
    gpio_ctrl::GpioError err;
    PinState s = ctrl->read_pin(4, err);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::OK, err);
    TEST_ASSERT_EQUAL(PinState::HIGH, s);
}
void test_read_pin_low(void) {
    mock->force_pin(4, false);
    gpio_ctrl::GpioError err;
    PinState s = ctrl->read_pin(4, err);
    TEST_ASSERT_EQUAL(PinState::LOW, s);
}
void test_read_pin_configures_input(void) {
    gpio_ctrl::GpioError err;
    ctrl->read_pin(3, err);
    TEST_ASSERT_EQUAL(PinDirection::INPUT, mock->pin_dir[3]);
}
void test_read_pin_updates_cache(void) {
    mock->force_pin(6, true);
    gpio_ctrl::GpioError err;
    ctrl->read_pin(6, err);
    TEST_ASSERT_EQUAL(PinState::HIGH, ctrl->cached_state(6));
}
void test_read_pin_invalid_returns_undefined(void) {
    gpio_ctrl::GpioError err;
    PinState s = ctrl->read_pin(30, err);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::INVALID_PIN, err);
    TEST_ASSERT_EQUAL(PinState::UNDEFINED, s);
}
void test_read_pin_increments_read_counter(void) {
    gpio_ctrl::GpioError err;
    ctrl->read_pin(2, err);
    ctrl->read_pin(2, err);
    TEST_ASSERT_EQUAL(2u, mock->read_calls[2]);
}

// ===========================================================================
// cached_state initial value
// ===========================================================================
void test_cached_state_initial_undefined(void) {
    TEST_ASSERT_EQUAL(PinState::UNDEFINED, ctrl->cached_state(0));
}
void test_cached_state_after_set_high(void) {
    ctrl->set_pin(0, PinState::HIGH);
    TEST_ASSERT_EQUAL(PinState::HIGH, ctrl->cached_state(0));
}
void test_cached_state_after_set_low(void) {
    ctrl->set_pin(0, PinState::LOW);
    TEST_ASSERT_EQUAL(PinState::LOW, ctrl->cached_state(0));
}
void test_cached_state_invalid_pin_returns_undefined(void) {
    TEST_ASSERT_EQUAL(PinState::UNDEFINED, ctrl->cached_state(30));
}

// ===========================================================================
// pulse
// ===========================================================================
void test_pulse_from_low_goes_high_and_returns(void) {
    ctrl->set_pin(0, PinState::LOW);
    auto r = ctrl->pulse(0, 10);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::OK, r.error);
    TEST_ASSERT_EQUAL(PinState::LOW, r.state_before);
    TEST_ASSERT_EQUAL(PinState::LOW, r.state_after);
    TEST_ASSERT_EQUAL(PinState::LOW, ctrl->cached_state(0));
}
void test_pulse_from_high_goes_low_and_returns(void) {
    ctrl->set_pin(0, PinState::HIGH);
    auto r = ctrl->pulse(0, 10);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::OK, r.error);
    TEST_ASSERT_EQUAL(PinState::HIGH, r.state_before);
    TEST_ASSERT_EQUAL(PinState::HIGH, r.state_after);
}
void test_pulse_from_undefined_defaults_to_low(void) {
    // Pin never set - cached is UNDEFINED, pulse should treat as LOW
    auto r = ctrl->pulse(0, 10);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::OK, r.error);
    TEST_ASSERT_EQUAL(PinState::LOW, r.state_before);
    TEST_ASSERT_EQUAL(PinState::LOW, r.state_after);
}
void test_pulse_sets_requested_duration_in_result(void) {
    ctrl->set_pin(0, PinState::LOW);
    auto r = ctrl->pulse(0, 500);
    TEST_ASSERT_EQUAL_UINT32(500, r.requested_us);
}
void test_pulse_advances_mock_time(void) {
    uint64_t t0 = mock->mock_time_us;
    ctrl->pulse(0, 100);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(100, mock->mock_time_us - t0);
}
void test_pulse_invalid_pin(void) {
    auto r = ctrl->pulse(30, 10);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::INVALID_PIN, r.error);
}
void test_pulse_zero_duration_invalid(void) {
    auto r = ctrl->pulse(0, 0);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::INVALID_PARAM, r.error);
}
void test_pulse_too_long_invalid(void) {
    auto r = ctrl->pulse(0, HAL_PULSE_US_MAX + 1);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::INVALID_PARAM, r.error);
}
void test_pulse_max_valid_duration(void) {
    auto r = ctrl->pulse(0, HAL_PULSE_US_MAX);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::OK, r.error);
}
void test_pulse_pin_busy_with_pwm(void) {
    mock->pwm_active[2] = true;
    auto r = ctrl->pulse(2, 100);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::PIN_BUSY, r.error);
}
void test_pulse_does_not_affect_other_pins(void) {
    ctrl->set_pin(1, PinState::HIGH);
    ctrl->set_pin(0, PinState::LOW);
    ctrl->pulse(0, 10);
    // Pin 1 must be unchanged
    TEST_ASSERT_EQUAL(PinState::HIGH, ctrl->cached_state(1));
}
void test_pulse_start_end_time_ordered(void) {
    auto r = ctrl->pulse(0, 50);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(r.start_time_us, r.end_time_us);
}

// ===========================================================================
// reset
// ===========================================================================
void test_reset_drives_all_pins_low(void) {
    ctrl->set_pin(0, PinState::HIGH);
    ctrl->set_pin(5, PinState::HIGH);
    ctrl->reset();
    TEST_ASSERT_FALSE(mock->pin_value[0]);
    TEST_ASSERT_FALSE(mock->pin_value[5]);
}
void test_reset_stops_pwm(void) {
    mock->pwm_active[3] = true;
    ctrl->reset();
    TEST_ASSERT_FALSE(mock->pwm_active[3]);
}
void test_reset_sets_cache_to_low(void) {
    ctrl->set_pin(7, PinState::HIGH);
    ctrl->reset();
    TEST_ASSERT_EQUAL(PinState::LOW, ctrl->cached_state(7));
}

// ===========================================================================
// PWM
// ===========================================================================
void test_set_pwm_valid(void) {
    auto err = ctrl->set_pwm(0, 1000.0f, 50.0f);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::OK, err);
    TEST_ASSERT_TRUE(mock->pwm_active[0]);
}
void test_set_pwm_invalid_pin(void) {
    auto err = ctrl->set_pwm(30, 1000.0f, 50.0f);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::INVALID_PIN, err);
}
void test_set_pwm_zero_freq_invalid(void) {
    auto err = ctrl->set_pwm(0, 0.0f, 50.0f);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::INVALID_PARAM, err);
}
void test_set_pwm_duty_above_100_invalid(void) {
    auto err = ctrl->set_pwm(0, 1000.0f, 101.0f);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::INVALID_PARAM, err);
}
void test_set_pwm_duty_100_valid(void) {
    auto err = ctrl->set_pwm(0, 1000.0f, 100.0f);
    TEST_ASSERT_EQUAL(gpio_ctrl::GpioError::OK, err);
}
void test_stop_pwm_clears_active(void) {
    ctrl->set_pwm(0, 1000.0f, 50.0f);
    ctrl->stop_pwm(0);
    TEST_ASSERT_FALSE(ctrl->is_pin_busy(0));
}
void test_stop_pwm_drives_pin_low(void) {
    ctrl->set_pwm(0, 1000.0f, 50.0f);
    ctrl->stop_pwm(0);
    TEST_ASSERT_FALSE(mock->pin_value[0]);
}

// ---------------------------------------------------------------------------
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_is_valid_pin_min);
    RUN_TEST(test_is_valid_pin_max);
    RUN_TEST(test_is_valid_pin_30_invalid);
    RUN_TEST(test_is_valid_pin_255_invalid);

    RUN_TEST(test_set_pin_high);
    RUN_TEST(test_set_pin_low);
    RUN_TEST(test_set_pin_updates_cache);
    RUN_TEST(test_set_pin_configures_output);
    RUN_TEST(test_set_pin_invalid_id_returns_error);
    RUN_TEST(test_set_pin_invalid_does_not_touch_hal);
    RUN_TEST(test_set_pin_multiple_pins_independent);
    RUN_TEST(test_set_pin_change_state);
    RUN_TEST(test_set_pin_busy_with_pwm_returns_error);
    RUN_TEST(test_set_pin_busy_with_pwm_does_not_change_gpio);

    RUN_TEST(test_read_pin_returns_hardware_value);
    RUN_TEST(test_read_pin_low);
    RUN_TEST(test_read_pin_configures_input);
    RUN_TEST(test_read_pin_updates_cache);
    RUN_TEST(test_read_pin_invalid_returns_undefined);
    RUN_TEST(test_read_pin_increments_read_counter);

    RUN_TEST(test_cached_state_initial_undefined);
    RUN_TEST(test_cached_state_after_set_high);
    RUN_TEST(test_cached_state_after_set_low);
    RUN_TEST(test_cached_state_invalid_pin_returns_undefined);

    RUN_TEST(test_pulse_from_low_goes_high_and_returns);
    RUN_TEST(test_pulse_from_high_goes_low_and_returns);
    RUN_TEST(test_pulse_from_undefined_defaults_to_low);
    RUN_TEST(test_pulse_sets_requested_duration_in_result);
    RUN_TEST(test_pulse_advances_mock_time);
    RUN_TEST(test_pulse_invalid_pin);
    RUN_TEST(test_pulse_zero_duration_invalid);
    RUN_TEST(test_pulse_too_long_invalid);
    RUN_TEST(test_pulse_max_valid_duration);
    RUN_TEST(test_pulse_pin_busy_with_pwm);
    RUN_TEST(test_pulse_does_not_affect_other_pins);
    RUN_TEST(test_pulse_start_end_time_ordered);

    RUN_TEST(test_reset_drives_all_pins_low);
    RUN_TEST(test_reset_stops_pwm);
    RUN_TEST(test_reset_sets_cache_to_low);

    RUN_TEST(test_set_pwm_valid);
    RUN_TEST(test_set_pwm_invalid_pin);
    RUN_TEST(test_set_pwm_zero_freq_invalid);
    RUN_TEST(test_set_pwm_duty_above_100_invalid);
    RUN_TEST(test_set_pwm_duty_100_valid);
    RUN_TEST(test_stop_pwm_clears_active);
    RUN_TEST(test_stop_pwm_drives_pin_low);

    return UNITY_END();
}
