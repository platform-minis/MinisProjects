/**
 * Hydra — model dostarczany przez sieć. Patrz nagłówek po różnice wobec skryptu.
 */

#include "hydra/infer/ModelDelivery.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("model")

namespace hydra {
namespace infer {

Status ModelDelivery::loadIntoEngine(const script::ImageRef& image) {
    if (engine_ == nullptr) return fail(Err::NotInitialized);
    if (!image.valid()) return fail(Err::NotFound);
    return engine_->load(image.data, image.bytes);
}

Status ModelDelivery::commit() {
    if (engine_ == nullptr) {
        HYDRA_LOGE("brak silnika — ustaw setEngine() przed odbiorem modelu");
        return fail(Err::NotInitialized);
    }

    // Skrót sprawdzany przed czymkolwiek innym: obraz uszkodzony w drodze
    // wygląda dla silnika jak model o nieznanym formacie, a komunikat
    // prowadziłby wtedy do szukania błędu w konwerterze.
    if (auto r = store_.verifyStaged(); !r) {
        ++stats_.rejected;
        HYDRA_LOGE("skrót się nie zgadza — obraz uszkodzony w drodze");
        return r;
    }

    /*
     * Zapamiętanie poprzedniego modelu **przed** przełączeniem.
     *
     * `activateStaged()` przesuwa sloty: to, co było aktywne, staje się
     * poprzednim. Gdyby wczytanie nowego zawiodło, wracamy tam — ale referencję
     * trzeba wziąć wcześniej, bo po nieudanym wczytaniu silnik nie ma już
     * żadnego modelu i nie ma czego pytać.
     */
    const bool hadPrevious = store_.active().valid();

    const auto activated = store_.activateStaged();
    if (!activated) {
        ++stats_.rejected;
        return fail(activated.error());
    }

    if (auto r = loadIntoEngine(activated.value()); !r) {
        HYDRA_LOGE("silnik odrzucił model: %s", engine_->error());
        ++stats_.rejected;

        if (hadPrevious) {
            // Urządzenie bez żadnego modelu przestaje robić to, po co je
            // postawiono — a transfer „się udał", więc nikt tego nie widzi.
            const auto restored = store_.rollback();
            if (restored && loadIntoEngine(restored.value()).has_value()) {
                ++stats_.rollbacks;
                HYDRA_LOGW("wrócono do poprzedniego modelu");
            } else {
                HYDRA_LOGE("powrót do poprzedniego modelu też się nie powiódł");
            }
        }
        return r;
    }

    ++stats_.accepted;
    HYDRA_LOGI("model przyjęty: %u B, arena %u B",
               static_cast<unsigned>(activated.value().bytes),
               static_cast<unsigned>(engine_->arenaUsedBytes()));
    return ok();
}

Status ModelDelivery::rollback() {
    if (!store_.canRollback()) return fail(Err::NotFound);

    const auto restored = store_.rollback();
    if (!restored) return fail(restored.error());

    if (auto r = loadIntoEngine(restored.value()); !r) {
        HYDRA_LOGE("poprzedni model nie wczytał się: %s", engine_->error());
        return r;
    }
    ++stats_.rollbacks;
    return ok();
}

}  // namespace infer
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
