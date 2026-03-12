// test_protocol.cpp
// Unit tests for protocol.cpp: parse_pin, parse_state, parse_edge,
// parse_uint, parse_float, parse_seq_step, tokenise, fmt_* functions.
// 35 tests.

#include "unity.h"
#include "protocol.hpp"
#include <cstring>

void setUp(void)    {}
void tearDown(void) {}

// ===========================================================================
// parse_pin
// ===========================================================================
void test_parse_pin_gp_prefix_lowercase(void) {
    uint8_t p; TEST_ASSERT_TRUE(protocol::parse_pin("gp0", p)); TEST_ASSERT_EQUAL_UINT8(0, p);
}
void test_parse_pin_gp_prefix_uppercase(void) {
    uint8_t p; TEST_ASSERT_TRUE(protocol::parse_pin("GP5", p)); TEST_ASSERT_EQUAL_UINT8(5, p);
}
void test_parse_pin_numeric_only(void) {
    uint8_t p; TEST_ASSERT_TRUE(protocol::parse_pin("10", p)); TEST_ASSERT_EQUAL_UINT8(10, p);
}
void test_parse_pin_zero(void) {
    uint8_t p; TEST_ASSERT_TRUE(protocol::parse_pin("0", p)); TEST_ASSERT_EQUAL_UINT8(0, p);
}
void test_parse_pin_max_valid(void) {
    uint8_t p; TEST_ASSERT_TRUE(protocol::parse_pin("GP29", p)); TEST_ASSERT_EQUAL_UINT8(29, p);
}
void test_parse_pin_out_of_range_30(void) {
    uint8_t p = 0; TEST_ASSERT_FALSE(protocol::parse_pin("30", p));
}
void test_parse_pin_out_of_range_255(void) {
    uint8_t p = 0; TEST_ASSERT_FALSE(protocol::parse_pin("255", p));
}
void test_parse_pin_empty(void) {
    uint8_t p = 0; TEST_ASSERT_FALSE(protocol::parse_pin("", p));
}
void test_parse_pin_null(void) {
    uint8_t p = 0; TEST_ASSERT_FALSE(protocol::parse_pin(nullptr, p));
}
void test_parse_pin_letters_only(void) {
    uint8_t p = 0; TEST_ASSERT_FALSE(protocol::parse_pin("ABC", p));
}
void test_parse_pin_trailing_chars(void) {
    uint8_t p = 0; TEST_ASSERT_FALSE(protocol::parse_pin("5x", p));
}
void test_parse_pin_negative(void) {
    uint8_t p = 0; TEST_ASSERT_FALSE(protocol::parse_pin("-1", p));
}

// ===========================================================================
// parse_state
// ===========================================================================
void test_parse_state_HIGH(void) {
    PinState s; TEST_ASSERT_TRUE(protocol::parse_state("HIGH", s)); TEST_ASSERT_EQUAL(PinState::HIGH, s);
}
void test_parse_state_LOW(void) {
    PinState s; TEST_ASSERT_TRUE(protocol::parse_state("LOW", s)); TEST_ASSERT_EQUAL(PinState::LOW, s);
}
void test_parse_state_1(void) {
    PinState s; TEST_ASSERT_TRUE(protocol::parse_state("1", s)); TEST_ASSERT_EQUAL(PinState::HIGH, s);
}
void test_parse_state_0(void) {
    PinState s; TEST_ASSERT_TRUE(protocol::parse_state("0", s)); TEST_ASSERT_EQUAL(PinState::LOW, s);
}
void test_parse_state_H_shorthand(void) {
    PinState s; TEST_ASSERT_TRUE(protocol::parse_state("H", s)); TEST_ASSERT_EQUAL(PinState::HIGH, s);
}
void test_parse_state_L_shorthand(void) {
    PinState s; TEST_ASSERT_TRUE(protocol::parse_state("L", s)); TEST_ASSERT_EQUAL(PinState::LOW, s);
}
void test_parse_state_lowercase_high(void) {
    PinState s; TEST_ASSERT_TRUE(protocol::parse_state("high", s)); TEST_ASSERT_EQUAL(PinState::HIGH, s);
}
void test_parse_state_invalid(void) {
    PinState s; TEST_ASSERT_FALSE(protocol::parse_state("MAYBE", s));
}
void test_parse_state_empty(void) {
    PinState s; TEST_ASSERT_FALSE(protocol::parse_state("", s));
}

