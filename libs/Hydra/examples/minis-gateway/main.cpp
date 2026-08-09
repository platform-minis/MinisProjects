/**
 * Hydra — przykład: minis-gateway.
 *
 * Bramka RS-485 ↔ Ethernet/Wi-Fi dla platformy MyCastle. Węzły za magistralą
 * nie mają stosu TCP/IP i nigdy nie zobaczą brokera; bramka przekłada ich
 * ramki na MQTT i z powrotem.
 *
 * Całe „bycie bramką" to **dwie linijki** — dwa łącza wpięte w ten sam router:
 *
 *     gMinis.addLink(gMqtt);     // do serwera
 *     gMinis.addLink(gBus);      // do węzłów
 *
 * Reszta dzieje się sama:
 *
 *  • pierwsza ramka z węzła uczy router, że `dev-node3` stoi za magistralą,
 *  • router prosi łącze MQTT o nasłuch komend dla tego urządzenia,
 *  • komenda z panelu schodzi na magistralę pod właściwy numer węzła,
 *  • potwierdzenie wraca tą samą drogą w drugą stronę.
 *
 * Nie ma tu tablicy urządzeń do utrzymania. Dołożenie czwartego czujnika do
 * magistrali nie wymaga zmiany w tym pliku ani ponownego wgrania wsadu —
 * i to jest jedyny powód, dla którego uczenie tras w ogóle istnieje.
 *
 * Bramka ma przy tym własne encje, bo jest też zwykłym urządzeniem: widać ją
 * w panelu obok węzłów, z własnym pomiarem i przekaźnikiem.
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
#include "hydra/minis/links/MqttLink.hpp"
#include "hydra/minis/links/SerialLink.hpp"
#include "hydra/net/NetModule.hpp"

HYDRA_LOG_MODULE("gateway")

using namespace hydra;
using namespace hydra::minis;

namespace {

constexpr const char* kUser   = "user1";
constexpr const char* kDevice = "dev-gateway";

// --- sieć -------------------------------------------------------------------

net::NetModule gNet{net::defaultNetworkInterface(), &net::defaultMdns()};

// --- łącza ------------------------------------------------------------------

/**
 * Łącze MQTT w trybie bramki.
 *
 * Tryb zmienia jedną rzecz: subskrypcje idą z symbolem wieloznacznym na
 * miejscu urządzenia (`minis/user1/+/command`) zamiast po jednej na węzeł.
 * Trzy subskrypcje zamiast trzech razy liczba węzłów — przy domyślnym limicie
 * ośmiu subskrypcji różnica między działającą bramką a taką, która obsługuje
 * dwa pierwsze węzły i milczy o reszcie.
 */
MqttLink   gMqtt{gNet.mqtt()};
SerialLink gBus{hal::Hal::uart(1), &hal::Hal::gpio()};

MinisIotModule gMinis;

// --- własne encje bramki ----------------------------------------------------

bool gFanOn = false;

Entity gCabinetTemp = Entity::sensor("cabinet_temp", "Temperatura szafki",
                                     "temperature", "°C");

Entity gFan = Entity::toggle("fan", "Wentylator szafki", [](json::JsonView value) {
    return value.asBool(gFanOn);
});

/** Diagnostyka bramki — publikowana jako zwykła telemetria. */
class GatewayModule : public ModuleBase {
public:
    GatewayModule() : ModuleBase("gateway") {}

protected:
    Status onInit() override { return ok(); }

    Status onStart() override {
        // Utrata i odzyskanie łączności jest zdarzeniem, nie stanem do
        // odpytywania — węzły za magistralą nie mają jak się o tym dowiedzieć
        // inaczej niż przez to, że ich ramki przestają dochodzić.
        EventBus::subscribe<MinisState>([](const MinisState& e) {
            HYDRA_LOGI("platforma %s, tras: %u",
                       e.online ? "osiągalna" : "nieosiągalna",
                       static_cast<unsigned>(e.routes));
        });
        EventBus::subscribe<MinisRouteLearned>([](const MinisRouteLearned& e) {
            HYDRA_LOGI("nowy węzeł za łączem %u — razem %u",
                       static_cast<unsigned>(e.link), static_cast<unsigned>(e.routes));
        });

        Task::Cfg cfg;
        cfg.name = "gw.report";
        cfg.prio = Prio::Low;
        return task_.startPeriodic(cfg, 30000, [this] { tick(); });
    }

    void onStop() override { task_.stopAndWait(); }

private:
    void tick() {
        const Router::Stats routing = gMinis.router().stats();
        const SerialLink::Stats bus = gBus.stats();

        const Metric metrics[] = {
            Metric::of("cabinet_temp", 28.5f, "°C"),
            Metric::of("fan", gFanOn),
            // Liczniki magistrali w telemetrii, a nie tylko w logu: rosnące
            // błędy CRC to jedyny sygnał, że terminator odpadł albo kabel
            // biegnie wzdłuż falownika — i widać go dopiero na wykresie.
            Metric::of("bus_frames", static_cast<float>(bus.framesRx)),
            Metric::of("bus_crc_errors", static_cast<float>(bus.crcErrors)),
            Metric::of("routed", static_cast<float>(routing.forwarded)),
        };
        (void)gMinis.sendTelemetry(metrics, 5);
    }

    Task task_{};
};

GatewayModule gGateway;
UartLogSink   gConsole;

}  // namespace

void setup() {
    // --- sieć ---------------------------------------------------------------
    net::NetModule::Config network;
    // Poświadczenia sieci dokłada ConnectionManager z pamięci trwałej —
    // wpisane tutaj trafiłyby do repozytorium razem z kodem.
    network.mqtt.clientId = "minis-dev-gateway";
    network.mqtt.host     = "mqtt.local";
    network.mqtt.port     = 1883;
    gNet.configure(network);

    // --- łącza --------------------------------------------------------------
    MqttLink::Config mqtt;
    mqtt.self    = DeviceAddr::of(kUser, kDevice);
    mqtt.gateway = true;      // subskrypcje z symbolem wieloznacznym
    gMqtt.configure(mqtt);

    SerialLink::Config bus;
    bus.self  = kGatewayNode;              // 0 = bramka
    bus.dePin = hal::kNoPin;               // pin DE nadajnika RS-485
    // Nieznany węzeł dostaje pytanie o tożsamość co pięć sekund. Bez tego
    // czujnik włączony po bramce milczałby do swojego pierwszego `hello`.
    bus.discoverEveryMs = 5000;
    gBus.configure(bus);
    gBus.setIdentity(mqtt.self);

    // --- moduł IoT ----------------------------------------------------------
    MinisIotModule::Config minis;
    minis.self  = mqtt.self;
    minis.label = "Bramka RS-485";
    gMinis.configure(minis);

    // Kolejność ma znaczenie: pierwsze łącze prowadzące do serwera staje się
    // trasą domyślną, czyli tym, dokąd idzie ruch bez znanego adresata.
    gMinis.addLink(gMqtt);
    gMinis.addLink(gBus);

    gMinis.addEntity(gCabinetTemp);
    gMinis.addEntity(gFan);

    App::config()
        .name(kDevice)
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gNet)      // sieć przed modułem IoT — kolejność rejestracji
        .add(gMinis)    // jest kolejnością uruchamiania
        .add(gGateway);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

void loop() {
    // Wszystko dzieje się w taskach: net.worker trzyma połączenie,
    // minis.worker przekłada ramki, gw.report raportuje.
}
