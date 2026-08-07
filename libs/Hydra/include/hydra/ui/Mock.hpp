#pragma once
/**
 * Hydra — atrapy panelu i wejścia dla buildu hostowego.
 *
 * Panel atrapowy rysuje do zwykłego framebuffera, więc test może obejrzeć
 * wynik piksel po pikselu, a przy okazji policzyć transfery i sprawdzić,
 * jaki obszar został wysłany. To ostatnie jest sednem etapu: odświeżanie
 * częściowe albo działa, albo cicho wysyła całą ramkę.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_PLAT_HOST && HYDRA_ENABLE_UI

#include "hydra/gfx/Framebuffer.hpp"
#include "hydra/ui/IDisplayBackend.hpp"
#include "hydra/ui/IInput.hpp"

namespace hydra {
namespace ui {
namespace mock {

class MockDisplay : public IDisplayBackend {
public:
    static constexpr i16 kWidth  = 64;
    static constexpr i16 kHeight = 32;

    MockDisplay();

    const char*    name() const override { return "mock"; }
    gfx::ISurface& surface() override { return fb_; }
    Status         begin() override;
    Status         present(Rect area) override;

    bool supportsPartial() const override { return partial_; }
    u8   bufferCount() const override { return buffers_; }

    Status setBacklight(u8 percent) override {
        backlight_ = percent;
        return ok();
    }

    // --- sterowanie atrapą ---
    void setPartial(bool on) { partial_ = on; }
    void setBufferCount(u8 n) { buffers_ = n; }
    /** Wymusza błąd przy kolejnym transferze. */
    void failNextPresent(Err e) { presentError_ = e; }

    u32  presents() const { return presents_; }
    Rect lastArea() const { return lastArea_; }
    u8   backlight() const { return backlight_; }
    bool begun() const { return begun_; }

    gfx::Framebuffer& framebuffer() { return fb_; }
    void clear();

private:
    u8               pixels_[gfx::Framebuffer::bytesNeeded(kWidth, kHeight,
                                                           gfx::PixelFormat::Rgb565)] = {};
    gfx::Framebuffer fb_;
    bool             partial_      = true;
    u8               buffers_      = 1;
    u32              presents_     = 0;
    Rect             lastArea_{};
    u8               backlight_    = 100;
    bool             begun_        = false;
    Err              presentError_ = Err::None;
};

class MockPointer : public IPointerDevice {
public:
    Result<PointerState> read() override;

    /** Ustawia stan, jaki zwróci kolejny odczyt. */
    void set(i16 x, i16 y, bool pressed);
    /** Kolejny odczyt zgłosi brak nowych danych. */
    void reportNoData() { noData_ = true; }
    void clear();

private:
    PointerState state_{};
    bool         noData_ = false;
};

class MockEncoder : public IEncoderDevice {
public:
    Result<EncoderState> read() override { return state_; }

    void setPosition(i32 pos) { state_.position = pos; }
    void setPressed(bool p) { state_.pressed = p; }
    void clear() { state_ = EncoderState{}; }

private:
    EncoderState state_{};
};

class MockButtons : public IButtonDevice {
public:
    static constexpr u8 kCount = 3;

    u8            count() const override { return kCount; }
    Result<bool>  pressed(u8 index) override;

    void set(u8 index, bool down);
    void clear();

private:
    bool state_[kCount] = {};
};

}  // namespace mock
}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST && HYDRA_ENABLE_UI
