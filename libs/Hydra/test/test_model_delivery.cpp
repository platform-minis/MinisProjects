/**
 * Testy dostarczania modelu przez sieć (etap 5).
 *
 * Model wgrywany zdalnie różni się od wgranego razem z firmware jedną rzeczą,
 * i to ona jest tu sprawdzana: **można go dostać zepsutego**. Uszkodzony
 * w drodze, wygenerowany innym konwerterem, za duży na arenę. Za każdym razem
 * urządzenie musi zostać z modelem, który działa — najlepiej poprzednim.
 *
 * Urządzenie bez żadnego modelu przestaje robić to, po co je postawiono,
 * a transfer „się udał", więc nikt tego nie widzi.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/infer/ModelDelivery.hpp"
#include "hydra/infer/TflmEngine.hpp"
#include "tflm_model_sine.h"

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

using namespace hydra;
using namespace hydra::infer;

namespace {

using SineResolver = tflite::MicroMutableOpResolver<1>;

SineResolver& resolver() {
    static SineResolver r;
    static bool ready = false;
    if (!ready) { r.AddFullyConnected(); ready = true; }
    return r;
}

/** Sloty muszą pomieścić model sinusa (3164 B) z zapasem. */
constexpr size_t kSlotBytes = 4096;

struct Rig {
    alignas(16) u8 arena[4096] = {};
    alignas(16) u8 slotA[kSlotBytes] = {};
    alignas(16) u8 slotB[kSlotBytes] = {};

    TflmEngine    engine;
    ModelDelivery delivery;

    Status build() {
        engine.setOpResolver(&resolver());
        if (auto r = engine.open(arena, sizeof(arena)); !r) return r;

        delivery.setEngine(&engine);
        script::ImageStore::Config cfg;
        cfg.slotA = ByteSpan{slotA, sizeof(slotA)};
        cfg.slotB = ByteSpan{slotB, sizeof(slotB)};
        return delivery.configure(cfg);
    }

    /** Wysyła obraz w kawałkach, tak jak zrobiłby to serwer. */
    Status send(const void* image, size_t bytes, u32 chunkSize = 512) {
        u8 sha[util::kSha256Size];
        util::Sha256 hasher;
        hasher.update(CByteSpan{static_cast<const u8*>(image), bytes});
        hasher.finish(sha);

        if (auto r = delivery.begin(bytes, sha); !r) return r;

        u32 seq = 0;
        for (size_t offset = 0; offset < bytes; offset += chunkSize) {
            const size_t take = (bytes - offset) < chunkSize ? (bytes - offset) : chunkSize;
            const auto* src = static_cast<const u8*>(image) + offset;
            if (auto r = delivery.chunk(seq++, CByteSpan{src, take}); !r) return r;
        }
        return delivery.commit();
    }

    /** Sprawdza, że model w silniku liczy sinus. */
    bool works() {
        float x = 1.0f;
        float y = 0.0f;
        if (!engine.setInput(0, &x, sizeof(x)).has_value()) return false;
        if (!engine.invoke().has_value()) return false;
        if (!engine.readOutput(0, &y, sizeof(y)).has_value()) return false;
        return y > 0.7f && y < 0.95f;   // sin(1) ≈ 0,841
    }
};

}  // namespace

TEST("model/delivery: przesłany model zaczyna działać") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(!rig.delivery.hasModel());

    CHECK(rig.send(g_hello_world_float_model_data,
                   g_hello_world_float_model_data_size).has_value());

    CHECK(rig.delivery.hasModel());
    CHECK(rig.works());
    CHECK_EQ(rig.delivery.stats().accepted, 1u);
}

TEST("model/delivery: obraz uszkodzony w drodze jest odrzucany przed podmianą") {
    /*
     * Skrót sprawdzany przed czymkolwiek innym. Obraz uszkodzony wygląda dla
     * silnika jak model o nieznanym formacie, a taki komunikat prowadziłby
     * do szukania błędu w konwerterze.
     */
    Rig rig;
    CHECK(rig.build().has_value());

    u8 sha[util::kSha256Size];
    util::Sha256 hasher;
    hasher.update(CByteSpan{g_hello_world_float_model_data,
                            g_hello_world_float_model_data_size});
    hasher.finish(sha);

    CHECK(rig.delivery.begin(g_hello_world_float_model_data_size, sha).has_value());

    // Przekłamany bajt w środku — skrót się nie zgodzi.
    u8 corrupted[g_hello_world_float_model_data_size];
    memcpy(corrupted, g_hello_world_float_model_data, sizeof(corrupted));
    corrupted[100] ^= 0xFF;

    u32 seq = 0;
    for (size_t off = 0; off < sizeof(corrupted); off += 512) {
        const size_t take = (sizeof(corrupted) - off) < 512 ? (sizeof(corrupted) - off) : 512;
        CHECK(rig.delivery.chunk(seq++, CByteSpan{corrupted + off, take}).has_value());
    }

    CHECK(!rig.delivery.commit().has_value());
    CHECK(!rig.delivery.hasModel());
    CHECK_EQ(rig.delivery.stats().rejected, 1u);
}

