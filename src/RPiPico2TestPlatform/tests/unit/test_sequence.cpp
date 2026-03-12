// test_sequence.cpp
// Unit tests for SequenceController.
// 22 tests.

#include "unity.h"
#include "mock_hal.hpp"
#include "gpio_ctrl.hpp"
#include "sequence.hpp"

static MockHAL*                          mock;
static gpio_ctrl::GpioController*        gpio;
static sequence_ctrl::SequenceController* seq;

void setUp(void) {
    mock = new MockHAL();
    gpio = new gpio_ctrl::GpioController(*mock, *mock);
    seq  = new sequence_ctrl::SequenceController(*gpio);
}
void tearDown(void) {
    delete seq;
    delete gpio;
    delete mock;
}

// Helper: build a single step
static protocol::SeqStep step(uint8_t pin, PinState state, uint32_t delay_us = 0) {
    return {pin, state, delay_us};
}

// ===========================================================================
// Empty / too-many
// ===========================================================================
void test_seq_empty_returns_error(void) {
    auto r = seq->execute(nullptr, 0);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::EMPTY_SEQUENCE, r.error);
    TEST_ASSERT_FALSE(r.completed);
}
void test_seq_too_many_steps_returns_error(void) {
    protocol::SeqStep steps[protocol::MAX_SEQ_STEPS + 1];
    for (size_t i = 0; i <= protocol::MAX_SEQ_STEPS; ++i) {
        steps[i] = step(0, PinState::HIGH);
    }
    auto r = seq->execute(steps, protocol::MAX_SEQ_STEPS + 1);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::TOO_MANY_STEPS, r.error);
    // No GPIO changes must have occurred
    TEST_ASSERT_EQUAL(0u, mock->set_calls[0]);
}

// ===========================================================================
// Validation-first guarantee
// ===========================================================================
void test_seq_invalid_pin_returns_error_before_gpio(void) {
    protocol::SeqStep steps[] = {
        step(0,  PinState::HIGH),   // valid
        step(30, PinState::HIGH),   // INVALID
        step(1,  PinState::LOW),    // never reached
    };
    auto r = seq->execute(steps, 3);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::INVALID_PIN, r.error);
    // NO GPIO changes because validation failed before execution
    TEST_ASSERT_EQUAL(0u, mock->set_calls[0]);
    TEST_ASSERT_EQUAL(0u, mock->set_calls[1]);
}
void test_seq_pwm_busy_pin_returns_error_before_gpio(void) {
    mock->pwm_active[2] = true;
    protocol::SeqStep steps[] = {
        step(0, PinState::HIGH),
        step(2, PinState::LOW),    // PIN_BUSY
    };
    auto r = seq->execute(steps, 2);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::PIN_BUSY, r.error);
    TEST_ASSERT_EQUAL(0u, mock->set_calls[0]);  // step 0 must NOT have run
}

// ===========================================================================
// Single-step execution
// ===========================================================================
void test_seq_single_step_sets_pin_high(void) {
    protocol::SeqStep steps[] = { step(0, PinState::HIGH) };
    auto r = seq->execute(steps, 1);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::OK, r.error);
    TEST_ASSERT_TRUE(r.completed);
    TEST_ASSERT_EQUAL_size_t(1, r.steps_executed);
    TEST_ASSERT_TRUE(mock->pin_value[0]);
}
void test_seq_single_step_sets_pin_low(void) {
    protocol::SeqStep steps[] = { step(5, PinState::LOW) };
    auto r = seq->execute(steps, 1);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::OK, r.error);
    TEST_ASSERT_FALSE(mock->pin_value[5]);
}

// ===========================================================================
// Multi-step execution
// ===========================================================================
void test_seq_multi_step_correct_order(void) {
    protocol::SeqStep steps[] = {
        step(0, PinState::HIGH),
        step(1, PinState::HIGH),
        step(0, PinState::LOW),
    };
    auto r = seq->execute(steps, 3);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::OK, r.error);
    TEST_ASSERT_TRUE(r.completed);
    TEST_ASSERT_EQUAL_size_t(3, r.steps_executed);
    // Final states
    TEST_ASSERT_FALSE(mock->pin_value[0]);
    TEST_ASSERT_TRUE(mock->pin_value[1]);
}
void test_seq_steps_total_correct(void) {
    protocol::SeqStep steps[] = { step(0, PinState::HIGH), step(1, PinState::LOW) };
    auto r = seq->execute(steps, 2);
    TEST_ASSERT_EQUAL_size_t(2, r.steps_total);
}
void test_seq_all_29_pins_independently(void) {
    protocol::SeqStep steps[HAL_NUM_PINS];
    for (uint8_t i = 0; i < HAL_NUM_PINS; ++i) {
        steps[i] = step(i, PinState::HIGH);
    }
    auto r = seq->execute(steps, HAL_NUM_PINS);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::OK, r.error);
    for (uint8_t i = 0; i < HAL_NUM_PINS; ++i) {
        TEST_ASSERT_TRUE(mock->pin_value[i]);
    }
}

