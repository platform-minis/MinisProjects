/**
 * @file Runtime.hpp
 * @brief Uruchamianie gier: `setup()`, `loop()` i podłączenie sprzętu.
 *
 * Gra na Arduboya nie ma `main()`. Ma `setup()` i `loop()`, a resztą zajmuje
 * się szkielet Arduino. Ten plik jest tym szkieletem — z tą różnicą, że na
 * celu natywnym musi jeszcze otworzyć okno i zamienić klawiaturę na przyciski.
 *
 * ## Co robi runtime, czego nie robi Arduino
 *
 * Na PC pętla gry nie może być pustym `while (true) loop();`. Okno trzeba
 * obsługiwać — bez odbierania zdarzeń system uzna program za zawieszony,
 * a zamknięcie okna nie zadziała. Dlatego runtime przeplata `loop()`
 * z `pump()` i kończy pracę, gdy okno zostanie zamknięte.
 *
 * ## Mapowanie klawiszy
 *
 *     ← ↑ → ↓     kierunki
 *     A / Z       przycisk A       (obie wygodne przy strzałkach)
 *     S / X       przycisk B
 *     Enter       przycisk A
 *     Backspace   przycisk B
 *     Esc         zamyka okno
 *
 * Dwa klawisze na przycisk, bo układy klawiatur różnią się położeniem Z i Y,
 * a gra platformowa sterowana strzałkami wymaga skoku pod kciukiem.
 *
 * ## Na układzie
 *
 * Runtime nie zakłada, czym jest wyświetlacz ani skąd biorą się przyciski.
 * Projekt podaje jedno i drugie:
 *
 *     hydra::arduboy::Runtime::Cfg cfg;
 *     cfg.flush   = [] { return panel.flush(); };
 *     cfg.buttons = [] { return readButtonPins(); };
 */
#pragma once

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY

#include "hydra/arduboy/Arduboy2.hpp"
#include "hydra/core/Expected.hpp"

namespace hydra {
namespace arduboy {

/**
 * Instancja, którą runtime obsługuje.
 *
 * Gra deklaruje swój obiekt globalnie (`Arduboy2 arduboy;`) i to jego runtime
 * musi podłączyć do ekranu. Rejestracja dzieje się w konstruktorze klasy, więc
 * gra nie robi w tej sprawie nic — tak jak na oryginalnym sprzęcie.
 */
Arduboy2Base* activeInstance();

/** Powiększenie okna na celu natywnym; 128×64 w skali 1:1 to znaczek pocztowy. */
constexpr u8 kDefaultScale = 6;

/**
 * Uruchamia grę: podłącza ekran i wejście, woła `setup()`, potem `loop()`
 * w pętli aż do zamknięcia okna.
 *
 * Na układzie ta funkcja nie istnieje — tam `setup()` i `loop()` woła Arduino,
 * a projekt podłącza sprzęt przez `attach()`.
 */
#if HYDRA_PLAT_HOST
Status run(const char* title = "Arduboy", u8 scale = kDefaultScale);
#endif

/**
 * Podłącza wyświetlacz i przyciski do aktywnej instancji.
 *
 * Wołane przez `run()` na celu natywnym; na układzie woła to projekt.
 */
void attach(Arduboy2Base::FlushFn flush, Arduboy2Base::ButtonSource buttons);

}  // namespace arduboy
}  // namespace hydra

/**
 * Punkty wejścia gry.
 *
 * Deklarujemy je tutaj, żeby runtime mógł je zawołać. Definicję dostarcza gra,
 * niezmieniona: `void setup()` i `void loop()`.
 */
extern void setup();
extern void loop();

#endif  // HYDRA_ENABLE_ARDUBOY
