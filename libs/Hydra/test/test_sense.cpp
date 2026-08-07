/** Testy modułu czujników: harmonogram, łańcuch przetwarzania, filtry (rozdz. 8). */

#include "hydra_test.hpp"

#include <atomic>

#include "hydra/core/App.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/sense/Filters.hpp"
#include "hydra/sense/ImuFusion.hpp"
#include "hydra/sense/Pipeline.hpp"
#include "hydra/sense/SensorHub.hpp"

using namespace hydra;
using namespace hydra::sense;

namespace {

/** Czujnik sterowany z testu: zwraca to, co mu podstawimy. */
class FakeSensor : public ISensor {
public:
    explicit FakeSensor(const char* n, u8 chans = 1, PollMode mode = PollMode::Periodic)
        : name_(n), chans_(chans), mode_(mode) {}

    const char* name() const override { return name_; }
    PollMode    pollMode() const override { return mode_; }
    u8          channels() const override { return chans_; }

    Status probe() override { return probeOk ? ok() : fail(Err::NotFound); }
    Status configure(const SensorCfg& cfg) override {
        configured = true;
        lastCfg    = cfg;
        return ok();
    }

    Status read(Sample& out) override {
        ++reads;
        if (nextError != Err::None) return fail(nextError);
        for (u8 i = 0; i < chans_; ++i) out.value[i] = next[i];
        out.n    = chans_;
        out.t_us = stampOverride;
        return ok();
    }

    bool      probeOk       = true;
    bool      configured    = false;
    SensorCfg lastCfg{};
    /** Zapisywane z taska sense.poll, czytane z wątku testu. */
    std::atomic<u32> reads{0};
    Err       nextError     = Err::None;
    float     next[kMaxChannels] = {};
    Micros    stampOverride = 0;

private:
    const char* name_;
    u8          chans_;
    PollMode    mode_;
};

void resetWorld() {
    App::reset();
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    EventBus::reset();
    EventBus::init();
    Log::init(LogLevel::Off, Log::Mode::Sync);
}

}  // namespace

// ---------------------------------------------------------------------------
// Harmonogram
// ---------------------------------------------------------------------------

TEST("gcd: podstawa grupowania okresów") {
    CHECK_EQ(static_cast<int>(gcd(100, 250)), 50);
    CHECK_EQ(static_cast<int>(gcd(1000, 1000)), 1000);
    CHECK_EQ(static_cast<int>(gcd(7, 13)), 1);
    CHECK_EQ(static_cast<int>(gcd(0, 500)), 500);
}

TEST("SensorHub: tyknięcie to GCD okresów, dzielniki wynikają z niego") {
    resetWorld();

    FakeSensor a("fast"), b("slow");
    SensorHub  hub;

    SensorHub::Registration ra;
    ra.sensor.periodMs = 100;
    SensorHub::Registration rb;
    rb.sensor.periodMs = 250;

    REQUIRE(hub.add(a, ra).has_value());
    REQUIRE(hub.add(b, rb).has_value());
    REQUIRE(hub.init().has_value());

    // 100 i 250 → tyknięcie 50 ms, dzielniki 2 i 5. Jeden task zamiast dwóch.
    CHECK_EQ(static_cast<int>(hub.tickMs()), 50);
    CHECK_EQ(static_cast<int>(hub.dividerOf(0)), 2);
    CHECK_EQ(static_cast<int>(hub.dividerOf(1)), 5);
}

TEST("SensorHub: sam czujnik data-ready nie narzuca okresu tyknięcia") {
    resetWorld();

    FakeSensor irq("irq", 1, PollMode::DataReadyIrq);
    SensorHub  hub;

    SensorHub::Registration r;
    r.sensor.irqPin = 4;
    REQUIRE(hub.add(irq, r).has_value());
    REQUIRE(hub.init().has_value());

    CHECK_EQ(static_cast<int>(hub.tickMs()), 100);  // wartość domyślna
}

