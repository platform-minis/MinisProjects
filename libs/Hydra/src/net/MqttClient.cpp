/**
 * Hydra — implementacja klienta MQTT 3.1.1 (rozdz. 7.2).
 *
 * Format ramek zgodny ze specyfikacją OASIS MQTT 3.1.1. Wszystkie długości
 * są big-endian, a pole „remaining length" używa kodowania zmiennej długości
 * po 7 bitów na bajt.
 */

#include "hydra/net/MqttClient.hpp"

#if HYDRA_ENABLE_NET

#include <string.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"

HYDRA_LOG_MODULE("net.mqtt")

namespace hydra {
namespace net {
namespace {

// Typy pakietów (górne cztery bity bajtu nagłówka).
constexpr u8 kConnect     = 0x10;
constexpr u8 kConnAck     = 0x20;
constexpr u8 kPublish     = 0x30;
constexpr u8 kPubAck      = 0x40;
constexpr u8 kSubscribe   = 0x82;  // z wymaganymi bitami 0010 w dolnym nibble
constexpr u8 kSubAck      = 0x90;
constexpr u8 kUnsubscribe = 0xA2;
constexpr u8 kPingReq     = 0xC0;
constexpr u8 kPingResp    = 0xD0;
constexpr u8 kDisconnect  = 0xE0;

/** Zapisuje długość w kodowaniu zmiennej długości. Zwraca liczbę bajtów. */
u8 encodeRemainingLength(u32 length, u8* out) {
    u8 n = 0;
    do {
        u8 digit = static_cast<u8>(length % 128);
        length /= 128;
        if (length > 0) digit |= 0x80;
        out[n++] = digit;
    } while (length > 0 && n < 4);
    return n;
}

/** Zapisuje napis w formacie MQTT: dwubajtowa długość i bajty. */
size_t writeString(u8* buf, size_t pos, const char* s) {
    const size_t len = s ? strlen(s) : 0;
    buf[pos++] = static_cast<u8>(len >> 8);
    buf[pos++] = static_cast<u8>(len & 0xFF);
    if (len) memcpy(buf + pos, s, len);
    return pos + len;
}

size_t stringFieldSize(const char* s) { return 2 + (s ? strlen(s) : 0); }

}  // namespace

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

Status IClient::readExactly(ByteSpan out, u32 timeoutMs) {
    size_t got = 0;
    const Millis deadline = rtos::nowMs() + timeoutMs;

    while (got < out.size()) {
        const size_t n = read(ByteSpan{out.data() + got, out.size() - got});
        if (n > 0) {
            got += n;
            continue;
        }
        if (!connected()) return fail(Err::IoError);
        if (static_cast<i32>(rtos::nowMs() - deadline) >= 0) return fail(Err::Timeout);
        rtos::delayMs(1);
    }
    return ok();
}

// ---------------------------------------------------------------------------
// Dopasowanie tematów
// ---------------------------------------------------------------------------

bool MqttClient::topicMatches(const char* filter, const char* topic) {
    if (!filter || !topic) return false;

    while (*filter && *topic) {
        if (*filter == '#') {
            // Wieloznacznik wielopoziomowy — pasuje do reszty tematu,
            // ale wyłącznie na końcu filtra.
            return *(filter + 1) == '\0';
        }
        if (*filter == '+') {
            // Wieloznacznik jednopoziomowy — pochłania dokładnie jeden poziom.
            while (*topic && *topic != '/') ++topic;
            ++filter;
            if (*filter == '/' && *topic == '/') {
                ++filter;
                ++topic;
            }
            continue;
        }
        if (*filter != *topic) return false;
        ++filter;
        ++topic;
    }

    if (*filter == '\0' && *topic == '\0') return true;
    // "a/b/#" pasuje także do samego "a/b" (specyfikacja, rozdz. 4.7.1.2).
    if (*topic == '\0' && filter[0] == '/' && filter[1] == '#' && filter[2] == '\0') {
        return true;
    }
    return *topic == '\0' && filter[0] == '#' && filter[1] == '\0';
}

// ---------------------------------------------------------------------------
// Konfiguracja i połączenie
// ---------------------------------------------------------------------------

Status MqttClient::configure(const Config& cfg) {
    if (!cfg.host || !cfg.clientId || cfg.clientId[0] == '\0') return fail(Err::BadArgument);
    if (cfg.willQos > 1) return fail(Err::NotSupported);
    cfg_ = cfg;
    return ok();
}

u16 MqttClient::nextPacketId() {
    // Identyfikator zero jest zarezerwowany przez specyfikację.
    if (++packetId_ == 0) packetId_ = 1;
    return packetId_;
}

Status MqttClient::writeAll(CByteSpan data) {
    const size_t sent = client_.write(data);
    return sent == data.size() ? ok() : fail(Err::IoError);
}

Status MqttClient::sendConnect() {
    const bool hasWill = cfg_.willTopic != nullptr && cfg_.willPayload != nullptr;
    const bool hasUser = cfg_.username != nullptr;
    const bool hasPass = hasUser && !cfg_.password.empty();

    size_t payloadLen = stringFieldSize(cfg_.clientId);
    if (hasWill) payloadLen += stringFieldSize(cfg_.willTopic) + stringFieldSize(cfg_.willPayload);
    if (hasUser) payloadLen += stringFieldSize(cfg_.username);
    if (hasPass) payloadLen += stringFieldSize(cfg_.password.reveal());

    const size_t variableLen = 10;  // nazwa protokołu, poziom, flagi, keepalive
    const u32    remaining   = static_cast<u32>(variableLen + payloadLen);
    if (remaining + 5 > sizeof(buf_)) return fail(Err::OutOfRange);

    size_t pos = 0;
    buf_[pos++] = kConnect;
    pos += encodeRemainingLength(remaining, buf_ + pos);

    pos = writeString(buf_, pos, "MQTT");
    buf_[pos++] = 0x04;  // poziom protokołu 3.1.1

    u8 flags = 0;
    if (cfg_.cleanSession) flags |= 0x02;
    if (hasWill) {
        flags |= 0x04;
        flags |= static_cast<u8>((cfg_.willQos & 0x03) << 3);
        if (cfg_.willRetain) flags |= 0x20;
    }
    if (hasUser) flags |= 0x80;
    if (hasPass) flags |= 0x40;
    buf_[pos++] = flags;

    buf_[pos++] = static_cast<u8>(cfg_.keepAliveSec >> 8);
    buf_[pos++] = static_cast<u8>(cfg_.keepAliveSec & 0xFF);

    // Kolejność pól ładunku jest ściśle określona przez specyfikację.
    pos = writeString(buf_, pos, cfg_.clientId);
    if (hasWill) {
        pos = writeString(buf_, pos, cfg_.willTopic);
        pos = writeString(buf_, pos, cfg_.willPayload);
    }
    if (hasUser) pos = writeString(buf_, pos, cfg_.username);
    if (hasPass) pos = writeString(buf_, pos, cfg_.password.reveal());

    return writeAll(CByteSpan{buf_, pos});
}

Status MqttClient::connect(Millis now) {
    if (state_ != State::Disconnected) return fail(Err::AlreadyExists);
    if (!cfg_.host) return fail(Err::NotInitialized);

    HYDRA_CHECK(client_.connect(cfg_.host, cfg_.port, cfg_.connectTimeoutMs));

    if (auto r = sendConnect(); !r) {
        client_.stop();
        return r;
    }

    state_          = State::Connecting;
    connectStartMs_ = now;
    lastActivityMs_ = now;
    pingPending_    = false;
    return ok();
}

void MqttClient::disconnect() {
    if (state_ == State::Connected) {
        const u8 packet[2] = {kDisconnect, 0x00};
        writeAll(CByteSpan{packet, sizeof(packet)});
    }
    client_.stop();
    if (state_ != State::Disconnected) {
        ++stats_.disconnects;
        EventBus::publish(MqttStateChanged{false, Err::None, subscriptionCount()});
    }
    state_ = State::Disconnected;
}

void MqttClient::dropConnection(Err reason, Millis now) {
    HYDRA_UNUSED(now);
    HYDRA_LOGW("połączenie z brokerem przerwane: %s", toString(reason));
    client_.stop();
    if (state_ != State::Disconnected) {
        ++stats_.disconnects;
        EventBus::publish(MqttStateChanged{false, reason, subscriptionCount()});
    }
    state_ = State::Disconnected;

    // Subskrypcje przeżywają zerwanie — po ponownym połączeniu zostaną
    // odtworzone bez udziału aplikacji.
    for (auto& s : subs_) s.acked = false;
    for (auto& f : inflight_) f.busy = false;
}

// ---------------------------------------------------------------------------
// Publikacja
// ---------------------------------------------------------------------------

MqttClient::InFlight* MqttClient::takeInFlightSlot() {
    for (auto& f : inflight_) {
        if (!f.busy) return &f;
    }
    return nullptr;
}

Status MqttClient::sendPublish(const char* topic, CByteSpan payload, u8 qos, bool retain,
                               u16 packetId, bool dup) {
    const size_t topicLen = strlen(topic);
    const u32 remaining = static_cast<u32>(2 + topicLen + (qos > 0 ? 2 : 0) + payload.size());
    if (remaining + 5 > sizeof(buf_)) {
        ++stats_.dropped;
        return fail(Err::OutOfRange);
    }

    size_t pos = 0;
    buf_[pos] = static_cast<u8>(kPublish | (qos << 1));
    if (retain) buf_[pos] |= 0x01;
    if (dup) buf_[pos] |= 0x08;
    ++pos;

    pos += encodeRemainingLength(remaining, buf_ + pos);
    pos = writeString(buf_, pos, topic);

    if (qos > 0) {
        buf_[pos++] = static_cast<u8>(packetId >> 8);
        buf_[pos++] = static_cast<u8>(packetId & 0xFF);
    }
    if (!payload.empty()) {
        memcpy(buf_ + pos, payload.data(), payload.size());
        pos += payload.size();
    }

    return writeAll(CByteSpan{buf_, pos});
}

Status MqttClient::publish(const char* topic, CByteSpan payload, u8 qos, bool retain) {
    if (state_ != State::Connected) return fail(Err::NotInitialized);
    if (!topic || topic[0] == '\0') return fail(Err::BadArgument);
    if (qos > 1) return fail(Err::NotSupported);

    u16       packetId = 0;
    InFlight* slot     = nullptr;

    if (qos == 1) {
        if (payload.size() > HYDRA_MQTT_INFLIGHT_PAYLOAD) return fail(Err::OutOfRange);
        slot = takeInFlightSlot();
        // Brak wolnego slotu oznacza, że broker nie nadąża z potwierdzeniami.
        // Odrzucenie jest tu uczciwsze niż kolejkowanie w nieskończoność.
        if (!slot) return fail(Err::Busy);
        packetId = nextPacketId();
    }

    HYDRA_CHECK(sendPublish(topic, payload, qos, retain, packetId, false));
    ++stats_.published;

    if (slot) {
        slot->packetId = packetId;
        strncpy(slot->topic, topic, kTopicMax - 1);
        slot->topic[kTopicMax - 1] = '\0';
        slot->length = static_cast<u16>(payload.size());
        if (slot->length) memcpy(slot->payload, payload.data(), slot->length);
        slot->sentAt  = rtos::nowMs();
        slot->retries = 0;
        slot->retain  = retain;
        slot->busy    = true;
    }
    return ok();
}

Status MqttClient::publish(const char* topic, const char* payload, u8 qos, bool retain) {
    const size_t len = payload ? strlen(payload) : 0;
    return publish(topic, CByteSpan{reinterpret_cast<const u8*>(payload), len}, qos, retain);
}

void MqttClient::retransmit(Millis now) {
    for (auto& f : inflight_) {
        if (!f.busy) continue;
        if (static_cast<i32>(now - (f.sentAt + cfg_.ackTimeoutMs)) < 0) continue;

        if (f.retries >= cfg_.maxRetransmits) {
            HYDRA_LOGW("porzucono publikację '%s' po %u próbach", f.topic,
                       static_cast<unsigned>(f.retries));
            f.busy = false;
            ++stats_.dropped;
            continue;
        }

        ++f.retries;
        ++stats_.retransmits;
        // Flaga DUP informuje brokera, że to powtórzenie, a nie nowa wiadomość.
        sendPublish(f.topic, CByteSpan{f.payload, f.length}, 1, f.retain, f.packetId, true);
        f.sentAt = now;
    }
}

// ---------------------------------------------------------------------------
// Subskrypcje
// ---------------------------------------------------------------------------

Status MqttClient::sendSubscribe(Subscription& sub) {
    const u32 remaining = static_cast<u32>(2 + stringFieldSize(sub.filter) + 1);
    if (remaining + 5 > sizeof(buf_)) return fail(Err::OutOfRange);

    const u16 packetId = nextPacketId();

    size_t pos = 0;
    buf_[pos++] = kSubscribe;
    pos += encodeRemainingLength(remaining, buf_ + pos);
    buf_[pos++] = static_cast<u8>(packetId >> 8);
    buf_[pos++] = static_cast<u8>(packetId & 0xFF);
    pos = writeString(buf_, pos, sub.filter);
    buf_[pos++] = sub.qos;

    return writeAll(CByteSpan{buf_, pos});
}

Status MqttClient::subscribe(const char* filter, u8 qos, MessageHandler handler) {
    if (!filter || filter[0] == '\0' || !handler) return fail(Err::BadArgument);
    if (qos > 1) return fail(Err::NotSupported);
    if (strlen(filter) >= kTopicMax) return fail(Err::OutOfRange);

    Subscription* slot = nullptr;
    for (auto& s : subs_) {
        if (s.active && strcmp(s.filter, filter) == 0) {
            slot = &s;  // ponowna subskrypcja tego samego filtra nadpisuje handler
            break;
        }
        if (!s.active && !slot) slot = &s;
    }
    if (!slot) return fail(Err::OutOfMemory);

    strncpy(slot->filter, filter, kTopicMax - 1);
    slot->filter[kTopicMax - 1] = '\0';
    slot->handler = handler;
    slot->qos     = qos;
    slot->active  = true;
    slot->acked   = false;

    // Subskrypcja bez połączenia jest zapamiętywana i wyśle się przy CONNACK.
    if (state_ != State::Connected) return ok();
    return sendSubscribe(*slot);
}

Status MqttClient::unsubscribe(const char* filter) {
    if (!filter) return fail(Err::BadArgument);

    for (auto& s : subs_) {
        if (!s.active || strcmp(s.filter, filter) != 0) continue;

        if (state_ == State::Connected) {
            const u32 remaining = static_cast<u32>(2 + stringFieldSize(s.filter));
            size_t    pos       = 0;
            buf_[pos++] = kUnsubscribe;
            pos += encodeRemainingLength(remaining, buf_ + pos);
            const u16 packetId = nextPacketId();
            buf_[pos++] = static_cast<u8>(packetId >> 8);
            buf_[pos++] = static_cast<u8>(packetId & 0xFF);
            pos = writeString(buf_, pos, s.filter);
            writeAll(CByteSpan{buf_, pos});
        }

        s.active  = false;
        s.acked   = false;
        s.handler.reset();
        return ok();
    }
    return fail(Err::NotFound);
}

u16 MqttClient::subscriptionCount() const {
    u16 n = 0;
    for (const auto& s : subs_) {
        if (s.active) ++n;
    }
    return n;
}

void MqttClient::resubscribeAll() {
    for (auto& s : subs_) {
        if (!s.active || s.acked) continue;
        if (auto r = sendSubscribe(s); !r) {
            HYDRA_LOGW("nie udało się odtworzyć subskrypcji '%s'", s.filter);
        }
    }
}

// ---------------------------------------------------------------------------
// Odbiór
// ---------------------------------------------------------------------------

void MqttClient::handleConnAck(u32 remaining, Millis now) {
    if (remaining != 2) return;

    u8 body[2] = {};
    if (!client_.readExactly(ByteSpan{body, 2}, 1000)) return;

    connectCode_ = static_cast<MqttConnectCode>(body[1]);
    if (connectCode_ != MqttConnectCode::Accepted) {
        HYDRA_LOGE("broker odrzucił połączenie, kod %u", static_cast<unsigned>(body[1]));
        // Rozróżnienie ma znaczenie dla warstwy wyżej: niedostępny broker
        // to powód do ponowienia, odrzucone poświadczenia — nie.
        Err reason = Err::Protocol;
        switch (connectCode_) {
            case MqttConnectCode::ServerUnavailable:  reason = Err::Busy; break;
            case MqttConnectCode::ClientIdRejected:   reason = Err::BadArgument; break;
            case MqttConnectCode::BadProtocolVersion: reason = Err::NotSupported; break;
            case MqttConnectCode::BadCredentials:
            case MqttConnectCode::NotAuthorized:      reason = Err::Protocol; break;
            case MqttConnectCode::Accepted:           break;
        }
        dropConnection(reason, now);
        return;
    }

    state_ = State::Connected;
    ++stats_.connects;
    HYDRA_LOGI("połączono z brokerem %s:%u", cfg_.host, static_cast<unsigned>(cfg_.port));

    resubscribeAll();
    EventBus::publish(MqttStateChanged{true, Err::None, subscriptionCount()});
}

void MqttClient::handlePubAck(u32 remaining) {
    if (remaining != 2) return;

    u8 body[2] = {};
    if (!client_.readExactly(ByteSpan{body, 2}, 1000)) return;
    const u16 packetId = static_cast<u16>(body[0] << 8 | body[1]);

    for (auto& f : inflight_) {
        if (f.busy && f.packetId == packetId) {
            f.busy = false;
            return;
        }
    }
}

void MqttClient::handleSubAck(u32 remaining) {
    if (remaining > sizeof(buf_)) return;

    if (!client_.readExactly(ByteSpan{buf_, remaining}, 1000)) return;
    // Broker potwierdza subskrypcje w kolejności wysłania; przy jednym filtrze
    // na pakiet wystarczy oznaczyć wszystkie oczekujące jako potwierdzone.
    for (auto& s : subs_) {
        if (s.active) s.acked = true;
    }
}

void MqttClient::handlePublish(u8 header, u32 remaining, Millis now) {
    HYDRA_UNUSED(now);

    if (remaining > sizeof(buf_)) {
        // Pakiet nie mieści się w buforze — trzeba go wyczytać do końca,
        // inaczej strumień rozjedzie się na kolejnym nagłówku.
        ++stats_.dropped;
        u32 left = remaining;
        u8  sink[32];
        while (left > 0) {
            const size_t chunk = left < sizeof(sink) ? left : sizeof(sink);
            if (!client_.readExactly(ByteSpan{sink, chunk}, 1000)) return;
            left -= static_cast<u32>(chunk);
        }
        return;
    }

    if (!client_.readExactly(ByteSpan{buf_, remaining}, 1000)) return;

    const u8  qos      = static_cast<u8>((header >> 1) & 0x03);
    const u16 topicLen = static_cast<u16>(buf_[0] << 8 | buf_[1]);
    if (2u + topicLen > remaining || topicLen >= kTopicMax) {
        ++stats_.dropped;
        return;
    }

    char topic[kTopicMax];
    memcpy(topic, buf_ + 2, topicLen);
    topic[topicLen] = '\0';

    size_t pos      = 2 + topicLen;
    u16    packetId = 0;
    if (qos > 0) {
        if (pos + 2 > remaining) return;
        packetId = static_cast<u16>(buf_[pos] << 8 | buf_[pos + 1]);
        pos += 2;
    }

    ++stats_.received;
    dispatch(topic, CByteSpan{buf_ + pos, remaining - pos});

    // Potwierdzenie wysyłamy po obsłudze wiadomości: gdyby handler się wywrócił,
    // broker powtórzy ją po ponownym połączeniu.
    if (qos == 1) {
        const u8 ack[4] = {kPubAck, 0x02, static_cast<u8>(packetId >> 8),
                           static_cast<u8>(packetId & 0xFF)};
        writeAll(CByteSpan{ack, sizeof(ack)});
    }
}

void MqttClient::dispatch(const char* topic, CByteSpan payload) {
    for (auto& s : subs_) {
        if (!s.active || !s.handler) continue;
        if (topicMatches(s.filter, topic)) s.handler(topic, payload);
    }
}

void MqttClient::handlePacket(u8 header, u32 remaining, Millis now) {
    switch (header & 0xF0) {
        case kConnAck:  handleConnAck(remaining, now); break;
        case kPublish:  handlePublish(header, remaining, now); break;
        case kPubAck:   handlePubAck(remaining); break;
        case kSubAck:   handleSubAck(remaining); break;
        case kPingResp: pingPending_ = false; break;
        default:
            // Nieznany pakiet trzeba wyczytać, żeby nie rozjechać strumienia.
            if (remaining > 0 && remaining <= sizeof(buf_)) {
                client_.readExactly(ByteSpan{buf_, remaining}, 1000);
            }
            break;
    }
}

Status MqttClient::sendPingReq() {
    const u8 packet[2] = {kPingReq, 0x00};
    return writeAll(CByteSpan{packet, sizeof(packet)});
}

// ---------------------------------------------------------------------------
// Pętla
// ---------------------------------------------------------------------------

void MqttClient::loop(Millis now) {
    if (state_ == State::Disconnected) return;

    if (!client_.connected()) {
        dropConnection(Err::IoError, now);
        return;
    }

    // Odbiór wszystkiego, co czeka. Pętla ograniczona liczbą pakietów, żeby
    // zalew wiadomości nie zagłodził pozostałych zadań taska sieciowego.
    for (u8 guard = 0; guard < 8 && client_.available() > 0; ++guard) {
        u8 header = 0;
        if (client_.read(ByteSpan{&header, 1}) != 1) break;

        // Remaining length: po siedem bitów na bajt, najwyżej cztery bajty.
        u32 remaining = 0;
        u32 multiplier = 1;
        for (u8 i = 0; i < 4; ++i) {
            u8 digit = 0;
            if (!client_.readExactly(ByteSpan{&digit, 1}, 1000)) {
                dropConnection(Err::Timeout, now);
                return;
            }
            remaining += (digit & 0x7F) * multiplier;
            if ((digit & 0x80) == 0) break;
            multiplier *= 128;
        }

        lastActivityMs_ = now;
        handlePacket(header, remaining, now);
        if (state_ == State::Disconnected) return;
    }

    if (state_ == State::Connecting) {
        if (now - connectStartMs_ >= cfg_.connectTimeoutMs) {
            HYDRA_LOGW("broker nie odpowiedział na CONNECT");
            dropConnection(Err::Timeout, now);
        }
        return;
    }

    retransmit(now);

    // Podtrzymanie sesji: PINGREQ w połowie okresu keepalive. Brak odpowiedzi
    // przez pełny okres oznacza łącze, które „działa", ale nic nie przepuszcza —
    // najgorszy przypadek, bo TCP sam go nie wykryje przez wiele minut.
    const u32 keepAliveMs = static_cast<u32>(cfg_.keepAliveSec) * 1000u;
    if (keepAliveMs == 0) return;

    if (pingPending_ && now - lastPingMs_ >= keepAliveMs) {
        dropConnection(Err::Timeout, now);
        return;
    }
    if (!pingPending_ && now - lastActivityMs_ >= keepAliveMs / 2) {
        if (sendPingReq()) {
            pingPending_ = true;
            lastPingMs_  = now;
        } else {
            dropConnection(Err::IoError, now);
        }
    }
}

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
