/**
 * Hydra — przykład: mqtt-telemetry.
 *
 * Kryterium ukończenia etapu M3 (rozdz. 14): urządzenie po restarcie samo
 * wraca online i publikuje telemetrię.
 *
 * W kodzie nie ma ani jednej linii obsługującej ponowne łączenie, backoff czy
 * odtwarzanie subskrypcji — to wszystko robi moduł sieciowy. Aplikacja
 * deklaruje jedynie, które zdarzenia mają jechać na brokera i które tematy
 * mają stawać się zdarzeniami.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/net/NetModule.hpp"
#include "hydra/net/TelemetryBridge.hpp"

HYDRA_LOG_MODULE("app")

using namespace hydra;

namespace {

/** Komenda przychodząca z brokera. */
struct SetInterval {
    u32 seconds;
};

net::NetModule       gNet(net::defaultNetworkInterface(), &net::defaultMdns());
net::TelemetryBridge gBridge(gNet.mqtt());
UartLogSink          gConsole;

/** Formater telemetrii: puls systemu → JSON. */
int formatHeartbeat(const SysHeartbeat& e, char* out, size_t cap) {
    return snprintf(out, cap,
                    "{\"uptime\":%lu,\"heap\":%lu,\"tasks\":%u}",
                    static_cast<unsigned long>(e.uptimeMs / 1000),
                    static_cast<unsigned long>(e.freeHeapBytes),
                    static_cast<unsigned>(e.taskCount));
}

/** Parser komendy: ładunek tekstowy → zdarzenie. */
bool parseInterval(CByteSpan payload, SetInterval& out) {
    char buf[12] = {};
    if (payload.empty() || payload.size() >= sizeof(buf)) return false;
    memcpy(buf, payload.data(), payload.size());

    const long v = atol(buf);
    if (v < 1 || v > 3600) return false;  // ładunek spoza sensownego zakresu
    out.seconds = static_cast<u32>(v);
    return true;
}

}  // namespace

HYDRA_DECLARE_EVENT(SetInterval, "app/set-interval")

void setup() {
    net::NetModule::Config cfg;
    cfg.mqtt.clientId    = "rover-01";
    cfg.mqtt.host        = "broker.local";
    cfg.mqtt.port        = 1883;
    cfg.mqtt.keepAliveSec = 30;
    // Last Will: broker ogłosi zgon urządzenia, gdy przestanie odpowiadać.
    cfg.mqtt.willTopic   = "hydra/rover-01/status";
    cfg.mqtt.willPayload = "offline";
    cfg.mqtt.willRetain  = true;
    cfg.mdnsHostname     = "rover-01";

    gNet.configure(cfg);

    // Poświadczenia trafiają do pamięci trwałej przy pierwszym uruchomieniu;
    // po restarcie moduł wczytuje je sam i nie potrzebuje tej linii.
    net::NetworkCredentials home;
    strncpy(home.ssid, "moja-siec", net::kSsidMax - 1);
    home.psk.set("haslo-do-wifi");
    gNet.connection().addNetwork(home);

    App::config()
        .name("rover-01")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .housekeepingMs(10000)
        .add(gNet);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return;
    }

    // Zapis poświadczeń po udanym starcie — od następnego uruchomienia
    // urządzenie wraca online bez udziału człowieka.
    gNet.connection().saveNetworks();

    // Cała telemetria to jedna deklaracja. Puls systemu jedzie na brokera
    // sam, bez kodu w pętli i bez pilnowania stanu połączenia.
    gBridge.publishOn<SysHeartbeat>("hydra/rover-01/sys", 0, false, formatHeartbeat);

    // I odwrotnie: wiadomość z tematu staje się zdarzeniem na magistrali,
    // które obsłuży dowolny moduł.
    gBridge.subscribeTo<SetInterval>("hydra/rover-01/cmd/interval", 1, parseInterval);

    EventBus::subscribe<SetInterval>([](const SetInterval& c) {
        HYDRA_LOGI("nowy okres telemetrii: %lus", static_cast<unsigned long>(c.seconds));
    });

    EventBus::subscribe<net::ConnStateChanged>([](const net::ConnStateChanged& e) {
        HYDRA_LOGI("połączenie: %s → %s", net::toString(e.from), net::toString(e.to));
    });

    EventBus::subscribe<net::MqttStateChanged>([](const net::MqttStateChanged& e) {
        HYDRA_LOGI("broker: %s (subskrypcji: %u)", e.connected ? "połączony" : "rozłączony",
                   static_cast<unsigned>(e.subscriptions));
    });
}

void loop() {}
