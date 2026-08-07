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

}  // namespace hydra
