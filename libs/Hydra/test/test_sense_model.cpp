/**
 * Testy detekcji nauczonej w module czujników (etap 4).
 *
 * Tu jest teza całej pracy nad inferencją: `AnomalyDetector` odpowiada na
 * pytania zadane z góry — czy wartość stoi, skoczyła, wyszła poza zakres —
 * a każde z nich wymaga liczby, którą trzeba znać. Przy wibracji silnika
 * albo poborze prądu tej liczby zwykle nikt nie zna, bo „normalne" zależy
 * od egzemplarza, obciążenia i zużycia.
 *
 * Najważniejszy test poniżej pokazuje przebieg, którego **żaden próg nie
 * łapie**, a model łapie: sygnał mieszczący się w zakresie, bez skoków między
 * próbkami i bez zamrożenia — za to o zmienionym kształcie.
 */

#include "hydra_test.hpp"

#include <math.h>
#include <string.h>

#include "hydra/infer/MockEngine.hpp"
#include "hydra/sense/Pipeline.hpp"
#include "hydra/sense/SensorHub.hpp"

using namespace hydra;
using namespace hydra::sense;

namespace {

constexpr u32 kWindow = 16;

infer::TensorInfo windowShape() {
    infer::TensorInfo t;
    t.type = infer::TensorType::F32;
    t.dims = 2;
    t.dim[0] = 1;
    t.dim[1] = static_cast<u16>(kWindow);
    return t;
}

infer::TensorInfo scalarShape() {
    infer::TensorInfo t;
    t.type = infer::TensorType::F32;
    t.dims = 1;
    t.dim[0] = 1;
    return t;
}

/** Detektor z atrapą liczącą energię okna. */
struct Rig {
    alignas(8) u8 arena[512] = {};
    float windowBuf[kWindow] = {};

    infer::MockEngine engine;
    ModelDetector     detector;

    Status build(float threshold, u16 hop = 0) {
        engine.setShape(windowShape(), scalarShape());
        if (auto r = engine.open(arena, sizeof(arena)); !r) return r;
        if (auto r = engine.load(nullptr, 0); !r) return r;

        detector.setEngine(&engine);
        detector.setWindowBuffer(windowBuf, kWindow);

        ModelDetector::Config cfg;
        cfg.channel = 0;
        cfg.threshold = threshold;
        cfg.hopSamples = hop;
        return detector.configure(cfg);
    }

    /** Podaje próbkę o zadanej wartości na kanale 0. */
    AnomalyDetector::Hit feed(float value) {
        Sample s;
        s.n = 1;
        s.value[0] = value;
        return detector.feed(s);
    }
};

}  // namespace

TEST("sense/model: bez bufora okna konfiguracja odmawia") {
    // Rozmiar okna wynika z modelu, więc aplikacja nie zna go przed
    // wczytaniem — brak bufora musi wyjść przy konfiguracji.
    alignas(8) u8 arena[512] = {};
    infer::MockEngine engine;
    engine.setShape(windowShape(), scalarShape());
    CHECK(engine.open(arena, sizeof(arena)).has_value());
    CHECK(engine.load(nullptr, 0).has_value());

    ModelDetector detector;
    detector.setEngine(&engine);
    CHECK(!detector.configure(ModelDetector::Config{}).has_value());
}

TEST("sense/model: bez silnika konfiguracja odmawia") {
    ModelDetector detector;
    float buf[kWindow] = {};
    detector.setWindowBuffer(buf, kWindow);
    CHECK(!detector.configure(ModelDetector::Config{}).has_value());
}

TEST("sense/model: milczy, dopóki okno się nie zbierze") {
    Rig rig;
    CHECK(rig.build(0.5f).has_value());

    // Piętnaście próbek to jeszcze nie okno — model nie ma czego policzyć,
    // a zgadywanie z niepełnego okna byłoby gorsze niż milczenie.
    for (u32 i = 0; i < kWindow - 1; ++i) {
        CHECK_EQ(static_cast<int>(rig.feed(1.0f).kind), static_cast<int>(AnomalyKind::None));
    }
    CHECK_EQ(rig.detector.evaluations(), 0u);

    rig.feed(1.0f);
    CHECK_EQ(rig.detector.evaluations(), 1u);
}