TEST("SensorHub: nieobecny czujnik nie blokuje startu urządzenia") {
    resetWorld();

    FakeSensor present("ok"), missing("gone");
    missing.probeOk = false;

    SensorHub hub;
    REQUIRE(hub.add(present, SensorHub::Registration{}).has_value());
    REQUIRE(hub.add(missing, SensorHub::Registration{}).has_value());

    // init przechodzi mimo brakującego czujnika — robot ma pojechać dalej.
    REQUIRE(hub.init().has_value());
    CHECK(hub.available(0));
    CHECK(!hub.available(1));
    CHECK(present.configured);
    CHECK(!missing.configured);

    // Odpytanie niedostępnego czujnika jest jawnym błędem, nie cichym pominięciem.
    CHECK(hub.pollOnce(1).error() == Err::NotInitialized);
}

TEST("SensorHub: rejestracja odrzuca błędne parametry") {
    resetWorld();

    FakeSensor s("x");
    SensorHub  hub;

    SensorHub::Registration zeroPeriod;
    zeroPeriod.sensor.periodMs = 0;
    CHECK(hub.add(s, zeroPeriod).error() == Err::BadArgument);

    FakeSensor irq("irq", 1, PollMode::DataReadyIrq);
    CHECK(hub.add(irq, SensorHub::Registration{}).error() == Err::BadArgument);

    // Po starcie modułu rejestr jest zamknięty — brak alokacji po begin().
    REQUIRE(hub.init().has_value());
    CHECK(hub.add(s, SensorHub::Registration{}).error() == Err::AlreadyExists);
}

// ---------------------------------------------------------------------------
// Łańcuch przetwarzania
// ---------------------------------------------------------------------------

TEST("SensorHub: próbka trafia na magistralę ze znacznikiem czasu") {
    resetWorld();

    FakeSensor s("temp");
    s.next[0] = 21.5f;

    SensorHub hub;
    REQUIRE(hub.add(s, SensorHub::Registration{}).has_value());
    REQUIRE(hub.init().has_value());

    Sample got{};
    int    count = 0;
    auto   sub   = EventBus::subscribe<Sample>([&](const Sample& e) {
        got = e;
        ++count;
    });
    REQUIRE(sub.has_value());

    const Micros before = hal::Hal::time().sampleStamp();
    REQUIRE(hub.pollOnce(0).has_value());
    const Micros after = hal::Hal::time().sampleStamp();

    CHECK_EQ(count, 1);
    CHECK_EQ(static_cast<int>(got.n), 1);
    CHECK(got.value[0] > 21.4f && got.value[0] < 21.6f);
    CHECK_EQ(got.topic, nameId("temp"));
    CHECK(got.q == Quality::Good);

    // Znacznik pochodzi z chwili pomiaru, mieści się między dwoma odczytami zegara.
    CHECK(got.t_us >= before);
    CHECK(got.t_us <= after);
}

TEST("SensorHub: czujnik może podać własny, dokładniejszy znacznik") {
    resetWorld();

    FakeSensor s("fifo");
    s.stampOverride = 123456;
    s.next[0]       = 1.0f;

    SensorHub hub;
    REQUIRE(hub.add(s, SensorHub::Registration{}).has_value());
    REQUIRE(hub.init().has_value());

    Sample got{};
    auto   sub = EventBus::subscribe<Sample>([&](const Sample& e) { got = e; });
    REQUIRE(sub.has_value());

    REQUIRE(hub.pollOnce(0).has_value());
    CHECK_EQ(static_cast<long long>(got.t_us), 123456LL);
}

