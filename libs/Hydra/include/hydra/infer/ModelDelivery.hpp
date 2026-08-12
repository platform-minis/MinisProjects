#pragma once
/**
 * Hydra — model dostarczany przez sieć.
 *
 * Ten sam kształt, co dostarczanie skryptu (`script::ScriptDelivery`), i ten
 * sam magazyn (`script::ImageStore`): fragmentowanie, skrót SHA-256,
 * weryfikacja przed podmianą, dwa sloty i wycofanie. Powtórzenie tej mechaniki
 * osobno dla modelu oznaczałoby drugie miejsce, w którym trzeba pamiętać
 * o kolejności „zweryfikuj, potem przełącz" — a to jest dokładnie ta kolejność,
 * której złamanie kosztuje wizytę z programatorem.
 *
 * ## Co jest inne niż przy skrypcie
 *
 * **Model nie ma okresu próbnego liczonego błędami wykonania.** Skrypt, który
 * wywraca się w `setup()`, daje serię błędów i po `maxConsecutiveErrors`
 * wraca do poprzedniej wersji. Model albo się wczyta — wtedy działa — albo
 * nie: `AllocateTensors()` zawodzi natychmiast i wiadomo to od razu. Próba
 * jest więc jednorazowa i dzieje się przy podmianie.
 *
 * **Model bywa większy od skryptu.** Slot dobiera aplikacja; przy modelu
 * rozpoznającym słowo kluczowe to kilkadziesiąt kilobajtów, przy detekcji
 * anomalii — kilka. Dwa sloty znaczą dwa razy tyle, i to jest cena możliwości
 * wycofania.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/infer/IInferenceEngine.hpp"
#include "hydra/script/ImageStore.hpp"

namespace hydra {
namespace infer {

/**
 * Odbiór modelu i podmiana bez restartu.
 *
 * Nie jest modułem Hydry: nie ma własnego taska ani cyklu życia. Wpina się
 * w istniejące rozszerzenie protokołu — tak samo jak `ScriptDelivery` wpina
 * się w `ext/script` — a stan trzyma w `ImageStore`.
 */
class ModelDelivery {
public:
    struct Stats {
        u32 accepted = 0;   ///< modele wczytane i uruchomione
        u32 rejected = 0;   ///< odrzucone: skrót, rozmiar albo silnik odmówił
        u32 rollbacks = 0;  ///< powroty do poprzedniego modelu
    };

    /**
     * Silnik, w którym ląduje model. Wskaźnik, nie własność — silnik żyje
     * dłużej niż transfer i ma własną arenę.
     */
    void setEngine(IInferenceEngine* engine) { engine_ = engine; }

    Status configure(const script::ImageStore::Config& cfg) { return store_.configure(cfg); }

    /** Największy model, jaki magazyn przyjmie. */
    size_t capacity() const { return store_.capacity(); }

    /** Otwiera transfer. Skrót jest znany z góry — weryfikacja jest przed podmianą. */
    Status begin(size_t totalBytes, const u8 expectedSha[util::kSha256Size]) {
        return store_.beginTransfer(totalBytes, expectedSha);
    }

    Status chunk(u32 seq, CByteSpan data) { return store_.appendChunk(seq, data); }

    /**
     * Weryfikuje skrót i próbuje wczytać model do silnika.
     *
     * Przy niepowodzeniu wczytania **wraca do poprzedniego modelu**, o ile
     * jakiś był. Urządzenie, które przyjęło zepsuty model i zostało bez
     * żadnego, przestaje robić to, po co je postawiono — a nikt tego nie widzi,
     * bo transfer się „udał".
     */
    Status commit();

    /** Porzuca trwający transfer; aktywny model zostaje nietknięty. */
    void abort() { store_.abortTransfer(); }

    /** Ręczny powrót do poprzedniego modelu — dla shella diagnostycznego. */
    Status rollback();

    bool hasModel() const { return store_.active().valid(); }
    bool canRollback() const { return store_.canRollback(); }
    const Stats& stats() const { return stats_; }

private:
    /** Wczytuje wskazany obraz do silnika. */
    Status loadIntoEngine(const script::ImageRef& image);

    script::ImageStore store_{};
    IInferenceEngine*  engine_ = nullptr;
    Stats stats_{};
};

}  // namespace infer
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
