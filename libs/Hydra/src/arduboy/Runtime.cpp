#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY

#include "hydra/arduboy/Runtime.hpp"

#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"

#if HYDRA_PLAT_HOST
#  include "hydra/arduboy/Eeprom.hpp"
#  include "hydra/core/App.hpp"
#  include "hydra/core/LogSinks.hpp"
#  include "hydra/hal/Hal.hpp"
#  include "hydra/gfx/SdlDisplay.hpp"
#endif

HYDRA_LOG_MODULE("arduboy")

namespace hydra {
namespace arduboy {

void attach(Arduboy2Base::FlushFn flush, Arduboy2Base::ButtonSource buttons) {
    Arduboy2Base* game = activeInstance();
    if (game == nullptr) return;
    game->setFlush(flush);
    game->setButtonSource(buttons);
}

#if HYDRA_PLAT_HOST

namespace {

/**
 * Kody klawiszy SDL.
 *
 * Wypisane wprost, a nie przez `#include <SDL.h>`, i to jest zgodne
 * z zamysłem `SdlDisplay`: przekazuje kody surowe właśnie po to, żeby
 * wołający nie musiał wciągać nagłówków SDL. Interesuje nas osiem klawiszy,
 * a nie cała tablica.
 *
 * Klawisze bez odpowiednika w ASCII mają w SDL ustawiony bit 30 i numer
 * ze skanowania — stąd te wartości.
 */
constexpr u32 kKeyLeft      = (1u << 30) | 80;
constexpr u32 kKeyRight     = (1u << 30) | 79;
constexpr u32 kKeyUp        = (1u << 30) | 82;
constexpr u32 kKeyDown      = (1u << 30) | 81;
constexpr u32 kKeyReturn    = 13;
constexpr u32 kKeyBackspace = 8;
constexpr u32 kKeyA         = 'a';
constexpr u32 kKeyS         = 's';
constexpr u32 kKeyZ         = 'z';
constexpr u32 kKeyX         = 'x';

/** Bieżący stan przycisków, składany ze zdarzeń klawiatury. */
u8 gButtons = 0;

/** Zamienia kod klawisza na maskę przycisku; 0 dla klawiszy nieobsługiwanych. */
u8 buttonFor(u32 keycode) {
    switch (keycode) {
        case kKeyLeft:      return kLeftButton;
        case kKeyRight:     return kRightButton;
        case kKeyUp:        return kUpButton;
        case kKeyDown:      return kDownButton;
        case kKeyA:
        case kKeyZ:
        case kKeyReturn:    return kAButton;
        case kKeyS:
        case kKeyX:
        case kKeyBackspace: return kBButton;
        default:            return 0;
    }
}

StdoutLogSink gConsole;

}  // namespace

Status run(const char* title, u8 scale) {
    Arduboy2Base* game = activeInstance();
    if (game == nullptr) {
        // Gra nie zadeklarowała żadnej instancji Arduboya. Bez niej nie ma
        // czego uruchamiać, a pusta pętla wyglądałaby jak zawieszenie.
        return fail(Err::NotInitialized);
    }

    // Bez taska porządkowego: gra ma własną pętlę i woła housekeeping niżej.
    //
    // Osobny wątek nic tu nie wnosi, a na celu przeglądarkowym kosztuje —
    // każdy wątek emscriptena to Web Worker, a te wymagają SharedArrayBuffer,
    // czyli nagłówków COOP/COEP na serwerze. Jedna ścieżka dla okna na PC
    // i dla kanwy w przeglądarce jest warta więcej niż ten wątek.
    App::config().name("arduboy").logLevel(LogLevel::Info).logSink(gConsole)
                 .housekeepingMs(0);
    HYDRA_CHECK(App::begin());

    gfx::SdlDisplay display;

    gfx::SdlDisplay::Cfg cfg;
    cfg.title  = title;
    cfg.width  = kWidth;
    cfg.height = kHeight;
    cfg.scale  = scale;
    cfg.format = gfx::PixelFormat::Mono1;
    cfg.vsync  = true;

    // Okno rysuje wprost z bufora gry — bez kopii pośredniej. `display()`
    // przekłada układ stronicowy na wierszowy w tę właśnie pamięć.
    if (auto r = display.begin(game->monoBuffer(), cfg); !r) {
        // Najczęstsza przyczyna to budowa bez SDL. Bez tego komunikatu program
        // kończy się cichym kodem 1, a użytkownik szuka błędu w grze.
        HYDRA_LOGE("nie udalo sie otworzyc okna: %s", toString(r.error()));
        HYDRA_LOGE("cel natywny wymaga SDL2 — zainstaluj libsdl2-dev i zbuduj ponownie");
        App::stop();
        return fail(r.error());
    }

    display.setKeyHandler([](u32 keycode, bool down) {
        const u8 mask = buttonFor(keycode);
        if (mask == 0) return;
        if (down) gButtons = static_cast<u8>(gButtons | mask);
        else      gButtons = static_cast<u8>(gButtons & ~mask);
    });

    attach([&display] { return display.framebuffer().flush(); },
           [] { return gButtons; });

    // Rekordy gracza na nośniku, o ile jakiś jest.
    //
    // Na celu natywnym nośnikiem jest katalog uruchomienia, więc `eeprom.bin`
    // ląduje obok binarki i przeżywa zamknięcie okna. Gra nie robi w tej
    // sprawie nic — na oryginalnym sprzęcie też nie robiła.
    if (hal::Hal::hasFileSystem()) {
        if (auto r = eeprom().begin(hal::Hal::fileSystem()); !r) {
            HYDRA_LOGW("nie udalo sie wczytac eeprom.bin: %s", toString(r.error()));
        }
    }

    setup();

    Millis lastHouseMs = rtos::nowMs();

    while (display.pump()) {
        loop();

        // Zrzut rekordów następuje sam, chwilę po ostatniej zmianie.
        eeprom().tick();

        // Praca, którą normalnie wykonuje task core.house: drenaż kolejki
        // zdarzeń i logów, statystyki. Raz na sekundę, czyli w tym samym
        // rytmie, co domyślny okres taska — a nie 60 razy na sekundę razem
        // z klatkami gry.
        const Millis nowMs = rtos::nowMs();
        if (nowMs - lastHouseMs >= 1000) {
            lastHouseMs = nowMs;
            App::housekeeping();
        }

        // Oddech dla systemu, gdy gra czeka na swoją klatkę.
        //
        // `loop()` gry na Arduboya zaczyna się od `if (!nextFrame()) return;`,
        // więc bez tego kręcilibyśmy pustą pętlę na pełnym rdzeniu — na
        // urządzeniu bez znaczenia, na laptopie słyszalne w wentylatorze.
        rtos::delayMs(1);
    }

    // Ostatni zrzut przed wyjściem: gra zapisująca wynik tuż przed zamknięciem
    // okna nie zdążyłaby inaczej wyczekać opóźnienia.
    (void)eeprom().commit();

    App::stop();
    return ok();
}

#endif  // HYDRA_PLAT_HOST

}  // namespace arduboy
}  // namespace hydra

#if HYDRA_PLAT_HOST && !defined(HYDRA_ARDUBOY_NO_MAIN)
/**
 * Punkt wejścia dla niezmienionej gry.
 *
 * Gra na Arduboya nie ma `main()` i nie powinna go dostawać od nas w źródle —
 * wtedy przestałaby być niezmieniona. Dostaje go z biblioteki: konsolidator
 * wciąga ten moduł, żeby zaspokoić `main`, a ten z kolei odwołuje się do
 * `setup()` i `loop()` gry.
 *
 * Projekt, który chce własnego `main()` — bo dokłada moduły Hydry albo sam
 * decyduje o oknie — definiuje `HYDRA_ARDUBOY_NO_MAIN` i woła
 * `hydra::arduboy::run()` samodzielnie.
 */
int main() {
    const auto result = hydra::arduboy::run();
    return result ? 0 : 1;
}
#endif

#endif  // HYDRA_ENABLE_ARDUBOY
