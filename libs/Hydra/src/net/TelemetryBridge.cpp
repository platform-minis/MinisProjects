/** Hydra — implementacja mostka MQTT ↔ EventBus (rozdz. 7.2). */

#include "hydra/net/TelemetryBridge.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("net.bridge")

namespace hydra {
namespace net {

TelemetryBridge::~TelemetryBridge() {
    // Subskrypcje EventBusa przeżyłyby mostek i wołały metodę na zwolnionym
    // obiekcie — zdejmujemy je jawnie.
    for (u8 i = 0; i < outCount_; ++i) {
        if (outbound_[i].sub != kInvalidSub) EventBus::unsubscribe(outbound_[i].sub);
    }
    for (u8 i = 0; i < inCount_; ++i) {
        if (inbound_[i].filter) mqtt_.unsubscribe(inbound_[i].filter);
    }
}

void TelemetryBridge::emit(u8 index, const void* event) {
    if (index >= outCount_) return;
    const Outbound& route = outbound_[index];
    if (!route.shim || !route.fn) return;

    char      payload[HYDRA_BRIDGE_PAYLOAD_MAX];
    const int written = route.shim(route.fn, event, payload, sizeof(payload));

    if (written < 0) {
        ++stats_.publishFailed;
        return;
    }
    // snprintf zwraca długość, jaką *chciał* zapisać — obcięcie oznacza
    // niekompletny ładunek, a taki lepiej porzucić niż wysłać uszkodzony.
    if (static_cast<size_t>(written) >= sizeof(payload)) {
        ++stats_.publishFailed;
        HYDRA_LOGW("ładunek dla '%s' nie zmieścił się w buforze", route.topic);
        return;
    }

    if (auto r = mqtt_.publish(route.topic,
                               CByteSpan{reinterpret_cast<const u8*>(payload),
                                         static_cast<size_t>(written)},
                               route.qos, route.retain);
        !r) {
        ++stats_.publishFailed;
        return;
    }
    ++stats_.publishedOk;
}

void TelemetryBridge::ingest(u8 index, CByteSpan payload) {
    if (index >= inCount_) return;
    const Inbound& route = inbound_[index];
    if (!route.shim || !route.fn) return;

    if (route.shim(route.fn, payload)) {
        ++stats_.ingested;
    } else {
        ++stats_.rejected;
        HYDRA_LOGW("odrzucony ładunek z '%s'", route.filter);
    }
}

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
