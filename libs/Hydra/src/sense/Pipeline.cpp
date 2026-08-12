/** Hydra — implementacja kalibracji i detekcji anomalii (rozdz. 8). */

#include "hydra/sense/Pipeline.hpp"

#if HYDRA_ENABLE_SENSE

#include <stdio.h>
#include <string.h>

#include "hydra/core/Log.hpp"
#include "hydra/hal/Hal.hpp"

HYDRA_LOG_MODULE("sense")

namespace hydra {
namespace sense {
namespace {

/**
 * Buduje klucz kalibracji. Klucze w NVS mają twardy limit 15 znaków, więc
 * długie nazwy czujników są przycinane — kolizja jest tu mniej groźna niż
 * cicho odrzucony zapis.
 */
void calKey(const char* sensorName, char* out, size_t cap) {
    snprintf(out, cap, "c.%.*s", static_cast<int>(cap - 3), sensorName ? sensorName : "?");
}

float absf(float v) { return v < 0.0f ? -v : v; }

}  // namespace

// ---------------------------------------------------------------------------
// Kalibracja
// ---------------------------------------------------------------------------

void Calibration::apply(const SensorCal& cal, Sample& s) {
    for (u8 i = 0; i < s.n && i < kMaxChannels; ++i) {
        s.value[i] = (s.value[i] + cal.ch[i].offset) * cal.ch[i].gain;
    }
}

Status Calibration::load(const char* sensorName, SensorCal& out) {
    out = SensorCal{};  // współczynniki neutralne, gdyby odczyt się nie udał

    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kCalibrationNamespace, true));

    char key[hal::kStorageKeyMax + 1];
    calKey(sensorName, key, sizeof(key));

    SensorCal loaded;
    auto r = storage.getBlob(key, ByteSpan{reinterpret_cast<u8*>(&loaded), sizeof(loaded)});
    if (!r) {
        // Brak wpisu to normalny stan świeżego urządzenia, nie awaria.
        return r.error() == Err::NotFound ? ok() : fail(r.error());
    }
    if (*r != sizeof(loaded)) return fail(Err::Protocol);

    out = loaded;
    return ok();
}

Status Calibration::save(const char* sensorName, const SensorCal& cal) {
    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kCalibrationNamespace, false));

    char key[hal::kStorageKeyMax + 1];
    calKey(sensorName, key, sizeof(key));

    HYDRA_CHECK(storage.setBlob(
        key, CByteSpan{reinterpret_cast<const u8*>(&cal), sizeof(cal)}));
    return storage.commit();
}

Status Calibration::erase(const char* sensorName) {
    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kCalibrationNamespace, false));

    char key[hal::kStorageKeyMax + 1];
    calKey(sensorName, key, sizeof(key));

    HYDRA_CHECK(storage.erase(key));
    return storage.commit();
}

// ---------------------------------------------------------------------------
// Detekcja anomalii
// ---------------------------------------------------------------------------

void AnomalyDetector::configure(const AnomalyCfg& cfg) {
    cfg_ = cfg;
    reset();
}

void AnomalyDetector::reset() {
    primed_ = false;
    for (u8 i = 0; i < kMaxChannels; ++i) {
        last_[i]      = 0.0f;
        sameCount_[i] = 0;
    }
}

AnomalyDetector::Hit AnomalyDetector::check(const Sample& s) {
    Hit hit;

    for (u8 i = 0; i < s.n && i < kMaxChannels; ++i) {
        const float v = s.value[i];

        // 1. Zakres — najtańsza i najpewniejsza kontrola.
        if (cfg_.minValue != cfg_.maxValue && (v < cfg_.minValue || v > cfg_.maxValue)) {
            if (hit.kind == AnomalyKind::None) hit = Hit{AnomalyKind::OutOfRange, i, v};
        }

        if (primed_) {
            // 2. Skok — zmiana większa niż fizycznie możliwa w jednym okresie.
            if (cfg_.spikeDelta > 0.0f && absf(v - last_[i]) > cfg_.spikeDelta) {
                if (hit.kind == AnomalyKind::None) hit = Hit{AnomalyKind::Spike, i, v};
            }

            // 3. Zamrożenie — czujnik odpowiada, ale wartość nie drgnie.
            // Porównanie dokładne jest tu właściwe: zawieszony układ zwraca
            // identyczny bajt po bajcie odczyt, a nie wartość „prawie taką samą".
            if (v == last_[i]) {
                if (sameCount_[i] < 0xFFFF) ++sameCount_[i];
            } else {
                sameCount_[i] = 0;
            }

            if (cfg_.frozenLimit > 0 && sameCount_[i] >= cfg_.frozenLimit) {
                if (hit.kind == AnomalyKind::None) hit = Hit{AnomalyKind::Frozen, i, v};
            }
        }

        last_[i] = v;
    }

    primed_ = true;
    return hit;
}