TEST("sense/model: spokojny przebieg nie wywołuje alarmu") {
    Rig rig;
    // Atrapa liczy energię okna; próg powyżej energii spokojnego sygnału.
    CHECK(rig.build(2.0f).has_value());

    AnomalyDetector::Hit hit;
    for (u32 i = 0; i < kWindow; ++i) hit = rig.feed(1.0f);

    CHECK_EQ(static_cast<int>(hit.kind), static_cast<int>(AnomalyKind::None));
    CHECK_EQ(rig.detector.evaluations(), 1u);
    // Wynik jest dostępny także poniżej progu — bez tego nie da się progu
    // dobrać do rozkładu wyników na danych bez usterki.
    CHECK(rig.detector.lastScore() > 0.9f && rig.detector.lastScore() < 1.1f);
}

TEST("sense/model: przebieg odstający daje trafienie z rodzajem Learned") {
    Rig rig;
    CHECK(rig.build(2.0f).has_value());

    AnomalyDetector::Hit hit;
    for (u32 i = 0; i < kWindow; ++i) hit = rig.feed(5.0f);

    CHECK_EQ(static_cast<int>(hit.kind), static_cast<int>(AnomalyKind::Learned));
    CHECK_EQ(hit.channel, 0u);
    // Wartością jest wynik modelu, nie ostatnia próbka: „5,0" mówi, jak bardzo
    // przebieg odstaje, a pojedyncza próbka nie mówi o oknie nic.
    CHECK(hit.value > 4.9f && hit.value < 5.1f);
}

TEST("sense/model: model łapie to, czego próg nie widzi") {
    /*
     * Sedno etapu.
     *
     * Przebieg drga: wartości skaczą między 0,9 a 1,1 co próbkę. Poziom się
     * nie zmienia — średnia zostaje 1,0 — więc zmiana jest w **kształcie**.
     * Sprawdzamy, że żaden z trzech progów tego nie widzi, a model tak.
     *
     * Model liczy tu odchylenie od średniej okna, a nie energię: energia
     * mierzy poziom i dla tego przebiegu wychodzi prawie taka sama jak dla
     * spokojnego (1,005 wobec 1,0). To nie jest szczegół testu, tylko sedno
     * różnicy — miara musi patrzeć na to, co się naprawdę zmienia, i właśnie
     * tego uczy się model zamiast dostać to wpisane progiem.
     */
    AnomalyCfg thresholds;
    thresholds.frozenLimit = 4;        // czujnik nie stoi — wartości się zmieniają
    thresholds.spikeDelta  = 0.5f;     // skoki po 0,2 mieszczą się w limicie
    thresholds.minValue    = 0.0f;     // wszystko w zakresie
    thresholds.maxValue    = 2.0f;

    AnomalyDetector classic;
    classic.configure(thresholds);

    Rig rig;
    CHECK(rig.build(0.05f).has_value());
    rig.engine.setResponder([](const float* in, u32 count, float* out, u32 outCount) {
        if (outCount == 0 || count == 0) return;
        float mean = 0.0f;
        for (u32 i = 0; i < count; ++i) mean += in[i];
        mean /= static_cast<float>(count);

        float variance = 0.0f;
        for (u32 i = 0; i < count; ++i) {
            const float d = in[i] - mean;
            variance += d * d;
        }
        out[0] = sqrtf(variance / static_cast<float>(count));
    });

    AnomalyDetector::Hit classicHit;
    AnomalyDetector::Hit modelHit;

    for (u32 i = 0; i < kWindow; ++i) {
        const float value = (i % 2 == 0) ? 0.9f : 1.1f;

        Sample s;
        s.n = 1;
        s.value[0] = value;

        const auto c = classic.check(s);
        if (c.kind != AnomalyKind::None) classicHit = c;
        modelHit = rig.detector.feed(s);
    }

    // Progi milczą: wartość się zmienia, skoki są małe, zakres zachowany.
    CHECK_EQ(static_cast<int>(classicHit.kind), static_cast<int>(AnomalyKind::None));
    // Model widzi, że przebieg nie wygląda jak zwykle.
    CHECK_EQ(static_cast<int>(modelHit.kind), static_cast<int>(AnomalyKind::Learned));
    CHECK(modelHit.value > 0.09f && modelHit.value < 0.11f);
}

TEST("sense/model: ten sam model milczy na przebiegu spokojnym") {
    // Druga strona poprzedniego testu: miara czuła na kształt nie może
    // krzyczeć na wszystko, bo wtedy nie niesie informacji.
    Rig rig;
    CHECK(rig.build(0.05f).has_value());
    rig.engine.setResponder([](const float* in, u32 count, float* out, u32 outCount) {
        if (outCount == 0 || count == 0) return;
        float mean = 0.0f;
        for (u32 i = 0; i < count; ++i) mean += in[i];
        mean /= static_cast<float>(count);
        float variance = 0.0f;
        for (u32 i = 0; i < count; ++i) {
            const float d = in[i] - mean;
            variance += d * d;
        }
        out[0] = sqrtf(variance / static_cast<float>(count));
    });

    AnomalyDetector::Hit hit;
    for (u32 i = 0; i < kWindow; ++i) hit = rig.feed(1.0f);

    CHECK_EQ(static_cast<int>(hit.kind), static_cast<int>(AnomalyKind::None));
}

