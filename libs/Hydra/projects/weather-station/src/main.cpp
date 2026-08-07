/**
 * weather-station — czujniki, filtrowanie i wysyłka pomiarów.
 *
 * Pokazuje układ, który powtarza się w każdym urządzeniu pomiarowym: czujniki
 * rejestrowane w hubie razem z opisem, jak mają być odpytywane i przetwarzane,
 * a wyniki idą na magistralę zdarzeń. Nic tu nie odpytuje czujnika ręcznie
 * i nic nie czeka w pętli — hub ma własny task, a aplikacja tylko słucha.
 *
 * Odpowiednikiem tego pliku w projekcie jest `weather-station.hydra`: to samo
 * urządzenie opisane danymi. Docelowo rejestracja bierze się stamtąd; tutaj
 * jest wpisana wprost, żeby przykład dało się przeczytać bez generatora.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/drivers/sense/Bme280.hpp"
#include "hydra/drivers/sense/Ina219.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/sense/SensorHub.hpp"

HYDRA_LOG_MODULE("weather")

using namespace hydra;

namespace {

drivers::Bme280  gBaro;
drivers::Ina219  gPower;
sense::SensorHub gHub;
UartLogSink      gConsole;

/**
 * Rejestracja czujnika to jeden opis: co, jak często, przez jaki filtr i co
 * uznajemy za wartość niewiarygodną. Wykrywanie usterek jest częścią
 * rejestracji, a nie czymś dopisywanym później — czujnik zwracający w kółko
 * tę samą wartość to typowy objaw urwanego przewodu, nie pomiar.
 */
Status registerSensors() {
    sense::SensorHub::Registration baro;
    baro.sensor.periodMs     = 2000;
    baro.sensor.address      = drivers::Bme280::kDefaultAddress;
    baro.filter.kind         = sense::FilterKind::Ema;
    baro.filter.emaAlpha     = 0.3f;
    baro.anomaly.minValue    = -40.0f;
    baro.anomaly.maxValue    = 85.0f;
    baro.anomaly.frozenLimit = 10;
    HYDRA_CHECK(gHub.add(gBaro, baro));

    sense::SensorHub::Registration power;
    power.sensor.periodMs     = 500;
    power.sensor.address      = drivers::Ina219::kDefaultAddress;
    power.filter.kind         = sense::FilterKind::Median;
    power.filter.medianWindow = 5;
    HYDRA_CHECK(gHub.add(gPower, power));

    return ok();
}

}  // namespace

void setup() {
    App::config()
        .name("weather-01")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gHub);

    if (auto r = registerSensors(); !r) {
        HYDRA_LOGE("rejestracja czujników nieudana: %s", toString(r.error()));
        return;
    }

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return;
    }

    // Pomiary przychodzą magistralą, a nie odpytywaniem — dzięki temu dołożenie
    // drugiego odbiorcy (ekranu, zapisu na kartę) nie dotyka tego kodu.
    EventBus::subscribe<sense::Sample>([](const sense::Sample& sample) {
        if (sample.q != Quality::Good) {
            HYDRA_LOGW("czujnik 0x%04X: dane %s", sample.topic,
                       sample.q == Quality::Stale ? "nieaktualne" : "podejrzane");
            return;
        }
        HYDRA_LOGI("0x%04X: %.2f", sample.topic, static_cast<double>(sample.first()));
    });

    EventBus::subscribe<sense::SensorAnomaly>([](const sense::SensorAnomaly& anomaly) {
        HYDRA_LOGW("anomalia na czujniku 0x%04X", anomaly.topic);
    });

    HYDRA_LOGI("stacja gotowa na płytce %s", hal::board::name);
}

void loop() {}
