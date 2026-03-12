#include "command_handler.hpp"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Small JSON builder (stack-allocated, no heap)
// ---------------------------------------------------------------------------
struct JsonBuf {
    char*  buf;
    size_t size;
    size_t pos;
    bool   first_field;

    JsonBuf(char* b, size_t s) : buf(b), size(s), pos(0), first_field(true) {
        append('{');
    }
    void append(char c) {
        if (pos + 1 < size) buf[pos++] = c;
    }
    void append_str(const char* s) {
        while (s && *s && pos + 1 < size) buf[pos++] = *s++;
    }
    void key(const char* k) {
        if (!first_field) append(',');
        first_field = false;
        append('"');
        append_str(k);
        append('"');
        append(':');
    }
    void val_str(const char* v) {
        append('"'); append_str(v); append('"');
    }
    void val_uint(uint64_t v) {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)v);
        append_str(tmp);
    }
    void val_bool(bool v) { append_str(v ? "true" : "false"); }
    size_t finish() {
        append('}');
        if (pos < size) buf[pos] = '\0';
        return pos;
    }
};

// ---------------------------------------------------------------------------
CommandHandler::CommandHandler(gpio_ctrl::GpioController&       gpio,
                               capture_ctrl::CaptureController& capture,
                               sequence_ctrl::SequenceController& seq,
                               pwm_ctrl::PwmController&          pwm)
    : gpio_(gpio), capture_(capture), seq_(seq), pwm_(pwm) {}

// ---------------------------------------------------------------------------
size_t CommandHandler::handle(const char* line, char* buf, size_t buf_size) {
    // Make a mutable copy for the tokeniser
    char tmp[protocol::MAX_LINE_LEN + 1];
    strncpy(tmp, line, protocol::MAX_LINE_LEN);
    tmp[protocol::MAX_LINE_LEN] = '\0';

    protocol::ParsedCommand cmd{};
    if (!protocol::tokenise(tmp, cmd)) {
        return protocol::fmt_err(buf, buf_size, "INVALID_CMD", "empty or malformed line");
    }

    // Dispatch
    if      (strcmp(cmd.name, "SET")      == 0) return do_set(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "READ")     == 0) return do_read(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "PULSE")    == 0) return do_pulse(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "CAPTURE")  == 0) return do_capture(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "SEQUENCE") == 0) return do_sequence(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "PWM")      == 0) return do_pwm(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "PWM_STOP") == 0) return do_pwm_stop(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "RESET")    == 0) return do_reset(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "STATUS")   == 0) return do_status(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "VERSION")  == 0) return do_version(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "PINS")     == 0) return do_pins(cmd, buf, buf_size);
    else if (strcmp(cmd.name, "HELP")     == 0) return do_help(cmd, buf, buf_size);
    else {
        char msg[64];
        snprintf(msg, sizeof(msg), "unknown command '%s'", cmd.name);
        return protocol::fmt_err(buf, buf_size, "INVALID_CMD", msg);
    }
}

// ---------------------------------------------------------------------------
// SET GP<n> HIGH|LOW
// ---------------------------------------------------------------------------
size_t CommandHandler::do_set(const protocol::ParsedCommand& cmd, char* buf, size_t sz) {
    if (cmd.argc < 2)
        return protocol::fmt_err(buf, sz, "INVALID_PARAM", "usage: SET GP<n> HIGH|LOW");

    uint8_t  pin;
    PinState state;
    if (!protocol::parse_pin(cmd.args[0], pin))
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "bad pin argument");
    if (!protocol::parse_state(cmd.args[1], state))
        return protocol::fmt_err(buf, sz, "INVALID_STATE", "expected HIGH or LOW");

    gpio_ctrl::GpioError err = gpio_.set_pin(pin, state);
    if (err == gpio_ctrl::GpioError::INVALID_PIN)
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "pin out of range");
    if (err == gpio_ctrl::GpioError::PIN_BUSY)
        return protocol::fmt_err(buf, sz, "PIN_BUSY", "pin has active PWM");
    return protocol::fmt_ok(buf, sz);
}

// ---------------------------------------------------------------------------
// READ GP<n>
// ---------------------------------------------------------------------------
size_t CommandHandler::do_read(const protocol::ParsedCommand& cmd, char* buf, size_t sz) {
    if (cmd.argc < 1)
        return protocol::fmt_err(buf, sz, "INVALID_PARAM", "usage: READ GP<n>");

    uint8_t pin;
    if (!protocol::parse_pin(cmd.args[0], pin))
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "bad pin argument");

    gpio_ctrl::GpioError err;
    PinState state = gpio_.read_pin(pin, err);
    if (err == gpio_ctrl::GpioError::INVALID_PIN)
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "pin out of range");

    const char* s = (state == PinState::HIGH) ? "HIGH" :
                    (state == PinState::LOW)  ? "LOW"  : "UNDEFINED";
    return protocol::fmt_ok_str(buf, sz, s);
}

