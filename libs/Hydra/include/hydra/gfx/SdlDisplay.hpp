#pragma once
/**
 * Hydra — okno SDL jako powierzchnia rysowania na buildzie natywnym.
 *
 * To jest odpowiednik panelu na SPI dla celu `native`: aplikacja rysuje przez
 * to samo `gfx::ISurface`, co na sprzęcie, a różnicę widać wyłącznie w tych
 * kilku liniach, które tworzą powierzchnię. Dzięki temu ekran zaprojektowany
 * w oknie na PC trafia na OLED bez zmiany ani jednej linii kodu rysującego.
 *
 *     static u8 vram[hydra::gfx::SdlDisplay::bytesNeeded(320, 240,
 *                                                        hydra::gfx::PixelFormat::Rgb565)];
 *     hydra::gfx::SdlDisplay display;
 *     hydra::gfx::SdlDisplay::Cfg cfg;
 *     cfg.title = "rover-01";
 *     cfg.width = 320; cfg.height = 240; cfg.scale = 2;
 *     HYDRA_CHECK(display.begin(hydra::ByteSpan{vram, sizeof(vram)}, cfg));
 *
 *     while (display.pump()) {
 *         draw(display.surface());
 *         display.surface().flush();
 *     }
 *
 * Nagłówek nie włącza SDL — cała biblioteka jest widoczna wyłącznie
 * w `src/gfx/sdl/SdlDisplay.cpp`, tak samo jak Arduino w katalogach backendów
 * i LVGL w `LvglApi.hpp`. Reguła zależności z rozdz. 3 pilnuje tego w CI.
 *
 * Dwie rzeczy, które trzeba wiedzieć, zanim się tego użyje:
 *
 * 1. **Okno należy do jednego wątku.** `begin()`, `pump()` i `flush()` muszą
 *    być wołane z tego samego wątku, a na macOS musi to być wątek główny —
 *    tak wymaga Cocoa, nie SDL. Taski Hydry mają liczyć dane, a nie rysować;
 *    to zresztą ten sam podział, co na urządzeniu, gdzie panel obsługuje
 *    wyłącznie `ui.render`.
 * 2. **Powierzchnia nie jest chroniona zamkiem.** Rysowanie z drugiego wątku
 *    w trakcie `flush()` to wyścig, który TSan zgłosi — i słusznie.
 *
 * Gdy build powstał bez SDL (brak `HYDRA_WITH_SDL`), klasa nadal istnieje
 * i linkuje się, a `begin()` zwraca `Err::NotSupported`. Aplikacja
 * bezokienna — testy, CI, praca zdalna bez X11 — kompiluje się bez zmian.
 */

#include "hydra/core/Delegate.hpp"
#include "hydra/gfx/Color.hpp"
#include "hydra/gfx/Framebuffer.hpp"

namespace hydra {
namespace gfx {

class SdlDisplay : NonCopyable {
public:
    struct Cfg {
        /** Tytuł okna; widoczny na pasku i na liście okien systemu. */
        const char* title  = "Hydra";
        /** Rozmiar **powierzchni**, nie okna — okno jest `scale` razy większe. */
        i16         width  = 320;
        i16         height = 240;
        /**
         * Powiększenie. Panel 128×64 w skali 1:1 jest na monitorze 4K
         * znaczkiem pocztowym; skalowanie jest całkowitoliczbowe i bez
         * wygładzania, żeby piksel na ekranie odpowiadał pikselowi bufora.
         */
        u8          scale  = 2;
        PixelFormat format = PixelFormat::Rgb565;
        /** Synchronizacja z odświeżaniem monitora — pętla nie kręci się na 100% CPU. */
        bool        vsync  = true;
        /** Barwy dla powierzchni jednobitowych: „piksel zapalony" i tło. */
        Color       monoOn  = colors::white;
        Color       monoOff = colors::black;
    };

    /** Stan wskaźnika w układzie współrzędnych **powierzchni**, nie okna. */
    struct Pointer {
        i16  x    = 0;
        i16  y    = 0;
        bool down = false;
    };

    /**
     * Klawisz: kod SDL (`SDLK_*`) i kierunek zmiany. Kod przekazujemy surowy,
     * bo tłumaczenie go na własne wyliczenie oznaczałoby tablicę stu wpisów,
     * z których aplikacja użyje pięciu.
     */
    using KeyFn = Delegate<void(u32 keycode, bool down)>;

    SdlDisplay() = default;
    ~SdlDisplay();

    /** Bajty potrzebne na bufor obrazu — ten sam rachunek co w Framebuffer. */
    static constexpr size_t bytesNeeded(i16 w, i16 h, PixelFormat fmt) {
        return Framebuffer::bytesNeeded(w, h, fmt);
    }

    /**
     * Tworzy okno i podpina bufor.
     *
     * Bufor należy do wołającego i musi przeżyć wyświetlacz — tak samo jak
     * w `Framebuffer` i z tego samego powodu (rozdz. 11: nic nie alokujemy
     * po starcie).
     *
     * Zwraca `Err::NotSupported`, gdy build powstał bez SDL, `Err::IoError`,
     * gdy nie udało się otworzyć okna (brak serwera X, brak sterownika),
     * i `Err::OutOfRange`, gdy bufor jest za mały.
     */
    Status begin(ByteSpan buffer, const Cfg& cfg);

    /** Zamyka okno. Wywoływane też przez destruktor. */
    void end();

    bool open() const { return window_ != nullptr; }

    ISurface&    surface()     { return fb_; }
    Framebuffer& framebuffer() { return fb_; }

    /**
     * Obsługuje kolejkę zdarzeń okna. Zwraca `false`, gdy użytkownik zamknął
     * okno albo nacisnął Esc — to jest warunek pętli głównej aplikacji.
     *
     * Wołanie `pump()` jest obowiązkowe nawet wtedy, gdy aplikacja nie czyta
     * wejścia: bez opróżniania kolejki system uznaje okno za zawieszone.
     */
    bool pump();

    /** Czy padło żądanie zamknięcia. `pump()` zwraca wtedy `false`. */
    bool quitRequested() const { return quit_; }

    Pointer pointer() const { return pointer_; }

    /** Reakcja na klawisze. Wołana z wnętrza `pump()`, w jego wątku. */
    void setKeyHandler(KeyFn fn) { key_ = fn; }

    /** Klatki wypchnięte na ekran od `begin()` — do pomiaru tempa rysowania. */
    u32 framesPresented() const { return frames_; }

private:
    /** Podpinane jako `PresentFn` bufora: konwersja formatu i wypchnięcie klatki. */
    Status present(CByteSpan pixels, Size size, PixelFormat format);

    Framebuffer fb_{};
    Cfg         cfg_{};

    // Uchwyty SDL trzymane jako void* — inaczej nagłówek musiałby włączyć
    // SDL.h i biblioteka przeciekłaby do całego API Hydry.
    void* window_   = nullptr;
    void* renderer_ = nullptr;
    void* texture_  = nullptr;
    /** Czy to my zainicjowaliśmy podsystem wideo — tylko wtedy go zamykamy. */
    bool  ownsSdl_  = false;

    Pointer pointer_{};
    KeyFn   key_{};
    bool    quit_   = false;
    u32     frames_ = 0;
};

}  // namespace gfx
}  // namespace hydra