// ===========================================================================
// Delays
// ===========================================================================
void test_seq_delay_advances_mock_time(void) {
    uint64_t t0 = mock->mock_time_us;
    protocol::SeqStep steps[] = {
        step(0, PinState::HIGH, 500),  // 500 µs delay after
        step(0, PinState::LOW,  0),
    };
    auto r = seq->execute(steps, 2);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::OK, r.error);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(500ULL, mock->mock_time_us - t0);
}
void test_seq_zero_delay_does_not_wait(void) {
    uint64_t t0 = mock->mock_time_us;
    protocol::SeqStep steps[] = { step(0, PinState::HIGH, 0) };
    seq->execute(steps, 1);
    TEST_ASSERT_EQUAL_UINT64(t0, mock->mock_time_us);
}
void test_seq_multiple_delays_accumulate(void) {
    uint64_t t0 = mock->mock_time_us;
    protocol::SeqStep steps[] = {
        step(0, PinState::HIGH, 100),
        step(1, PinState::HIGH, 200),
        step(2, PinState::HIGH, 300),
    };
    seq->execute(steps, 3);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(600ULL, mock->mock_time_us - t0);
}

// ===========================================================================
// Completed flag
// ===========================================================================
void test_seq_completed_true_on_success(void) {
    protocol::SeqStep steps[] = { step(0, PinState::HIGH) };
    auto r = seq->execute(steps, 1);
    TEST_ASSERT_TRUE(r.completed);
}
void test_seq_completed_false_on_validation_error(void) {
    protocol::SeqStep steps[] = { step(30, PinState::HIGH) };
    auto r = seq->execute(steps, 1);
    TEST_ASSERT_FALSE(r.completed);
}
void test_seq_completed_false_on_empty(void) {
    auto r = seq->execute(nullptr, 0);
    TEST_ASSERT_FALSE(r.completed);
}

// ===========================================================================
// Max steps boundary
// ===========================================================================
void test_seq_max_steps_executes_all(void) {
    protocol::SeqStep steps[protocol::MAX_SEQ_STEPS];
    for (size_t i = 0; i < protocol::MAX_SEQ_STEPS; ++i) {
        steps[i] = step(i % HAL_NUM_PINS, PinState::HIGH);
    }
    auto r = seq->execute(steps, protocol::MAX_SEQ_STEPS);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::OK, r.error);
    TEST_ASSERT_EQUAL_size_t(protocol::MAX_SEQ_STEPS, r.steps_executed);
}

// ===========================================================================
// Repeated same pin
// ===========================================================================
void test_seq_toggle_same_pin_multiple_times(void) {
    protocol::SeqStep steps[] = {
        step(0, PinState::HIGH),
        step(0, PinState::LOW),
        step(0, PinState::HIGH),
        step(0, PinState::LOW),
    };
    auto r = seq->execute(steps, 4);
    TEST_ASSERT_EQUAL(sequence_ctrl::SequenceError::OK, r.error);
    TEST_ASSERT_EQUAL_size_t(4, r.steps_executed);
    TEST_ASSERT_FALSE(mock->pin_value[0]);  // final state is LOW
}

// ===========================================================================
// Steps_executed on partial run (error mid-sequence)
// This tests the defensive contract: all steps_executed before the bad one ran.
// ===========================================================================
void test_seq_steps_executed_zero_on_validation_failure(void) {
    mock->pwm_active[0] = true;  // make all steps potentially fail validation
    protocol::SeqStep steps[] = { step(0, PinState::HIGH) };
    auto r = seq->execute(steps, 1);
    TEST_ASSERT_EQUAL_size_t(0, r.steps_executed);
}

// ---------------------------------------------------------------------------
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_seq_empty_returns_error);
    RUN_TEST(test_seq_too_many_steps_returns_error);
    RUN_TEST(test_seq_invalid_pin_returns_error_before_gpio);
    RUN_TEST(test_seq_pwm_busy_pin_returns_error_before_gpio);

    RUN_TEST(test_seq_single_step_sets_pin_high);
    RUN_TEST(test_seq_single_step_sets_pin_low);

    RUN_TEST(test_seq_multi_step_correct_order);
    RUN_TEST(test_seq_steps_total_correct);
    RUN_TEST(test_seq_all_29_pins_independently);

    RUN_TEST(test_seq_delay_advances_mock_time);
    RUN_TEST(test_seq_zero_delay_does_not_wait);
    RUN_TEST(test_seq_multiple_delays_accumulate);

    RUN_TEST(test_seq_completed_true_on_success);
    RUN_TEST(test_seq_completed_false_on_validation_error);
    RUN_TEST(test_seq_completed_false_on_empty);

    RUN_TEST(test_seq_max_steps_executes_all);
    RUN_TEST(test_seq_toggle_same_pin_multiple_times);
    RUN_TEST(test_seq_steps_executed_zero_on_validation_failure);

    return UNITY_END();
}
