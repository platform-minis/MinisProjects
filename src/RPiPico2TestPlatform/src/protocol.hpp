#pragma once
// Text-based command protocol for the Stimulus Controller
//
// Each command is an ASCII line terminated by '\n'.
// Each response is an ASCII line terminated by '\n'.
//
// Response prefixes:
//   OK\n              - success, no payload
//   OK:<data>\n       - success with string payload
//   ERR:<CODE>:<msg>\n - error with code and human-readable message
//   DATA:<json>\n     - success with JSON payload
//
// Error codes (stable identifiers used by host-side parsers):
//   INVALID_CMD    - unrecognised command name
//   INVALID_PIN    - pin ID out of range or unparseable
//   INVALID_STATE  - state argument is not HIGH or LOW
//   INVALID_EDGE   - edge argument is not RISING, FALLING or BOTH
//   INVALID_PARAM  - numeric argument out of range or unparseable
//   PIN_BUSY       - pin has an active PWM output
//   TIMEOUT        - operation did not complete within the allowed time
//   OVERFLOW       - sequence is too long or buffer would overflow

#include <cstdint>
#include <cstddef>
#include <optional>
#include "hal.hpp"

namespace protocol {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr const char* FIRMWARE_VERSION = "1.0.0";
static constexpr const char* DEVICE_NAME      = "McuTestPlatform";
static constexpr size_t      MAX_LINE_LEN     = 256;
static constexpr size_t      MAX_SEQ_STEPS    = 64;
static constexpr size_t      MAX_CAPTURE_SAMPLES = 4096;
static constexpr uint32_t    MAX_TIMEOUT_MS   = 60'000;
static constexpr float       MAX_PWM_FREQ_HZ  = 100'000'000.0f;

// ---------------------------------------------------------------------------
// Parsed argument types
// ---------------------------------------------------------------------------

// Represents one step of a SEQUENCE command: GP<n>:HIGH|LOW:<delay_us>
struct SeqStep {
    uint8_t  pin;
    PinState state;
    uint32_t delay_after_us;  // delay AFTER setting pin (may be 0)
};

// A command after tokenisation (name + raw argument strings)
struct ParsedCommand {
    // NULL-terminated command name (upper-cased during parse)
    char name[24];

    // Raw argument tokens (points into the original line buffer)
    // Valid for the lifetime of the source buffer.
    const char* args[MAX_SEQ_STEPS + 4];
    size_t      argc;
};

// ---------------------------------------------------------------------------
// Low-level parsing helpers
// (return false / empty optional on failure; no side effects on error)
// ---------------------------------------------------------------------------

// Parse "GPn" or plain "n" (0..HAL_MAX_PIN) into a pin ID.
// Returns false if the string cannot be parsed or pin ID is out of range.
bool parse_pin(const char* s, uint8_t& out);

// Parse "HIGH", "LOW", "1", "0", "H", "L" (case-insensitive).
bool parse_state(const char* s, PinState& out);

// Parse "RISING", "FALLING", "BOTH" (case-insensitive).
bool parse_edge(const char* s, EdgeType& out);

// Parse an unsigned integer in [min_val, max_val].
bool parse_uint(const char* s, uint32_t min_val, uint32_t max_val, uint32_t& out);

// Parse a float in [min_val, max_val].
bool parse_float(const char* s, float min_val, float max_val, float& out);

// Parse a sequence step token "GP<n>:HIGH|LOW[:<delay_us>]"
// The three sub-fields are separated by ':'.
bool parse_seq_step(const char* token, SeqStep& out);

// ---------------------------------------------------------------------------
// Tokeniser
// The caller must guarantee that 'buf' is writable and lives at least as long
// as 'cmd' is used (args[] point into buf).
// Returns false if the line is empty or exceeds MAX_LINE_LEN.
// ---------------------------------------------------------------------------
bool tokenise(char* buf, ParsedCommand& cmd);

// ---------------------------------------------------------------------------
// Response formatting
// Writes into caller-supplied 'buf' of 'buf_size' bytes.
// All responses include the trailing '\n'.
// Returns number of bytes written (excluding null terminator), or 0 on error.
// ---------------------------------------------------------------------------
size_t fmt_ok(char* buf, size_t buf_size);
size_t fmt_ok_str(char* buf, size_t buf_size, const char* data);
size_t fmt_err(char* buf, size_t buf_size, const char* code, const char* msg);

// Minimal JSON builder for DATA responses.
// Usage: open data_json(), append key-value pairs, close with data_json_end().
// Alternatively use fmt_data_raw() for a pre-built JSON string.
size_t fmt_data_raw(char* buf, size_t buf_size, const char* json);

} // namespace protocol