// ---------------------------------------------------------------------------
// PULSE GP<n> <duration_us>
// ---------------------------------------------------------------------------
size_t CommandHandler::do_pulse(const protocol::ParsedCommand& cmd, char* buf, size_t sz) {
    if (cmd.argc < 2)
        return protocol::fmt_err(buf, sz, "INVALID_PARAM", "usage: PULSE GP<n> <us>");

    uint8_t  pin;
    uint32_t dur_us;
    if (!protocol::parse_pin(cmd.args[0], pin))
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "bad pin argument");
    if (!protocol::parse_uint(cmd.args[1], HAL_PULSE_US_MIN, HAL_PULSE_US_MAX, dur_us))
        return protocol::fmt_err(buf, sz, "INVALID_PARAM", "duration_us out of range [1,1000000]");

    gpio_ctrl::PulseResult r = gpio_.pulse(pin, dur_us);
    if (r.error == gpio_ctrl::GpioError::INVALID_PIN)
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "pin out of range");
    if (r.error == gpio_ctrl::GpioError::PIN_BUSY)
        return protocol::fmt_err(buf, sz, "PIN_BUSY", "pin has active PWM");

    char json[128];
    JsonBuf j(json, sizeof(json));
    j.key("pin");         j.val_uint(pin);
    j.key("requested_us"); j.val_uint(r.requested_us);
    j.key("actual_us");    j.val_uint(r.end_time_us - r.start_time_us);
    j.key("ok");           j.val_bool(true);
    j.finish();
    return protocol::fmt_data_raw(buf, sz, json);
}

// ---------------------------------------------------------------------------
// CAPTURE GP<n> RISING|FALLING|BOTH <samples> [timeout_ms]
// ---------------------------------------------------------------------------
size_t CommandHandler::do_capture(const protocol::ParsedCommand& cmd, char* buf, size_t sz) {
    if (cmd.argc < 3)
        return protocol::fmt_err(buf, sz, "INVALID_PARAM",
                                 "usage: CAPTURE GP<n> RISING|FALLING|BOTH <samples> [timeout_ms]");

    uint8_t  pin;
    EdgeType edge;
    uint32_t samples, timeout_ms = 1000;
    if (!protocol::parse_pin(cmd.args[0], pin))
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "bad pin argument");
    if (!protocol::parse_edge(cmd.args[1], edge))
        return protocol::fmt_err(buf, sz, "INVALID_EDGE", "expected RISING, FALLING or BOTH");
    if (!protocol::parse_uint(cmd.args[2], 1, capture_ctrl::MAX_CAPTURE_SAMPLES, samples))
        return protocol::fmt_err(buf, sz, "INVALID_PARAM", "samples out of range");
    if (cmd.argc >= 4) {
        if (!protocol::parse_uint(cmd.args[3], 1, capture_ctrl::MAX_CAPTURE_TIMEOUT_MS, timeout_ms))
            return protocol::fmt_err(buf, sz, "INVALID_PARAM", "timeout_ms out of range");
    }

    capture_ctrl::CaptureResult r = capture_.capture(pin, edge, samples, timeout_ms);

    // Build compact JSON: {"count":N,"timed_out":bool,"duration_us":N,"events":[...]}
    // For brevity in the response only send count and timed_out; events are large.
    char json[128];
    JsonBuf j(json, sizeof(json));
    j.key("pin");        j.val_uint(pin);
    j.key("count");      j.val_uint(r.samples_captured);
    j.key("timed_out");  j.val_bool(r.timed_out);
    j.key("duration_us");j.val_uint(r.duration_us);
    j.finish();
    return protocol::fmt_data_raw(buf, sz, json);
}

// ---------------------------------------------------------------------------
// SEQUENCE GP<n>:HIGH|LOW[:<delay_us>] ...
// ---------------------------------------------------------------------------
size_t CommandHandler::do_sequence(const protocol::ParsedCommand& cmd, char* buf, size_t sz) {
    if (cmd.argc == 0)
        return protocol::fmt_err(buf, sz, "INVALID_PARAM",
                                 "usage: SEQUENCE GP<n>:HIGH|LOW[:<us>] ...");
    if (cmd.argc > protocol::MAX_SEQ_STEPS)
        return protocol::fmt_err(buf, sz, "OVERFLOW", "too many steps");

    protocol::SeqStep steps[protocol::MAX_SEQ_STEPS];
    for (size_t i = 0; i < cmd.argc; ++i) {
        if (!protocol::parse_seq_step(cmd.args[i], steps[i])) {
            char msg[64];
            snprintf(msg, sizeof(msg), "bad step %zu: '%s'", i + 1, cmd.args[i]);
            return protocol::fmt_err(buf, sz, "INVALID_PARAM", msg);
        }
    }

    sequence_ctrl::SequenceResult r = seq_.execute(steps, cmd.argc);

    char json[128];
    JsonBuf j(json, sizeof(json));
    j.key("executed");  j.val_uint(r.steps_executed);
    j.key("total");     j.val_uint(r.steps_total);
    j.key("completed"); j.val_bool(r.completed);
    j.key("duration_us"); j.val_uint(r.duration_us);
    j.finish();
    return protocol::fmt_data_raw(buf, sz, json);
}

