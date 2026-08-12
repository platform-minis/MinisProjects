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
#include "hydra/infer/IInferenceEngine.hpp"
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

// ---------------------------------------------------------------------------

/**
 * Detekcja anomalii **nauczona** zamiast napisanej.
 *
 * `AnomalyDetector` odpowiada na pytania, które ktoś zadał z góry: czy wartość
 * stoi, czy skoczyła, czy wyszła poza zakres. Każde z nich wymaga liczby, którą
 * trzeba znać — a przy wibracji silnika albo poborze prądu tej liczby zwykle
 * nikt nie zna, bo „normalne" zależy od egzemplarza, obciążenia i zużycia.
 *
 * Ten detektor odpowiada na inne pytanie: czy ostatnie `N` próbek **wygląda
 * jak zwykle**. Odpowiedź jest jedną liczbą z modelu, porównywaną z progiem.
 *
 * ## Świadomie ta sama droga wyjścia
 *
 * Wynik to `AnomalyDetector::Hit` i to samo zdarzenie `SensorAnomaly` —
 * odbiorca nie musi wiedzieć, skąd wzięło się rozpoznanie, a `AnomalyKind`
 * pozwala mu je rozróżnić, jeśli chce. Osobne zdarzenie oznaczałoby, że każdy
 * odbiorca anomalii musi teraz subskrybować dwa tematy, żeby nie przegapić
 * połowy.
 *
 * ## Okno i pamięć
 *
 * Bufor okna podaje aplikacja, tak jak arenę silnika: jego rozmiar wynika
 * z modelu, którego przy kompilacji jeszcze nie ma. Detektor nie alokuje.
 */
class ModelDetector {
public:
    struct Config {
        /**
         * Który kanał obserwować. Model patrzy na jedną wielkość — prąd albo
         * kąt — a nie na wszystkie naraz: okno z przeplecionych kanałów
         * wymagałoby modelu uczonego dokładnie na tym przeplocie.
         */
        u8    channel = 0;
        /**
         * Próg zgłoszenia. Wynik modelu **większy** niż próg to anomalia.
         *
         * Dobiera się go do rozkładu wyników na danych bez usterki, a nie
         * z góry — dlatego jest w konfiguracji, a nie w kodzie.
         */
        float threshold = 0.0f;
        /**
         * Co ile próbek liczyć. Zero znaczy „po każdym pełnym oknie, bez
         * zakładki". Mniejsza wartość daje szybszą reakcję kosztem obciążenia.
         */
        u16   hopSamples = 0;
    };

    void setEngine(infer::IInferenceEngine* engine) { engine_ = engine; }
    /** Bufor na okno; musi pomieścić `engine->input(0).bytes()`. */
    void setWindowBuffer(float* buffer, u32 count) {
        window_ = buffer;
        capacity_ = count;
    }

    /** Sprawdza kształt modelu wobec bufora. Wołać po wczytaniu modelu. */
    Status configure(const Config& cfg);

    /**
     * Dokłada próbkę do okna i liczy, gdy jest pełne.
     *
     * Zwraca trafienie tylko w tym przebiegu, w którym model policzył i wynik
     * przekroczył próg. Dla pozostałych próbek `kind` zostaje `None` — okno
     * jeszcze się zbiera.
     */
    AnomalyDetector::Hit feed(const Sample& s);

    void reset();

    /** Ile razy model policzył — detektor, który nie liczy, milczy tak samo jak sprawny. */
    u32 evaluations() const { return evaluations_; }
    /** Ostatni wynik modelu, także poniżej progu — do dobrania progu. */
    float lastScore() const { return lastScore_; }
    const Config& config() const { return cfg_; }

private:
    Config cfg_{};
    infer::IInferenceEngine* engine_ = nullptr;

    float* window_ = nullptr;
    u32    capacity_ = 0;
    u32    windowSamples_ = 0;
    u32    filled_ = 0;

    u32   evaluations_ = 0;
    float lastScore_ = 0.0f;
};

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
