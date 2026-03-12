// test_cmd_handler.cpp
// Unit tests for CommandHandler - the text protocol dispatch layer.
// Verifies that each command produces the correct response prefix and
// that errors surface the right error codes.
// 28 tests.

#include "unity.h"
#include "mock_hal.hpp"
#include "gpio_ctrl.hpp"
#include "capture.hpp"
#include "sequence.hpp"
#include "pwm_ctrl.hpp"
#include "command_handler.hpp"
#include <cstring>

static MockHAL*                           mock;
static MockCaptureHAL*                    mock_cap;
static gpio_ctrl::GpioController*         gpio;
static capture_ctrl::CaptureController*   cap;
static sequence_ctrl::SequenceController* seq;
static pwm_ctrl::PwmController*           pwm;
static CommandHandler*                    handler;

static char resp[1024];

void setUp(void) {
    mock     = new MockHAL();
    mock_cap = new MockCaptureHAL();
    gpio     = new gpio_ctrl::GpioController(*mock, *mock);
    cap      = new capture_ctrl::CaptureController(*mock_cap);
    seq      = new sequence_ctrl::SequenceController(*gpio);
    pwm      = new pwm_ctrl::PwmController(*mock);
    handler  = new CommandHandler(*gpio, *cap, *seq, *pwm);
    memset(resp, 0, sizeof(resp));
}
void tearDown(void) {
    delete handler; delete pwm; delete seq; delete cap; delete gpio;
    delete mock_cap; delete mock;
}

// Helper: run a command and return the response buffer
static const char* run(const char* cmd) {
    handler->handle(cmd, resp, sizeof(resp));
    return resp;
}

// Helper: check response starts with prefix
static bool starts_with(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// ===========================================================================
// Unknown command
// ===========================================================================
void test_unknown_command_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("FOOBAR"), "ERR:INVALID_CMD:"));
}
void test_empty_line_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("\n"), "ERR:INVALID_CMD:"));
}

// ===========================================================================
// VERSION / HELP / STATUS / RESET / PINS
// ===========================================================================
void test_version_returns_ok(void) {
    TEST_ASSERT_TRUE(starts_with(run("VERSION"), "OK:McuTestPlatform/"));
}
void test_help_returns_ok(void) {
    TEST_ASSERT_TRUE(starts_with(run("HELP"), "OK:"));
}
void test_status_returns_data(void) {
    TEST_ASSERT_TRUE(starts_with(run("STATUS"), "DATA:"));
}
void test_reset_returns_ok(void) {
    TEST_ASSERT_TRUE(starts_with(run("RESET"), "OK"));
}
void test_pins_returns_data(void) {
    TEST_ASSERT_TRUE(starts_with(run("PINS"), "DATA:"));
}

// ===========================================================================
// SET command
// ===========================================================================
void test_set_valid_high_returns_ok(void) {
    TEST_ASSERT_TRUE(starts_with(run("SET GP0 HIGH"), "OK"));
    TEST_ASSERT_TRUE(mock->pin_value[0]);
}
void test_set_valid_low_returns_ok(void) {
    run("SET GP0 HIGH");
    TEST_ASSERT_TRUE(starts_with(run("SET GP0 LOW"), "OK"));
    TEST_ASSERT_FALSE(mock->pin_value[0]);
}
void test_set_invalid_pin_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("SET GP30 HIGH"), "ERR:INVALID_PIN:"));
}
void test_set_invalid_state_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("SET GP0 MAYBE"), "ERR:INVALID_STATE:"));
}
void test_set_missing_args_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("SET GP0"), "ERR:INVALID_PARAM:"));
}
void test_set_busy_pin_returns_err(void) {
    mock->pwm_active[1] = true;
    TEST_ASSERT_TRUE(starts_with(run("SET GP1 HIGH"), "ERR:PIN_BUSY:"));
}
void test_set_lowercase_command_works(void) {
    TEST_ASSERT_TRUE(starts_with(run("set GP0 HIGH"), "OK"));
}

// ===========================================================================
// READ command
// ===========================================================================
void test_read_high_returns_ok_high(void) {
    mock->force_pin(3, true);
    const char* r = run("READ GP3");
    TEST_ASSERT_TRUE(starts_with(r, "OK:HIGH"));
}
void test_read_low_returns_ok_low(void) {
    mock->force_pin(3, false);
    const char* r = run("READ GP3");
    TEST_ASSERT_TRUE(starts_with(r, "OK:LOW"));
}
void test_read_invalid_pin_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("READ GP30"), "ERR:INVALID_PIN:"));
}
void test_read_missing_pin_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("READ"), "ERR:INVALID_PARAM:"));
}

