/** Hydra — implementacja zapisu okoliczności awarii (rozdz. 13). */

#include "hydra/diag/CrashRecorder.hpp"

#include <string.h>

#include "hydra/core/Log.hpp"
#include "hydra/hal/Hal.hpp"

HYDRA_LOG_MODULE("diag.crash")

namespace hydra {
namespace diag {
namespace {

constexpr const char* kNamespace = "hydra-diag";
constexpr const char* kKeyRecord = "crash";
constexpr const char* kKeyBoots  = "boots";
constexpr const char* kKeyLog    = "crashlog";

}  // namespace

Status CrashRecorder::begin() {
    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kNamespace, false));

    // Licznik rozruchów rośnie zawsze — sama jego wartość bywa wskazówką,
    // gdy urządzenie wpada w pętlę restartów, a każdy pojedynczy zapis
    // wygląda niewinnie.
    bootCount_ = storage.getU32(kKeyBoots, 0).value_or(0) + 1;
    storage.setU32(kKeyBoots, bootCount_);

    CrashRecord record;
    auto read = storage.getBlob(kKeyRecord,
                                ByteSpan{reinterpret_cast<u8*>(&record), sizeof(record)});
    if (read && *read == sizeof(record) && record.valid()) {
        previous_    = record;
        hasPrevious_ = true;
    }

    // Przyczyna resetu pochodzi z rejestrów procesora i jest dostępna nawet
    // wtedy, gdy oprogramowanie nie zdążyło niczego zapisać — na przykład
    // przy zadziałaniu watchdoga albo zaniku napięcia.
    const ResetReason hardware = hal::Hal::resetReason();
    if (!hasPrevious_ && hardware != ResetReason::PowerOn &&
        hardware != ResetReason::Unknown) {
        previous_        = CrashRecord{};
        previous_.reason = hardware;
        hasPrevious_     = true;
    }

    storage.commit();
    return ok();
}

Status CrashRecorder::record(ResetReason reason, u16 sourceId, u16 code,
                             const char* detail) {
    CrashRecord entry;
    entry.reason    = reason;
    entry.uptimeMs  = rtos::nowMs();
    entry.sourceId  = sourceId;
    entry.code      = code;
    entry.bootCount = bootCount_;
    if (detail) {
        strncpy(entry.detail, detail, kCrashDetailMax - 1);
        entry.detail[kCrashDetailMax - 1] = '\0';
    }

    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kNamespace, false));
    HYDRA_CHECK(storage.setBlob(
        kKeyRecord, CByteSpan{reinterpret_cast<const u8*>(&entry), sizeof(entry)}));
    return storage.commit();
}

Status CrashRecorder::saveLogTail() {
    char buffer[HYDRA_CRASH_LOG_BYTES];
    const size_t written = Log::dump(buffer, sizeof(buffer));
    if (written == 0) return ok();

    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kNamespace, false));

    // Zapisujemy ogon, nie początek: interesuje nas to, co działo się tuż
    // przed awarią, a nie w chwili startu.
    const size_t limit = written < sizeof(buffer) ? written : sizeof(buffer) - 1;
    const char*  tail  = buffer;
    size_t       length = limit;
    if (limit > 200) {
        tail   = buffer + (limit - 200);
        length = 200;
    }

    char trimmed[201];
    memcpy(trimmed, tail, length);
    trimmed[length] = '\0';

    HYDRA_CHECK(storage.setString(kKeyLog, trimmed));
    return storage.commit();
}

Result<size_t> CrashRecorder::loadLogTail(char* out, size_t capacity) {
    if (!out || capacity == 0) return unexpected(Err::BadArgument);

    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kNamespace, true));
    return storage.getString(kKeyLog, out, capacity);
}

Status CrashRecorder::publishAndClear() {
    if (!hasPrevious_) return ok();

    EventBus::publish(CrashReported{previous_.reason, previous_.uptimeMs,
                                    previous_.sourceId, previous_.code});
    HYDRA_LOGW("poprzedni rozruch zakończony: %s po %lus%s%s", toString(previous_.reason),
               static_cast<unsigned long>(previous_.uptimeMs / 1000),
               previous_.detail[0] ? ", " : "", previous_.detail);
    return clear();
}

Status CrashRecorder::clear() {
    hasPrevious_ = false;
    previous_    = CrashRecord{};

    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kNamespace, false));
    storage.erase(kKeyRecord);
    return storage.commit();
}

}  // namespace diag
}  // namespace hydra