// ===========================================================================
// parse_edge
// ===========================================================================
void test_parse_edge_RISING(void) {
    EdgeType e; TEST_ASSERT_TRUE(protocol::parse_edge("RISING", e)); TEST_ASSERT_EQUAL(EdgeType::RISING, e);
}
void test_parse_edge_FALLING(void) {
    EdgeType e; TEST_ASSERT_TRUE(protocol::parse_edge("FALLING", e)); TEST_ASSERT_EQUAL(EdgeType::FALLING, e);
}
void test_parse_edge_BOTH(void) {
    EdgeType e; TEST_ASSERT_TRUE(protocol::parse_edge("BOTH", e)); TEST_ASSERT_EQUAL(EdgeType::BOTH, e);
}
void test_parse_edge_lowercase(void) {
    EdgeType e; TEST_ASSERT_TRUE(protocol::parse_edge("rising", e)); TEST_ASSERT_EQUAL(EdgeType::RISING, e);
}
void test_parse_edge_invalid(void) {
    EdgeType e; TEST_ASSERT_FALSE(protocol::parse_edge("UP", e));
}

// ===========================================================================
// parse_uint / parse_float
// ===========================================================================
void test_parse_uint_in_range(void) {
    uint32_t v; TEST_ASSERT_TRUE(protocol::parse_uint("500", 1, 1000, v)); TEST_ASSERT_EQUAL_UINT32(500, v);
}
void test_parse_uint_at_min(void) {
    uint32_t v; TEST_ASSERT_TRUE(protocol::parse_uint("1", 1, 1000, v)); TEST_ASSERT_EQUAL_UINT32(1, v);
}
void test_parse_uint_at_max(void) {
    uint32_t v; TEST_ASSERT_TRUE(protocol::parse_uint("1000", 1, 1000, v)); TEST_ASSERT_EQUAL_UINT32(1000, v);
}
void test_parse_uint_below_min(void) {
    uint32_t v; TEST_ASSERT_FALSE(protocol::parse_uint("0", 1, 1000, v));
}
void test_parse_uint_above_max(void) {
    uint32_t v; TEST_ASSERT_FALSE(protocol::parse_uint("1001", 1, 1000, v));
}
void test_parse_uint_not_numeric(void) {
    uint32_t v; TEST_ASSERT_FALSE(protocol::parse_uint("abc", 0, 1000, v));
}
void test_parse_float_valid(void) {
    float v; TEST_ASSERT_TRUE(protocol::parse_float("1000.5", 1.0f, 2000.0f, v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.5f, v);
}
void test_parse_float_out_of_range(void) {
    float v; TEST_ASSERT_FALSE(protocol::parse_float("0.5", 1.0f, 2000.0f, v));
}

// ===========================================================================
// parse_seq_step
// ===========================================================================
void test_parse_seq_step_full(void) {
    protocol::SeqStep s;
    TEST_ASSERT_TRUE(protocol::parse_seq_step("GP3:HIGH:250", s));
    TEST_ASSERT_EQUAL_UINT8(3, s.pin);
    TEST_ASSERT_EQUAL(PinState::HIGH, s.state);
    TEST_ASSERT_EQUAL_UINT32(250, s.delay_after_us);
}
void test_parse_seq_step_no_delay(void) {
    protocol::SeqStep s;
    TEST_ASSERT_TRUE(protocol::parse_seq_step("GP0:LOW", s));
    TEST_ASSERT_EQUAL_UINT8(0, s.pin);
    TEST_ASSERT_EQUAL(PinState::LOW, s.state);
    TEST_ASSERT_EQUAL_UINT32(0, s.delay_after_us);
}
void test_parse_seq_step_numeric_pin(void) {
    protocol::SeqStep s;
    TEST_ASSERT_TRUE(protocol::parse_seq_step("7:HIGH:100", s));
    TEST_ASSERT_EQUAL_UINT8(7, s.pin);
}
void test_parse_seq_step_invalid_pin(void) {
    protocol::SeqStep s; TEST_ASSERT_FALSE(protocol::parse_seq_step("GP30:HIGH", s));
}
void test_parse_seq_step_invalid_state(void) {
    protocol::SeqStep s; TEST_ASSERT_FALSE(protocol::parse_seq_step("GP0:MAYBE", s));
}
void test_parse_seq_step_missing_state(void) {
    protocol::SeqStep s; TEST_ASSERT_FALSE(protocol::parse_seq_step("GP0", s));
}

// ===========================================================================
// tokenise
// ===========================================================================
void test_tokenise_simple_set(void) {
    char buf[] = "SET GP0 HIGH\n";
    protocol::ParsedCommand cmd{};
    TEST_ASSERT_TRUE(protocol::tokenise(buf, cmd));
    TEST_ASSERT_EQUAL_STRING("SET", cmd.name);
    TEST_ASSERT_EQUAL_size_t(2, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("GP0",  cmd.args[0]);
    TEST_ASSERT_EQUAL_STRING("HIGH", cmd.args[1]);
}
void test_tokenise_strips_cr_lf(void) {
    char buf[] = "READ GP5\r\n";
    protocol::ParsedCommand cmd{};
    TEST_ASSERT_TRUE(protocol::tokenise(buf, cmd));
    TEST_ASSERT_EQUAL_STRING("READ", cmd.name);
    TEST_ASSERT_EQUAL_UINT32(1, cmd.argc);
}
void test_tokenise_uppercases_name(void) {
    char buf[] = "version";
    protocol::ParsedCommand cmd{};
    TEST_ASSERT_TRUE(protocol::tokenise(buf, cmd));
    TEST_ASSERT_EQUAL_STRING("VERSION", cmd.name);
}
void test_tokenise_empty_line(void) {
    char buf[] = "\n";
    protocol::ParsedCommand cmd{};
    TEST_ASSERT_FALSE(protocol::tokenise(buf, cmd));
}
void test_tokenise_no_args(void) {
    char buf[] = "RESET";
    protocol::ParsedCommand cmd{};
    TEST_ASSERT_TRUE(protocol::tokenise(buf, cmd));
    TEST_ASSERT_EQUAL_STRING("RESET", cmd.name);
    TEST_ASSERT_EQUAL_UINT32(0, cmd.argc);
}

// ===========================================================================
// Response formatters
// ===========================================================================
void test_fmt_ok_no_data(void) {
    char buf[32]; protocol::fmt_ok(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("OK\n", buf);
}
void test_fmt_ok_with_data(void) {
    char buf[32]; protocol::fmt_ok_str(buf, sizeof(buf), "HIGH");
    TEST_ASSERT_EQUAL_STRING("OK:HIGH\n", buf);
}
void test_fmt_err(void) {
    char buf[64]; protocol::fmt_err(buf, sizeof(buf), "INVALID_PIN", "out of range");
    TEST_ASSERT_EQUAL_STRING("ERR:INVALID_PIN:out of range\n", buf);
}
void test_fmt_data_raw(void) {
    char buf[64]; protocol::fmt_data_raw(buf, sizeof(buf), "{\"k\":1}");
    TEST_ASSERT_EQUAL_STRING("DATA:{\"k\":1}\n", buf);
}
void test_fmt_ok_truncates_gracefully(void) {
    char buf[5]; size_t n = protocol::fmt_ok(buf, sizeof(buf));
    // Should not write beyond buf; buf[4] is the terminator
    TEST_ASSERT_LESS_OR_EQUAL_size_t(5, n + 1);
}

// ===========================================================================
// invert_state helper (from hal.hpp)
// ===========================================================================
void test_invert_high_gives_low(void) {
    TEST_ASSERT_EQUAL(PinState::LOW, invert_state(PinState::HIGH));
}
void test_invert_low_gives_high(void) {
    TEST_ASSERT_EQUAL(PinState::HIGH, invert_state(PinState::LOW));
}
void test_invert_undefined_unchanged(void) {
    TEST_ASSERT_EQUAL(PinState::UNDEFINED, invert_state(PinState::UNDEFINED));
}
void test_invert_float_unchanged(void) {
    TEST_ASSERT_EQUAL(PinState::FLOAT, invert_state(PinState::FLOAT));
}

// ---------------------------------------------------------------------------
int main(void) {
    UNITY_BEGIN();

    // parse_pin
    RUN_TEST(test_parse_pin_gp_prefix_lowercase);
    RUN_TEST(test_parse_pin_gp_prefix_uppercase);
    RUN_TEST(test_parse_pin_numeric_only);
    RUN_TEST(test_parse_pin_zero);
    RUN_TEST(test_parse_pin_max_valid);
    RUN_TEST(test_parse_pin_out_of_range_30);
    RUN_TEST(test_parse_pin_out_of_range_255);
    RUN_TEST(test_parse_pin_empty);
    RUN_TEST(test_parse_pin_null);
    RUN_TEST(test_parse_pin_letters_only);
    RUN_TEST(test_parse_pin_trailing_chars);
    RUN_TEST(test_parse_pin_negative);

    // parse_state
    RUN_TEST(test_parse_state_HIGH);
    RUN_TEST(test_parse_state_LOW);
    RUN_TEST(test_parse_state_1);
    RUN_TEST(test_parse_state_0);
    RUN_TEST(test_parse_state_H_shorthand);
    RUN_TEST(test_parse_state_L_shorthand);
    RUN_TEST(test_parse_state_lowercase_high);
    RUN_TEST(test_parse_state_invalid);
    RUN_TEST(test_parse_state_empty);

    // parse_edge
    RUN_TEST(test_parse_edge_RISING);
    RUN_TEST(test_parse_edge_FALLING);
    RUN_TEST(test_parse_edge_BOTH);
    RUN_TEST(test_parse_edge_lowercase);
    RUN_TEST(test_parse_edge_invalid);

    // parse_uint / parse_float
    RUN_TEST(test_parse_uint_in_range);
    RUN_TEST(test_parse_uint_at_min);
    RUN_TEST(test_parse_uint_at_max);
    RUN_TEST(test_parse_uint_below_min);
    RUN_TEST(test_parse_uint_above_max);
    RUN_TEST(test_parse_uint_not_numeric);
    RUN_TEST(test_parse_float_valid);
    RUN_TEST(test_parse_float_out_of_range);

    // parse_seq_step
    RUN_TEST(test_parse_seq_step_full);
    RUN_TEST(test_parse_seq_step_no_delay);
    RUN_TEST(test_parse_seq_step_numeric_pin);
    RUN_TEST(test_parse_seq_step_invalid_pin);
    RUN_TEST(test_parse_seq_step_invalid_state);
    RUN_TEST(test_parse_seq_step_missing_state);

    // tokenise
    RUN_TEST(test_tokenise_simple_set);
    RUN_TEST(test_tokenise_strips_cr_lf);
    RUN_TEST(test_tokenise_uppercases_name);
    RUN_TEST(test_tokenise_empty_line);
    RUN_TEST(test_tokenise_no_args);

    // formatters
    RUN_TEST(test_fmt_ok_no_data);
    RUN_TEST(test_fmt_ok_with_data);
    RUN_TEST(test_fmt_err);
    RUN_TEST(test_fmt_data_raw);
    RUN_TEST(test_fmt_ok_truncates_gracefully);

    // hal helpers
    RUN_TEST(test_invert_high_gives_low);
    RUN_TEST(test_invert_low_gives_high);
    RUN_TEST(test_invert_undefined_unchanged);
    RUN_TEST(test_invert_float_unchanged);

    return UNITY_END();
}
