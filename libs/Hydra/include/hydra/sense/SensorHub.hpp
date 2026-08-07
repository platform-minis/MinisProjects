#pragma once
/**
 * Hydra — harmonogram i przetwarzanie odczytów czujników (rozdz. 8).
 *
 * Hub odpowiada za wszystko, czego nie robi pojedynczy czujnik:
 *
 *   - **harmonogram**: czujniki okresowe grupowane są w jednym tasku wg
 *     największego wspólnego dzielnika okresów. Czujnik 100 ms i czujnik 250 ms
 *     dają tyknięcie 50 ms i dzielniki 2 i 5 — jeden task zamiast dwóch,
 *     bez dryfu i bez budzenia procesora częściej, niż trzeba;
 *   - **tryb data-ready**: przerwanie wyłącznie publikuje zdarzenie ze
 *     znacznikiem czasu, a odczyt magistrali odbywa się w tasku (rozdz. 10);
 *   - **łańcuch przetwarzania**: kalibracja → filtr → detekcja anomalii →
 *     publikacja na EventBus.
 *
 * Znacznik czasu pochodzi z chwili *pozyskania* danych, a nie publikacji:
 * w trybie data-ready z ISR, w trybie okresowym sprzed transferu. Odczyt przez
 * I2C przy 100 kHz trwa nawet setki mikrosekund — stemplowanie po nim
 * przesuwałoby wszystkie próbki i psuło liczenie odstępów.
 *
 * Brakujący czujnik nie blokuje startu urządzenia: nieudany probe() oznacza
 * wpis jako niedostępny i zostawia ślad w logu. Robot ma pojechać bez jednego
 * czujnika odległości, a nie odmówić uruchomienia.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/core/EventBus.hpp"
#include "hydra/core/IModule.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/sense/Filters.hpp"
#include "hydra/sense/ISensor.hpp"
#include "hydra/sense/Pipeline.hpp"

namespace hydra {
namespace sense {

/** Maksymalna liczba czujników w hubie. Każdy wpis to ok. 300 B statycznie. */
#ifndef HYDRA_SENSE_MAX_SENSORS
#  define HYDRA_SENSE_MAX_SENSORS 8
#endif

/** Głębokość skrzynki na zgłoszenia data-ready z przerwań. */
#ifndef HYDRA_SENSE_INBOX_DEPTH
#  define HYDRA_SENSE_INBOX_DEPTH 8
#endif

class SensorHub : public ModuleBase {
public:
    /** Komplet ustawień jednego czujnika: sprzęt, filtr, kontrola poprawności. */
    struct Registration {
        SensorCfg  sensor;
        FilterCfg  filter;
        AnomalyCfg anomaly;
    };

    struct Stats {
        u32 reads     = 0;  ///< udane odczyty
        u32 faults    = 0;  ///< nieudane transfery
        u32 skipped   = 0;  ///< brak nowych danych (Err::WouldBlock)
        u32 anomalies = 0;
        u32 published = 0;
    };

    SensorHub() : ModuleBase("sense") {}

    /**
     * Rejestruje czujnik. Wolno wołać wyłącznie przed App::begin() —
     * po starcie hub nie alokuje ani nie zmienia harmonogramu (rozdz. 11).
     * Zwraca indeks wpisu.
     */
    Result<u8> add(ISensor& sensor, const Registration& reg);

    u8 count() const { return count_; }

    /** Czy czujnik odpowiedział przy starcie (udany probe). */
    bool available(u8 index) const;

    /** Identyfikator czujnika w polu Sample::topic — nameId(nazwa czujnika). */
    TopicId topicOf(u8 index) const;

    /** Okres taska sense.poll: GCD okresów czujników okresowych. */
    u32 tickMs() const { return tickMs_; }

    /** Co ile tyknięć odpytywany jest dany czujnik. */
    u16 dividerOf(u8 index) const;

    Stats stats() const { return total_; }
    Stats stats(u8 index) const;

    /**
     * Wymusza jeden przebieg łańcucha dla wskazanego czujnika.
     * Używane przez shell diagnostyczny i testy — pozwala sprawdzić całą
     * ścieżkę bez czekania na tyknięcie taska.
     */
    Status pollOnce(u8 index);

    /** Bieżąca kalibracja i jej zapis — dla procedury serwisowej. */
    SensorCal calibration(u8 index) const;
    Status    setCalibration(u8 index, const SensorCal& cal, bool persist = true);

protected:
    Status onInit() override;
    Status onStart() override;
    void   onStop() override;

private:
    struct Entry {
        ISensor*      sensor = nullptr;
        Registration  cfg{};
        SensorCal     cal{};
        ChannelFilter filter[kMaxChannels];
        AnomalyDetector anomaly;

        TopicId  topic    = kInvalidTopic;
        PollMode mode     = PollMode::Periodic;
        u16      divider  = 1;
        u16      counter  = 0;
        u8       index    = 0;
        u8       channels = 0;
        bool     ready    = false;
        u32      consecutiveFaults = 0;
        Stats    stats{};
    };

    /** Punkt wejścia przerwania data-ready. Publikuje zdarzenie i wraca. */
    static void onDataReadyIsr(void* arg);

    void   tick();
    Status process(Entry& e, Micros stampUs);

    Entry  entries_[HYDRA_SENSE_MAX_SENSORS];
    u8     count_  = 0;
    u32    tickMs_ = 0;
    Stats  total_{};
    Task   task_;
    Inbox  inbox_;
    SubId  dataReadySub_ = kInvalidSub;
};

/** Największy wspólny dzielnik — podstawa grupowania okresów. */
u32 gcd(u32 a, u32 b);

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
