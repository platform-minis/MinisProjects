/** Testy logowania: filtry, bufor pierścieniowy, tryb deferowany (rozdz. 13). */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("test.log")

using namespace hydra;

namespace {

/** Sink zapamiętujący linie — pozwala sprawdzić, co i kiedy zostało wysłane. */
class CaptureSink : public ILogSink {
public:
    void write(LogLevel level, const char* line, size_t len) override {
        if (count >= kMax) return;
        levels[count] = level;
        const size_t n = len < sizeof(lines[0]) - 1 ? len : sizeof(lines[0]) - 1;
        memcpy(lines[count], line, n);
        lines[count][n] = '\0';
        ++count;
    }

    void clear() { count = 0; }
    bool contains(const char* needle) const {
        for (int i = 0; i < count; ++i) {
            if (strstr(lines[i], needle)) return true;
        }
        return false;
    }

    static constexpr int kMax = 32;
    char     lines[kMax][192] = {};
    LogLevel levels[kMax]     = {};
    int      count            = 0;
};

CaptureSink& sink() {
    static CaptureSink s;
    return s;
}

}  // namespace

TEST("Log: poziom globalny odcina niższe wpisy") {
    Log::reset();
    sink().clear();
    Log::init(LogLevel::Warn, Log::Mode::Sync);
    REQUIRE(Log::addSink(sink()).has_value());

    HYDRA_LOGD("to nie powinno przejść");
    HYDRA_LOGI("to też nie");
    HYDRA_LOGW("ostrzeżenie %d", 7);
    HYDRA_LOGE("błąd");

    CHECK_EQ(sink().count, 2);
    CHECK(sink().contains("ostrzeżenie 7"));
    CHECK(sink().contains("błąd"));
    CHECK(!sink().contains("to nie powinno przejść"));
}

TEST("Log: poziom per moduł nadpisuje globalny") {
    Log::reset();
    sink().clear();
    Log::init(LogLevel::Error, Log::Mode::Sync);
    REQUIRE(Log::addSink(sink()).has_value());
    REQUIRE(Log::setModuleLevel("test.log", LogLevel::Trace).has_value());

    CHECK(Log::enabled("test.log", LogLevel::Trace));
    CHECK(!Log::enabled("inny.moduł", LogLevel::Warn));

    HYDRA_LOGT("szczegół");
    CHECK(sink().contains("szczegół"));
}

TEST("Log: linia zawiera znacznik czasu, poziom i moduł") {
    Log::reset();
    sink().clear();
    Log::init(LogLevel::Info, Log::Mode::Sync);
    REQUIRE(Log::addSink(sink()).has_value());

    HYDRA_LOGI("wiadomość");
    REQUIRE(sink().count == 1);
    CHECK(strstr(sink().lines[0], "INF") != nullptr);
    CHECK(strstr(sink().lines[0], "test.log") != nullptr);
    CHECK(strstr(sink().lines[0], "wiadomość") != nullptr);
    CHECK(sink().lines[0][0] == '[');
}

TEST("Log: tryb Deferred wstrzymuje zapis do drain()") {
    Log::reset();
    sink().clear();
    Log::init(LogLevel::Info, Log::Mode::Deferred);
    REQUIRE(Log::addSink(sink()).has_value());

    HYDRA_LOGI("pierwsza");
    HYDRA_LOGI("druga");
    // Kosztowny zapis na UART jeszcze się nie odbył — to robota core.house.
    CHECK_EQ(sink().count, 0);

    CHECK_EQ(static_cast<int>(Log::drain(16)), 2);
    CHECK_EQ(sink().count, 2);
    CHECK(sink().contains("pierwsza"));
    CHECK(sink().contains("druga"));

    // Powtórny drain nie duplikuje linii.
    CHECK_EQ(static_cast<int>(Log::drain(16)), 0);
    CHECK_EQ(sink().count, 2);
}

TEST("Log: drain respektuje limit linii na wywołanie") {
    Log::reset();
    sink().clear();
    Log::init(LogLevel::Info, Log::Mode::Deferred);
    REQUIRE(Log::addSink(sink()).has_value());

    for (int i = 0; i < 5; ++i) HYDRA_LOGI("linia %d", i);

    CHECK_EQ(static_cast<int>(Log::drain(2)), 2);
    CHECK_EQ(sink().count, 2);
    CHECK_EQ(static_cast<int>(Log::drain(10)), 3);
    CHECK_EQ(sink().count, 5);
}

TEST("Log: bufor pierścieniowy zachowuje ostatnie linie do zrzutu po awarii") {
    Log::reset();
    Log::init(LogLevel::Info, Log::Mode::Sync);

    HYDRA_LOGI("alfa");
    HYDRA_LOGI("beta");

    char buf[512];
    const size_t n = Log::dump(buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "alfa") != nullptr);
    CHECK(strstr(buf, "beta") != nullptr);
}

TEST("Log: nadmiar linii nadpisuje najstarsze i jest raportowany") {
    Log::reset();
    Log::init(LogLevel::Info, Log::Mode::Deferred);

    // Każda linia ma ok. 40 bajtów — pewne przepełnienie bufora.
    for (int i = 0; i < 200; ++i) HYDRA_LOGI("wypełniacz %04d abcdefghijklmnop", i);

    const auto s = Log::stats();
    CHECK_EQ(static_cast<int>(s.emitted), 200);
    CHECK(s.ringDropped > 0);

    // Bufor wciąż zawiera najnowsze wpisy.
    char buf[HYDRA_LOG_RING_SIZE + 64];
    Log::dump(buf, sizeof(buf));
    CHECK(strstr(buf, "0199") != nullptr);
    CHECK(strstr(buf, "0000") == nullptr);
}

TEST("Log: statystyki liczą wpisy odfiltrowane") {
    Log::reset();
    Log::init(LogLevel::Error, Log::Mode::Sync);

    // Makro odcina wpis przed formatowaniem, więc statystyka rośnie tylko przy
    // wywołaniu API bezpośrednio — tak liczymy realny koszt logowania.
    Log::write(LogLevel::Debug, "test.log", "odfiltrowane");
    CHECK_EQ(static_cast<int>(Log::stats().filtered), 1);
    CHECK_EQ(static_cast<int>(Log::stats().emitted), 0);
}
