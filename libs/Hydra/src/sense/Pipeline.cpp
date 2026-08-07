/** Hydra — implementacja kalibracji i detekcji anomalii (rozdz. 8). */

#include "hydra/sense/Pipeline.hpp"

#if HYDRA_ENABLE_SENSE

#include <stdio.h>
#include <string.h>

#include "hydra/hal/Hal.hpp"

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

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
