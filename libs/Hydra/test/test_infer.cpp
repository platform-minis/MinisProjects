/**
 * Testy szwu inferencji (etap 0).
 *
 * Sprawdzamy tu kontrakt, nie jakość wnioskowania: czy arena jest naprawdę
 * używana, czy niezgodny kształt jest odrzucany, i czy kwantyzacja przechodzi
 * w obie strony. To są dokładnie te trzy rzeczy, które przy podmianie atrapy
 * na TFLM albo zadziałają tak samo, albo wywrócą wszystko powyżej — a wtedy
 * lepiej, żeby wywróciły test.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/infer/MockEngine.hpp"

using namespace hydra;
using namespace hydra::infer;

namespace {

/** Okno 8 próbek `float` na wejściu, jedna liczba na wyjściu. */
TensorInfo window(u16 count, TensorType type = TensorType::F32) {
    TensorInfo t;
    t.type = type;
    t.dims = 2;
    t.dim[0] = 1;
    t.dim[1] = count;
    return t;
}

TensorInfo scalar(TensorType type = TensorType::F32) {
    TensorInfo t;
    t.type = type;
    t.dims = 1;
    t.dim[0] = 1;
    return t;
}

struct Rig {
    alignas(8) u8 arena[512] = {};
    MockEngine engine;

    Status open(u16 inputs = 8) {
        engine.setShape(window(inputs), scalar());
        if (auto r = engine.open(arena, sizeof(arena)); !r) return r;
        return engine.load(nullptr, 0);
    }
};

}  // namespace

TEST("infer: kształt tensora liczy elementy i bajty") {
    const TensorInfo t = window(16);
    CHECK_EQ(t.elements(), 16u);
    CHECK_EQ(t.bytes(), 64u);
    CHECK(t.valid());
    CHECK(!TensorInfo{}.valid());
}

TEST("infer: kwantyzacja przelicza surową wartość na wielkość fizyczną") {
    TensorInfo t = scalar(TensorType::S8);
    t.scale = 0.5f;
    t.zeroPoint = -128;
    // Bez tego przeliczenia `-128` z wyjścia modelu nie znaczy nic.
    CHECK_EQ(t.dequantize(-128), 0.0f);
    CHECK_EQ(t.dequantize(-126), 1.0f);

    // Tensor bez kwantyzacji przechodzi tożsamościowo — wołający nie musi
    // sprawdzać, z jakim modelem ma do czynienia.
    CHECK_EQ(scalar().dequantize(7), 7.0f);
}

TEST("infer: silnik bez areny odmawia otwarcia") {
    MockEngine engine;
    CHECK(!engine.open(nullptr, 128).has_value());
    CHECK(!engine.ready());
}

TEST("infer: bez kształtu nie da się wczytać modelu") {
    // Element potoku pyta silnik, ile próbek zebrać. Brak odpowiedzi musi być
    // błędem, a nie cichym zerem, bo zero znaczyłoby okno bez próbek.
    alignas(8) u8 arena[128] = {};
    MockEngine engine;
    CHECK(engine.open(arena, sizeof(arena)).has_value());
    CHECK(!engine.load(nullptr, 0).has_value());
}

TEST("infer: za mała arena jest zgłaszana przy wczytaniu, nie przy liczeniu") {
    alignas(8) u8 tiny[16] = {};
    MockEngine engine;
    engine.setShape(window(64), scalar());
    CHECK(engine.open(tiny, sizeof(tiny)).has_value());
    const auto loaded = engine.load(nullptr, 0);
    CHECK(!loaded.has_value());
    CHECK_EQ(loaded.error(), Err::OutOfMemory);
}

TEST("infer: arena jest naprawdę używana") {
    Rig rig;
    CHECK(rig.open(8).has_value());
    // 8 wejść + 1 wyjście, po 4 bajty. Silnik, który areny nie tyka,
    // przepuściłby zbyt małą — a problem wyszedłby dopiero przy TFLM.
    CHECK_EQ(rig.engine.arenaUsedBytes(), 9u * 4u);
}

TEST("infer: wejście o złym rozmiarze jest odrzucane") {
    Rig rig;
    CHECK(rig.open(8).has_value());

    const float tooFew[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(!rig.engine.setInput(0, tooFew, sizeof(tooFew)).has_value());
}

TEST("infer: domyślne wnioskowanie liczy energię okna") {
    Rig rig;
    CHECK(rig.open(4).has_value());

    // Stały sygnał o amplitudzie 3 ma energię 3 — łatwo sprawdzić w pamięci.
    const float samples[4] = {3.0f, -3.0f, 3.0f, -3.0f};
    CHECK(rig.engine.setInput(0, samples, sizeof(samples)).has_value());
    CHECK(rig.engine.invoke().has_value());

    float out = 0.0f;
    CHECK(rig.engine.readOutput(0, &out, sizeof(out)).has_value());
    CHECK(out > 2.99f && out < 3.01f);
    CHECK_EQ(rig.engine.invocations(), 1u);
}

TEST("infer: cisza daje zero, a nie śmieci z areny") {
    Rig rig;
    CHECK(rig.open(4).has_value());

    const float samples[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(rig.engine.setInput(0, samples, sizeof(samples)).has_value());
    CHECK(rig.engine.invoke().has_value());

    float out = -1.0f;
    CHECK(rig.engine.readOutput(0, &out, sizeof(out)).has_value());
    CHECK_EQ(out, 0.0f);
}

TEST("infer: wejście skwantyzowane przechodzi przez skalę") {
    alignas(8) u8 arena[256] = {};
    MockEngine engine;

    TensorInfo in = window(4, TensorType::S8);
    in.scale = 0.5f;
    in.zeroPoint = 0;
    engine.setShape(in, scalar());
    CHECK(engine.open(arena, sizeof(arena)).has_value());
    CHECK(engine.load(nullptr, 0).has_value());

    // Surowe 6 przy skali 0,5 znaczy 3,0 — energia ma wyjść 3, a nie 6.
    const i8 raw[4] = {6, -6, 6, -6};
    CHECK(engine.setInput(0, raw, sizeof(raw)).has_value());
    CHECK(engine.invoke().has_value());

    float out = 0.0f;
    CHECK(engine.readOutput(0, &out, sizeof(out)).has_value());
    CHECK(out > 2.99f && out < 3.01f);
}

TEST("infer: sposób liczenia da się podmienić") {
    Rig rig;
    CHECK(rig.open(4).has_value());
    rig.engine.setResponder([](const float* in, u32 count, float* out, u32 outCount) {
        HYDRA_UNUSED(in);
        HYDRA_UNUSED(count);
        if (outCount > 0) out[0] = 42.0f;
    });

    const float samples[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    CHECK(rig.engine.setInput(0, samples, sizeof(samples)).has_value());
    CHECK(rig.engine.invoke().has_value());

    float out = 0.0f;
    CHECK(rig.engine.readOutput(0, &out, sizeof(out)).has_value());
    CHECK_EQ(out, 42.0f);
}

TEST("infer: zamknięty silnik nie liczy") {
    Rig rig;
    CHECK(rig.open(4).has_value());
    rig.engine.close();

    CHECK(!rig.engine.ready());
    CHECK(!rig.engine.invoke().has_value());
}

TEST("infer: silnik podaje swoją nazwę") {
    // Nazwa idzie do logu i do diagnostyki: bez niej nie widać, czy urządzenie
    // liczy modelem, czy atrapą.
    MockEngine engine;
    CHECK_STR(engine.name(), "mock");
}