TEST("SensorHub: kalibracja i filtr działają w zadanej kolejności") {
    resetWorld();

    FakeSensor s("cal");
    SensorHub  hub;

    SensorHub::Registration r;
    r.filter.kind     = FilterKind::Ema;
    r.filter.emaAlpha = 0.5f;
    REQUIRE(hub.add(s, r).has_value());
    REQUIRE(hub.init().has_value());

    // Korekta: (surowy + 1) * 2
    SensorCal cal;
    cal.ch[0].offset = 1.0f;
    cal.ch[0].gain   = 2.0f;
    REQUIRE(hub.setCalibration(0, cal, false).has_value());

    Sample got{};
    auto   sub = EventBus::subscribe<Sample>([&](const Sample& e) { got = e; });
    REQUIRE(sub.has_value());

    // Pierwsza próbka: EMA startuje od wartości wejściowej, więc widać
    // wyłącznie efekt kalibracji: (10 + 1) * 2 = 22.
    s.next[0] = 10.0f;
    REQUIRE(hub.pollOnce(0).has_value());
    CHECK(got.value[0] > 21.9f && got.value[0] < 22.1f);

    // Druga próbka: kalibracja daje (20 + 1) * 2 = 42, EMA uśrednia z 22 → 32.
    s.next[0] = 20.0f;
    REQUIRE(hub.pollOnce(0).has_value());
    CHECK(got.value[0] > 31.9f && got.value[0] < 32.1f);
}

TEST("SensorHub: nieudany odczyt publikuje SensorFault i liczy serię") {
    resetWorld();

    FakeSensor s("bad");
    s.nextError = Err::IoError;

    SensorHub hub;
    REQUIRE(hub.add(s, SensorHub::Registration{}).has_value());
    REQUIRE(hub.init().has_value());

    int  faults = 0;
    u32  streak = 0;
    auto sub    = EventBus::subscribe<SensorFault>([&](const SensorFault& e) {
        ++faults;
        streak = e.consecutive;
    });
    REQUIRE(sub.has_value());

    CHECK(!hub.pollOnce(0).has_value());
    CHECK(!hub.pollOnce(0).has_value());
    CHECK_EQ(faults, 2);
    CHECK_EQ(static_cast<int>(streak), 2);

    // Udany odczyt zeruje serię — inaczej pojedyncze zakłócenie zostawałoby
    // w statystykach na zawsze.
    s.nextError = Err::None;
    REQUIRE(hub.pollOnce(0).has_value());
    s.nextError = Err::IoError;
    CHECK(!hub.pollOnce(0).has_value());
    CHECK_EQ(static_cast<int>(streak), 1);

    CHECK_EQ(static_cast<int>(hub.stats(0).faults), 3);
    CHECK_EQ(static_cast<int>(hub.stats(0).reads), 1);
}

TEST("SensorHub: brak nowych danych nie jest awarią") {
    resetWorld();

    FakeSensor s("free", 1, PollMode::Free);
    s.nextError = Err::WouldBlock;

    SensorHub hub;
    REQUIRE(hub.add(s, SensorHub::Registration{}).has_value());
    REQUIRE(hub.init().has_value());

    int faults = 0;
    auto sub = EventBus::subscribe<SensorFault>([&](const SensorFault&) { ++faults; });
    REQUIRE(sub.has_value());

    CHECK(hub.pollOnce(0).error() == Err::WouldBlock);
    CHECK_EQ(faults, 0);
    CHECK_EQ(static_cast<int>(hub.stats(0).skipped), 1);
    CHECK_EQ(static_cast<int>(hub.stats(0).faults), 0);
}

