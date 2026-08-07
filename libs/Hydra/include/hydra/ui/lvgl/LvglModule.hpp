#pragma once
/**
 * Hydra — moduł uruchamiający LVGL (rozdz. 6, 4.2).
 *
 * Cały LVGL żyje w jednym tasku — ui.render — i to jest tu najważniejsze.
 * LVGL nie jest thread-safe: wywołanie `lv_label_set_text` z taska czujników
 * w chwili, gdy `lv_timer_handler` przechodzi po drzewie obiektów, uszkadza
 * listę i objawia się kilka klatek później, w zupełnie innym miejscu.
 *
 * Moduł daje na to gwarancję konstrukcyjną: jedyną drogą zmiany interfejsu
 * spoza taska jest kolejka poleceń, ta sama, co w rendererze programowym.
 *
 *     lvgl.queue().post([] { lv_label_set_text(label, "gotowe"); });
 *
 * Moduł jest szablonem po typie cech opisującym API LVGL. Dzięki temu Hydra
 * nie włącza `lvgl.h`, a cała logika pętli — odmierzanie czasu, kolejność
 * kroków, obsługa wywołań zwrotnych — daje się przetestować atrapą.
 * Prawdziwe wiązanie jest w LvglApi.hpp.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/IModule.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/ui/IInput.hpp"
#include "hydra/ui/RenderQueue.hpp"
#include "hydra/ui/lvgl/LvglBridge.hpp"

namespace hydra {
namespace ui {
namespace lvgl {

/**
 * Sygnatura wywołania zwrotnego, którym LVGL oddaje narysowany fragment.
 * `last` jest ustawione przy ostatnim fragmencie klatki — dopiero wtedy
 * warto ruszać panel.
 */
using FlushCallback = void (*)(void* user, Rect area, const u8* pixels, bool last);

/**
 * Kontrakt, jaki musi spełnić typ cech `Lv`:
 *
 *     static Status init();
 *     static void   deinit();
 *     static Status createDisplay(i16 w, i16 h, ColorFormat, FlushCallback, void* user);
 *     static void   flushReady();
 *     static void   tickInc(u32 ms);
 *     static u32    timerHandler();          // ile ms do kolejnego wywołania
 *     static Status createPointer();
 *     static void   feedPointer(PointerFeed);
 *     static Status createEncoder();
 *     static void   feedEncoder(EncoderFeed);
 */
template <typename Lv>
class LvglModule : public ModuleBase {
public:
    struct Config {
        u32         framePeriodMs = 33;
        ColorFormat format        = ColorFormat::Rgb565;
        Prio        priority      = Prio::Low;
        Core        core          = Core::Core0;
        u32         stackWords    = HYDRA_DEFAULT_STACK;
    };

    struct Stats {
        u32 ticks    = 0;
        u32 commands = 0;
        u32 frames   = 0;  ///< klatki zakończone transferem na panel
    };

    explicit LvglModule(IDisplayBackend& display)
        : ModuleBase("ui.lvgl"), display_(display) {}

    Status configure(const Config& cfg) {
        if (cfg.framePeriodMs == 0) return fail(Err::BadArgument);
        cfg_ = cfg;
        return ok();
    }

    void attachPointer(IPointerDevice& device) { pointer_ = &device; }
    void attachEncoder(IEncoderDevice& device) { encoder_ = &device; }

    /** Jedyna bezpieczna droga zmiany interfejsu spoza taska ui.render. */
    RenderQueue& queue() { return queue_; }
    LvglBridge&  bridge() { return bridge_; }
    Stats        stats() const { return stats_; }

