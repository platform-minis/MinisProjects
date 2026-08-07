#pragma once
/**
 * Hydra — moduł sieciowy spinający łącze, protokoły i cykl życia (rozdz. 7).
 *
 * Kryterium ukończenia etapu M3: urządzenie po restarcie samo wraca online
 * i publikuje telemetrię. Odpowiadają za to trzy mechanizmy:
 *   - poświadczenia w pamięci trwałej, wczytywane przy inicjalizacji,
 *   - maszyna stanów z wykładniczym backoffem i sieciami zapasowymi,
 *   - odtwarzanie sesji MQTT wraz z subskrypcjami po każdym zerwaniu.
 *
 * Warstwy są rozdzielone: utrata brokera przy sprawnym Wi-Fi przenosi
 * połączenie w stan Degraded i uruchamia ponowne łączenie z brokerem, ale
 * nie zrywa łącza. Zrywanie Wi-Fi z powodu problemów brokera tylko
 * wydłużałoby przestój.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/core/IModule.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/net/ConnectionManager.hpp"
#include "hydra/net/MqttClient.hpp"

namespace hydra {
namespace net {

class NetModule : public ModuleBase {
public:
    struct Config {
        ConnectionManager::Config connection;
        MqttClient::Config        mqtt;

        /** Okres taska net.worker. */
        u32  workerPeriodMs = 50;
        bool enableMqtt     = true;
        /** Odczekanie między próbami połączenia z brokerem. */
        u32  mqttRetryMs    = 5000;

        /** Nazwa ogłaszana w mDNS jako <hostname>.local (rozdz. 7.2). */
        const char* mdnsHostname = nullptr;
        u16         mdnsPort     = 80;
    };

    struct Stats {
        u32 mqttAttempts = 0;
        u32 workerTicks  = 0;
    };

    explicit NetModule(INetworkInterface& iface, IMdns* mdns = nullptr);

    /** Ustawia konfigurację. Wołane przed App::begin(). */
    Status configure(const Config& cfg);

    ConnectionManager& connection() { return conn_; }
    MqttClient&        mqtt() { return mqtt_; }

    /**
     * Jeden krok pętli sieciowej. Normalnie woła ją task net.worker;
     * wystawiona publicznie, żeby testy mogły sterować czasem.
     */
    void step(Millis now);

    Stats stats() const { return stats_; }

protected:
    Status onInit() override;
    Status onStart() override;
    void   onStop() override;

private:
    void onLinkUp();

    INetworkInterface& iface_;
    IMdns*             mdns_ = nullptr;
    /** Deklarowany przed mqtt_ — ten porządek gwarantuje ważność referencji. */
    IClient*           client_ = nullptr;

    ConnectionManager conn_;
    MqttClient        mqtt_;
    Config            cfg_{};
    Task              task_;

    Millis mqttRetryAt_ = 0;
    bool   mdnsStarted_ = false;
    Stats  stats_{};
};

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