TEST("SensorHub: anomalia zmienia jakość próbki i publikuje zdarzenie") {
    resetWorld();

    FakeSensor s("frozen");
    SensorHub  hub;

    SensorHub::Registration r;
    r.anomaly.frozenLimit = 2;
    r.anomaly.spikeDelta  = 5.0f;
    REQUIRE(hub.add(s, r).has_value());
    REQUIRE(hub.init().has_value());

    AnomalyKind kind = AnomalyKind::None;
    int         hits = 0;
    Sample      last{};
    auto s1 = EventBus::subscribe<SensorAnomaly>([&](const SensorAnomaly& e) {
        kind = e.kind;
        ++hits;
    });
    auto s2 = EventBus::subscribe<Sample>([&](const Sample& e) { last = e; });
    REQUIRE(s1.has_value());
    REQUIRE(s2.has_value());

    s.next[0] = 1.0f;
    for (int i = 0; i < 3; ++i) REQUIRE(hub.pollOnce(0).has_value());

    CHECK(hits >= 1);
    CHECK(kind == AnomalyKind::Frozen);
    // Zamrożona wartość to dane nieaktualne, nie po prostu podejrzane.
    CHECK(last.q == Quality::Stale);

    // Skok większy niż spodziewany zmienia jakość na Suspect.
    s.next[0] = 100.0f;
    REQUIRE(hub.pollOnce(0).has_value());
    CHECK(kind == AnomalyKind::Spike);
    CHECK(last.q == Quality::Suspect);
}

TEST("SensorHub: przerwanie data-ready wyzwala odczyt w tasku, nie w ISR") {
    resetWorld();

    auto& mockHal = hal::mock::backend();
    FakeSensor s("irq", 1, PollMode::DataReadyIrq);
    s.next[0] = 7.0f;

    SensorHub hub;
    SensorHub::Registration r;
    r.sensor.irqPin = 4;
    REQUIRE(hub.add(s, r).has_value());
    REQUIRE(hub.init().has_value());
    REQUIRE(hub.start().has_value());

    std::atomic<int> samples{0};
    auto sub = EventBus::subscribe<Sample>([&](const Sample&) { ++samples; });
    REQUIRE(sub.has_value());

    const u32 readsBefore = s.reads.load();
    CHECK(mockHal.gpio.triggerInterrupt(4));

    // ISR wyłącznie publikuje zdarzenie — czujnik nie został jeszcze odczytany.
    CHECK_EQ(static_cast<int>(s.reads.load()), static_cast<int>(readsBefore));

    // Odczyt następuje po przepompowaniu kolejki przez task sense.poll.
    EventBus::drainIsr();
    rtos::delayMs(150);

    CHECK(s.reads.load() > readsBefore);
    CHECK(samples.load() >= 1);

    hub.stop();
}

TEST("SensorHub: task publikuje cyklicznie z zadanym okresem") {
    resetWorld();

    FakeSensor fast("f"), slow("s");
    fast.next[0] = 1.0f;
    slow.next[0] = 2.0f;

    SensorHub hub;
    SensorHub::Registration rf;
    rf.sensor.periodMs = 20;
    SensorHub::Registration rs;
    rs.sensor.periodMs = 100;

    REQUIRE(hub.add(fast, rf).has_value());
    REQUIRE(hub.add(slow, rs).has_value());
    REQUIRE(hub.init().has_value());
    CHECK_EQ(static_cast<int>(hub.tickMs()), 20);

    REQUIRE(hub.start().has_value());
    rtos::delayMs(230);
    hub.stop();

    // ~230 ms: szybki czujnik co 20 ms, wolny co 100 ms.
    CHECK(fast.reads.load() >= 7);
    CHECK(slow.reads.load() >= 1);
    CHECK(slow.reads.load() < fast.reads.load());
}

// ---------------------------------------------------------------------------
// Kalibracja trwała
// ---------------------------------------------------------------------------

TEST("Calibration: zapis przeżywa ponowne wczytanie") {
    resetWorld();

    SensorCal cal;
    cal.ch[0].offset = -1.5f;
    cal.ch[0].gain   = 1.02f;
    cal.ch[1].offset = 3.0f;
    REQUIRE(Calibration::save("bme280", cal).has_value());

    SensorCal loaded;
    REQUIRE(Calibration::load("bme280", loaded).has_value());
    CHECK(loaded.ch[0].offset < -1.49f && loaded.ch[0].offset > -1.51f);
    CHECK(loaded.ch[0].gain > 1.019f && loaded.ch[0].gain < 1.021f);
    CHECK(loaded.ch[1].offset > 2.99f && loaded.ch[1].offset < 3.01f);
}

