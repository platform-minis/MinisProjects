/** Hydra — implementacja modułu sieciowego (rozdz. 7). */

#include "hydra/net/NetModule.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("net")

namespace hydra {
namespace net {

NetModule::NetModule(INetworkInterface& iface, IMdns* mdns)
    : ModuleBase("net"),
      iface_(iface),
      mdns_(mdns),
      client_(iface.createClient()),
      conn_(iface),
      mqtt_(*client_) {}

Status NetModule::configure(const Config& cfg) {
    if (cfg.workerPeriodMs == 0) return fail(Err::BadArgument);
    cfg_ = cfg;
    return ok();
}

Status NetModule::onInit() {
    if (!client_) return fail(Err::NotSupported);

    HYDRA_CHECK(conn_.init(cfg_.connection));

    // Poświadczenia zapisane wcześniej mają pierwszeństwo — to one sprawiają,
    // że urządzenie po restarcie wraca online bez udziału człowieka.
    // Sieci dodane programowo przed init() zostają, gdy pamięć jest pusta.
    if (auto r = conn_.loadNetworks(); !r) {
        if (conn_.networkCount() == 0) {
            HYDRA_LOGW("brak zapisanych sieci — wymagane wprowadzenie poświadczeń");
        }
    }

    if (cfg_.enableMqtt) {
        HYDRA_CHECK(mqtt_.configure(cfg_.mqtt));
    }
    return ok();
}

Status NetModule::onStart() {
    if (conn_.networkCount() > 0) {
        if (auto r = conn_.start(rtos::nowMs()); !r) {
            HYDRA_LOGE("start połączenia nieudany: %s", toString(r.error()));
            return r;
        }
    }

    Task::Cfg cfg;
    cfg.name = "net.worker";
    cfg.prio = Prio::Normal;
    // Rdzeń 0: na ESP32 obsługuje on stos Wi-Fi, a taski czasu rzeczywistego
    // siedzą na rdzeniu 1 (rozdz. 10).
    cfg.core = Core::Core0;
    return task_.startPeriodic(cfg, cfg_.workerPeriodMs, [this] { step(rtos::nowMs()); });
}

void NetModule::onStop() {
    task_.stopAndWait();
    mqtt_.disconnect();
    conn_.stop();
    if (mdns_ && mdnsStarted_) {
        mdns_->end();
        mdnsStarted_ = false;
    }
}

void NetModule::onLinkUp() {
    if (!mdns_ || mdnsStarted_ || !cfg_.mdnsHostname) return;

    if (auto r = mdns_->begin(cfg_.mdnsHostname); !r) {
        HYDRA_LOGW("mDNS niedostępne: %s", toString(r.error()));
        return;
    }
    mdns_->addService("_http", "_tcp", cfg_.mdnsPort);
    mdnsStarted_ = true;
    HYDRA_LOGI("urządzenie ogłoszone jako %s.local", cfg_.mdnsHostname);
}

void NetModule::step(Millis now) {
    ++stats_.workerTicks;

    const ConnState before = conn_.state();
    conn_.tick(now);
    const ConnState state = conn_.state();

    const bool linkReady = (state == ConnState::Online || state == ConnState::Degraded);

    if (linkReady && before != ConnState::Online && before != ConnState::Degraded) {
        onLinkUp();
        // Świeże łącze — nie ma powodu odczekiwać przed pierwszą próbą
        // połączenia z brokerem.
        mqttRetryAt_ = now;
    }

    if (!cfg_.enableMqtt) return;

    if (!linkReady) {
        // Bez łącza gniazdo i tak jest martwe; zamykamy je jawnie, żeby
        // klient nie próbował pisać w nieistniejące połączenie.
        if (mqtt_.connected()) mqtt_.disconnect();
        return;
    }

    if (!mqtt_.connected() && static_cast<i32>(now - mqttRetryAt_) >= 0) {
        ++stats_.mqttAttempts;
        if (auto r = mqtt_.connect(now); !r) {
            HYDRA_LOGW("połączenie z brokerem nieudane: %s", toString(r.error()));
        }
        mqttRetryAt_ = now + cfg_.mqttRetryMs;
    }

    mqtt_.loop(now);

    // Sprawny broker to warunek pełnej sprawności usługi. Jego brak przenosi
    // maszynę stanów w Degraded, ale nie zrywa łącza.
    conn_.reportServiceHealth(mqtt_.connected());
}

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
