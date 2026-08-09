#pragma once
/**
 * Hydra — moduł IoT platformy MyCastle.
 *
 * To, co widzi aplikacja: encje, telemetria, komendy, rozszerzenia. Ani jedno
 * z tych pojęć nie wie, czy pod spodem jest MQTT, WebSocket, czy RS-485 —
 * łącza wpina się do routera i moduł nadaje ramkę, nie pakiet.
 *
 *     MinisIotModule minis;
 *     minis.configure({ .self = DeviceAddr::of("user1", "dev-iot3") });
 *     minis.addLink(mqtt);          // węzeł podłączony wprost
 *     minis.addEntity(gTemp);
 *     minis.addEntity(gRelay);
 *     App::config().add(minis);
 *
 * Bramka różni się dwiema linijkami:
 *
 *     minis.addLink(mqtt);          // do serwera
 *     minis.addLink(rs485);         // do węzłów
 *
 * — resztę robi router: uczy się, kto stoi za magistralą, i przekłada ramki
 * w obie strony. Bramka może przy tym mieć własne encje albo nie mieć żadnych.
 *
 * **Cykl życia.** `onStart()` uruchamia task `minis.worker`, który odpytuje
 * łącza, wysyła puls i pilnuje ponownego przedstawienia się po odzyskaniu
 * łączności. Wszystko dzieje się w jednym miejscu i w jednym wątku; aplikacja
 * woła tylko `sendTelemetry()`.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MINIS

#include "hydra/core/IModule.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/minis/Entities.hpp"
#include "hydra/minis/Router.hpp"

/** Bufor na składaną wiadomość. Ogranicza `hello` z encjami i telemetrię. */
#ifndef HYDRA_MINIS_TX_BUFFER
#  define HYDRA_MINIS_TX_BUFFER 512
#endif

/** Ile encji naraz. */
#ifndef HYDRA_MINIS_MAX_ENTITIES
#  define HYDRA_MINIS_MAX_ENTITIES 12
#endif

/** Ile rozszerzeń naraz. */
#ifndef HYDRA_MINIS_MAX_EXTENSIONS
#  define HYDRA_MINIS_MAX_EXTENSIONS 4
#endif

namespace hydra {
namespace minis {

class MinisIotModule : public ModuleBase {
public:
    /** Komenda z panelu albo z REST-a. Po obsłużeniu trzeba wywołać `ackCommand()`. */
    using CommandHandler = Delegate<void(const char* id, const char* name,
                                         json::JsonView payload)>;
    /** Żądanie do rozszerzenia. Po obsłużeniu trzeba wywołać `extRespond()`. */
    using ExtHandler = Delegate<void(const char* id, const char* op,
                                     json::JsonView params)>;

    struct Config {
        DeviceAddr self{};
        /** Nazwa pokazywana w panelu zgłoszeń; brak = identyfikator urządzenia. */
        const char* label = nullptr;
        /**
         * Co ile wysyłać puls. Serwer uznaje urządzenie za offline po
         * 2,5-krotności tego czasu. Telemetria też odświeża obecność, więc
         * przy częstych pomiarach wolno tu dać 0 i oszczędzić ruch.
         */
        u32  heartbeatSec = 60;
        u32  workerPeriodMs = 100;
        /** Czy zgłaszać się do listy urządzeń po każdym połączeniu. */
        bool autoRegister = true;
        Router::Config routing{};
    };

    MinisIotModule() : ModuleBase("minis") {}

    struct Stats {
        u32 telemetry = 0;
        u32 commands  = 0;
        u32 extCalls  = 0;
        u32 helloSent = 0;
        u32 truncated = 0;   ///< wiadomości, które nie zmieściły się w buforze
    };

    Status configure(const Config& cfg);

    /** Rejestruje łącze. Kolejność wyznacza numery; pierwsze do serwera = trasa domyślna. */
    Result<LinkId> addLink(ILink& link);

    /** Encja musi przeżyć moduł — zwykle jest zmienną globalną. */
    Status addEntity(Entity& entity);
    Status addExtension(const char* extType, ExtHandler handler);
    void   onCommand(CommandHandler handler) { onCommand_ = handler; }

    Router& router() { return router_; }

    // --- wysyłanie ----------------------------------------------------------

    Status sendTelemetry(const Metric* metrics, size_t count,
                         float battery = 0.0f, i16 rssi = 0);
    Status sendTelemetry(const Metric& metric) { return sendTelemetry(&metric, 1); }

    /** Ogłasza obecność wraz z listą encji i rozszerzeń. */
    Status sendHello();
    Status sendHeartbeat(float battery = 0.0f);
    Status sendRegisterRequest(const char* label = nullptr);

    Status ackCommand(const char* id, bool success, const char* reason = nullptr);
    Status extRespond(const char* extType, const char* id, bool success,
                      const char* dataJson = nullptr,
                      const char* errorCode = nullptr, const char* errorMsg = nullptr);

    /** Zgłasza stan bliźniaka — dowolny obiekt JSON jako tekst. */
    Status reportTwin(const char* json);

    /** Jeden krok pętli. Normalnie woła ją task; publiczne dla testów. */
    void step(Millis now);

    bool  online() const { return router_.online(); }
    Stats stats() const { return stats_; }

protected:
    Status onInit() override;
    Status onStart() override;
    void   onStop() override;

private:
    struct Extension {
        char       type[kExtTypeMax] = {};
        ExtHandler handler{};
        bool       used = false;
    };

    /** Ramki adresowane do tego urządzenia. */
    void onLocalFrame(const Frame& frame);
    void handleCommand(json::JsonView doc);
    void handleExtRequest(const char* extType, json::JsonView doc);
    /** Czy komenda trafia w encję zapisywalną; `true` = obsłużona i potwierdzona. */
    bool routeToEntity(const char* id, const char* name, json::JsonView payload);

    Status publish(MsgKind kind, const char* payload, const char* extType = nullptr);

    Router router_;
    Config cfg_{};
    Task   task_;

    Entity*   entities_[HYDRA_MINIS_MAX_ENTITIES] = {};
    u8        entityCount_ = 0;
    Extension extensions_[HYDRA_MINIS_MAX_EXTENSIONS];

    CommandHandler onCommand_{};

    /** Bufor składanych wiadomości. Jeden, bo nadajemy z jednego wątku. */
    u8 tx_[HYDRA_MINIS_TX_BUFFER] = {};

    Millis lastHeartbeatMs_ = 0;
    Millis startedAtMs_     = 0;
    bool   announced_       = false;
    bool   wasOnline_       = false;
    Stats  stats_{};
};

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
