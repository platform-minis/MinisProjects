#pragma once
/**
 * Hydra — wiązanie typu cech z prawdziwym API LVGL 9.
 *
 * JEDYNY plik Hydry włączający `lvgl.h`. Włącza się sam tylko wtedy, gdy
 * biblioteka jest dostępna, więc build bez LVGL po prostu go pomija.
 *
 * Aplikacja używa go tak:
 *
 *     #include <hydra/ui/lvgl/LvglApi.hpp>
 *     #include <hydra/ui/lvgl/LvglModule.hpp>
 *
 *     hydra::ui::lvgl::LvglModule<hydra::ui::lvgl::LvglApi> ui(display);
 *
 * Bufory rysowania są statyczne i konfigurowalne rozmiarem: LVGL nie musi
 * mieć bufora na pełną ramkę — dwa bufory po jednej dziesiątej ekranu
 * wystarczają i tak właśnie zaleca dokumentacja biblioteki. Dla panelu
 * 320×240 to około 2 × 15 kB, co odpowiada budżetowi z rozdz. 6.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI && (defined(HYDRA_UI_LVGL) || __has_include(<lvgl.h>))

#include <lvgl.h>

#include "hydra/ui/lvgl/LvglModule.hpp"

namespace hydra {
namespace ui {
namespace lvgl {

/** Ile linii ekranu mieści bufor rysowania LVGL. */
#ifndef HYDRA_LVGL_BUFFER_LINES
#  define HYDRA_LVGL_BUFFER_LINES 24
#endif
/** Maksymalna szerokość panelu, pod którą wymiarowany jest bufor. */
#ifndef HYDRA_LVGL_MAX_WIDTH
#  define HYDRA_LVGL_MAX_WIDTH 320
#endif

struct LvglApi {
    static Status init() {
        lv_init();
        return ok();
    }

    static void deinit() { lv_deinit(); }

    static Status createDisplay(i16 w, i16 h, ColorFormat format, FlushCallback callback,
                                void* user) {
        if (!callback) return fail(Err::BadArgument);

        display_  = lv_display_create(w, h);
        if (!display_) return fail(Err::OutOfMemory);

        callback_ = callback;
        user_     = user;

        lv_display_set_color_format(display_, toLvFormat(format));
        // Tryb częściowy: LVGL oddaje obraz fragmentami mieszczącymi się
        // w buforze, zamiast wymagać pamięci na pełną ramkę.
        lv_display_set_buffers(display_, buffer1_, buffer2_, sizeof(buffer1_),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display_, &LvglApi::flushThunk);
        return ok();
    }

    static void flushReady() {
        if (display_) lv_display_flush_ready(display_);
    }

    static void tickInc(u32 ms) { lv_tick_inc(ms); }
    static u32  timerHandler() { return lv_timer_handler(); }

    static Status createPointer() {
        pointerDev_ = lv_indev_create();
        if (!pointerDev_) return fail(Err::OutOfMemory);
        lv_indev_set_type(pointerDev_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(pointerDev_, &LvglApi::pointerThunk);
        return ok();
    }

    static void feedPointer(PointerFeed feed) { pointerFeed_ = feed; }

    static Status createEncoder() {
        encoderDev_ = lv_indev_create();
        if (!encoderDev_) return fail(Err::OutOfMemory);
        lv_indev_set_type(encoderDev_, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(encoderDev_, &LvglApi::encoderThunk);
        return ok();
    }

    static void feedEncoder(EncoderFeed feed) {
        // Różnice sumujemy do chwili, aż LVGL po nie sięgnie: pętla interfejsu
        // biegnie z inną częstotliwością niż odczyt enkodera i pojedyncze
        // kliknięcia nie mogą przepadać.
        encoderFeed_.diff = static_cast<i16>(encoderFeed_.diff + feed.diff);
        encoderFeed_.pressed = feed.pressed;
    }

private:
    static lv_color_format_t toLvFormat(ColorFormat format) {
        switch (format) {
            case ColorFormat::Rgb565:
            case ColorFormat::Rgb565Swapped: return LV_COLOR_FORMAT_RGB565;
            case ColorFormat::Rgb888:        return LV_COLOR_FORMAT_RGB888;
            case ColorFormat::Argb8888:      return LV_COLOR_FORMAT_ARGB8888;
            case ColorFormat::Mono1:         return LV_COLOR_FORMAT_I1;
        }
        return LV_COLOR_FORMAT_RGB565;
    }

    static void flushThunk(lv_display_t* display, const lv_area_t* area, uint8_t* px) {
        if (!callback_) {
            lv_display_flush_ready(display);
            return;
        }
        // lv_area_t jest domknięty z obu stron, Rect Hydry trzyma szerokość.
        const Rect rect(static_cast<i16>(area->x1), static_cast<i16>(area->y1),
                        static_cast<i16>(area->x2 - area->x1 + 1),
                        static_cast<i16>(area->y2 - area->y1 + 1));
        callback_(user_, rect, px, lv_display_flush_is_last(display));
    }

    static void pointerThunk(lv_indev_t*, lv_indev_data_t* data) {
        data->point.x = pointerFeed_.x;
        data->point.y = pointerFeed_.y;
        data->state   = pointerFeed_.pressed ? LV_INDEV_STATE_PRESSED
                                             : LV_INDEV_STATE_RELEASED;
    }

    static void encoderThunk(lv_indev_t*, lv_indev_data_t* data) {
        data->enc_diff = encoderFeed_.diff;
        data->state    = encoderFeed_.pressed ? LV_INDEV_STATE_PRESSED
                                              : LV_INDEV_STATE_RELEASED;
        // Różnica jest jednorazowa — po odczycie licznik startuje od zera.
        encoderFeed_.diff = 0;
    }

    static constexpr size_t kBufferBytes =
        static_cast<size_t>(HYDRA_LVGL_MAX_WIDTH) * HYDRA_LVGL_BUFFER_LINES * 2;

    static inline lv_display_t* display_    = nullptr;
    static inline lv_indev_t*   pointerDev_ = nullptr;
    static inline lv_indev_t*   encoderDev_ = nullptr;
    static inline FlushCallback callback_   = nullptr;
    static inline void*         user_       = nullptr;
    static inline PointerFeed   pointerFeed_{};
    static inline EncoderFeed   encoderFeed_{};
    static inline u8            buffer1_[kBufferBytes] = {};
    static inline u8            buffer2_[kBufferBytes] = {};
};

}  // namespace lvgl
}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI && LVGL dostępne
