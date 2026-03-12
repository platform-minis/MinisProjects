#pragma once
// Sequence Controller - SEQUENCE <step1> <step2> ...
// Executes an ordered list of GPIO transitions with per-step delays.
// All steps are validated BEFORE any GPIO change is made (atomic-ish).

#include "hal.hpp"
#include "gpio_ctrl.hpp"
#include "protocol.hpp"
#include <cstdint>
#include <cstddef>

namespace sequence_ctrl {

static constexpr size_t MAX_STEPS = protocol::MAX_SEQ_STEPS;

enum class SequenceError : uint8_t {
    OK = 0,
    EMPTY_SEQUENCE,    // no steps provided
    TOO_MANY_STEPS,    // exceeds MAX_STEPS
    INVALID_PIN,       // step references an invalid pin
    PIN_BUSY,          // step references a pin with active PWM
    INVALID_PARAM,     // malformed step string
};

struct SequenceResult {
    SequenceError error;
    size_t        steps_total;
    size_t        steps_executed;  // number of steps that ran before error
    uint64_t      duration_us;     // wall-clock time from first to last step
    bool          completed;       // steps_executed == steps_total && error == OK
};

class SequenceController {
public:
    // 'gpio' must outlive this object.
    explicit SequenceController(gpio_ctrl::GpioController& gpio);

    // Execute a pre-parsed sequence of steps.
    // Validates all steps first. On validation failure, no GPIO changes occur.
    // On execution failure mid-sequence, the executed steps are NOT rolled back
    // (hardware state is non-deterministic after a mid-run error).
    SequenceResult execute(const protocol::SeqStep* steps, size_t count);

private:
    gpio_ctrl::GpioController& gpio_;

    // Validate all steps without touching hardware.
    // Returns the first error found, or OK if all valid.
    SequenceError validate(const protocol::SeqStep* steps, size_t count) const;
};

} // namespace sequence_ctrl