// ===========================================================================
// PULSE command
// ===========================================================================
void test_pulse_valid_returns_data(void) {
    TEST_ASSERT_TRUE(starts_with(run("PULSE GP0 10"), "DATA:"));
}
void test_pulse_zero_duration_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("PULSE GP0 0"), "ERR:INVALID_PARAM:"));
}
void test_pulse_invalid_pin_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("PULSE GP30 10"), "ERR:INVALID_PIN:"));
}
void test_pulse_missing_duration_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("PULSE GP0"), "ERR:INVALID_PARAM:"));
}

// ===========================================================================
// CAPTURE command
// ===========================================================================
void test_capture_valid_returns_data(void) {
    // No injected events; will timeout immediately (1ms)
    TEST_ASSERT_TRUE(starts_with(run("CAPTURE GP0 BOTH 1 1"), "DATA:"));
}
void test_capture_invalid_edge_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("CAPTURE GP0 UP 1 100"), "ERR:INVALID_EDGE:"));
}
void test_capture_missing_args_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("CAPTURE GP0 BOTH"), "ERR:INVALID_PARAM:"));
}

// ===========================================================================
// SEQUENCE command
// ===========================================================================
void test_sequence_valid_returns_data(void) {
    TEST_ASSERT_TRUE(starts_with(run("SEQUENCE GP0:HIGH GP1:LOW"), "DATA:"));
    TEST_ASSERT_TRUE(mock->pin_value[0]);
    TEST_ASSERT_FALSE(mock->pin_value[1]);
}
void test_sequence_bad_step_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("SEQUENCE GP0:MAYBE"), "ERR:INVALID_PARAM:"));
}
void test_sequence_no_args_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("SEQUENCE"), "ERR:INVALID_PARAM:"));
}

// ===========================================================================
// PWM / PWM_STOP
// ===========================================================================
void test_pwm_valid_returns_ok(void) {
    TEST_ASSERT_TRUE(starts_with(run("PWM GP0 1000 50"), "OK"));
    TEST_ASSERT_TRUE(mock->pwm_active[0]);
}
void test_pwm_stop_valid_returns_ok(void) {
    run("PWM GP0 1000 50");
    TEST_ASSERT_TRUE(starts_with(run("PWM_STOP GP0"), "OK"));
    TEST_ASSERT_FALSE(mock->pwm_active[0]);
}
void test_pwm_invalid_pin_returns_err(void) {
    TEST_ASSERT_TRUE(starts_with(run("PWM GP30 1000 50"), "ERR:INVALID_PIN:"));
}
void test_pwm_set_on_busy_pin_returns_err(void) {
    run("PWM GP0 1000 50");
    TEST_ASSERT_TRUE(starts_with(run("SET GP0 HIGH"), "ERR:PIN_BUSY:"));
}

// ---------------------------------------------------------------------------
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_unknown_command_returns_err);
    RUN_TEST(test_empty_line_returns_err);

    RUN_TEST(test_version_returns_ok);
    RUN_TEST(test_help_returns_ok);
    RUN_TEST(test_status_returns_data);
    RUN_TEST(test_reset_returns_ok);
    RUN_TEST(test_pins_returns_data);

    RUN_TEST(test_set_valid_high_returns_ok);
    RUN_TEST(test_set_valid_low_returns_ok);
    RUN_TEST(test_set_invalid_pin_returns_err);
    RUN_TEST(test_set_invalid_state_returns_err);
    RUN_TEST(test_set_missing_args_returns_err);
    RUN_TEST(test_set_busy_pin_returns_err);
    RUN_TEST(test_set_lowercase_command_works);

    RUN_TEST(test_read_high_returns_ok_high);
    RUN_TEST(test_read_low_returns_ok_low);
    RUN_TEST(test_read_invalid_pin_returns_err);
    RUN_TEST(test_read_missing_pin_returns_err);

    RUN_TEST(test_pulse_valid_returns_data);
    RUN_TEST(test_pulse_zero_duration_returns_err);
    RUN_TEST(test_pulse_invalid_pin_returns_err);
    RUN_TEST(test_pulse_missing_duration_returns_err);

    RUN_TEST(test_capture_valid_returns_data);
    RUN_TEST(test_capture_invalid_edge_returns_err);
    RUN_TEST(test_capture_missing_args_returns_err);

    RUN_TEST(test_sequence_valid_returns_data);
    RUN_TEST(test_sequence_bad_step_returns_err);
    RUN_TEST(test_sequence_no_args_returns_err);

    RUN_TEST(test_pwm_valid_returns_ok);
    RUN_TEST(test_pwm_stop_valid_returns_ok);
    RUN_TEST(test_pwm_invalid_pin_returns_err);
    RUN_TEST(test_pwm_set_on_busy_pin_returns_err);

    return UNITY_END();
}
