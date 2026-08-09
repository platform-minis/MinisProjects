/** Hydra — implementacja łącza MQTT dla protokołu MyCastle. */

#include "hydra/minis/links/MqttLink.hpp"

#if HYDRA_ENABLE_MINIS && HYDRA_ENABLE_NET

#include "hydra/core/Log.hpp"

#include <stdio.h>
#include <string.h>

HYDRA_LOG_MODULE("minis.mqtt")

namespace hydra {
namespace minis {

Status MqttLink::configure(const Config& cfg) {
    if (!cfg.self.valid()) return fail(Err::BadArgument);
    cfg_ = cfg;
    return ok();
}

size_t MqttLink::mtu() const {
    // Bufor klienta musi pomieścić nagłówek MQTT i temat oprócz ładunku.
    // Zwracamy to, co naprawdę zostaje — inaczej router przepuściłby ramkę,
    // którą klient odrzuci dopiero przy składaniu pakietu.
    constexpr size_t kOverhead = kTopicMax + 8;
    return HYDRA_MQTT_BUFFER > kOverhead ? HYDRA_MQTT_BUFFER - kOverhead : 0;
}

Status MqttLink::begin() {
    if (subscribed_) return ok();
    // Symbol wieloznaczny na miejscu urządzenia — powód w nagłówku klasy.
    HYDRA_CHECK(subscribeDownlink(cfg_.self.user,
                                  cfg_.gateway ? "+" : cfg_.self.device));
    subscribed_ = true;
    return ok();
}

Status MqttLink::observe(const DeviceAddr& addr) {
    // Bramka słyszy już wszystko dla swojego użytkownika, więc nie ma czego
    // dopisywać. Zwracamy powodzenie, bo router pyta o to przy każdej nowej
    // trasie i błąd oznaczałby ostrzeżenie przy każdym nowym węźle.
    if (cfg_.gateway) return ok();
    if (!addr.valid()) return fail(Err::BadArgument);
    return subscribeDownlink(addr.user, addr.device);
}

Status MqttLink::subscribeDownlink(const char* user, const char* device) {
    // Wyłącznie kierunek z serwera. Zapisanie się na własną telemetrię
    // zawróciłoby ją do routera — broker odsyła publikacje także nadawcy.
    static const char* const kSuffixes[] = {"command", "twin/desired", "ext/+/req"};

    auto handler = net::MqttClient::MessageHandler(
        [this](const char* topic, CByteSpan payload) { onMessage(topic, payload); });

    for (const char* suffix : kSuffixes) {
        char filter[kTopicMax];
        const int written = snprintf(filter, sizeof(filter), "minis/%s/%s/%s",
                                     user, device, suffix);
        if (written < 0 || static_cast<size_t>(written) >= sizeof(filter)) {
            return fail(Err::OutOfRange);
        }
        HYDRA_CHECK(mqtt_.subscribe(filter, cfg_.qos, handler));
        ++stats_.subs;
    }
    return ok();
}

Status MqttLink::send(const Frame& frame) {
    if (!mqtt_.connected()) return fail(Err::NotInitialized);

    char topic[kTopicMax];
    if (!buildTopic(topic, sizeof(topic), frame.addr, frame.kind,
                    frame.extType[0] != '\0' ? frame.extType : nullptr)) {
        return fail(Err::BadArgument);
    }

    HYDRA_CHECK(mqtt_.publish(topic, frame.payload, frame.qos, false));
    ++stats_.sent;
    return ok();
}

void MqttLink::poll(Millis now) {
    if (cfg_.ownsClientLoop) mqtt_.loop(now);
}

void MqttLink::onMessage(const char* topic, CByteSpan payload) {
    Frame frame;
    if (!parseTopic(topic, frame.addr, frame.kind, frame.extType, kExtTypeMax)) {
        ++stats_.badTopic;
        HYDRA_LOGD("temat spoza przestrzeni minis: %s", topic);
        return;
    }

    // Ruch w górę na wejściu oznacza echo: albo broker odesłał nam własną
    // publikację, albo ktoś nadaje w cudzym imieniu. W obu przypadkach
    // przekazanie tego routerowi zrobiłoby pętlę.
    if (flowsUpstream(frame.kind)) return;

    frame.payload = payload;
    frame.qos     = cfg_.qos;
    ++stats_.received;
    deliver(frame);
}

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS && HYDRA_ENABLE_NET
