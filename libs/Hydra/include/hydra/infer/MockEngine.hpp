#pragma once
/**
 * Hydra — silnik inferencji bez modelu.
 *
 * Nie jest atrapą „na potrzeby testów", choć testy z niego korzystają. To
 * normalna ścieżka dla dwóch sytuacji:
 *
 *  1. **Zanim model powstanie.** Potok, element, budżet czasu i publikacja
 *     wyniku dają się złożyć i uruchomić, gdy modelu jeszcze nie ma. Kolejność
 *     odwrotna — najpierw model, potem miejsce dla niego — oznacza, że pierwszy
 *     model jest debugowany razem z pierwszym potokiem.
 *  2. **Punkt odniesienia.** Detekcja anomalii napisana ręcznie (`AnomalyCfg`)
 *     i nauczona odpowiadają na to samo pytanie. Żeby powiedzieć, co model
 *     wnosi, trzeba mieć obie za tym samym interfejsem.
 *
 * Domyślnie liczy energię okna (RMS) i podaje ją jako jedyne wyjście. To
 * świadomie prosta miara: dla wibracji silnika rosnąca energia jest pierwszym
 * przybliżeniem anomalii, więc wynik da się porównać z modelem, a nie tylko
 * z zerem.
 */

#include "hydra/core/Config.hpp"
#include "hydra/infer/IInferenceEngine.hpp"

namespace hydra {
namespace infer {

/**
 * Funkcja licząca wyjście z wejścia — podstawiana w testach.
 *
 * @param input   próbki wejściowe, już przeliczone na `float`
 * @param count   ile ich jest
 * @param output  gdzie zapisać wynik
 * @param outCount ile wartości wyjściowych oczekuje model
 */
using MockResponder = void (*)(const float* input, u32 count, float* output, u32 outCount);

class MockEngine : public IInferenceEngine {
public:
    MockEngine() = default;

    /**
     * Kształt „modelu". Ustawiane przed `load()`, bo `load()` w atrapie nie ma
     * czego przeczytać — a kształt musi być znany, żeby element potoku wiedział,
     * ile próbek zebrać w oknie.
     */
    void setShape(const TensorInfo& in, const TensorInfo& out) {
        input_ = in;
        output_ = out;
    }

    /** Podmienia sposób liczenia. `nullptr` przywraca energię okna. */
    void setResponder(MockResponder fn) { responder_ = fn; }

    const char* name() const override { return "mock"; }

    Status open(void* arena, size_t arenaBytes) override;
    void   close() override;
    bool   ready() const override { return ready_; }

    Status load(const void* model, size_t modelBytes) override;

    u8 inputCount() const override { return input_.valid() ? 1 : 0; }
    u8 outputCount() const override { return output_.valid() ? 1 : 0; }
    TensorInfo input(u8 index) const override { return index == 0 ? input_ : TensorInfo{}; }
    TensorInfo output(u8 index) const override { return index == 0 ? output_ : TensorInfo{}; }

    Status setInput(u8 index, const void* data, size_t bytes) override;
    Status invoke() override;
    Status readOutput(u8 index, void* data, size_t bytes) const override;

    u32 arenaUsedBytes() const override { return used_; }
    const char* error() const override { return error_; }

    /** Ile razy policzono — do sprawdzenia, czy potok naprawdę woła model. */
    u32 invocations() const { return invocations_; }

private:
    TensorInfo input_{};
    TensorInfo output_{};

    /*
     * Arena atrapy trzyma dwie tablice `float`: przeliczone wejście i wyjście.
     * Nie dlatego, że to potrzebne do liczenia energii — dlatego, że arena ma
     * być naprawdę używana. Silnik, który jej nie tyka, przepuściłby zbyt małą
     * i problem wyszedłby dopiero przy podmianie na TFLM.
     */
    float* inputBuffer_  = nullptr;
    float* outputBuffer_ = nullptr;
    void*  arena_ = nullptr;
    size_t arenaBytes_ = 0;
    u32    used_ = 0;

    MockResponder responder_ = nullptr;
    bool ready_ = false;
    bool loaded_ = false;
    u32  invocations_ = 0;
    const char* error_ = "";
};

}  // namespace infer
}  // namespace hydra
