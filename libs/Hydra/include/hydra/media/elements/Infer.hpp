#pragma once
/**
 * Hydra — inferencja jako element potoku.
 *
 * Ujście: przyjmuje strumień, nie oddaje nic. Wynik idzie zdarzeniem
 * `InferenceReady`, bo werdykt modelu zdarza się rzadko i nieregularnie,
 * a jego odbiorcą jest logika sterowania albo łącze do serwera — nie kolejny
 * element. Ta sama droga, którą idzie `SensorAnomaly`.
 *
 * ## Dlaczego wejściem jest audio, a nie osobny „tensor"
 *
 * Model dostaje okno liczb. Próbki mikrofonu i odczyty prądu z INA219 są
 * jednym i tym samym: ciągiem wartości w czasie, o znanej częstotliwości.
 * Dokładanie `MediaKind::Tensor` zanim pojawi się coś, co naprawdę nie jest
 * strumieniem w czasie — a takim czymś będą dopiero cechy MFCC — byłoby
 * wyprzedzaniem faktów i drugim formatem do negocjowania w każdym elemencie.
 *
 * ## Okno i przesuw
 *
 * Model ma stały kształt wejścia, a bloki przychodzą w rozmiarze wygodnym dla
 * źródła — te dwie liczby nie mają powodu być równe. Element zbiera próbki
 * w oknie i liczy dopiero, gdy jest pełne. `hop` mówi, o ile okno przesuwa się
 * po każdej inferencji: równy oknu daje ciąg rozłączny, mniejszy — zakładkę,
 * bez której krótkie zdarzenie potrafi wypaść na styku dwóch okien.
 *
 * ## Bufor okna
 *
 * Podawany z zewnątrz, tak samo jak arena silnika. Element nie alokuje —
 * to reguła Hydry, nie preferencja. Aplikacja i tak musi zdecydować, czy
 * bufor ma leżeć w pamięci wewnętrznej, czy może w PSRAM.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/infer/IInferenceEngine.hpp"
#include "hydra/media/Element.hpp"

/**
 * Ile wyjść modelu element potrafi obejrzeć.
 *
 * Bufor stoi na stosie `process()`, więc rozmiar musi być znany. Trzydzieści
 * dwie klasy starczają na rozpoznawanie słów kluczowych i na klasyfikację
 * stanów maszyny; model o większym wyjściu jest odrzucany przy `prepare()`
 * z jasnym komunikatem, a nie obcinany po cichu.
 */
#ifndef HYDRA_INFER_MAX_OUTPUTS
#  define HYDRA_INFER_MAX_OUTPUTS 32
#endif

namespace hydra {
namespace media {

class Inference : public Element {
public:
    Inference() : Element("infer") {}

    /**
     * Silnik. Wskaźnik, nie własność: silnik tworzy aplikacja, bo to ona wie,
     * gdzie leży arena i model.
     */
    void setEngine(infer::IInferenceEngine* engine) { engine_ = engine; }
    infer::IInferenceEngine* engine() const { return engine_; }

    /**
     * Bufor na okno. Musi pomieścić `engine->input(0).bytes()`; sprawdzane
     * w `onPrepare()`, kiedy kształt modelu jest już znany.
     */
    void setWindowBuffer(void* buffer, u32 bytes) {
        window_ = static_cast<u8*>(buffer);
        windowCapacity_ = bytes;
    }

    /**
     * O ile próbek przesunąć okno po inferencji. Zero znaczy „o całe okno".
     */
    void setHopSamples(u32 hop) { hop_ = hop; }
    u32  hopSamples() const { return hop_; }

    /**
     * Budżet czasu na jedno `invoke()`. Przekroczenie nie przerywa liczenia —
     * nie da się go przerwać — ale jest liczone i zgłaszane jako usterka
     * potoku, żeby nie ginęło w statystykach.
     */
    void setBudgetUs(u32 us) { budgetUs_ = us; }

    u8 inputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

    /** Ile razy model policzył — potok, który nie woła modelu, wygląda tak samo jak sprawny. */
    u32 inferences() const { return inferences_; }
    /** Ile razy `invoke()` przekroczył budżet. */
    u32 overruns() const { return overruns_; }
    /** Najdłuższe `invoke()` w mikrosekundach. */
    u32 worstUs() const { return worstUs_; }
    /** Ostatni werdykt — ten sam, który poszedł zdarzeniem. */
    const infer::InferenceReady& last() const { return last_; }

private:
    /** Dokłada próbki z bloku do okna; zwraca, ile bajtów przyjęto. */
    u32 fill(const u8* data, u32 bytes);
    /** Liczy i publikuje wynik. */
    void run();
    /** Przesuwa okno o `hop` i zostawia resztę na początku. */
    void slide();

    Pipeline* pipeline_ = nullptr;
    infer::IInferenceEngine* engine_ = nullptr;

    u8* window_ = nullptr;
    u32 windowCapacity_ = 0;
    /** Ile bajtów okna jest już wypełnione. */
    u32 filled_ = 0;
    /** Rozmiar pełnego okna w bajtach — z kształtu modelu. */
    u32 windowBytes_ = 0;
    /** Bajty jednej próbki wejścia modelu. */
    u8  elementBytes_ = 0;

    u32 hop_ = 0;
    u32 budgetUs_ = 0;

    u32 inferences_ = 0;
    u32 overruns_ = 0;
    u32 worstUs_ = 0;
    infer::InferenceReady last_{"infer", 0, 0.0f, 0};
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
