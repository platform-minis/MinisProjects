#pragma once
// CommandHandler - ties protocol parser to all controllers.
// One call: handle(line, buf, buf_size) -> response bytes written.

#include "protocol.hpp"
#include "gpio_ctrl.hpp"
#include "capture.hpp"
#include "sequence.hpp"
#include "pwm_ctrl.hpp"
#include <cstddef>

class CommandHandler {
public:
    CommandHandler(gpio_ctrl::GpioController&    gpio,
                   capture_ctrl::CaptureController& capture,
                   sequence_ctrl::SequenceController& seq,
                   pwm_ctrl::PwmController&       pwm);

    // Parse 'line' (null-terminated, may have trailing \r\n) and write the
    // ASCII response into 'buf'. Returns bytes written (includes '\n').
    size_t handle(const char* line, char* buf, size_t buf_size);

private:
    gpio_ctrl::GpioController&      gpio_;
    capture_ctrl::CaptureController& capture_;
    sequence_ctrl::SequenceController& seq_;
    pwm_ctrl::PwmController&         pwm_;

    size_t do_set    (const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_read   (const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_pulse  (const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_capture(const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_sequence(const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_pwm    (const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_pwm_stop(const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_reset  (const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_status (const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_version(const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_pins   (const protocol::ParsedCommand& cmd, char* buf, size_t sz);
    size_t do_help   (const protocol::ParsedCommand& cmd, char* buf, size_t sz);
};