TEST("sense/model: zakładka zwiększa częstość ocen") {
    Rig rig;
    // Przesuw o połowę okna: szybsza reakcja kosztem dwukrotnie większego
    // obciążenia.
    CHECK(rig.build(100.0f, kWindow / 2).has_value());

    for (u32 i = 0; i < kWindow * 2; ++i) rig.feed(1.0f);

    // 32 próbki, okno 16, przesuw 8 → oceny po 16, 24, 32.
    CHECK_EQ(rig.detector.evaluations(), 3u);
}

TEST("sense/model: obserwuje wskazany kanał, nie pierwszy z brzegu") {
    // Model patrzy na jedną wielkość — prąd albo kąt. Okno z przeplecionych
    // kanałów wymagałoby modelu uczonego dokładnie na tym przeplocie.
    Rig rig;
    CHECK(rig.build(2.0f).has_value());

    ModelDetector::Config cfg = rig.detector.config();
    cfg.channel = 1;
    CHECK(rig.detector.configure(cfg).has_value());

    AnomalyDetector::Hit hit;
    for (u32 i = 0; i < kWindow; ++i) {
        Sample s;
        s.n = 2;
        s.value[0] = 0.0f;    // kanał spokojny
        s.value[1] = 5.0f;    // kanał obserwowany — odstaje
        hit = rig.detector.feed(s);
    }

    CHECK_EQ(static_cast<int>(hit.kind), static_cast<int>(AnomalyKind::Learned));
    CHECK_EQ(hit.channel, 1u);
}

TEST("sense/model: próbka bez obserwowanego kanału jest pomijana") {
    Rig rig;
    CHECK(rig.build(0.1f).has_value());

    ModelDetector::Config cfg = rig.detector.config();
    cfg.channel = 3;
    CHECK(rig.detector.configure(cfg).has_value());

    // Czujnik podaje jeden kanał, a detektor patrzy na czwarty — okno nie ma
    // z czego rosnąć i model nie może policzyć.
    for (u32 i = 0; i < kWindow * 2; ++i) rig.feed(1.0f);
    CHECK_EQ(rig.detector.evaluations(), 0u);
}

TEST("sense/model: reset czyści okno") {
    Rig rig;
    CHECK(rig.build(100.0f).has_value());

    for (u32 i = 0; i < kWindow - 1; ++i) rig.feed(1.0f);
    rig.detector.reset();

    // Po zresetowaniu brakuje znowu pełnego okna — inaczej pierwsza ocena po
    // ponownym starcie liczyłaby się z próbek sprzed przerwy.
    for (u32 i = 0; i < kWindow - 1; ++i) rig.feed(1.0f);
    CHECK_EQ(rig.detector.evaluations(), 0u);

    rig.feed(1.0f);
    CHECK_EQ(rig.detector.evaluations(), 1u);
}

TEST("sense/model: rodzaj anomalii ma czytelną nazwę") {
    // Nazwa idzie do logu i do telemetrii — bez niej odbiorca widzi liczbę.
    CHECK_STR(toString(AnomalyKind::Learned), "learned");
}

// ---------------------------------------------------------------------------
// Wpięcie w SensorHub
// ---------------------------------------------------------------------------

namespace {

/** Czujnik podający wartości z listy — sterowany wprost przez test. */
class ScriptedSensor : public ISensor {
public:
    const char* name() const override { return "scripted"; }
    PollMode    pollMode() const override { return PollMode::Periodic; }
    u8          channels() const override { return 1; }

    Status probe() override { return ok(); }
    Status configure(const SensorCfg&) override { return ok(); }
    Status read(Sample& out) override {
        out.n = 1;
        out.value[0] = next;
        return ok();
    }

    float next = 1.0f;
};

}  // namespace