TEST("Calibration: brak zapisu daje współczynniki neutralne, nie błąd") {
    resetWorld();

    SensorCal cal;
    cal.ch[0].gain = 999.0f;  // wartość, którą load musi nadpisać

    // Świeże urządzenie nie ma kalibracji i to jest normalny stan.
    REQUIRE(Calibration::load("nieznany", cal).has_value());
    CHECK(cal.ch[0].gain > 0.99f && cal.ch[0].gain < 1.01f);
    CHECK(cal.ch[0].offset == 0.0f);
}

TEST("Calibration: apply nakłada korektę tylko na wypełnione kanały") {
    SensorCal cal;
    for (auto& c : cal.ch) {
        c.offset = 1.0f;
        c.gain   = 2.0f;
    }

    Sample s;
    s.n        = 2;
    s.value[0] = 1.0f;
    s.value[1] = 2.0f;
    s.value[2] = 99.0f;  // kanał poza zakresem n — nietykalny

    Calibration::apply(cal, s);
    CHECK(s.value[0] == 4.0f);
    CHECK(s.value[1] == 6.0f);
    CHECK(s.value[2] == 99.0f);
}

// ---------------------------------------------------------------------------
// Detekcja anomalii
// ---------------------------------------------------------------------------

TEST("AnomalyDetector: zamrożona wartość po zadanej liczbie próbek") {
    AnomalyCfg cfg;
    cfg.frozenLimit = 3;
    AnomalyDetector det;
    det.configure(cfg);

    Sample s;
    s.n        = 1;
    s.value[0] = 5.0f;

    CHECK(det.check(s).kind == AnomalyKind::None);  // pierwsza próbka: brak historii
    CHECK(det.check(s).kind == AnomalyKind::None);
    CHECK(det.check(s).kind == AnomalyKind::None);
    CHECK(det.check(s).kind == AnomalyKind::Frozen);

    s.value[0] = 6.0f;
    CHECK(det.check(s).kind == AnomalyKind::None);  // zmiana kasuje licznik
}

TEST("AnomalyDetector: skok i zakres") {
    AnomalyCfg cfg;
    cfg.spikeDelta = 10.0f;
    cfg.minValue   = 0.0f;
    cfg.maxValue   = 100.0f;
    AnomalyDetector det;
    det.configure(cfg);

    Sample s;
    s.n        = 1;
    s.value[0] = 50.0f;
    CHECK(det.check(s).kind == AnomalyKind::None);

    s.value[0] = 55.0f;
    CHECK(det.check(s).kind == AnomalyKind::None);

    s.value[0] = 90.0f;
    auto hit = det.check(s);
    CHECK(hit.kind == AnomalyKind::Spike);
    CHECK(hit.value == 90.0f);

    // Zakres ma pierwszeństwo — jest najpewniejszym sygnałem uszkodzenia.
    s.value[0] = 500.0f;
    CHECK(det.check(s).kind == AnomalyKind::OutOfRange);
}

TEST("AnomalyDetector: wyłączone reguły nic nie zgłaszają") {
    AnomalyDetector det;
    det.configure(AnomalyCfg{});  // wszystko zerowe = wyłączone

    Sample s;
    s.n = 1;
    for (int i = 0; i < 20; ++i) {
        s.value[0] = 1.0f;
        CHECK(det.check(s).kind == AnomalyKind::None);
    }
}

// ---------------------------------------------------------------------------
// Filtry
// ---------------------------------------------------------------------------

TEST("Filtr None: przepuszcza wartość bez zmian") {
    ChannelFilter f;
    REQUIRE(f.configure(FilterCfg{}).has_value());
    CHECK(f.apply(3.75f) == 3.75f);
    CHECK(f.apply(-1.0f) == -1.0f);
}

