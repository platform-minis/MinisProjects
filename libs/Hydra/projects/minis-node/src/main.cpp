/**
 * minis-node — węzeł IoT MyCastle, ten sam kod na układzie i w przeglądarce.
 *
 * Encje, telemetria i komendy nie wiedzą, gdzie stoją. Różni je **jedna
 * linijka**: skąd bierze się gniazdo dla MQTT.
 *
 *     esp32s3        WiFiClient → TCP → broker
 *     przegladarka   WebSocket przeglądarki → broker
 *
 * Karta przeglądarki nie ma gniazd TCP — ma wyłącznie WebSockety. Ponieważ
 * `MqttClient` rozmawia z `IClient`, a nie z konkretnym gniazdem, podmiana
 * transportu nie dotyka protokołu ani encji. To jest cały powód, dla którego
 * te warstwy są rozdzielone.
 *
 * ## Co z tego wynika praktycznie
 *
 * Symulowane urządzenie w karcie łączy się z **prawdziwym** brokerem i widać
 * je w MyCastle obok fizycznych. Nie jest to makieta protokołu — to ten sam
 * kod, który pójdzie na płytkę, tylko z innym gniazdem pod spodem.
 *
 * Broker musi wystawiać port WebSocket (Aedes robi to obok portu TCP)
 * i podprotokół `mqtt`.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/minis/MinisModule.hpp"
#include "hydra/minis/links/MqttLink.hpp"
#include "hydra/net/MqttClient.hpp"

#if defined(HYDRA_PLAT_WASM) && HYDRA_PLAT_WASM
namespace hydra {
namespace net {
/** Gniazdo przeglądarki; definicja w src/wasm/WasmWebSocket.cpp. */
IClient& browserWebSocket();
}  // namespace net
}  // namespace hydra
#else
#  include "hydra/net/NetModule.hpp"
#endif

HYDRA_LOG_MODULE("node")

using namespace hydra;
using namespace hydra::minis;

namespace {

constexpr const char* kUser   = "user1";
constexpr const char* kDevice = "wasm-node";
constexpr const char* kBroker = "localhost";

/**
 * Port zależy od transportu, nie od preferencji.
 *
 * 1883 to MQTT po TCP; 9001 to ten sam protokół po WebSockecie i broker
 * nasłuchuje tam osobno. Pomyłka kończy się połączeniem, które się nawiązuje
 * i zaraz zrywa — bez komunikatu wskazującego przyczynę.
 */
#if defined(HYDRA_PLAT_WASM) && HYDRA_PLAT_WASM
constexpr u16 kPort = 9001;
#else
constexpr u16 kPort = 1883;
#endif

// ── Encje ──────────────────────────────────────────────────────────────────

bool  gRelayOn = false;
float gTemperature = 21.5f;

Entity gTemp  = Entity::sensor("temperature", "Temperatura", "temperature", "°C");
Entity gRelay = Entity::toggle("relay", "Przekaźnik", [](json::JsonView value) {
    gRelayOn = value.asBool(gRelayOn);
    HYDRA_LOGI("przekaznik: %s", gRelayOn ? "wlaczony" : "wylaczony");
    return gRelayOn;
});

// ── Warstwy ────────────────────────────────────────────────────────────────

net::IClient& transport() {
#if defined(HYDRA_PLAT_WASM) && HYDRA_PLAT_WASM
    return net::browserWebSocket();
#else
    return *net::defaultNetworkInterface().createClient();
#endif
}

net::MqttClient gMqtt{transport()};
MqttLink        gLink{gMqtt};
MinisIotModule  gMinis;

#if HYDRA_PLAT_HOST
StdoutLogSink gConsole;
#else
UartLogSink   gConsole;
#endif

Status setup() {
    net::MqttClient::Config mqtt;
    mqtt.clientId = kDevice;
    mqtt.host     = kBroker;
    mqtt.port     = kPort;
    HYDRA_CHECK(gMqtt.configure(mqtt));

    MqttLink::Config link;
    link.self = DeviceAddr::of(kUser, kDevice);
    HYDRA_CHECK(gLink.configure(link));

    MinisIotModule::Config node;
    node.self         = DeviceAddr::of(kUser, kDevice);
    node.label        = "Węzeł w przeglądarce";
    node.heartbeatSec = 30;
    HYDRA_CHECK(gMinis.configure(node));

    HYDRA_TRY(const auto id, gMinis.addLink(gLink));
    (void)id;
    HYDRA_CHECK(gMinis.addEntity(gTemp));
    HYDRA_CHECK(gMinis.addEntity(gRelay));

    App::config()
        .name(kDevice)
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gMinis);

    return App::begin();
}

}  // namespace

#if HYDRA_PLAT_HOST

int main() {
    if (auto r = setup(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return 1;
    }

    // Telemetria z suwaka panelu: `stim` podmienia odczyt, a węzeł wysyła to,
    // co przeczytał — tak samo jak zrobiłby z prawdziwym czujnikiem.
    for (;;) {
        // Dwie wielkości w jednej wiadomości, nie dwie wiadomości: serwer
        // dostaje spójny obraz chwili, a nie dwa odczyty z różnych momentów.
        const Metric metrics[] = {
            {"temperature", "°C", Metric::Type::Float, gTemperature, false, nullptr},
            {"relay",       nullptr, Metric::Type::Bool, 0.0f, gRelayOn, nullptr},
        };
        (void)gMinis.sendTelemetry(metrics, 2);
        rtos::delayMs(5000);
    }
}

#else

void setup_() { (void)setup(); }
void setup() { setup_(); }
void loop() {}

#endif
