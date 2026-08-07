/**
 * Hydra — przykład: telemetry.
 *
 * Kryterium ukończenia etapu M2 (rozdz. 14): telemetria czujników na
 * EventBusie z poprawnymi znacznikami czasu.
 *
 * Trzy czujniki o różnych okresach trafiają do jednego taska sense.poll —
 * hub grupuje je po największym wspólnym dzielniku okresów. Aplikacja nie
 * dotyka magistrali, nie tworzy tasków dla czujników i nie odpytuje niczego
 * sama: subskrybuje jeden temat i dostaje gotowe, skalibrowane i przefiltrowane
 * próbki.
 *
 * Odstęp między próbkami liczony jest ze znaczników czasu, a nie z chwili
 * odebrania zdarzenia — to różnica, która decyduje o sensowności całkowania
 * (odometria, fuzja IMU) przy nierównomiernym obciążeniu systemu.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
// Deklaracje setup() i loop(). Potrzebne, bo STM32duino umieszcza je w bloku
// extern "C" — bez tej deklaracji definicje poniżej dostają wiązanie C++
// i konsolidator ich nie znajduje. Na ESP32 i RP2040 deklaracje są zwykłe,
// więc włączenie niczego nie zmienia.
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/drivers/sense/As5600.hpp"
#include "hydra/drivers/sense/Bme280.hpp"
#include "hydra/drivers/sense/Ina219.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/sense/SensorHub.hpp"

HYDRA_LOG_MODULE("telemetry")

using namespace hydra;

namespace {

sense::SensorHub  gHub;
drivers::Bme280   gWeather;
drivers::Ina219   gPower;
drivers::As5600   gAngle;
UartLogSink       gConsole;

/** Ostatni znacznik czasu na czujnik — do liczenia rzeczywistego odstępu. */
struct Tracker {
    TopicId topic  = kInvalidTopic;
    Micros  lastUs = 0;
};
Tracker gTrackers[3];

Tracker& trackerFor(TopicId topic) {
    for (auto& t : gTrackers) {
        if (t.topic == topic) return t;
    }
    for (auto& t : gTrackers) {
        if (t.topic == kInvalidTopic) {
            t.topic = topic;
            return t;
        }
    }
    return gTrackers[0];
}

void onSample(const sense::Sample& s) {
    Tracker& t = trackerFor(s.topic);

    // Odstęp ze znaczników próbek, a nie z czasu odebrania zdarzenia.
    const u32 deltaMs =
        t.lastUs == 0 ? 0 : static_cast<u32>((s.t_us - t.lastUs) / 1000ull);
    t.lastUs = s.t_us;

    if (s.q != Quality::Good) {
        HYDRA_LOGW("czujnik 0x%04X: dane %s", s.topic,
                   s.q == Quality::Stale ? "nieaktualne" : "podejrzane");
    }

    switch (s.n) {
        case 1:
            HYDRA_LOGI("0x%04X [+%lums] %.2f", s.topic,
                       static_cast<unsigned long>(deltaMs),
                       static_cast<double>(s.value[0]));
            break;
        case 3:
            HYDRA_LOGI("0x%04X [+%lums] %.2f / %.2f / %.2f", s.topic,
                       static_cast<unsigned long>(deltaMs),
                       static_cast<double>(s.value[0]), static_cast<double>(s.value[1]),
                       static_cast<double>(s.value[2]));
            break;
        default:
            HYDRA_LOGI("0x%04X [+%lums] %u kanałów", s.topic,
                       static_cast<unsigned long>(deltaMs), static_cast<unsigned>(s.n));
            break;
    }
}

}  // namespace

void setup() {
    // Pogoda: wolno zmienna wielkość, mocne wygładzenie, kontrola zakresu.
    sense::SensorHub::Registration weather;
    weather.sensor.periodMs   = 2000;
    weather.sensor.address    = drivers::Bme280::kDefaultAddress;
    weather.filter.kind       = sense::FilterKind::Ema;
    weather.filter.emaAlpha   = 0.3f;
    weather.anomaly.minValue  = -40.0f;
    weather.anomaly.maxValue  = 85.0f;
    weather.anomaly.frozenLimit = 10;

    // Zasilanie: szybki pomiar, mediana przeciw zakłóceniom impulsowym
    // od przełączania silników.
    sense::SensorHub::Registration power;
    power.sensor.periodMs     = 500;
    power.sensor.address      = drivers::Ina219::kDefaultAddress;
    power.filter.kind         = sense::FilterKind::Median;
    power.filter.medianWindow = 5;

    // Kąt: najszybszy z trzech, filtr dolnoprzepustowy o znanym paśmie.
    sense::SensorHub::Registration angle;
    angle.sensor.periodMs     = 100;
    angle.sensor.address      = drivers::As5600::kDefaultAddress;
    angle.filter.kind         = sense::FilterKind::Butterworth;
    angle.filter.cutoffHz     = 2.0f;
    angle.anomaly.spikeDelta  = 90.0f;

    gHub.add(gWeather, weather);
    gHub.add(gPower, power);
    gHub.add(gAngle, angle);

    // Jedna subskrypcja obsługuje wszystkie czujniki — rozróżnia je
    // pole Sample::topic.
    EventBus::subscribe<sense::Sample>(onSample);

    EventBus::subscribe<sense::SensorFault>([](const sense::SensorFault& e) {
        HYDRA_LOGE("czujnik 0x%04X: %s (%lu z rzędu)", e.topic, toString(e.error),
                   static_cast<unsigned long>(e.consecutive));
    });

    EventBus::subscribe<sense::SensorAnomaly>([](const sense::SensorAnomaly& e) {
        HYDRA_LOGW("czujnik 0x%04X kanał %u: %s (%.2f)", e.topic,
                   static_cast<unsigned>(e.channel), toString(e.kind),
                   static_cast<double>(e.value));
    });

    App::config()
        .name("telemetry")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gHub);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return;
    }

    // Okresy 2000/500/100 ms dają tyknięcie 100 ms — jeden task na trzy czujniki.
    HYDRA_LOGI("okres taska sense.poll: %lu ms",
               static_cast<unsigned long>(gHub.tickMs()));
}

void loop() {}
