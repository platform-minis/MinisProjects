#pragma once
// Capture Controller - CAPTURE <pin> RISING|FALLING|BOTH <samples> [timeout_ms]
// Collects up to 'samples' edge events on a single pin within timeout_ms.
// Edge filtering (RISING / FALLING / BOTH) is applied in this layer so that
// MockCaptureHAL can inject raw transitions and the controller filters correctly.

#include "hal.hpp"
#include <cstdint>
#include <cstddef>

namespace capture_ctrl {

static constexpr size_t   MAX_CAPTURE_SAMPLES = 4096;
static constexpr uint32_t MAX_CAPTURE_TIMEOUT_MS = 60'000;

enum class CaptureError : uint8_t {
    OK = 0,
    INVALID_PIN,
    INVALID_PARAM,  // bad samples or timeout value
    TIMEOUT,        // fewer than 'samples' events arrived before timeout
};

// A single captured edge event
struct CaptureEvent {
    uint64_t timestamp_us;  // µs elapsed since capture() was called
    PinState new_state;     // pin state AFTER the transition
};

// Complete result of one CAPTURE call
struct CaptureResult {
    CaptureError error;
    size_t       samples_requested;
    size_t       samples_captured;  // may be < requested when timed_out == true
    bool         timed_out;
    uint64_t     duration_us;       // total wall-clock time of the capture window

    // Chronologically ordered events (indices 0 .. samples_captured-1 are valid)
    CaptureEvent events[MAX_CAPTURE_SAMPLES];
};

class CaptureController {
public:
    // 'hal' must outlive this object.
    explicit CaptureController(CaptureHAL& hal);

    // Capture up to 'samples' matching edges on 'pin'.
    //   RISING  - records LOW->HIGH transitions only
    //   FALLING - records HIGH->LOW transitions only
    //   BOTH    - records every transition
    // timeout_ms must be in [1 .. MAX_CAPTURE_TIMEOUT_MS].
    // The result is always populated; check result.error for failures.
    CaptureResult capture(uint8_t  pin,
                          EdgeType edge,
                          size_t   samples,
                          uint32_t timeout_ms);

private:
    CaptureHAL& hal_;

    // Returns true if the transition prev->next should be recorded given 'edge'.
    static bool matches_edge(PinState prev, PinState next, EdgeType edge);
};

} // namespace capture_ctrl
