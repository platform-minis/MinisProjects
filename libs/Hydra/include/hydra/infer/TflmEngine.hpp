#pragma once
/**
 * Hydra — silnik inferencji na TensorFlow Lite Micro.
 *
 * Jedyny plik biblioteki, który włącza TFLM, to `src/infer/TflmEngine.cpp`.
 * Ten nagłówek operuje na `void*` i buforze bajtów — dokładnie tak, jak
 * `SdlDisplay.hpp` operuje na `void*` zamiast na typach SDL, i z tego samego
 * powodu: aplikacja ma widzieć `IInferenceEngine`, a nie kilkaset nagłówków
 * TFLM w każdej jednostce kompilacji. Reguła w `tools/check_includes.sh`
 * tego pilnuje.
 *
 * ## Operatory podaje aplikacja
 *
 * TFLM wymaga listy operatorów **znanej w czasie kompilacji** — to ona
 * decyduje o rozmiarze wsadu. Model rozpoznający słowo kluczowe potrzebuje
 * czterech, model widzenia kilkunastu, a `AllOpsResolver` wciąga wszystkie
 * sto z okładem i sam waży więcej niż typowy model.
 *
 * Dlatego silnik nie tworzy resolvera. Aplikacja robi to sama:
 *
 *     static tflite::MicroMutableOpResolver<2> resolver;
 *     resolver.AddFullyConnected();
 *     resolver.AddSoftmax();
 *     engine.setOpResolver(&resolver);
 *
 * Wskaźnik jest nieprzezroczysty (`void*`), bo inaczej ten nagłówek musiałby
 * znać typ z TFLM. Zła rzecz podana tutaj kończy się natychmiastowym błędem
 * przy `load()`, a nie sieczką w wyniku.
 */

#include "hydra/core/Config.hpp"
#include "hydra/infer/IInferenceEngine.hpp"

#if HYDRA_ENABLE_INFER_TFLM

namespace hydra {
namespace infer {

/**
 * Miejsce na obiekty TFLM konstruowane w miejscu.
 *
 * `MicroInterpreter` powstaje dopiero przy `load()`, bo potrzebuje modelu —
 * a Hydra nie alokuje po starcie. Rozmiar jest sprawdzany `static_assert`-em
 * w pliku źródłowym: gdyby TFLM urósł, budowa się zatrzyma z komunikatem
 * mówiącym wprost, ile brakuje, zamiast psuć pamięć obok.
 */
#ifndef HYDRA_TFLM_INTERPRETER_STORAGE
#  define HYDRA_TFLM_INTERPRETER_STORAGE 512
#endif

class TflmEngine : public IInferenceEngine {
public:
    TflmEngine() = default;
    ~TflmEngine() override;

    /**
     * Resolver operatorów — wskaźnik na `tflite::MicroOpResolver`.
     * Musi przeżyć silnik; zwykle jest obiektem statycznym aplikacji.
     */
    void setOpResolver(void* resolver) { resolver_ = resolver; }

    const char* name() const override { return "tflm"; }

    Status open(void* arena, size_t arenaBytes) override;
    void   close() override;
    bool   ready() const override { return ready_; }

    Status load(const void* model, size_t modelBytes) override;

    u8 inputCount() const override;
    u8 outputCount() const override;
    TensorInfo input(u8 index) const override;
    TensorInfo output(u8 index) const override;

    Status setInput(u8 index, const void* data, size_t bytes) override;
    Status invoke() override;
    Status readOutput(u8 index, void* data, size_t bytes) const override;

    u32 arenaUsedBytes() const override;
    const char* error() const override { return error_; }

private:
    /** Niszczy interpreter, jeśli istnieje. Wołane przy `close()` i ponownym `load()`. */
    void destroyInterpreter();

    alignas(8) u8 interpreterStorage_[HYDRA_TFLM_INTERPRETER_STORAGE] = {};
    bool  hasInterpreter_ = false;

    void*  arena_ = nullptr;
    size_t arenaBytes_ = 0;
    void*  resolver_ = nullptr;

    bool ready_ = false;
    const char* error_ = "";
};

}  // namespace infer
}  // namespace hydra

#endif  // HYDRA_ENABLE_INFER_TFLM
