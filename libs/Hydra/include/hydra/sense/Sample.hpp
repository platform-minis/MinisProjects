#pragma once
/**
 * Hydra — próbka czujnika i zdarzenia modułu sense (rozdz. 8).
 *
 * Próbka jest jednocześnie zdarzeniem na magistrali, więc musi zmieścić się
 * w budżecie ładunku (HYDRA_EVENT_MAX_SIZE). Pola ułożone są tak, by struktura
 * miała dokładnie 32 bajty bez marnowania miejsca na wyrównanie — pilnuje tego
 * static_assert niżej.
 *
 * Znacznik czasu jest monotoniczny (mikrosekundy od resetu), a nie kalendarzowy:
 * służy do liczenia odstępów między odczytami, więc nie może skakać po
 * synchronizacji NTP. Przeliczenie na czas UTC robi dopiero telemetria.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace sense {

/** Maksymalna liczba kanałów w jednej próbce (np. 3 osie IMU, 4 dla kwaternionu). */
constexpr u8 kMaxChannels = 4;

/**
 * Pojedynczy odczyt czujnika.
 *
 * Wartości są w typie float także na RP2040, który nie ma FPU: pojawiają się
 * w ścieżce akwizycji, a nie w pętli regulacji, więc koszt konwersji jest
 * pomijalny wobec czasu transferu na magistrali. Pętle czasu rzeczywistego
 * dalej używają real_t (rozdz. 9).
 */
struct Sample {
    Micros  t_us  = 0;                 ///< monotoniczny znacznik chwili pomiaru
    float   value[kMaxChannels] = {};
    TopicId topic = kInvalidTopic;     ///< identyfikuje czujnik-źródło
    u8      n     = 0;                 ///< liczba wypełnionych kanałów
    Quality q     = Quality::Good;

    float first() const { return n > 0 ? value[0] : 0.0f; }
};

static_assert(sizeof(Sample) <= HYDRA_EVENT_MAX_SIZE,
              "Sample nie mieści się w budżecie ładunku zdarzenia — zmniejsz "
              "kMaxChannels albo zwiększ HYDRA_EVENT_MAX_SIZE");

/** Rodzaj wykrytej nieprawidłowości (rozdz. 8, detekcja anomalii). */
enum class AnomalyKind : u8 {
    None = 0,
    Frozen,   ///< wartość nie zmienia się mimo kolejnych odczytów
    Spike,    ///< skok większy niż spodziewany między próbkami
    OutOfRange,
};

constexpr const char* toString(AnomalyKind k) {
    switch (k) {
        case AnomalyKind::None:       return "none";
        case AnomalyKind::Frozen:     return "frozen";
        case AnomalyKind::Spike:      return "spike";
        case AnomalyKind::OutOfRange: return "out-of-range";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Zdarzenia modułu
// ---------------------------------------------------------------------------

/** Wykryta nieprawidłowość w danych czujnika. */
struct SensorAnomaly {
    TopicId     topic;
    AnomalyKind kind;
    u8          channel;
    float       value;
};

/** Nieudany odczyt czujnika — czujnik żyje, ale transfer się nie powiódł. */
struct SensorFault {
    TopicId topic;
    Err     error;
    u32     consecutive;  ///< ile odczytów z rzędu zawiodło
};

/**
 * Zgłoszenie gotowości danych z przerwania (rozdz. 8, tryb DataReadyIrq).
 * ISR wyłącznie publikuje to zdarzenie ze znacznikiem czasu — sam odczyt
 * magistrali odbywa się później, w tasku sense.poll (rozdz. 10).
 */
struct SensorDataReady {
    Micros t_us;
    u8     index;  ///< pozycja czujnika w rejestrze huba
};

}  // namespace sense
}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::sense::Sample,          "sense/sample")
HYDRA_DECLARE_EVENT(hydra::sense::SensorAnomaly,   "sense/anomaly")
HYDRA_DECLARE_EVENT(hydra::sense::SensorFault,     "sense/fault")
HYDRA_DECLARE_EVENT(hydra::sense::SensorDataReady, "sense/data-ready")

#endif  // HYDRA_ENABLE_SENSE