TEST("model/delivery: model odrzucony przez silnik nie zostawia urządzenia bez modelu") {
    /*
     * Sedno etapu.
     *
     * Pierwszy model działa. Drugi jest poprawnie przesłany — skrót się zgadza
     * — ale silnik go nie wczyta, bo to nie jest model. Urządzenie musi zostać
     * z tym, co działało.
     */
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.send(g_hello_world_float_model_data,
                   g_hello_world_float_model_data_size).has_value());
    CHECK(rig.works());

    // Poprawnie przesłane śmieci: transfer bez zarzutu, treść bez sensu.
    u8 garbage[256];
    memset(garbage, 0xAB, sizeof(garbage));
    CHECK(!rig.send(garbage, sizeof(garbage)).has_value());

    // Poprzedni model wrócił i liczy dalej.
    CHECK(rig.delivery.hasModel());
    CHECK(rig.works());
    CHECK_EQ(rig.delivery.stats().rollbacks, 1u);
    CHECK_EQ(rig.delivery.stats().rejected, 1u);
}

TEST("model/delivery: podmiana działającego modelu na działający") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.send(g_hello_world_float_model_data,
                   g_hello_world_float_model_data_size).has_value());

    // Ten sam model jeszcze raz — to najprostsza podmiana, jaką da się
    // sprawdzić bez drugiego modelu w drzewie, a ścieżka jest ta sama.
    CHECK(rig.send(g_hello_world_float_model_data,
                   g_hello_world_float_model_data_size).has_value());

    CHECK(rig.works());
    CHECK_EQ(rig.delivery.stats().accepted, 2u);
    CHECK_EQ(rig.delivery.stats().rollbacks, 0u);
}

TEST("model/delivery: obraz większy niż slot jest odrzucany przed przesłaniem") {
    // Lepiej wiedzieć przed wysłaniem kilkudziesięciu kilobajtów przez łącze
    // szeregowe niż po.
    Rig rig;
    CHECK(rig.build().has_value());

    u8 sha[util::kSha256Size] = {};
    CHECK(!rig.delivery.begin(kSlotBytes * 2, sha).has_value());
}

TEST("model/delivery: porzucony transfer nie rusza aktywnego modelu") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.send(g_hello_world_float_model_data,
                   g_hello_world_float_model_data_size).has_value());

    u8 sha[util::kSha256Size] = {};
    CHECK(rig.delivery.begin(1024, sha).has_value());
    rig.delivery.abort();

    // Połączenie potrafi paść w środku transferu — to normalny stan, nie awaria.
    CHECK(rig.delivery.hasModel());
    CHECK(rig.works());
}

TEST("model/delivery: ręczne wycofanie wraca do poprzedniego modelu") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.send(g_hello_world_float_model_data,
                   g_hello_world_float_model_data_size).has_value());
    CHECK(!rig.delivery.canRollback());   // pierwszy model nie ma do czego wracać

    CHECK(rig.send(g_hello_world_float_model_data,
                   g_hello_world_float_model_data_size).has_value());
    CHECK(rig.delivery.canRollback());

    // Dla shella diagnostycznego: „ten nowy zachowuje się dziwnie, wróć".
    CHECK(rig.delivery.rollback().has_value());
    CHECK(rig.works());
}

TEST("model/delivery: bez silnika odbiór jest błędem, a nie cichym niczym") {
    ModelDelivery delivery;
    alignas(16) static u8 a[512], b[512];
    script::ImageStore::Config cfg;
    cfg.slotA = ByteSpan{a, sizeof(a)};
    cfg.slotB = ByteSpan{b, sizeof(b)};
    CHECK(delivery.configure(cfg).has_value());

    CHECK(!delivery.commit().has_value());
}
