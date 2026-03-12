// test_pwm_ctrl.cpp
// Unit tests for PwmController.
// 16 tests.

#include "unity.h"
#include "mock_hal.hpp"
#include "pwm_ctrl.hpp"

static MockHAL*              mock;
static pwm_ctrl::PwmController* ctrl;

void setUp(void) {
    mock = new MockHAL();
    ctrl = new pwm_ctrl::PwmController(*mock);
}
void tearDown(void) {
    delete ctrl;
    delete mock;
}

// ===========================================================================
// start()
// ===========================================================================
void test_pwm_start_valid(void) {
    auto err = ctrl->start(0, 1000.0f, 50.0f);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::OK, err);
    TEST_ASSERT_TRUE(mock->pwm_active[0]);
}
void test_pwm_start_stores_frequency(void) {
    ctrl->start(0, 5000.0f, 25.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5000.0f, mock->pwm_freq[0]);
}
void test_pwm_start_converts_duty_to_fraction(void) {
    ctrl->start(0, 1000.0f, 75.0f);
    // MockHAL stores duty as fraction (0..1); controller divides by 100
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, mock->pwm_duty[0]);
}
void test_pwm_start_invalid_pin(void) {
    auto err = ctrl->start(30, 1000.0f, 50.0f);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::INVALID_PIN, err);
    TEST_ASSERT_FALSE(mock->pwm_active[0]);
}
void test_pwm_start_pin_0_boundary(void) {
    auto err = ctrl->start(0, 1.0f, 0.0f);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::OK, err);
}
void test_pwm_start_pin_29_boundary(void) {
    auto err = ctrl->start(29, 1.0f, 100.0f);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::OK, err);
    TEST_ASSERT_TRUE(mock->pwm_active[29]);
}
void test_pwm_start_freq_below_min_invalid(void) {
    auto err = ctrl->start(0, 0.0f, 50.0f);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::INVALID_PARAM, err);
}
void test_pwm_start_freq_above_max_invalid(void) {
    auto err = ctrl->start(0, pwm_ctrl::MAX_FREQ_HZ + 1.0f, 50.0f);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::INVALID_PARAM, err);
}
void test_pwm_start_duty_below_zero_invalid(void) {
    auto err = ctrl->start(0, 1000.0f, -1.0f);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::INVALID_PARAM, err);
}
void test_pwm_start_duty_above_100_invalid(void) {
    auto err = ctrl->start(0, 1000.0f, 100.1f);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::INVALID_PARAM, err);
}
void test_pwm_start_duty_0_valid(void) {
    auto err = ctrl->start(0, 1000.0f, 0.0f);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::OK, err);
}
void test_pwm_start_duty_100_valid(void) {
    auto err = ctrl->start(0, 1000.0f, 100.0f);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::OK, err);
}

// ===========================================================================
// stop()
// ===========================================================================
void test_pwm_stop_clears_active(void) {
    ctrl->start(0, 1000.0f, 50.0f);
    ctrl->stop(0);
    TEST_ASSERT_FALSE(ctrl->is_active(0));
    TEST_ASSERT_FALSE(mock->pwm_active[0]);
}
void test_pwm_stop_drives_pin_low(void) {
    ctrl->start(0, 1000.0f, 50.0f);
    ctrl->stop(0);
    TEST_ASSERT_FALSE(mock->pin_value[0]);
}
void test_pwm_stop_invalid_pin(void) {
    auto err = ctrl->stop(30);
    TEST_ASSERT_EQUAL(pwm_ctrl::PwmError::INVALID_PIN, err);
}

// ===========================================================================
// is_active()
// ===========================================================================
void test_pwm_is_active_initially_false(void) {
    TEST_ASSERT_FALSE(ctrl->is_active(0));
}

// ---------------------------------------------------------------------------
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_pwm_start_valid);
    RUN_TEST(test_pwm_start_stores_frequency);
    RUN_TEST(test_pwm_start_converts_duty_to_fraction);
    RUN_TEST(test_pwm_start_invalid_pin);
    RUN_TEST(test_pwm_start_pin_0_boundary);
    RUN_TEST(test_pwm_start_pin_29_boundary);
    RUN_TEST(test_pwm_start_freq_below_min_invalid);
    RUN_TEST(test_pwm_start_freq_above_max_invalid);
    RUN_TEST(test_pwm_start_duty_below_zero_invalid);
    RUN_TEST(test_pwm_start_duty_above_100_invalid);
    RUN_TEST(test_pwm_start_duty_0_valid);
    RUN_TEST(test_pwm_start_duty_100_valid);
    RUN_TEST(test_pwm_stop_clears_active);
    RUN_TEST(test_pwm_stop_drives_pin_low);
    RUN_TEST(test_pwm_stop_invalid_pin);
    RUN_TEST(test_pwm_is_active_initially_false);

    return UNITY_END();
}