// ---------------------------------------------------------------------------
// Detekcja nauczona
// ---------------------------------------------------------------------------

Status ModelDetector::configure(const Config& cfg) {
    if (engine_ == nullptr) {
        HYDRA_LOGE("brak silnika — ustaw setEngine() przed configure()");
        return fail(Err::NotInitialized);
    }
    if (cfg.channel >= kMaxChannels) return fail(Err::BadArgument);

    const infer::TensorInfo in = engine_->input(0);
    if (!in.valid()) {
        HYDRA_LOGE("silnik nie podaje kształtu wejścia — czy model jest wczytany?");
        return fail(Err::NotInitialized);
    }
    if (in.type != infer::TensorType::F32) {
        // Detektor karmi model próbkami po kalibracji i filtrze, czyli
        // liczbami zmiennoprzecinkowymi. Model skwantyzowany wymagałby skali,
        // którą trzeba znać przy uczeniu — a wtedy to nie jest już to samo
        // wejście, tylko inne.
        HYDRA_LOGE("model chce %s, a próbki czujnika są zmiennoprzecinkowe",
                   infer::toString(in.type));
        return fail(Err::NotSupported);
    }

    windowSamples_ = in.elements();
    if (window_ == nullptr || capacity_ < windowSamples_) {
        // Rozmiar okna wynika z modelu, więc aplikacja nie zna go przed
        // wczytaniem — sprawdzamy tutaj, a nie przy podawaniu bufora.
        HYDRA_LOGE("bufor okna ma %u próbek, a model potrzebuje %u",
                   static_cast<unsigned>(capacity_), static_cast<unsigned>(windowSamples_));
        return fail(Err::OutOfMemory);
    }

    cfg_ = cfg;
    if (cfg_.hopSamples == 0) cfg_.hopSamples = static_cast<u16>(windowSamples_);
    filled_ = 0;
    return ok();
}

void ModelDetector::reset() {
    filled_ = 0;
    lastScore_ = 0.0f;
}

AnomalyDetector::Hit ModelDetector::feed(const Sample& s) {
    AnomalyDetector::Hit hit;
    if (engine_ == nullptr || window_ == nullptr || windowSamples_ == 0) return hit;
    if (cfg_.channel >= s.n) return hit;

    window_[filled_++] = s.value[cfg_.channel];
    if (filled_ < windowSamples_) return hit;

    if (auto r = engine_->setInput(0, window_, windowSamples_ * sizeof(float)); !r) {
        HYDRA_LOGE("setInput: %s", engine_->error());
        filled_ = 0;
        return hit;
    }
    if (auto r = engine_->invoke(); !r) {
        HYDRA_LOGE("invoke: %s", engine_->error());
        filled_ = 0;
        return hit;
    }

    const infer::TensorInfo out = engine_->output(0);
    float score = 0.0f;
    if (out.elements() == 1 && out.type == infer::TensorType::F32) {
        (void)engine_->readOutput(0, &score, sizeof(score));
    } else {
        // Model o wielu wyjściach to klasyfikator, a nie miara nietypowości —
        // detektor czujnika nie ma jak przypisać jego klasom znaczenia.
        // Taki model należy do elementu potoku, nie tutaj.
        HYDRA_LOGW("model ma %u wyjść — detektor czujnika oczekuje jednej liczby",
                   static_cast<unsigned>(out.elements()));
    }

    lastScore_ = score;
    ++evaluations_;

    // Przesuw okna: zakładka pozwala zareagować szybciej niż raz na pełne
    // okno, kosztem tylu inferencji, ile razy okna na siebie zachodzą.
    if (cfg_.hopSamples >= filled_) {
        filled_ = 0;
    } else {
        const u32 keep = filled_ - cfg_.hopSamples;
        memmove(window_, window_ + cfg_.hopSamples, keep * sizeof(float));
        filled_ = keep;
    }

    if (score > cfg_.threshold) {
        hit.kind = AnomalyKind::Learned;
        hit.channel = cfg_.channel;
        // Wartością jest wynik modelu, nie próbka: „0,87" mówi, jak bardzo
        // przebieg odstaje, a ostatnia próbka nie mówi o oknie nic.
        hit.value = score;
    }
    return hit;
}

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