TEST("Mediana: usuwa pojedynczy impuls, nie rozmazuje zbocza") {
    FilterCfg cfg;
    cfg.kind         = FilterKind::Median;
    cfg.medianWindow = 5;

    ChannelFilter f;
    REQUIRE(f.configure(cfg).has_value());

    for (int i = 0; i < 5; ++i) f.apply(10.0f);
    CHECK(f.apply(10.0f) == 10.0f);

    // Pojedyncze zakłócenie impulsowe znika całkowicie — to jest powód,
    // dla którego mediana wygrywa tu z uśrednianiem.
    CHECK(f.apply(1000.0f) == 10.0f);
    CHECK(f.apply(10.0f) == 10.0f);

    // Ale trwała zmiana poziomu przechodzi po zapełnieniu połowy okna.
    for (int i = 0; i < 3; ++i) f.apply(20.0f);
    CHECK(f.apply(20.0f) == 20.0f);
}

TEST("Mediana: okno większe niż limit jest przycinane, nie odrzucane") {
    FilterCfg cfg;
    cfg.kind         = FilterKind::Median;
    cfg.medianWindow = 200;

    ChannelFilter f;
    REQUIRE(f.configure(cfg).has_value());
    CHECK(f.apply(5.0f) == 5.0f);
}

TEST("EMA: startuje od pierwszej próbki, nie od zera") {
    FilterCfg cfg;
    cfg.kind     = FilterKind::Ema;
    cfg.emaAlpha = 0.5f;

    ChannelFilter f;
    REQUIRE(f.configure(cfg).has_value());

    // Start od zera dałby tu 50 i przez kilkanaście okresów zwracałby
    // wartości, których czujnik nigdy nie zmierzył.
    CHECK(f.apply(100.0f) == 100.0f);
    CHECK(f.apply(0.0f) == 50.0f);
    CHECK(f.apply(0.0f) == 25.0f);
}

TEST("EMA: błędny współczynnik jest odrzucany") {
    FilterCfg cfg;
    cfg.kind = FilterKind::Ema;

    ChannelFilter f;
    cfg.emaAlpha = 0.0f;
    CHECK(f.configure(cfg).error() == Err::BadArgument);
    cfg.emaAlpha = 1.5f;
    CHECK(f.configure(cfg).error() == Err::BadArgument);
}

TEST("Butterworth: wzmocnienie jednostkowe dla składowej stałej") {
    FilterCfg cfg;
    cfg.kind     = FilterKind::Butterworth;
    cfg.cutoffHz = 5.0f;
    cfg.sampleHz = 100.0f;

    ChannelFilter f;
    REQUIRE(f.configure(cfg).has_value());

    // Stan początkowy jest ładowany pierwszą próbką, więc filtr nie ma
    // stanu przejściowego, który wyglądałby jak skok mierzonej wielkości.
    const float first = f.apply(7.0f);
    CHECK(first > 6.99f && first < 7.01f);

    for (int i = 0; i < 50; ++i) f.apply(7.0f);
    const float steady = f.apply(7.0f);
    CHECK(steady > 6.99f && steady < 7.01f);
}

TEST("Butterworth: tłumi składową powyżej odcięcia") {
    FilterCfg cfg;
    cfg.kind     = FilterKind::Butterworth;
    cfg.cutoffHz = 2.0f;
    cfg.sampleHz = 100.0f;

    ChannelFilter f;
    REQUIRE(f.configure(cfg).has_value());

    // Sygnał zmieniający znak co próbkę to częstotliwość Nyquista — dużo
    // powyżej odcięcia, więc amplituda na wyjściu musi być znikoma.
    float maxOut = 0.0f;
    for (int i = 0; i < 200; ++i) {
        const float y = f.apply((i % 2 == 0) ? 1.0f : -1.0f);
        if (i > 100 && (y > maxOut || -y > maxOut)) maxOut = y > 0 ? y : -y;
    }
    CHECK(maxOut < 0.05f);
}