// ---------------------------------------------------------------------------
// PWM GP<n> <freq_hz> <duty_pct>
// ---------------------------------------------------------------------------
size_t CommandHandler::do_pwm(const protocol::ParsedCommand& cmd, char* buf, size_t sz) {
    if (cmd.argc < 3)
        return protocol::fmt_err(buf, sz, "INVALID_PARAM", "usage: PWM GP<n> <freq_hz> <duty%>");

    uint8_t pin;
    float freq, duty;
    if (!protocol::parse_pin(cmd.args[0], pin))
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "bad pin argument");
    if (!protocol::parse_float(cmd.args[1], pwm_ctrl::MIN_FREQ_HZ, pwm_ctrl::MAX_FREQ_HZ, freq))
        return protocol::fmt_err(buf, sz, "INVALID_PARAM", "freq_hz out of range");
    if (!protocol::parse_float(cmd.args[2], pwm_ctrl::MIN_DUTY_PCT, pwm_ctrl::MAX_DUTY_PCT, duty))
        return protocol::fmt_err(buf, sz, "INVALID_PARAM", "duty_pct out of range [0,100]");

    pwm_ctrl::PwmError err = pwm_.start(pin, freq, duty);
    if (err == pwm_ctrl::PwmError::INVALID_PIN)
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "pin out of range");
    if (err == pwm_ctrl::PwmError::INVALID_PARAM)
        return protocol::fmt_err(buf, sz, "INVALID_PARAM", "bad freq or duty");
    return protocol::fmt_ok(buf, sz);
}

// ---------------------------------------------------------------------------
// PWM_STOP GP<n>
// ---------------------------------------------------------------------------
size_t CommandHandler::do_pwm_stop(const protocol::ParsedCommand& cmd, char* buf, size_t sz) {
    if (cmd.argc < 1)
        return protocol::fmt_err(buf, sz, "INVALID_PARAM", "usage: PWM_STOP GP<n>");

    uint8_t pin;
    if (!protocol::parse_pin(cmd.args[0], pin))
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "bad pin argument");

    pwm_ctrl::PwmError err = pwm_.stop(pin);
    if (err == pwm_ctrl::PwmError::INVALID_PIN)
        return protocol::fmt_err(buf, sz, "INVALID_PIN", "pin out of range");
    return protocol::fmt_ok(buf, sz);
}

// ---------------------------------------------------------------------------
// RESET
// ---------------------------------------------------------------------------
size_t CommandHandler::do_reset(const protocol::ParsedCommand&, char* buf, size_t sz) {
    gpio_.reset();
    return protocol::fmt_ok(buf, sz);
}

// ---------------------------------------------------------------------------
// STATUS
// ---------------------------------------------------------------------------
size_t CommandHandler::do_status(const protocol::ParsedCommand&, char* buf, size_t sz) {
    char json[128];
    JsonBuf j(json, sizeof(json));
    j.key("device");  j.val_str(protocol::DEVICE_NAME);
    j.key("version"); j.val_str(protocol::FIRMWARE_VERSION);
    j.finish();
    return protocol::fmt_data_raw(buf, sz, json);
}

// ---------------------------------------------------------------------------
// VERSION
// ---------------------------------------------------------------------------
size_t CommandHandler::do_version(const protocol::ParsedCommand&, char* buf, size_t sz) {
    char data[64];
    snprintf(data, sizeof(data), "%s/%s", protocol::DEVICE_NAME, protocol::FIRMWARE_VERSION);
    return protocol::fmt_ok_str(buf, sz, data);
}

// ---------------------------------------------------------------------------
// PINS
// ---------------------------------------------------------------------------
size_t CommandHandler::do_pins(const protocol::ParsedCommand&, char* buf, size_t sz) {
    // Return a summary of non-UNDEFINED pins (keep response compact)
    char json[512];
    snprintf(json, sizeof(json), "{\"num_pins\":%d,\"max_pin\":%d}",
             HAL_NUM_PINS, HAL_MAX_PIN);
    return protocol::fmt_data_raw(buf, sz, json);
}

// ---------------------------------------------------------------------------
// HELP
// ---------------------------------------------------------------------------
size_t CommandHandler::do_help(const protocol::ParsedCommand&, char* buf, size_t sz) {
    return protocol::fmt_ok_str(buf, sz,
        "SET GP<n> H|L | READ GP<n> | PULSE GP<n> <us> | "
        "CAPTURE GP<n> RISING|FALLING|BOTH <n> [ms] | "
        "SEQUENCE GP<n>:H|L[:<us>] ... | "
        "PWM GP<n> <hz> <duty%> | PWM_STOP GP<n> | "
        "RESET | STATUS | VERSION | PINS | HELP");
}
