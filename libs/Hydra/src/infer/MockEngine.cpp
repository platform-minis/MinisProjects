/**
 * Hydra — silnik inferencji bez modelu. Patrz nagłówek po powód istnienia.
 */

#include "hydra/infer/MockEngine.hpp"

#include "hydra/core/Log.hpp"
#include "hydra/core/RealMath.hpp"

HYDRA_LOG_MODULE("infer")

namespace hydra {
namespace infer {
namespace {

/**
 * Domyślne „wnioskowanie": pierwiastek ze średniej kwadratów okna.
 *
 * Dla wibracji silnika rosnąca energia jest pierwszym przybliżeniem anomalii,
 * więc wynik da się zestawić z modelem — inaczej niż stała, która mówiłaby
 * tylko tyle, że potok dowiózł dane.
 */
void energyOfWindow(const float* input, u32 count, float* output, u32 outCount) {
    if (outCount == 0) return;

    float sum = 0.0f;
    for (u32 i = 0; i < count; ++i) sum += input[i] * input[i];
    output[0] = count > 0 ? sqrtReal(sum / static_cast<float>(count)) : 0.0f;

    // Pozostałe wyjścia zerujemy: model o kilku wyjściach dostanie z atrapy
    // wartości określone, a nie zawartość areny sprzed chwili.
    for (u32 i = 1; i < outCount; ++i) output[i] = 0.0f;
}

}  // namespace

Status MockEngine::open(void* arena, size_t arenaBytes) {
    if (arena == nullptr || arenaBytes == 0) {
        error_ = "arena jest pusta";
        return fail(Err::BadArgument);
    }
    arena_ = arena;
    arenaBytes_ = arenaBytes;
    ready_ = true;
    error_ = "";
    return ok();
}

void MockEngine::close() {
    arena_ = nullptr;
    arenaBytes_ = 0;
    inputBuffer_ = nullptr;
    outputBuffer_ = nullptr;
    used_ = 0;
    ready_ = false;
    loaded_ = false;
}

/**
 * „Wczytanie modelu" sprowadza się do rozdania areny.
 *
 * Bufor modelu jest ignorowany — atrapa nie ma czego z niego przeczytać. Nie
 * jest jednak ignorowany kształt: bez `setShape()` element potoku nie wie, ile
 * próbek zebrać, więc brak kształtu musi być błędem, a nie cichym zerem.
 */
Status MockEngine::load(const void* model, size_t modelBytes) {
    HYDRA_UNUSED(model);
    HYDRA_UNUSED(modelBytes);

    if (!ready_) {
        error_ = "silnik nie jest otwarty";
        return fail(Err::NotInitialized);
    }
    if (!input_.valid() || !output_.valid()) {
        error_ = "nie podano kształtu wejścia i wyjścia (setShape)";
        return fail(Err::BadArgument);
    }

    const size_t needed = (static_cast<size_t>(input_.elements()) +
                           static_cast<size_t>(output_.elements())) * sizeof(float);
    if (needed > arenaBytes_) {
        // Ten sam komunikat, który poda TFLM przy za małej arenie — i ta sama
        // droga naprawy, więc niech wygląda tak samo już teraz.
        HYDRA_LOGE("arena za mała: potrzeba %u B, jest %u B",
                   static_cast<unsigned>(needed), static_cast<unsigned>(arenaBytes_));
        error_ = "arena za mała";
        return fail(Err::OutOfMemory);
    }

    inputBuffer_  = static_cast<float*>(arena_);
    outputBuffer_ = inputBuffer_ + input_.elements();
    used_ = static_cast<u32>(needed);
    loaded_ = true;
    error_ = "";
    return ok();
}

Status MockEngine::setInput(u8 index, const void* data, size_t bytes) {
    if (!loaded_ || index != 0) {
        error_ = "brak wczytanego modelu";
        return fail(Err::NotInitialized);
    }
    if (bytes != input_.bytes()) {
        // Rozmiar co do bajta: dane o innym kształcie niż model to nie jest
        // sytuacja, którą wolno domknąć obcięciem.
        error_ = "rozmiar wejścia nie zgadza się z kształtem";
        return fail(Err::BadArgument);
    }

    // Przeliczenie na `float` dzieje się tutaj, żeby liczące funkcje nie
    // musiały znać kwantyzacji — dokładnie tak, jak nie będzie musiał jej znać
    // model w TFLM, bo tam zajmie się nią silnik.
    const u32 count = input_.elements();
    switch (input_.type) {
        case TensorType::F32: {
            const float* src = static_cast<const float*>(data);
            for (u32 i = 0; i < count; ++i) inputBuffer_[i] = src[i];
            break;
        }
        case TensorType::S16: {
            const i16* src = static_cast<const i16*>(data);
            for (u32 i = 0; i < count; ++i) inputBuffer_[i] = input_.dequantize(src[i]);
            break;
        }
        case TensorType::S8: {
            const i8* src = static_cast<const i8*>(data);
            for (u32 i = 0; i < count; ++i) inputBuffer_[i] = input_.dequantize(src[i]);
            break;
        }
        case TensorType::U8: {
            const u8* src = static_cast<const u8*>(data);
            for (u32 i = 0; i < count; ++i) inputBuffer_[i] = input_.dequantize(src[i]);
            break;
        }
        default:
            error_ = "typ wejścia nieobsługiwany przez atrapę";
            return fail(Err::NotSupported);
    }
    return ok();
}

Status MockEngine::invoke() {
    if (!loaded_) {
        error_ = "brak wczytanego modelu";
        return fail(Err::NotInitialized);
    }

    const MockResponder fn = responder_ ? responder_ : &energyOfWindow;
    fn(inputBuffer_, input_.elements(), outputBuffer_, output_.elements());

    ++invocations_;
    return ok();
}

Status MockEngine::readOutput(u8 index, void* data, size_t bytes) const {
    if (!loaded_ || index != 0) return fail(Err::NotInitialized);
    if (bytes != output_.bytes()) return fail(Err::BadArgument);

    const u32 count = output_.elements();
    switch (output_.type) {
        case TensorType::F32: {
            float* dst = static_cast<float*>(data);
            for (u32 i = 0; i < count; ++i) dst[i] = outputBuffer_[i];
            return ok();
        }
        case TensorType::S8: {
            // Droga powrotna przez kwantyzację — wołający dostaje surowe
            // wartości i sam je przeliczy przez TensorInfo, tak samo jak
            // z prawdziwego modelu.
            i8* dst = static_cast<i8*>(data);
            for (u32 i = 0; i < count; ++i) {
                const float scaled = output_.scale > 0.0f
                    ? outputBuffer_[i] / output_.scale + static_cast<float>(output_.zeroPoint)
                    : outputBuffer_[i];
                dst[i] = static_cast<i8>(scaled < -128.0f ? -128.0f : (scaled > 127.0f ? 127.0f : scaled));
            }
            return ok();
        }
        default:
            return fail(Err::NotSupported);
    }
}

}  // namespace infer
}  // namespace hydra
