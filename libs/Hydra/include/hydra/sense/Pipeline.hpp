#pragma once
/**
 * Hydra — kalibracja i detekcja anomalii (rozdz. 8).
 *
 * Za odczytem stoi łańcuch: kalibracja → filtr → detekcja anomalii → publikacja.
 * Ten nagłówek opisuje pierwszy i trzeci człon; filtry są w Filters.hpp.
 *
 * Kalibracja jest trwała: współczynniki offset/gain wyznacza się raz (wzorcem
 * albo procedurą serwisową) i zapisuje w IStorage, żeby przeżyły restart.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/core/Expected.hpp"
#include "hydra/sense/Sample.hpp"

namespace hydra {
namespace sense {

/** Korekta liniowa jednego kanału: wynik = (surowy + offset) * gain. */
struct ChannelCal {
    float offset = 0.0f;
    float gain   = 1.0f;
};

struct SensorCal {
    ChannelCal ch[kMaxChannels];
};

/** Przestrzeń nazw w IStorage, w której trzymane są kalibracje. */
constexpr const char* kCalibrationNamespace = "hydra-cal";

class Calibration {
public:
    /** Nakłada korektę na wszystkie wypełnione kanały próbki. */
    static void apply(const SensorCal& cal, Sample& s);

    /**
     * Wczytuje kalibrację z IStorage. Brak zapisu nie jest błędem — zwraca
     * współczynniki neutralne, bo nieskalibrowany czujnik ma działać.
     */
    static Status load(const char* sensorName, SensorCal& out);
    static Status save(const char* sensorName, const SensorCal& cal);
    static Status erase(const char* sensorName);
};

// ---------------------------------------------------------------------------

/**
 * Konfiguracja detekcji anomalii. Każdy mechanizm wyłącza się wartością zerową,
 * bo nie każdy czujnik ma sensowny zakres albo spodziewaną dynamikę.
 */
struct AnomalyCfg {
    /** Ile identycznych odczytów z rzędu oznacza zawieszony czujnik (0 = brak). */
    u16   frozenLimit = 0;
    /** Maksymalna spodziewana zmiana między próbkami (0 = brak kontroli). */
    float spikeDelta  = 0.0f;
    /** Dopuszczalny zakres wartości; minValue == maxValue wyłącza kontrolę. */
    float minValue    = 0.0f;
    float maxValue    = 0.0f;
};

class AnomalyDetector {
public:
    struct Hit {
        AnomalyKind kind    = AnomalyKind::None;
        u8          channel = 0;
        float       value   = 0.0f;
    };

    void configure(const AnomalyCfg& cfg);
    void reset();

    /**
     * Sprawdza próbkę. Zwraca pierwszą wykrytą nieprawidłowość — jedna próbka
     * może naruszać kilka reguł naraz, ale zgłaszanie ich wszystkich zalałoby
     * magistralę przy uszkodzonym czujniku.
     */
    Hit check(const Sample& s);

    const AnomalyCfg& config() const { return cfg_; }

private:
    AnomalyCfg cfg_{};
    float      last_[kMaxChannels]      = {};
    u16        sameCount_[kMaxChannels] = {};
    bool       primed_                  = false;
};

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
