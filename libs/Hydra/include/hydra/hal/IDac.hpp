#pragma once
/**
 * Hydra — przetwornik cyfrowo-analogowy.
 *
 * Osobno od PWM, choć oba służą do wystawiania napięcia, bo różnią się tym,
 * co trzeba wiedzieć przy użyciu: PWM wymaga filtru dolnoprzepustowego
 * i ma częstotliwość nośną, DAC daje napięcie wprost i ma rozdzielczość.
 * Element audio korzystający z PWM musi dobrać nośną powyżej pasma; z DAC-a —
 * nie musi. Wspólny interfejs ukrywałby dokładnie tę różnicę.
 *
 * Kanałów jest niewiele (ESP32 klasyczne ma dwa, STM32G4 dwa), więc adresujemy
 * numerem kanału, a nie pinem — pin jest ustalony przez krzem i wyboru nie ma.
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace hal {

class IDac {
public:
    virtual ~IDac() = default;

    virtual Status enable(u8 channel) = 0;
    virtual void   disable(u8 channel) = 0;

    /** Wartość w pełnej skali przetwornika — patrz `resolutionBits()`. */
    virtual Status write(u8 channel, u16 value) = 0;

    virtual u8 resolutionBits() const = 0;
    virtual u8 channelCount() const = 0;
};

}  // namespace hal
}  // namespace hydra