TEST("Butterworth: odcięcie powyżej Nyquista jest odrzucane") {
    FilterCfg cfg;
    cfg.kind     = FilterKind::Butterworth;
    cfg.sampleHz = 10.0f;
    cfg.cutoffHz = 6.0f;  // powyżej połowy częstotliwości próbkowania

    ChannelFilter f;
    CHECK(f.configure(cfg).error() == Err::BadArgument);

    cfg.cutoffHz = 1.0f;
    CHECK(f.configure(cfg).has_value());
}

TEST("Filtr: reset przywraca stan początkowy") {
    FilterCfg cfg;
    cfg.kind     = FilterKind::Ema;
    cfg.emaAlpha = 0.5f;

    ChannelFilter f;
    REQUIRE(f.configure(cfg).has_value());
    f.apply(100.0f);
    f.reset();

    // Po resecie kolejna próbka znów jest punktem startowym.
    CHECK(f.apply(10.0f) == 10.0f);
}

// ---------------------------------------------------------------------------
// Fuzja IMU
// ---------------------------------------------------------------------------

namespace {

/** Publikuje próbkę tak, jakby przyszła z czujnika o danym identyfikatorze. */
void publishImu(TopicId topic, Micros t, float x, float y, float z) {
    Sample s;
    s.topic    = topic;
    s.t_us     = t;
    s.n        = 3;
    s.value[0] = x;
    s.value[1] = y;
    s.value[2] = z;
    EventBus::publish(s);
}

}  // namespace

TEST("ImuFusion: bez danych zgłasza brak nowej próbki, nie awarię") {
    resetWorld();

    ImuFusion fusion(nameId("accel"), nameId("gyro"));
    REQUIRE(fusion.configure(SensorCfg{}).has_value());

    Sample out;
    CHECK(fusion.read(out).error() == Err::WouldBlock);
}

TEST("ImuFusion: całkuje żyroskop po znacznikach czasu") {
    resetWorld();

    ImuFusion::Setup setup;
    setup.alpha = 1.0f;  // wyłącza korektę akcelerometrem — czysta integracja
    ImuFusion fusion(nameId("accel"), nameId("gyro"), setup);
    REQUIRE(fusion.configure(SensorCfg{}).has_value());

    // Pierwsza próbka wyznacza tylko punkt odniesienia czasu.
    publishImu(nameId("gyro"), 1000000, 0.0f, 0.0f, 0.0f);
    Sample out;
    CHECK(fusion.read(out).error() == Err::WouldBlock);

    // 90 °/s przez 100 ms to 9° — krok całkowania bierze się wprost
    // z różnicy znaczników próbek, a nie z okresu taska.
    publishImu(nameId("gyro"), 1100000, 90.0f, 0.0f, 0.0f);
    REQUIRE(fusion.read(out).has_value());

    float roll = 0, pitch = 0, yaw = 0;
    fusion.euler(roll, pitch, yaw);
    CHECK(roll > 8.9f && roll < 9.1f);
    CHECK_EQ(static_cast<long long>(out.t_us), 1100000LL);
    CHECK_EQ(static_cast<int>(out.n), 4);

    // Kolejne 100 ms z tą samą prędkością dokłada kolejne 9°.
    publishImu(nameId("gyro"), 1200000, 90.0f, 0.0f, 0.0f);
    REQUIRE(fusion.read(out).has_value());
    fusion.euler(roll, pitch, yaw);
    CHECK(roll > 17.9f && roll < 18.1f);
}

