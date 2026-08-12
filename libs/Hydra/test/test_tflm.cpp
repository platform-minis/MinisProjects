/**
 * Testy silnika TensorFlow Lite Micro (etap 1).
 *
 * Model jest wpisany w test jako tablica bajtów (`tflm_model_sine.cpp`),
 * tak samo jak moduły `.wasm` w `test_script_wasm.cpp` i z tego samego powodu:
 * testy nie mogą wymagać konwertera modeli, tak jak nie wymagają toolchaina
 * Arduino.
 *
 * Model uczy się `sin(x)` dla x z przedziału [0, 2π] — dwie warstwy gęste po
 * 16 neuronów. To najprostszy możliwy model, w którym **da się sprawdzić wynik
 * matematycznie**: sieć, która zwraca cokolwiek, przejdzie test „zwróciła
 * liczbę", ale nie przejdzie porównania z sinusem.
 */

#include "hydra_test.hpp"

#include <math.h>
#include <string.h>

#include "hydra/infer/TflmEngine.hpp"
#include "hydra/media/elements/Infer.hpp"
#include "hydra/media/Pipeline.hpp"
#include "tflm_model_sine.h"

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

using namespace hydra;
using namespace hydra::infer;

namespace {

/**
 * Resolver z jednym operatorem.
 *
 * Model ma dwie warstwy gęste, obie realizowane przez FULLY_CONNECTED —
 * i nic więcej. `AllOpsResolver` wciągnąłby sto operatorów, z których 99 nie
 * ma tu czego robić, a sam ważyłby więcej niż ten model.
 */
using SineResolver = tflite::MicroMutableOpResolver<1>;

SineResolver& sineResolver() {
    static SineResolver resolver;
    static bool ready = false;
    if (!ready) {
        resolver.AddFullyConnected();
        ready = true;
    }
    return resolver;
}

/** Arena dobrana doświadczalnie — patrz test „arena wystarcza z zapasem". */
constexpr size_t kArenaBytes = 4096;

struct Rig {
    alignas(16) u8 arena[kArenaBytes] = {};
    TflmEngine engine;

    Status open() {
        engine.setOpResolver(&sineResolver());
        if (auto r = engine.open(arena, sizeof(arena)); !r) return r;
        return engine.load(g_hello_world_float_model_data,
                           g_hello_world_float_model_data_size);
    }

    /** Przepuszcza x przez model. */
    float predict(float x) {
        float out = 0.0f;
        if (!engine.setInput(0, &x, sizeof(x)).has_value()) return NAN;
        if (!engine.invoke().has_value()) return NAN;
        if (!engine.readOutput(0, &out, sizeof(out)).has_value()) return NAN;
        return out;
    }
};

}  // namespace

TEST("tflm: model wczytuje się i podaje swój kształt") {
    Rig rig;
    CHECK(rig.open().has_value());
    CHECK(rig.engine.ready());
    CHECK_STR(rig.engine.name(), "tflm");

    CHECK_EQ(rig.engine.inputCount(), 1u);
    CHECK_EQ(rig.engine.outputCount(), 1u);

    const TensorInfo in = rig.engine.input(0);
    CHECK_EQ(static_cast<int>(in.type), static_cast<int>(TensorType::F32));
    CHECK_EQ(in.elements(), 1u);
    CHECK_EQ(in.bytes(), 4u);

    const TensorInfo out = rig.engine.output(0);
    CHECK_EQ(static_cast<int>(out.type), static_cast<int>(TensorType::F32));
    CHECK_EQ(out.elements(), 1u);
}

TEST("tflm: model liczy sinus") {
    Rig rig;
    CHECK(rig.open().has_value());

    // Tolerancja z testu referencyjnego TFLM: sieć o dwóch warstwach po
    // 16 neuronów nie odtwarza sinusa dokładnie i nie ma takiego obowiązku.
    // Chodzi o to, żeby odtwarzała **jego kształt** — wynik daleki od sinusa
    // znaczy, że model policzył coś innego, a nie że jest niedokładny.
    constexpr float kEpsilon = 0.05f;
    const float xs[] = {0.0f, 1.0f, 3.0f, 5.0f};

    for (float x : xs) {
        const float y = rig.predict(x);
        CHECK(!isnan(y));
        CHECK(fabsf(sinf(x) - y) <= kEpsilon);
    }
}

TEST("tflm: kolejne wywołania nie psują stanu") {
    // Interpreter zostaje między wywołaniami — gdyby coś przeciekało w arenie,
    // objawiłoby się dopiero po którymś z rzędu, a nie przy pierwszym.
    Rig rig;
    CHECK(rig.open().has_value());

    for (int i = 0; i < 50; ++i) {
        const float x = static_cast<float>(i) * 0.1f;
        CHECK(fabsf(sinf(x) - rig.predict(x)) <= 0.05f);
    }
}

