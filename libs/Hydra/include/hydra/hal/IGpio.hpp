#pragma once
/**
 * Hydra — wejścia/wyjścia cyfrowe i przerwania od zboczy (rozdz. 5).
 *
 * Handler przerwania to zwykły wskaźnik na funkcję, a nie Delegate: w ISR nie
 * wolno wykonywać niczego, co mogłoby alokować albo skoczyć przez domknięcie
 * o nieznanym koszcie. Kontrakt handlera (rozdz. 10):
 *   - odczyt rejestru, publikacja EventBus::publishFromIsr(), koniec,
 *   - zakaz alokacji, logowania i pracy na magistralach,
 *   - na ESP32 funkcja musi trafić do IRAM (makro HYDRA_ISR_ATTR).
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/hal/Pin.hpp"

namespace hydra {
namespace hal {

/** Procedura obsługi przerwania od pinu. Wołana w kontekście ISR. */
using IsrHandler = void (*)(void* arg);

class IGpio {
public:
    virtual ~IGpio() = default;

    virtual Status configure(PinNum pin, PinMode mode) = 0;
    virtual Status write(PinNum pin, bool high)        = 0;
    virtual Result<bool> read(PinNum pin)              = 0;

    /** Zmiana stanu wyjścia na przeciwny. */
    virtual Status toggle(PinNum pin) {
        auto v = read(pin);
        if (!v) return fail(v.error());
        return write(pin, !*v);
    }

    /**
     * Podpina przerwanie. Jeden handler na pin; ponowne wywołanie nadpisuje.
     * arg jest przekazywany do handlera i musi przeżyć czas trwania rejestracji.
     */
    virtual Status attachInterrupt(PinNum pin, Edge edge, IsrHandler handler, void* arg) = 0;
    virtual Status detachInterrupt(PinNum pin) = 0;
};

/**
 * Wygodna nakładka na pojedyncze wyjście — czytelniejsza w kodzie aplikacji
 * niż powtarzanie Hal::gpio().write(...). Nie trzyma żadnego stanu poza numerem
 * pinu, więc nie kosztuje pamięci.
 */
class OutputPin {
public:
    explicit OutputPin(PinNum pin) : pin_(pin) {}

    Status begin(bool initialHigh = false);
    Status set(bool high);
    Status high() { return set(true); }
    Status low()  { return set(false); }
    Status toggle();
    PinNum pin() const { return pin_; }

private:
    PinNum pin_;
};

/** Wygodna nakładka na pojedyncze wejście. */
class InputPin {
public:
    explicit InputPin(PinNum pin) : pin_(pin) {}

    Status begin(PinMode mode = PinMode::InputPullUp);
    Result<bool> read();
    PinNum pin() const { return pin_; }

private:
    PinNum pin_;
};

}  // namespace hal
}  // namespace hydra