TEST("ImuFusion: przerwa dłuższa niż maxDtSec nie wywraca orientacji") {
    resetWorld();

    ImuFusion::Setup setup;
    setup.alpha    = 1.0f;
    setup.maxDtSec = 0.2f;
    ImuFusion fusion(nameId("accel"), nameId("gyro"), setup);
    REQUIRE(fusion.configure(SensorCfg{}).has_value());

    publishImu(nameId("gyro"), 1000000, 0.0f, 0.0f, 0.0f);
    // Dziesięć sekund przerwy — tyle, ile potrwałoby zawieszenie taska.
    // Bez ograniczenia kroku orientacja skoczyłaby o 900°.
    publishImu(nameId("gyro"), 11000000, 90.0f, 0.0f, 0.0f);

    Sample out;
    REQUIRE(fusion.read(out).has_value());
    float roll = 0, pitch = 0, yaw = 0;
    fusion.euler(roll, pitch, yaw);
    CHECK(roll > 17.9f && roll < 18.1f);  // 90 °/s × 0,2 s
}

TEST("ImuFusion: kwaternion ma długość jednostkową") {
    resetWorld();

    ImuFusion fusion(nameId("accel"), nameId("gyro"));
    REQUIRE(fusion.configure(SensorCfg{}).has_value());

    publishImu(nameId("accel"), 1000000, 0.0f, 0.0f, 1.0f);
    publishImu(nameId("gyro"), 1000000, 0.0f, 0.0f, 0.0f);
    publishImu(nameId("gyro"), 1100000, 30.0f, 20.0f, 10.0f);

    Sample out;
    REQUIRE(fusion.read(out).has_value());

    const float norm = out.value[0] * out.value[0] + out.value[1] * out.value[1] +
                       out.value[2] * out.value[2] + out.value[3] * out.value[3];
    CHECK(norm > 0.999f && norm < 1.001f);
    CHECK(out.q == Quality::Good);
}

TEST("ImuFusion: akcelerometr koryguje dryf tylko przy spoczynku") {
    resetWorld();

    ImuFusion::Setup setup;
    setup.alpha = 0.5f;  // mocna korekta, żeby efekt był widoczny w jednym kroku
    ImuFusion fusion(nameId("accel"), nameId("gyro"), setup);
    REQUIRE(fusion.configure(SensorCfg{}).has_value());

    // Wektor 1 g przechylony o 45° wokół osi X.
    publishImu(nameId("accel"), 1000000, 0.0f, 0.7071f, 0.7071f);
    publishImu(nameId("gyro"), 1000000, 0.0f, 0.0f, 0.0f);
    publishImu(nameId("gyro"), 1010000, 0.0f, 0.0f, 0.0f);

    Sample out;
    REQUIRE(fusion.read(out).has_value());
    float roll = 0, pitch = 0, yaw = 0;
    fusion.euler(roll, pitch, yaw);
    CHECK(roll > 20.0f && roll < 25.0f);  // połowa drogi do 45°

    // Podczas gwałtownego ruchu moduł wektora odbiega od 1 g — wtedy
    // akcelerometr mierzy też przyspieszenie własne i korekta byłaby błędna.
    const float before = roll;
    publishImu(nameId("accel"), 1020000, 0.0f, 3.0f, 3.0f);
    publishImu(nameId("gyro"), 1020000, 0.0f, 0.0f, 0.0f);
    REQUIRE(fusion.read(out).has_value());
    fusion.euler(roll, pitch, yaw);
    CHECK(roll > before - 0.01f && roll < before + 0.01f);
}

TEST("ImuFusion: błędna konfiguracja jest odrzucana") {
    resetWorld();

    ImuFusion noTopics(kInvalidTopic, nameId("gyro"));
    CHECK(noTopics.configure(SensorCfg{}).error() == Err::BadArgument);

    ImuFusion::Setup bad;
    bad.alpha = 1.5f;
    ImuFusion badAlpha(nameId("a"), nameId("g"), bad);
    CHECK(badAlpha.configure(SensorCfg{}).error() == Err::BadArgument);
}
