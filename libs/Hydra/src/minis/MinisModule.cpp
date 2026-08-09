/** Hydra — implementacja modułu IoT MyCastle. */

#include "hydra/minis/MinisModule.hpp"

#if HYDRA_ENABLE_MINIS

#include "hydra/core/App.hpp"
#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"

#include <string.h>

HYDRA_LOG_MODULE("minis")

namespace hydra {
namespace minis {

// ---------------------------------------------------------------------------
// Encje
// ---------------------------------------------------------------------------

void Entity::describe(json::JsonWriter& out) const {
    out.beginObject();
    out.key("id").value(id);
    out.key("type").value(toString(kind));
    if (name) out.key("name").value(name);
    if (deviceClass) out.key("device_class").value(deviceClass);
    if (unit) out.key("unit").value(unit);

    if (kind == EntityKind::Number) {
        out.key("min").value(minimum);
        out.key("max").value(maximum);
        // Krok zerowy oznaczałby suwak, którego nie da się ruszyć — panel
        // przyjmuje wtedy własną wartość domyślną, a my nie kłamiemy.
        if (step > 0.0f) out.key("step").value(step);
    }
    if (kind == EntityKind::Select && options != nullptr) {
        out.key("options").beginArray();
        for (u8 i = 0; i < optionCount; ++i) out.value(options[i]);
        out.endArray();
    }
    out.endObject();
}

// ---------------------------------------------------------------------------
// Cykl życia
// ---------------------------------------------------------------------------

Status MinisIotModule::configure(const Config& cfg) {
    if (!cfg.self.valid()) return fail(Err::BadArgument);
    cfg_ = cfg;
    router_.configure(cfg.routing);
    router_.setLocal(cfg.self);
    router_.setLocalHandler([this](const Frame& frame) { onLocalFrame(frame); });
    return ok();
}

Result<LinkId> MinisIotModule::addLink(ILink& link) { return router_.addLink(link); }

Status MinisIotModule::addEntity(Entity& entity) {
    if (entity.id == nullptr || entity.id[0] == '\0') return fail(Err::BadArgument);
    if (entityCount_ >= HYDRA_MINIS_MAX_ENTITIES) return fail(Err::OutOfMemory);

    // Identyfikator encji jest zarazem nazwą komendy zapisu, więc duplikat
    // oznaczałby, że jedna z dwóch nigdy nie dostanie sterowania — i nie
    // dałoby się tego zauważyć inaczej niż przez „przycisk nie działa".
    for (u8 i = 0; i < entityCount_; ++i) {
        if (strcmp(entities_[i]->id, entity.id) == 0) return fail(Err::AlreadyExists);
    }
    entities_[entityCount_++] = &entity;
    return ok();
}

Status MinisIotModule::addExtension(const char* extType, ExtHandler handler) {
    if (extType == nullptr || extType[0] == '\0') return fail(Err::BadArgument);
    for (auto& slot : extensions_) {
        if (slot.used) continue;
        size_t i = 0;
        while (extType[i] != '\0' && i + 1 < kExtTypeMax) { slot.type[i] = extType[i]; ++i; }
        slot.type[i]  = 0;
        slot.handler  = handler;
        slot.used     = true;
        return ok();
    }
    return fail(Err::OutOfMemory);
}

Status MinisIotModule::onInit() {
    if (!cfg_.self.valid()) return fail(Err::NotInitialized);
    for (u8 i = 0; i < router_.linkCount(); ++i) {
        ILink* link = router_.link(i);
        if (link != nullptr) HYDRA_CHECK(link->begin());
    }
    return ok();
}

Status MinisIotModule::onStart() {
    startedAtMs_ = App::uptimeMs();
    Task::Cfg cfg;
    cfg.name = "minis.worker";
    cfg.prio = Prio::Normal;
    return task_.startPeriodic(cfg, cfg_.workerPeriodMs,
                               [this] { step(App::uptimeMs()); });
}

void MinisIotModule::onStop() { task_.stopAndWait(); }

void MinisIotModule::step(Millis now) {
    router_.poll(now);

    const bool online = router_.online();
    if (online != wasOnline_) {
        wasOnline_ = online;
        // Po odzyskaniu łączności trzeba się przedstawić od nowa: serwer mógł
        // w międzyczasie uznać urządzenie za offline i wyczyścić listę encji.
        if (!online) announced_ = false;
        EventBus::publish(MinisState{online, router_.uplink(),
                                     router_.routeCount(), router_.linksUpMask()});
    }
    if (!online) return;

    if (!announced_) {
        if (cfg_.autoRegister) (void)sendRegisterRequest(cfg_.label);
        // Kolejność jak w MinisLib: najpierw zgłoszenie do listy, potem hello.
        // Odwrotna daje `hello` od urządzenia, którego serwer jeszcze nie zna.
        if (sendHello()) {
            announced_ = true;
            lastHeartbeatMs_ = now;
        }
        return;
    }

    if (cfg_.heartbeatSec > 0 && now - lastHeartbeatMs_ >= cfg_.heartbeatSec * 1000u) {
        (void)sendHeartbeat();
        lastHeartbeatMs_ = now;
    }
}

// ---------------------------------------------------------------------------
// Nadawanie
// ---------------------------------------------------------------------------

Status MinisIotModule::publish(MsgKind kind, const char* payload, const char* extType) {
    Frame frame;
    frame.addr = cfg_.self;
    frame.kind = kind;
    if (extType != nullptr) {
        size_t i = 0;
        while (extType[i] != '\0' && i + 1 < kExtTypeMax) { frame.extType[i] = extType[i]; ++i; }
        frame.extType[i] = 0;
    }
    frame.payload = CByteSpan{reinterpret_cast<const u8*>(payload), strlen(payload)};
    return router_.send(frame, App::uptimeMs());
}

Status MinisIotModule::sendTelemetry(const Metric* metrics, size_t count,
                                     float battery, i16 rssi) {
    if (metrics == nullptr && count > 0) return fail(Err::BadArgument);

    json::JsonWriter out{ByteSpan{tx_, sizeof(tx_)}};
    out.beginObject();
    out.key("metrics").beginArray();
    for (size_t i = 0; i < count; ++i) {
        const Metric& m = metrics[i];
        out.beginObject();
        out.key("key").value(m.key);
        switch (m.type) {
            case Metric::Type::Float: out.key("value").value(m.number); break;
            case Metric::Type::Bool:  out.key("value").value(m.flag);   break;
            case Metric::Type::Text:  out.key("value").value(m.text);   break;
        }
        if (m.unit) out.key("unit").value(m.unit);
        out.endObject();
    }
    out.endArray();
    if (rssi != 0)          out.key("rssi").value(static_cast<i32>(rssi));
    if (battery > 0.0f)     out.key("battery").value(battery);
    out.endObject();

    if (!out.ok()) {
        // Obcięta telemetria to dokument, który serwer odrzuci w całości —
        // razem z pomiarami, które by się zmieściły. Lepiej powiedzieć wprost,
        // że bufor jest za mały, niż wysłać coś, co zniknie po drodze.
        ++stats_.truncated;
        HYDRA_LOGW("telemetria nie mieści się w %u B (potrzeba %u) — podnieś "
                   "HYDRA_MINIS_TX_BUFFER albo wyślij mniej pomiarów naraz",
                   static_cast<unsigned>(sizeof(tx_)),
                   static_cast<unsigned>(out.needed()));
        return fail(Err::OutOfRange);
    }

    HYDRA_CHECK(publish(MsgKind::Telemetry, out.text()));
    ++stats_.telemetry;
    // Telemetria odświeża obecność tak samo jak puls — nie ma powodu wysyłać
    // obu w tej samej sekundzie.
    lastHeartbeatMs_ = App::uptimeMs();
    return ok();
}

Status MinisIotModule::sendHello() {
    json::JsonWriter out{ByteSpan{tx_, sizeof(tx_)}};
    out.beginObject();
    out.key("uptime").value(static_cast<u32>((App::uptimeMs() - startedAtMs_) / 1000));

    bool anyExt = false;
    for (const auto& ext : extensions_) if (ext.used) { anyExt = true; break; }
    if (anyExt) {
        out.key("extensions").beginArray();
        for (const auto& ext : extensions_) {
            if (!ext.used) continue;
            out.beginObject().key("type").value(ext.type).key("enabled").value(true).endObject();
        }
        out.endArray();
    }

    if (entityCount_ > 0) {
        out.key("entities").beginArray();
        for (u8 i = 0; i < entityCount_; ++i) entities_[i]->describe(out);
        out.endArray();
    }
    out.endObject();

    if (!out.ok()) {
        ++stats_.truncated;
        HYDRA_LOGE("hello z %u encjami nie mieści się w %u B (potrzeba %u) — "
                   "panel nie zobaczy części elementów",
                   static_cast<unsigned>(entityCount_),
                   static_cast<unsigned>(sizeof(tx_)),
                   static_cast<unsigned>(out.needed()));
        return fail(Err::OutOfRange);
    }

    HYDRA_CHECK(publish(MsgKind::Hello, out.text()));
    ++stats_.helloSent;
    return ok();
}

Status MinisIotModule::sendHeartbeat(float battery) {
    json::JsonWriter out{ByteSpan{tx_, sizeof(tx_)}};
    out.beginObject();
    out.key("uptime").value(static_cast<u32>((App::uptimeMs() - startedAtMs_) / 1000));
    if (battery > 0.0f) out.key("battery").value(battery);
    out.endObject();
    if (!out.ok()) return fail(Err::OutOfRange);
    return publish(MsgKind::Heartbeat, out.text());
}

Status MinisIotModule::sendRegisterRequest(const char* label) {
    json::JsonWriter out{ByteSpan{tx_, sizeof(tx_)}};
    out.beginObject();
    out.key("kind").value("firmware");
    out.key("label").value((label && label[0]) ? label : cfg_.self.device);
    // Identyfikator urządzenia jest zarazem numerem seryjnym — tak samo jak
    // w MinisLib, gdzie wstrzykuje go generator wsadu.
    out.key("sn").value(cfg_.self.device);
    out.endObject();
    if (!out.ok()) return fail(Err::OutOfRange);
    return publish(MsgKind::RegisterRequest, out.text());
}

Status MinisIotModule::ackCommand(const char* id, bool success, const char* reason) {
    json::JsonWriter out{ByteSpan{tx_, sizeof(tx_)}};
    out.beginObject();
    out.key("id").value(id);
    out.key("status").value(success ? "ACKNOWLEDGED" : "FAILED");
    if (!success && reason) out.key("reason").value(reason);
    out.endObject();
    if (!out.ok()) return fail(Err::OutOfRange);
    return publish(MsgKind::CommandAck, out.text());
}

Status MinisIotModule::extRespond(const char* extType, const char* id, bool success,
                                  const char* dataJson,
                                  const char* errorCode, const char* errorMsg) {
    json::JsonWriter out{ByteSpan{tx_, sizeof(tx_)}};
    out.beginObject();
    out.key("id").value(id);
    out.key("ok").value(success);
    // `raw`, nie `value`: rozszerzenie oddaje gotowy obiekt, a opakowanie go
    // w napis dałoby na serwerze tekst zamiast danych.
    if (success && dataJson) out.key("data").raw(dataJson);
    if (!success) {
        out.key("error").beginObject();
        if (errorCode) out.key("code").value(errorCode);
        if (errorMsg)  out.key("message").value(errorMsg);
        out.endObject();
    }
    out.endObject();
    if (!out.ok()) return fail(Err::OutOfRange);
    return publish(MsgKind::ExtResponse, out.text(), extType);
}

Status MinisIotModule::reportTwin(const char* jsonText) {
    if (jsonText == nullptr) return fail(Err::BadArgument);
    return publish(MsgKind::TwinReported, jsonText);
}

// ---------------------------------------------------------------------------
// Odbiór
// ---------------------------------------------------------------------------

void MinisIotModule::onLocalFrame(const Frame& frame) {
    // Ładunek nie ma zera kończącego — JsonView bierze długość osobno, więc
    // nie ma potrzeby kopiowania go tylko po to, żeby je dopisać.
    const json::JsonView doc{reinterpret_cast<const char*>(frame.payload.data()),
                             frame.payload.size()};

    switch (frame.kind) {
        case MsgKind::Command:    handleCommand(doc); break;
        case MsgKind::ExtRequest: handleExtRequest(frame.extType, doc); break;
        case MsgKind::TwinDesired:
            // Bliźniak nie ma domyślnej obsługi: co znaczy „pożądany stan",
            // wie wyłącznie aplikacja. Przekazujemy jak komendę o tej nazwie.
            if (onCommand_) onCommand_("", "twin/desired", doc);
            break;
        default:
            HYDRA_LOGD("ramka %s bez obsługi", toString(frame.kind));
            break;
    }
}

void MinisIotModule::handleCommand(json::JsonView doc) {
    char id[40] = {};
    char name[32] = {};
    doc.get("id").asString(id, sizeof(id));
    doc.get("name").asString(name, sizeof(name));
    const json::JsonView payload = doc.get("payload");

    ++stats_.commands;

    if (routeToEntity(id, name, payload)) return;

    if (onCommand_) {
        onCommand_(id, name, payload);
        return;
    }

    // Brak obsługi jest odpowiedzią, a nie ciszą: panel czeka na potwierdzenie
    // i bez niego pokaże komendę jako wiszącą aż do przekroczenia czasu.
    (void)ackCommand(id, false, "brak obsługi tej komendy");
}

bool MinisIotModule::routeToEntity(const char* id, const char* name,
                                   json::JsonView payload) {
    for (u8 i = 0; i < entityCount_; ++i) {
        Entity& entity = *entities_[i];
        if (!entity.writable() || strcmp(entity.id, name) != 0) continue;

        // Wartość bywa podana jako `payload.value` albo jako sam `payload` —
        // panel wysyła to pierwsze, automatyzacje bywa że drugie.
        json::JsonView value = payload.get("value");
        if (!value.valid()) value = payload;

        const bool okResult = entity.onWrite ? entity.onWrite(value) : false;
        (void)ackCommand(id, okResult,
                         okResult ? nullptr : "encja odrzuciła wartość");
        return true;
    }
    return false;
}

void MinisIotModule::handleExtRequest(const char* extType, json::JsonView doc) {
    char id[40] = {};
    char op[32] = {};
    doc.get("id").asString(id, sizeof(id));
    doc.get("op").asString(op, sizeof(op));

    ++stats_.extCalls;

    for (const auto& ext : extensions_) {
        if (!ext.used || strcmp(ext.type, extType) != 0) continue;
        if (ext.handler) ext.handler(id, op, doc.get("params"));
        return;
    }

    (void)extRespond(extType, id, false, nullptr, "unsupported",
                     "urządzenie nie ma tego rozszerzenia");
}

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