TEST("sense/model: werdykt modelu idzie tą samą drogą co próg") {
    /*
     * Świadomie to samo zdarzenie `SensorAnomaly`, a nie osobne.
     *
     * Osobne oznaczałoby, że każdy odbiorca anomalii musi teraz subskrybować
     * dwa tematy, żeby nie przegapić połowy — a `AnomalyKind` i tak pozwala
     * rozróżnić źródło temu, kogo to interesuje.
     */
    Rig rig;
    CHECK(rig.build(2.0f).has_value());

    ScriptedSensor sensor;
    SensorHub hub;
    SensorHub::Registration reg;
    const auto index = hub.add(sensor, reg);
    CHECK(index.has_value());
    CHECK(hub.attachModel(index.value(), rig.detector).has_value());

    static u32 anomalies = 0;
    static AnomalyKind lastKind = AnomalyKind::None;
    anomalies = 0;
    lastKind = AnomalyKind::None;

    const auto sub = EventBus::subscribe<SensorAnomaly>([](const SensorAnomaly& e) {
        ++anomalies;
        lastKind = e.kind;
    });
    CHECK(sub.has_value());

    CHECK(hub.init().has_value());
    CHECK(hub.start().has_value());

    // Wartość odstająca; próg wyłączony, więc jedynym źródłem alarmu jest model.
    sensor.next = 5.0f;
    for (u32 i = 0; i < kWindow; ++i) CHECK(hub.pollOnce(index.value()).has_value());

    CHECK_EQ(anomalies, 1u);
    CHECK_EQ(static_cast<int>(lastKind), static_cast<int>(AnomalyKind::Learned));

    hub.stop();
}

TEST("sense/model: czujnik bez modelu działa jak dotąd") {
    // Detektor jest wskaźnikiem właśnie po to: czujniki bez modelu nie płacą
    // za okno próbek, którego nie używają.
    ScriptedSensor sensor;
    SensorHub hub;
    const auto index = hub.add(sensor, SensorHub::Registration{});
    CHECK(index.has_value());

    CHECK(hub.init().has_value());
    CHECK(hub.start().has_value());

    sensor.next = 5.0f;
    for (u32 i = 0; i < kWindow; ++i) CHECK(hub.pollOnce(index.value()).has_value());

    // Bez modelu i bez progów nie ma czego zgłaszać.
    CHECK_EQ(hub.stats(index.value()).anomalies, 0u);
    hub.stop();
}

TEST("sense/model: próg ma pierwszeństwo przed modelem") {
    /*
     * Dwa zdarzenia o tej samej próbce znaczyłyby dla odbiorcy „dwie
     * anomalie", a nie „ta sama, widziana dwa razy". Próg jest przy tym
     * pewniejszy: mówi, **co** jest nie tak, a model tylko, że coś.
     *
     * Model dostaje próbkę mimo to — pominięcie zostawiłoby dziurę w oknie
     * i dałoby mu przebieg, którego nie widział przy uczeniu.
     */
    Rig rig;
    CHECK(rig.build(2.0f).has_value());

    ScriptedSensor sensor;
    SensorHub hub;
    SensorHub::Registration reg;
    reg.anomaly.minValue = 0.0f;
    reg.anomaly.maxValue = 1.0f;   // 5,0 wychodzi poza zakres
    const auto index = hub.add(sensor, reg);
    CHECK(index.has_value());
    CHECK(hub.attachModel(index.value(), rig.detector).has_value());

    static AnomalyKind lastKind = AnomalyKind::None;
    lastKind = AnomalyKind::None;
    const auto sub = EventBus::subscribe<SensorAnomaly>(
        [](const SensorAnomaly& e) { lastKind = e.kind; });
    CHECK(sub.has_value());

    CHECK(hub.init().has_value());
    CHECK(hub.start().has_value());

    sensor.next = 5.0f;
    for (u32 i = 0; i < kWindow; ++i) CHECK(hub.pollOnce(index.value()).has_value());

    CHECK_EQ(static_cast<int>(lastKind), static_cast<int>(AnomalyKind::OutOfRange));
    // Okno mimo to płynęło — model policzył.
    CHECK_EQ(rig.detector.evaluations(), 1u);

    hub.stop();
}

TEST("sense/model: odpięcie wraca do samych progów") {
    Rig rig;
    CHECK(rig.build(2.0f).has_value());

    ScriptedSensor sensor;
    SensorHub hub;
    const auto index = hub.add(sensor, SensorHub::Registration{});
    CHECK(index.has_value());
    CHECK(hub.attachModel(index.value(), rig.detector).has_value());
    hub.detachModel(index.value());

    CHECK(hub.init().has_value());
    CHECK(hub.start().has_value());

    sensor.next = 5.0f;
    for (u32 i = 0; i < kWindow; ++i) CHECK(hub.pollOnce(index.value()).has_value());

    CHECK_EQ(rig.detector.evaluations(), 0u);
    hub.stop();
}
