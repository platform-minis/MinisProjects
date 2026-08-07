/**
 * Hydra — implementacja logowania (rozdz. 13).
 *
 * Bufor pierścieniowy trzyma ostatnie linie w RAM niezależnie od trybu pracy —
 * to on jest zrzucany po awarii. Tryb Deferred dokłada do tego kursor wysyłki:
 * linia trafia do bufora natychmiast (tanio), a kosztowny zapis na UART robi
 * task core.house. Dzięki temu logowanie z pętli sterowania nie wprowadza
 * jitteru zależnego od prędkości portu szeregowego.
 *
 * Format rekordu w buforze:  [poziom:u8][długość:u16][bajty linii]
 */

#include "hydra/core/Log.hpp"

#include <stdio.h>
#include <string.h>

#include "hydra/core/Rtos.hpp"

namespace hydra {
namespace {

constexpr u8  kMaxSinks       = 4;
constexpr u8  kMaxModuleRules = 8;
constexpr u8  kModuleNameMax  = 15;
constexpr u16 kHeaderSize     = 3;  // poziom + długość

struct ModuleRule {
    char     name[kModuleNameMax + 1];
    LogLevel level;
    bool     used;
};

struct Ring {
    u8  buf[HYDRA_LOG_RING_SIZE];
    u16 head      = 0;  ///< pozycja zapisu
    u16 tail      = 0;  ///< najstarszy rekord (początek zrzutu)
    u16 flush     = 0;  ///< najstarszy rekord niewysłany do sinków
    u16 used      = 0;  ///< zajęte bajty
    u16 records   = 0;
    u16 unflushed = 0;
};

struct State {
    rtos::Mutex mtx;
    Ring        ring;
    ILogSink*   sinks[kMaxSinks] = {};
    u8          sinkCount        = 0;
    ModuleRule  rules[kMaxModuleRules] = {};
    LogLevel    level            = LogLevel::Info;
    Log::Mode   mode             = Log::Mode::Sync;
    Log::Stats  stats;
};

State& st() {
    static State s;
    return s;
}

// --- operacje na buforze pierścieniowym (wywoływane pod blokadą) -----------

void ringWriteByte(Ring& r, u8 b) {
    r.buf[r.head] = b;
    r.head        = static_cast<u16>((r.head + 1) % HYDRA_LOG_RING_SIZE);
    ++r.used;
}

u8 ringReadByteAt(const Ring& r, u16& pos) {
    const u8 b = r.buf[pos];
    pos        = static_cast<u16>((pos + 1) % HYDRA_LOG_RING_SIZE);
    return b;
}

/** Zdejmuje najstarszy rekord. Zwraca true, jeśli był jeszcze niewysłany. */
bool ringPopOldest(Ring& r) {
    if (r.records == 0) return false;

    u16       pos = r.tail;
    const u8  lvl = ringReadByteAt(r, pos);
    const u16 lo  = ringReadByteAt(r, pos);
    const u16 hi  = ringReadByteAt(r, pos);
    HYDRA_UNUSED(lvl);
    const u16 len   = static_cast<u16>(lo | (hi << 8));
    const u16 total = static_cast<u16>(kHeaderSize + len);

    const bool wasUnflushed = (r.tail == r.flush) && (r.unflushed > 0);

    r.tail = static_cast<u16>((r.tail + total) % HYDRA_LOG_RING_SIZE);
    r.used = static_cast<u16>(r.used - total);
    --r.records;

    if (wasUnflushed) {
        r.flush = r.tail;
        --r.unflushed;
    }
    return wasUnflushed;
}

void ringPush(Ring& r, LogLevel lvl, const char* line, u16 len, u32& droppedOut) {
    if (len > HYDRA_LOG_LINE_MAX) len = HYDRA_LOG_LINE_MAX;
    const u16 total = static_cast<u16>(kHeaderSize + len);
    if (total > HYDRA_LOG_RING_SIZE) return;  // linia większa niż cały bufor

    while (HYDRA_LOG_RING_SIZE - r.used < total) {
        if (ringPopOldest(r)) ++droppedOut;
    }

    ringWriteByte(r, static_cast<u8>(lvl));
    ringWriteByte(r, static_cast<u8>(len & 0xFF));
    ringWriteByte(r, static_cast<u8>(len >> 8));
    for (u16 i = 0; i < len; ++i) ringWriteByte(r, static_cast<u8>(line[i]));
    ++r.records;
    ++r.unflushed;
}

/** Czyta rekord spod pos do bufora wołającego; przesuwa pos na kolejny rekord. */
u16 ringReadRecord(const Ring& r, u16& pos, LogLevel& lvl, char* out, u16 cap) {
    lvl          = static_cast<LogLevel>(ringReadByteAt(r, pos));
    const u16 lo = ringReadByteAt(r, pos);
    const u16 hi = ringReadByteAt(r, pos);
    u16       len = static_cast<u16>(lo | (hi << 8));

    for (u16 i = 0; i < len; ++i) {
        const u8 b = ringReadByteAt(r, pos);
        if (i < cap) out[i] = static_cast<char>(b);
    }
    if (len > cap) len = cap;
    return len;
}

/** Migawka listy sinków — zapis odbywa się poza blokadą (patrz vwrite/drain). */
struct SinkList {
    ILogSink* items[kMaxSinks] = {};
    u8        count            = 0;
};

SinkList snapshotSinks(const State& s) {
    SinkList out;
    for (u8 i = 0; i < s.sinkCount; ++i) {
        if (s.sinks[i]) out.items[out.count++] = s.sinks[i];
    }
    return out;
}

void emitToSinks(const SinkList& sinks, LogLevel lvl, const char* line, size_t len) {
    for (u8 i = 0; i < sinks.count; ++i) sinks.items[i]->write(lvl, line, len);
}

}  // namespace

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

void Log::init(LogLevel level, Mode mode) {
    State& s = st();
    rtos::LockGuard g(s.mtx);
    s.level = level;
    s.mode  = mode;
}

Status Log::addSink(ILogSink& sink) {
    State& s = st();
    rtos::LockGuard g(s.mtx);
    if (s.sinkCount >= kMaxSinks) return fail(Err::OutOfMemory);
    for (u8 i = 0; i < s.sinkCount; ++i) {
        if (s.sinks[i] == &sink) return ok();
    }
    s.sinks[s.sinkCount++] = &sink;
    return ok();
}

void Log::clearSinks() {
    State& s = st();
    rtos::LockGuard g(s.mtx);
    for (auto& p : s.sinks) p = nullptr;
    s.sinkCount = 0;
}

void Log::setLevel(LogLevel l) {
    State& s = st();
    rtos::LockGuard g(s.mtx);
    s.level = l;
}

LogLevel Log::level() {
    State& s = st();
    rtos::LockGuard g(s.mtx);
    return s.level;
}

void Log::setMode(Mode m) {
    State& s = st();
    rtos::LockGuard g(s.mtx);
    s.mode = m;
}

Status Log::setModuleLevel(const char* module, LogLevel l) {
    if (!module) return fail(Err::BadArgument);
    State& s = st();
    rtos::LockGuard g(s.mtx);

    for (auto& r : s.rules) {
        if (r.used && strncmp(r.name, module, kModuleNameMax) == 0) {
            r.level = l;
            return ok();
        }
    }
    for (auto& r : s.rules) {
        if (r.used) continue;
        strncpy(r.name, module, kModuleNameMax);
        r.name[kModuleNameMax] = '\0';
        r.level                = l;
        r.used                 = true;
        return ok();
    }
    return fail(Err::OutOfMemory);
}

LogLevel Log::moduleLevel(const char* module) {
    State& s = st();
    rtos::LockGuard g(s.mtx);
    if (module) {
        for (const auto& r : s.rules) {
            if (r.used && strncmp(r.name, module, kModuleNameMax) == 0) return r.level;
        }
    }
    return s.level;
}

bool Log::enabled(const char* module, LogLevel l) {
    if (rtos::inIsr()) return false;  // logowanie z ISR jest zabronione (rozdz. 10)
    return static_cast<u8>(l) >= static_cast<u8>(moduleLevel(module));
}

void Log::write(LogLevel l, const char* module, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwrite(l, module, fmt, ap);
    va_end(ap);
}

void Log::vwrite(LogLevel l, const char* module, const char* fmt, va_list ap) {
    State& s = st();

    if (rtos::inIsr()) {
        ++s.stats.isrDropped;
        return;
    }
    if (!enabled(module, l)) {
        ++s.stats.filtered;
        return;
    }

    char line[HYDRA_LOG_LINE_MAX];
    int  n = snprintf(line, sizeof(line), "[%8lu] %s %-8s ",
                      static_cast<unsigned long>(rtos::nowMs()), toString(l),
                      module ? module : "-");
    if (n < 0) return;
    if (n > static_cast<int>(sizeof(line)) - 1) n = static_cast<int>(sizeof(line)) - 1;

    const int m = vsnprintf(line + n, sizeof(line) - static_cast<size_t>(n), fmt, ap);
    size_t    len = (m < 0) ? static_cast<size_t>(n)
                            : static_cast<size_t>(n) + static_cast<size_t>(m);
    if (len > sizeof(line) - 1) len = sizeof(line) - 1;
    line[len] = '\0';

    SinkList sinks;
    {
        rtos::LockGuard g(s.mtx);
        ++s.stats.emitted;
        ringPush(s.ring, l, line, static_cast<u16>(len), s.stats.ringDropped);

        if (s.mode == Mode::Sync) {
            sinks = snapshotSinks(s);
            // Rekord zostaje w buforze na potrzeby zrzutu po awarii,
            // ale jest już policzony jako wysłany.
            s.ring.flush     = s.ring.head;
            s.ring.unflushed = 0;
        }
    }
    // Zapis do sinka poza blokadą — UART bywa wolny, a mutex logu blokowałby
    // wtedy każdy inny task próbujący logować.
    emitToSinks(sinks, l, line, len);
}

u32 Log::drain(u32 maxLines) {
    State& s = st();
    u32    sent = 0;

    while (sent < maxLines) {
        char     line[HYDRA_LOG_LINE_MAX];
        LogLevel lvl = LogLevel::Info;
        u16      len = 0;
        SinkList sinks;
        {
            rtos::LockGuard g(s.mtx);
            if (s.ring.unflushed == 0) break;
            u16 pos      = s.ring.flush;
            len          = ringReadRecord(s.ring, pos, lvl, line, sizeof(line) - 1);
            s.ring.flush = pos;
            --s.ring.unflushed;
            sinks = snapshotSinks(s);
        }
        line[len] = '\0';
        emitToSinks(sinks, lvl, line, len);
        ++sent;
    }
    return sent;
}

size_t Log::dump(char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    State& s = st();
    rtos::LockGuard g(s.mtx);

    size_t written = 0;
    u16    pos     = s.ring.tail;
    for (u16 i = 0; i < s.ring.records; ++i) {
        char     line[HYDRA_LOG_LINE_MAX];
        LogLevel lvl = LogLevel::Info;
        const u16 len = ringReadRecord(s.ring, pos, lvl, line, sizeof(line) - 1);

        if (written + len + 1 >= cap) break;
        memcpy(out + written, line, len);
        written += len;
        out[written++] = '\n';
    }
    out[written < cap ? written : cap - 1] = '\0';
    return written;
}

Log::Stats Log::stats() {
    State& s = st();
    rtos::LockGuard g(s.mtx);
    return s.stats;
}

void Log::reset() {
    State& s = st();
    rtos::LockGuard g(s.mtx);
    s.ring = Ring{};
    for (auto& p : s.sinks) p = nullptr;
    s.sinkCount = 0;
    for (auto& r : s.rules) r = ModuleRule{};
    s.level = LogLevel::Info;
    s.mode  = Log::Mode::Sync;
    s.stats = Log::Stats{};
}

#if HYDRA_PLAT_HOST
namespace {
class StdoutSink : public ILogSink {
public:
    void write(LogLevel, const char* line, size_t len) override {
        fwrite(line, 1, len, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
};
}  // namespace

ILogSink& stdoutSink() {
    static StdoutSink s;
    return s;
}
#endif

}  // namespace hydra