    /** Jeden przebieg pętli. Wystawiony publicznie na potrzeby testów. */
    void step(Millis now) {
        ++stats_.ticks;

        // 1. Czas. LVGL prowadzi własne liczniki animacji i odmierza je
        //    wyłącznie tym, co mu podamy.
        if (lastTick_ != 0 && now > lastTick_) Lv::tickInc(now - lastTick_);
        lastTick_ = now;

        // 2. Zmiany z innych tasków — przed przetworzeniem drzewa obiektów,
        //    żeby trafiły w tę samą klatkę.
        stats_.commands += queue_.drain();

        // 3. Wejście. LVGL sam rozpoznaje gesty i przeciąganie, więc dostaje
        //    surowy stan, a nie zdarzenia z InputRoutera.
        feedInput();

        // 4. Rysowanie. W trakcie tego wywołania LVGL odda fragmenty obrazu
        //    przez onFlush().
        Lv::timerHandler();
    }

protected:
    Status onInit() override {
        HYDRA_CHECK(display_.begin());
        HYDRA_CHECK(Lv::init());
        HYDRA_CHECK(bridge_.init(display_, cfg_.format));

        const Size size = display_.size();
        HYDRA_CHECK(Lv::createDisplay(size.w, size.h, cfg_.format, &LvglModule::onFlush,
                                      this));

        if (pointer_) {
            HYDRA_CHECK(pointer_->begin());
            HYDRA_CHECK(Lv::createPointer());
        }
        if (encoder_) {
            HYDRA_CHECK(encoder_->begin());
            HYDRA_CHECK(Lv::createEncoder());
        }

        queue_.reset();
        lastTick_ = 0;
        return ok();
    }

    Status onStart() override {
        Task::Cfg cfg;
        cfg.name       = "ui.render";
        cfg.prio       = cfg_.priority;
        cfg.core       = cfg_.core;
        cfg.stackWords = cfg_.stackWords;
        return task_.startPeriodic(cfg, cfg_.framePeriodMs, [this] { step(rtos::nowMs()); });
    }

    void onStop() override {
        task_.stopAndWait();
        Lv::deinit();
    }

private:
    /** Wywołanie zwrotne LVGL. Statyczne, bo LVGL jest biblioteką w C. */
    static void onFlush(void* user, Rect area, const u8* pixels, bool last) {
        auto* self = static_cast<LvglModule*>(user);
        if (!self) return;

        self->bridge_.flushArea(area, pixels);
        self->pendingArea_ = self->pendingArea_.unite(area);

        if (last) {
            // Panel ruszamy raz na klatkę, po zebraniu wszystkich fragmentów —
            // transfer po każdym z osobna byłby wielokrotnie droższy.
            self->bridge_.present(self->pendingArea_);
            self->pendingArea_ = Rect();
            ++self->stats_.frames;
        }

        // Potwierdzenie musi pójść na końcu: dopiero teraz bufor LVGL
        // jest wolny i wolno mu rysować w nim kolejny fragment.
        Lv::flushReady();
    }

    void feedInput() {
        if (pointer_) {
            if (auto state = pointer_->read(); state) {
                Lv::feedPointer(PointerFeed{state->x, state->y, state->pressed});
            }
        }
        if (encoder_) {
            if (auto state = encoder_->read(); state) {
                // LVGL oczekuje różnicy, nie pozycji bezwzględnej.
                const i32 delta = encoderKnown_ ? state->position - lastEncoderPos_ : 0;
                lastEncoderPos_ = state->position;
                encoderKnown_   = true;

                const i32 clamped = delta > 32767 ? 32767 : (delta < -32768 ? -32768 : delta);
                Lv::feedEncoder(EncoderFeed{static_cast<i16>(clamped), state->pressed});
            }
        }
    }

    IDisplayBackend& display_;
    LvglBridge       bridge_;
    RenderQueue      queue_;
    Config           cfg_{};
    Task             task_;

    IPointerDevice* pointer_ = nullptr;
    IEncoderDevice* encoder_ = nullptr;
    i32             lastEncoderPos_ = 0;
    bool            encoderKnown_   = false;

    Millis lastTick_ = 0;
    Rect   pendingArea_{};
    Stats  stats_{};
};

}  // namespace lvgl
}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
