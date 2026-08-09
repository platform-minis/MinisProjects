/** Hydra — adresy i tematy protokołu MyCastle. */

#include "hydra/minis/MinisTypes.hpp"

#if HYDRA_ENABLE_MINIS

#include <stdio.h>
#include <string.h>

namespace hydra {
namespace minis {
namespace {

void copyBounded(char* dst, size_t capacity, const char* src) {
    if (capacity == 0) return;
    if (src == nullptr) { dst[0] = 0; return; }
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < capacity) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

/** Kopiuje segment tematu do bufora; `false`, gdy się nie mieści. */
bool copySegment(char* dst, size_t capacity, const char* start, size_t length) {
    if (length + 1 > capacity) return false;
    memcpy(dst, start, length);
    dst[length] = 0;
    return true;
}

}  // namespace

bool DeviceAddr::equals(const DeviceAddr& other) const {
    return strncmp(user, other.user, kUserMax) == 0 &&
           strncmp(device, other.device, kDeviceMax) == 0;
}

void DeviceAddr::set(const char* userName, const char* deviceName) {
    copyBounded(user, kUserMax, userName);
    copyBounded(device, kDeviceMax, deviceName);
}

// ---------------------------------------------------------------------------
// Tematy
// ---------------------------------------------------------------------------

bool buildTopic(char* out, size_t capacity, const DeviceAddr& addr,
                MsgKind kind, const char* extType) {
    if (out == nullptr || capacity == 0) return false;
    out[0] = 0;
    if (!addr.valid()) return false;

    int written;
    if (kind == MsgKind::ExtRequest || kind == MsgKind::ExtResponse) {
        if (extType == nullptr || extType[0] == '\0') return false;
        written = snprintf(out, capacity, "minis/%s/%s/ext/%s/%s",
                           addr.user, addr.device, extType,
                           kind == MsgKind::ExtRequest ? "req" : "res");
    } else {
        if (kind == MsgKind::Unknown) return false;
        written = snprintf(out, capacity, "minis/%s/%s/%s",
                           addr.user, addr.device, toString(kind));
    }

    // Obcięty temat trafiłby do innego urządzenia albo do nieistniejącego —
    // gorsze niż niewysłanie wiadomości, bo cichsze.
    if (written < 0 || static_cast<size_t>(written) >= capacity) {
        out[0] = 0;
        return false;
    }
    return true;
}

bool parseTopic(const char* topic, DeviceAddr& addr, MsgKind& kind,
                char* extType, size_t extCapacity) {
    addr = DeviceAddr{};
    kind = MsgKind::Unknown;
    if (extType != nullptr && extCapacity > 0) extType[0] = 0;
    if (topic == nullptr) return false;

    static constexpr char kPrefix[] = "minis/";
    static constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (strncmp(topic, kPrefix, kPrefixLen) != 0) return false;

    const char* userStart = topic + kPrefixLen;
    const char* slash1 = strchr(userStart, '/');
    if (slash1 == nullptr) return false;

    const char* deviceStart = slash1 + 1;
    const char* slash2 = strchr(deviceStart, '/');
    if (slash2 == nullptr) return false;

    if (!copySegment(addr.user, kUserMax, userStart,
                     static_cast<size_t>(slash1 - userStart))) return false;
    if (!copySegment(addr.device, kDeviceMax, deviceStart,
                     static_cast<size_t>(slash2 - deviceStart))) return false;

    const char* rest = slash2 + 1;

    // Rozszerzenia najpierw: ich sufiks jest trzyczłonowy, więc porównanie
    // z płaskimi nazwami i tak by go nie objęło.
    static constexpr char kExt[] = "ext/";
    if (strncmp(rest, kExt, sizeof(kExt) - 1) == 0) {
        const char* typeStart = rest + sizeof(kExt) - 1;
        const char* slash3 = strchr(typeStart, '/');
        if (slash3 == nullptr) return false;
        if (extType != nullptr &&
            !copySegment(extType, extCapacity, typeStart,
                         static_cast<size_t>(slash3 - typeStart))) return false;

        const char* verb = slash3 + 1;
        if (strcmp(verb, "req") == 0)      kind = MsgKind::ExtRequest;
        else if (strcmp(verb, "res") == 0) kind = MsgKind::ExtResponse;
        else return false;
        return true;
    }

    static const struct { const char* suffix; MsgKind kind; } kMap[] = {
        {"telemetry",        MsgKind::Telemetry},
        {"hello",            MsgKind::Hello},
        {"heartbeat",        MsgKind::Heartbeat},
        {"register-request", MsgKind::RegisterRequest},
        {"command/ack",      MsgKind::CommandAck},
        {"command",          MsgKind::Command},
        {"twin/reported",    MsgKind::TwinReported},
        {"twin/desired",     MsgKind::TwinDesired},
    };
    // „command/ack" stoi przed „command" świadomie: porównujemy całość, więc
    // kolejność nic nie zmienia, ale przy zmianie na porównanie prefiksu
    // odwrotna kolejność cicho zamieniłaby potwierdzenie w komendę.
    for (const auto& entry : kMap) {
        if (strcmp(rest, entry.suffix) == 0) { kind = entry.kind; return true; }
    }
    return false;
}

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
