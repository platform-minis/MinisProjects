#pragma once
/**
 * Hydra — łącze MyCastle po MQTT.
 *
 * Cienka warstwa nad `net::MqttClient`: zamienia adres i rodzaj wiadomości na
 * temat i z powrotem. Nie ma tu ani jednej decyzji dotyczącej treści —
 * telemetria, komendy i rozszerzenia wyglądają stąd identycznie.
 *
 * **Subskrypcje.** Broker odsyła publikację także temu, kto ją nadał, więc
 * zapisanie się na `minis/{user}/{device}/#` zapętliłoby własną telemetrię.
 * Zapisujemy się wyłącznie na to, co przychodzi z serwera: `command`,
 * `twin/desired` i `ext/+/req`.
 *
 * Bramka robi to inaczej i to jest istota jej trybu. Trzy subskrypcje na
 * urządzenie razy szesnaście węzłów za magistralą to czterdzieści osiem
 * subskrypcji — przy `HYDRA_MQTT_MAX_SUBS` równym ośmiu kończy się to
 * urządzeniem, które obsługuje dwa pierwsze węzły i milczy o reszcie.
 * Dlatego bramka zapisuje się z symbolem wieloznacznym na **miejscu
 * urządzenia**: `minis/{user}/+/command`. Trzy subskrypcje niezależnie od
 * liczby węzłów, a ruch nieswój i tak odpada w routerze na braku trasy.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MINIS && HYDRA_ENABLE_NET

#include "hydra/minis/ILink.hpp"
#include "hydra/net/MqttClient.hpp"

namespace hydra {
namespace minis {

class MqttLink : public ILink {
public:
    struct Config {
        /** Adres tego urządzenia. W trybie bramki liczy się z niego nazwa użytkownika. */
        DeviceAddr self{};
        /**
         * Tryb bramki: subskrypcje z symbolem wieloznacznym zamiast jawnych.
         * Włącz, gdy za tym urządzeniem stoją inne.
         */
        bool gateway = false;
        u8   qos     = 1;
        /**
         * Czy to łącze ma samo kręcić pętlą klienta MQTT.
         *
         * Domyślnie nie: gdy w systemie jest `net::NetModule`, to on woła
         * `MqttClient::loop()` z taska sieciowego. Podwójne wołanie nie psuje
         * protokołu, ale podwaja odczyty z gniazda i zaciemnia, kto właściwie
         * odpowiada za połączenie.
         */
        bool ownsClientLoop = false;
    };

    struct Stats {
        u32 sent      = 0;
        u32 received  = 0;
        u32 badTopic  = 0;   ///< temat spoza przestrzeni minis/
        u32 subs      = 0;
    };

    explicit MqttLink(net::MqttClient& client) : mqtt_(client) {}

    Status configure(const Config& cfg);

    // --- ILink ---------------------------------------------------------------

    const char* name() const override { return cfg_.gateway ? "mqtt-gw" : "mqtt"; }
    Status begin() override;
    bool   up() const override { return mqtt_.connected(); }
    size_t mtu() const override;
    bool   isUplink() const override { return true; }
    Status send(const Frame& frame) override;
    void   poll(Millis now) override;
    Status observe(const DeviceAddr& addr) override;

    Stats stats() const { return stats_; }

private:
    /** Zapisuje się na wszystkie trzy tematy zstępujące dla podanego wzorca. */
    Status subscribeDownlink(const char* user, const char* device);
    void   onMessage(const char* topic, CByteSpan payload);

    net::MqttClient& mqtt_;
    Config           cfg_{};
    bool             subscribed_ = false;
    Stats            stats_{};
};

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS && HYDRA_ENABLE_NET
