/** Hydra — implementacja atrap panelu i wejścia (build hostowy). */

#include "hydra/ui/Mock.hpp"

#if HYDRA_PLAT_HOST && HYDRA_ENABLE_UI

namespace hydra {
namespace ui {
namespace mock {

MockDisplay::MockDisplay() {
    fb_.attach(ByteSpan{pixels_, sizeof(pixels_)}, kWidth, kHeight,
               gfx::PixelFormat::Rgb565);
}

Status MockDisplay::begin() {
    begun_ = true;
    return ok();
}

Status MockDisplay::present(Rect area) {
    if (presentError_ != Err::None) {
        const Err e   = presentError_;
        presentError_ = Err::None;  // wymuszony błąd jest jednorazowy
        return fail(e);
    }
    ++presents_;
    lastArea_ = area;
    return ok();
}

void MockDisplay::clear() {
    presents_     = 0;
    lastArea_     = Rect();
    partial_      = true;
    buffers_      = 1;
    backlight_    = 100;
    begun_        = false;
    presentError_ = Err::None;
    for (auto& b : pixels_) b = 0;
    fb_.clearDirty();
    fb_.resetClip();
}

Result<PointerState> MockPointer::read() {
    if (noData_) {
        noData_ = false;
        return unexpected(Err::WouldBlock);
    }
    return state_;
}

void MockPointer::set(i16 x, i16 y, bool pressed) {
    state_.x       = x;
    state_.y       = y;
    state_.pressed = pressed;
}

void MockPointer::clear() {
    state_  = PointerState{};
    noData_ = false;
}

Result<bool> MockButtons::pressed(u8 index) {
    if (index >= kCount) return unexpected(Err::OutOfRange);
    return state_[index];
}

void MockButtons::set(u8 index, bool down) {
    if (index < kCount) state_[index] = down;
}

void MockButtons::clear() {
    for (auto& s : state_) s = false;
}

}  // namespace mock
}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST && HYDRA_ENABLE_UI
