#pragma once
/**
 * Hydra — enkodery (rozdz. 9).
 *
 * Interfejs zwraca surowe zliczenia narastające, a nie prędkość: przeliczenie
 * na drogę i prędkość zależy od promienia koła i przełożenia, które zna
 * kinematyka, nie sterownik enkodera.
 *
 * Dekodowanie kwadraturowe na docelowych platformach robi sprzęt — jednostka
 * PCNT na ESP32, tryb enkoderowy timera na STM32, PIO na RP2. Klasa poniżej
 * jest wariantem programowym: działa wszędzie, kosztuje przerwanie na zbocze
 * i wystarcza dla enkoderów wolniejszych niż kilkanaście tysięcy zliczeń
 * na sekundę. Sterowniki sprzętowe implementują ten sam interfejs.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/core/Expected.hpp"
#include "hydra/hal/Pin.hpp"
#include "hydra/motion/MotionTypes.hpp"

namespace hydra {
namespace motion {

class IEncoder {
public:
    virtual ~IEncoder() = default;

    virtual Status begin() = 0;
    /** Licznik narastający; przepełnienie jest normalne i obsługiwane różnicowo. */
    virtual Result<i32> counts() = 0;
    virtual Status reset() = 0;
};

/**
 * Programowy dekoder kwadraturowy w trybie czterokrotnym.
 *
 * Zlicza wszystkie cztery zbocza na okres sygnału, więc rozdzielczość jest
 * czterokrotnie wyższa niż liczba szczelin tarczy. Nieprawidłowe przejścia
 * stanów — takie, które nie mogą wystąpić przy poprawnym sygnale — są liczone
 * osobno: ich rosnąca liczba oznacza drgania styków albo zbyt szybki obrót
 * dla wybranej metody, i jest pierwszym sygnałem, że trzeba przejść na
 * dekodowanie sprzętowe.
 */
class QuadratureEncoder : public IEncoder {
public:
    struct Config {
        hal::PinNum a      = hal::kNoPin;
        hal::PinNum b      = hal::kNoPin;
        bool        invert = false;
        /** Włącza podciąganie wejść; wyłączyć przy enkoderach z wyjściem push-pull. */
        bool        pullUp = true;
    };

    Status configure(const Config& cfg);
    Status begin() override;
    Result<i32> counts() override;
    Status reset() override;

    /**
     * Krok dekodera dla nowego stanu wejść. Wywoływane z przerwania albo
     * z odpytywania; nie alokuje, nie loguje i nie dotyka magistral.
     */
    void update(bool a, bool b);

    /** Liczba nieprawidłowych przejść stanów od ostatniego zerowania. */
    u32 glitches() const { return glitches_; }

    /** Podpina przerwania od obu wejść. Wymaga wcześniejszego begin(). */
    Status attachInterrupts();
    Status detachInterrupts();

private:
    HYDRA_ISR_ATTR static void onEdge(void* arg);

    Config cfg_{};
    /** Zapis 2-bitowy poprzedniego stanu (A, B). */
    volatile u8  lastState_ = 0;
    volatile i32 counts_    = 0;
    volatile u32 glitches_  = 0;
    bool         primed_    = false;
    bool         ready_     = false;
    bool         irqActive_ = false;
};

/**
 * Przelicznik zliczeń enkodera na drogę i prędkość koła.
 *
 * Trzyma poprzedni odczyt, więc różnicę liczy odpornie na przepełnienie
 * licznika: odejmowanie w arytmetyce uzupełnieniowej daje poprawny wynik
 * także wtedy, gdy licznik przekręcił się między odczytami.
 */
class WheelOdometer {
public:
    struct Config {
        /** Zliczenia na pełny obrót koła, po przełożeniu. */
        i32    countsPerRevolution = 1;
        /** Promień koła w metrach. */
        real_t wheelRadius = real(0.05f);
    };

    Status configure(const Config& cfg);

    /**
     * Przyjmuje nowy odczyt licznika i zwraca drogę przebytą od poprzedniego
     * wywołania, w metrach.
     */
    real_t advance(i32 counts);

    /** Prędkość liniowa koła w metrach na sekundę. */
    real_t velocity(i32 counts, real_t dtSeconds);

    real_t distanceTotal() const { return total_; }
    void   reset();

private:
    Config cfg_{};
    i32    last_   = 0;
    bool   primed_ = false;
    real_t total_  = real(0.0f);
    real_t perCount_ = real(0.0f);
};

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