TEST("tflm: arena wystarcza z zapasem i silnik mówi ile zajął") {
    Rig rig;
    CHECK(rig.open().has_value());

    const u32 used = rig.engine.arenaUsedBytes();
    // Bez tej liczby jedynym sposobem dobrania areny jest zgadywanie w górę,
    // a każdy zgadnięty kilobajt zabiera pamięć pętli sterowania.
    CHECK(used > 0u);
    CHECK(used <= kArenaBytes);
}

TEST("tflm: za mała arena jest zgłaszana przy wczytaniu") {
    alignas(16) u8 tiny[256] = {};
    TflmEngine engine;
    engine.setOpResolver(&sineResolver());
    CHECK(engine.open(tiny, sizeof(tiny)).has_value());

    const auto loaded = engine.load(g_hello_world_float_model_data,
                                    g_hello_world_float_model_data_size);
    CHECK(!loaded.has_value());
    CHECK_EQ(loaded.error(), Err::OutOfMemory);
}

TEST("tflm: bez resolvera model się nie wczyta") {
    // Lista operatorów należy do aplikacji — to ona decyduje o rozmiarze
    // wsadu. Brak listy musi być błędem z komunikatem, a nie interpreterem,
    // który nie umie wykonać ani jednej warstwy.
    alignas(16) u8 arena[kArenaBytes] = {};
    TflmEngine engine;
    CHECK(engine.open(arena, sizeof(arena)).has_value());
    CHECK(!engine.load(g_hello_world_float_model_data,
                       g_hello_world_float_model_data_size).has_value());
}

TEST("tflm: wejście o złym rozmiarze jest odrzucane") {
    Rig rig;
    CHECK(rig.open().has_value());

    const float twoValues[2] = {1.0f, 2.0f};
    CHECK(!rig.engine.setInput(0, twoValues, sizeof(twoValues)).has_value());
}

TEST("tflm: model da się podmienić bez zamykania silnika") {
    // Podmiana modelu w locie to ta sama operacja, co podmiana skryptu:
    // ponowne `load()`. Poprzedni interpreter musi zniknąć, bo trzyma
    // wskaźniki w arenie — inaczej drugie wczytanie psuje pamięć pierwszego.
    Rig rig;
    CHECK(rig.open().has_value());
    const float before = rig.predict(1.0f);

    CHECK(rig.engine.load(g_hello_world_float_model_data,
                          g_hello_world_float_model_data_size).has_value());
    const float after = rig.predict(1.0f);

    CHECK(fabsf(before - after) < 0.0001f);
}

TEST("tflm: śmieci zamiast modelu są odrzucane, a nie wykonywane") {
    alignas(16) u8 arena[kArenaBytes] = {};
    alignas(16) u8 garbage[64] = {};
    memset(garbage, 0xAB, sizeof(garbage));

    TflmEngine engine;
    engine.setOpResolver(&sineResolver());
    CHECK(engine.open(arena, sizeof(arena)).has_value());
    CHECK(!engine.load(garbage, sizeof(garbage)).has_value());
}

TEST("tflm: zamknięty silnik nie liczy") {
    Rig rig;
    CHECK(rig.open().has_value());
    rig.engine.close();

    CHECK(!rig.engine.ready());
    CHECK(!rig.engine.invoke().has_value());
    CHECK_EQ(rig.engine.inputCount(), 0u);
}

// ---------------------------------------------------------------------------
// Szew: element potoku z prawdziwym silnikiem
// ---------------------------------------------------------------------------

TEST("tflm: element potoku bierze kształt okna z prawdziwego modelu") {
    /*
     * Dowód, że szew z etapu 0 trzyma po podmianie atrapy na TFLM.
     *
     * Element nie wie, jaki silnik dostał — pyta go o kształt wejścia i z tego
     * wylicza okno. Model sinusa ma wejście jednoelementowe, więc okno ma jedną
     * próbkę `float`; gdyby element zakładał cokolwiek o rozmiarze, ta liczba
     * by się nie zgadzała.
     */
    Rig rig;
    CHECK(rig.open().has_value());

    alignas(4) u8 windowBuf[64] = {};
    media::Inference element;
    element.setEngine(&rig.engine);
    element.setWindowBuffer(windowBuf, sizeof(windowBuf));

    media::Pipeline pipeline;
    CHECK(element.onPrepare(pipeline).has_value());

    // Model chce `float`, więc strumień `S16` musi zostać odrzucony — element
    // świadomie nie przelicza formatów.
    CHECK(!element.negotiate(0, media::MediaFormat::audio(16000, media::SampleFormat::S16, 1)).has_value());
    CHECK(element.negotiate(0, media::MediaFormat::audio(16000, media::SampleFormat::F32, 1)).has_value());
}
