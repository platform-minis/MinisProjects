#pragma once
/**
 * Hydra — gotowe odbiorniki logów.
 *
 * Adapter między rdzeniem a warstwą HAL. Mieszka w core, a nie w hal, bo
 * zależności biegną w dół: core wolno sięgnąć po hal, odwrotnie nie
 * (rozdz. 3, reguła 1).
 */

#include "hydra/core/Log.hpp"
#include "hydra/hal/IBus.hpp"

namespace hydra {

/**
 * Zapis logów na port szeregowy. Sam port musi być już otwarty — sink go nie
 * konfiguruje, żeby nie odbierać aplikacji kontroli nad prędkością i pinami.
 */
class UartLogSink : public ILogSink {
public:
    /** index: numer portu w rejestrze HAL (0 = konsola). */
    explicit UartLogSink(u8 uartIndex = 0) : index_(uartIndex) {}

    void write(LogLevel level, const char* line, size_t len) override;

private:
    u8 index_;
};

#if HYDRA_PLAT_HOST
/**
 * Konsola procesu — log dla celu `native`.
 *
 * Na hoście `UartLogSink` pisze do atrapy portu, czyli donikąd: dla testów
 * jednostkowych to właściwe zachowanie (log nie zaśmieca wyniku), ale dla
 * aplikacji uruchamianej w oknie oznaczało program, który milczy także wtedy,
 * gdy start się nie powiódł. To jest ten sam błąd, przed którym broni
 * publikowanie spóźnień taska jako zdarzeń: cisza nie jest informacją.
 *
 * Piszemy na wyjście diagnostyczne, nie na standardowe. Dwa powody: strumień
 * diagnostyczny nie jest buforowany, więc ostatnie wiersze przetrwają
 * przerwanie programu, a standardowe wyjście zostaje wolne dla tego, co
 * aplikacja ma naprawdę do powiedzenia.
 */
class StdoutLogSink : public ILogSink {
public:
    void write(LogLevel level, const char* line, size_t len) override;
};
#endif

}  // namespace hydra
