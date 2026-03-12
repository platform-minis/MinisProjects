#include "sequence.hpp"

namespace sequence_ctrl {

SequenceController::SequenceController(gpio_ctrl::GpioController& gpio)
    : gpio_(gpio) {}

// ---------------------------------------------------------------------------
SequenceError SequenceController::validate(const protocol::SeqStep* steps,
                                            size_t count) const
{
    for (size_t i = 0; i < count; ++i) {
        if (!gpio_ctrl::GpioController::is_valid_pin(steps[i].pin)) {
            return SequenceError::INVALID_PIN;
        }
        if (gpio_.is_pin_busy(steps[i].pin)) {
            return SequenceError::PIN_BUSY;
        }
    }
    return SequenceError::OK;
}

// ---------------------------------------------------------------------------
SequenceResult SequenceController::execute(const protocol::SeqStep* steps,
                                            size_t count)
{
    SequenceResult result{};
    result.steps_total    = count;
    result.steps_executed = 0;
    result.completed      = false;

    if (count == 0) {
        result.error = SequenceError::EMPTY_SEQUENCE;
        return result;
    }
    if (count > MAX_STEPS) {
        result.error = SequenceError::TOO_MANY_STEPS;
        return result;
    }

    // Validate ALL steps before touching any GPIO.
    SequenceError val_err = validate(steps, count);
    if (val_err != SequenceError::OK) {
        result.error = val_err;
        return result;
    }

    // Execute
    for (size_t i = 0; i < count; ++i) {
        gpio_ctrl::GpioError err = gpio_.set_pin(steps[i].pin, steps[i].state);
        if (err != gpio_ctrl::GpioError::OK) {
            // Translate gpio error
            result.error = (err == gpio_ctrl::GpioError::INVALID_PIN)
                               ? SequenceError::INVALID_PIN
                               : SequenceError::PIN_BUSY;
            result.steps_executed = i;
            return result;
        }
        ++result.steps_executed;

        if (steps[i].delay_after_us > 0) {
            // GpioHAL is accessed through the GpioController; we need the raw
            // HAL for delay. We expose it via a friend accessor or call gpio_.delay.
            // To keep the controller decoupled, sequence_ctrl uses a delay callback
            // captured from the HAL via gpio_.delay_us() workaround:
            // NOTE: In the real Pico firmware GpioController exposes delay_us().
            // For the mock, we rely on MockHAL advancing time instantly.
            gpio_.delay_us(steps[i].delay_after_us);
        }
    }

    result.error    = SequenceError::OK;
    result.completed = true;
    return result;
}

} // namespace sequence_ctrl
