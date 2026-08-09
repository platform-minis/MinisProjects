/**
 * Hydra — przykład: minis-node.
 *
 * Węzeł IoT MyCastle na końcu magistrali RS-485. Nie ma stosu TCP/IP, nie zna
 * MQTT i nie wie, że gdzieś dalej stoi bramka — nadaje ramki, a dokąd trafią,
 * jest sprawą routera.
 *
 * To jest cała teza modułu `minis`: ten sam plik po podmianie łącza
 * na `MqttLink` łączy się z MyCastle wprost, bez zmiany ani jednej linii
 * poniżej `setup()`.
 *
 * Węzeł deklaruje trzy encje — czujnik, przekaźnik i nastawę. Panel MyCastle
 * narysuje z tego wykres, przełącznik i suwak, bo dostaje deklaracje
 * w wiadomości `hello`.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/minis/MinisModule.hpp"
#include "hydra/minis/links/SerialLink.hpp"

HYDRA_LOG_MODULE("node")

using namespace hydra;
using namespace hydra::minis;

namespace {

/**
 * Numer węzła na magistrali.
 *
 * Jedyna rzecz, którą trzeba ustawić ręcznie przy montażu — jak adres
 * w Modbusie. Wszystko poza tym (tożsamość, trasa, subskrypcje po stronie
 * bramki) bierze się z ruchu.
 */
constexpr NodeId kNodeId = 3;

// --- stan urządzenia --------------------------------------------------------

bool  gRelayOn   = false;
float gThreshold = 22.0f;

// --- encje ------------------------------------------------------------------
//
// Encja tylko do odczytu jest deklaracją; wartość idzie telemetrią pod tym
// samym kluczem. Encja zapisywalna przechwytuje komendę o nazwie równej
// swojemu identyfikatorowi i sama ją potwierdza — nie ma tu ręcznego ackCommand().

Entity gTemp = Entity::sensor("temperature", "Temperatura", "temperature", "°C");

Entity gRelay = Entity::toggle("relay", "Przekaźnik", [](json::JsonView value) {
    if (!value.asBool(gRelayOn)) return false;
    HYDRA_LOGI("przekaźnik %s", gRelayOn ? "włączony" : "wyłączony");
    return true;
});

Entity gSetpoint = Entity::number("threshold", "Próg alarmu", 5.0f, 40.0f, 0.5f,
                                  [](json::JsonView value) {
                                      return value.asFloat(gThreshold);
                                  },
                                  "°C");

// --- moduły -----------------------------------------------------------------

MinisIotModule gMinis;
SerialLink     gBus{hal::Hal::uart(1), &hal::Hal::gpio()};

/** Cykliczny pomiar — jedyne miejsce, w którym aplikacja mówi do platformy. */
class SensorModule : public ModuleBase {
public:
    SensorModule() : ModuleBase("sensor") {}

protected:
    Status onInit() override { return ok(); }

    Status onStart() override {
        Task::Cfg cfg;
        cfg.name = "sense.poll";
        cfg.prio = Prio::Normal;
        return task_.startPeriodic(cfg, 5000, [this] { tick(); });
    }

    void onStop() override { task_.stopAndWait(); }

private:
    void tick() {
        // Tu normalnie stałby odczyt z BMP280 przez hal::Hal::i2c().
        const float celsius = 21.0f + static_cast<float>(reading_ % 5) * 0.5f;
        ++reading_;

        const Metric metrics[] = {
            Metric::of("temperature", celsius, "°C"),
            Metric::of("relay", gRelayOn),
        };
        if (auto r = gMinis.sendTelemetry(metrics, 2); !r) {
            // Nieudana wysyłka na magistrali jest normalna (kolizja, bramka
            // zajęta) — logujemy na poziomie diagnostycznym i próbujemy
            // za pięć sekund. Powtarzanie w miejscu zablokowałoby task.
            HYDRA_LOGD("telemetria nie poszła: %s", toString(r.error()));
        }
    }

    Task task_{};
    u32  reading_ = 0;
};

SensorModule gSensor;
UartLogSink  gConsole;

}  // namespace

void setup() {
    SerialLink::Config bus;
    bus.self  = kNodeId;
    // Pin sterujący nadajnikiem RS-485. Na płytce bez niego (RS-232 albo
    // konwerter z automatycznym przełączaniem) zostaw kNoPin.
    bus.dePin = hal::kNoPin;
    gBus.configure(bus);

    MinisIotModule::Config minis;
    minis.self  = DeviceAddr::of("user1", "dev-node3");
    minis.label = "Czujnik korytarz";
    // Puls rzadziej niż telemetria: pomiar co 5 s i tak odświeża obecność,
    // a osobny puls byłby ruchem bez treści na łączu 9600 bodów.
    minis.heartbeatSec = 0;
    gMinis.configure(minis);

    // Tożsamość idzie w pierwszych ramkach — z niej bramka uczy się, kto stoi
    // pod numerem 3, i zaczyna dla nas subskrybować komendy.
    gBus.setIdentity(minis.self);
    gMinis.addLink(gBus);

    gMinis.addEntity(gTemp);
    gMinis.addEntity(gRelay);
    gMinis.addEntity(gSetpoint);

    App::config()
        .name("minis-node")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gMinis)
        .add(gSensor);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

void loop() {
    // Cała praca dzieje się w taskach: minis.worker odpytuje magistralę,
    // sense.poll mierzy.
}
