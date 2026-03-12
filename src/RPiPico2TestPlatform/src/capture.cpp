#include "capture.hpp"

namespace capture_ctrl {

CaptureController::CaptureController(CaptureHAL& hal) : hal_(hal) {}

// ---------------------------------------------------------------------------
bool CaptureController::matches_edge(PinState prev, PinState next, EdgeType edge) {
    // Only logic-level transitions are valid events.
    if (!is_logic(prev) || !is_logic(next)) return false;
    if (prev == next) return false;

    bool rising  = (prev == PinState::LOW  && next == PinState::HIGH);
    bool falling = (prev == PinState::HIGH && next == PinState::LOW);

    switch (edge) {
        case EdgeType::RISING:  return rising;
        case EdgeType::FALLING: return falling;
        case EdgeType::BOTH:    return rising || falling;
    }
    return false;
}

// ---------------------------------------------------------------------------
CaptureResult CaptureController::capture(uint8_t  pin,
                                          EdgeType edge,
                                          size_t   samples,
                                          uint32_t timeout_ms)
{
    CaptureResult result{};
    result.samples_requested = samples;

    // --- Validation ---
    if (pin > HAL_MAX_PIN) {
        result.error = CaptureError::INVALID_PIN;
        return result;
    }
    if (samples == 0 || samples > MAX_CAPTURE_SAMPLES) {
        result.error = CaptureError::INVALID_PARAM;
        return result;
    }
    if (timeout_ms == 0 || timeout_ms > MAX_CAPTURE_TIMEOUT_MS) {
        result.error = CaptureError::INVALID_PARAM;
        return result;
    }

    // --- Arm capture ---
    hal_.configure(pin, edge);
    hal_.start();

    const uint64_t timeout_us = (uint64_t)timeout_ms * 1000ULL;
    uint64_t       elapsed_us = 0;
    PinState       prev_state = PinState::UNDEFINED;

    while (result.samples_captured < samples) {
        uint64_t remaining_us = (elapsed_us < timeout_us)
                                    ? (timeout_us - elapsed_us)
                                    : 0;
        if (remaining_us == 0) {
            result.timed_out = true;
            break;
        }

        SignalEvent ev{};
        bool got = hal_.wait_event(ev, remaining_us);

        // Update elapsed (HAL fills ev.timestamp_us as µs since start)
        if (got) {
            elapsed_us = ev.timestamp_us;
        } else {
            elapsed_us = timeout_us;  // HAL returned because timeout expired
        }

        if (!got) {
            result.timed_out = true;
            break;
        }

        // Apply edge filter
        if (!matches_edge(prev_state, ev.state, edge)) {
            // Event does not match filter; update prev but don't record
            prev_state = ev.state;
            continue;
        }

        result.events[result.samples_captured].timestamp_us = ev.timestamp_us;
        result.events[result.samples_captured].new_state     = ev.state;
        ++result.samples_captured;
        prev_state = ev.state;
    }

    hal_.stop();

    result.duration_us = elapsed_us;
    result.error = (result.timed_out && result.samples_captured < samples)
                       ? CaptureError::TIMEOUT
                       : CaptureError::OK;
    return result;
}

} // namespace capture_ctrl
