#pragma once
/**
 * Hydra — kontrakt czujnika (rozdz. 8).
 *
 * Czujnik odpowiada wyłącznie za rozmowę ze swoim układem: wykrycie obecności,
 * konfigurację i pojedynczy odczyt. Cała reszta — harmonogram, kalibracja,
 * filtracja, detekcja anomalii i publikacja — należy do SensorHub. Dzięki temu
 * adapter konkretnego układu ma kilkanaście linii i nie powiela logiki.
 *
 * Odstępstwo od szkicu w specyfikacji: metody zwracają Status zamiast bool.
 * Hub potrzebuje wiedzieć, *dlaczego* odczyt zawiódł (brak układu, timeout
 * magistrali, błąd protokołu), żeby odróżnić przejściowy błąd transferu od
 * czujnika, który zniknął z magistrali.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/core/Expected.hpp"
#include "hydra/hal/Pin.hpp"
#include "hydra/sense/Sample.hpp"

namespace hydra {
namespace sense {

/** Sposób pozyskiwania danych z czujnika. */
enum class PollMode : u8 {
    /** Hub odpytuje w stałym okresie — domyślny tryb większości układów. */
    Periodic,
    /** Układ zgłasza gotowość danych przerwaniem (data-ready). */
    DataReadyIrq,
    /** Czujnik sam decyduje; hub próbuje w każdym tyknięciu, a czujnik
     *  odpowiada Err::WouldBlock, gdy nie ma nowych danych. */
    Free,
};

/** Konfiguracja pojedynczego czujnika przekazywana przy rejestracji. */
struct SensorCfg {
    /** Okres odpytywania w trybie Periodic. Ignorowany w trybie DataReadyIrq. */
    u32        periodMs = 1000;
    /** Adres na magistrali I2C (0 = nie dotyczy). */
    u8         address  = 0;
    /** Numer magistrali w rejestrze HAL. */
    u8         busIndex = 0;
    /** Pin data-ready dla trybu DataReadyIrq. */
    hal::PinNum irqPin  = hal::kNoPin;
    hal::Edge   irqEdge = hal::Edge::Falling;
};

class ISensor {
public:
    virtual ~ISensor() = default;

    /** Krótka nazwa — klucz tematu, kalibracji w IStorage i logów. */
    virtual const char* name() const = 0;

    /** Wykrycie obecności układu: skan I2C, odczyt rejestru WHO_AM_I itp. */
    virtual Status probe() = 0;

    /** Konfiguracja układu. Wołane raz, po udanym probe(). */
    virtual Status configure(const SensorCfg& cfg) = 0;

    virtual PollMode pollMode() const = 0;

    /**
     * Pojedynczy odczyt. Czujnik wypełnia value[] i n; znacznik czasu, temat
     * i jakość ustawia hub. Err::WouldBlock oznacza „brak nowych danych"
     * i nie jest liczone jako awaria.
     */
    virtual Status read(Sample& out) = 0;

    /** Liczba kanałów, jakie czujnik wypełnia. Używane do kalibracji i filtrów. */
    virtual u8 channels() const = 0;

    /** Jednostka kanału — wyłącznie do opisu w telemetrii ("degC", "hPa", "mA"). */
    virtual const char* unit(u8 channel) const { HYDRA_UNUSED(channel); return ""; }
};

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
