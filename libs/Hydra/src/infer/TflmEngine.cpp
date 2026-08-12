/**
 * Hydra — silnik inferencji na TensorFlow Lite Micro.
 *
 * Jedyny plik w bibliotece, który włącza TFLM. Patrz nagłówek po powód.
 */

#include "hydra/infer/TflmEngine.hpp"

#if HYDRA_ENABLE_INFER_TFLM

#include <string.h>

#include "hydra/core/Log.hpp"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

HYDRA_LOG_MODULE("tflm")

namespace hydra {
namespace infer {
namespace {

using Interpreter = tflite::MicroInterpreter;

static_assert(sizeof(Interpreter) <= HYDRA_TFLM_INTERPRETER_STORAGE,
              "HYDRA_TFLM_INTERPRETER_STORAGE za małe dla tflite::MicroInterpreter — "
              "podnieś stałą w TflmEngine.hpp");
static_assert(alignof(Interpreter) <= 8, "MicroInterpreter wymaga silniejszego wyrównania");

/** Typ TFLM → typ Hydry. Nieznany zostaje `None` i jest odrzucany wyżej. */
TensorType fromTflite(TfLiteType type) {
    switch (type) {
        case kTfLiteFloat32: return TensorType::F32;
        case kTfLiteInt8:    return TensorType::S8;
        case kTfLiteUInt8:   return TensorType::U8;
        case kTfLiteInt16:   return TensorType::S16;
        case kTfLiteInt32:   return TensorType::S32;
        default:             return TensorType::None;
    }
}

/**
 * Opis tensora TFLM → `TensorInfo`.
 *
 * Kwantyzacja przechodzi razem z kształtem, bo bez niej wyjście modelu jest
 * ciągiem liczb bez jednostki. TFLM trzyma ją per tensor w `params`.
 */
TensorInfo describe(const TfLiteTensor* tensor) {
    TensorInfo info;
    if (tensor == nullptr || tensor->dims == nullptr) return info;

    info.type = fromTflite(tensor->type);
    const int dims = tensor->dims->size;
    info.dims = static_cast<u8>(dims > HYDRA_INFER_MAX_DIMS ? HYDRA_INFER_MAX_DIMS : dims);
    for (u8 i = 0; i < info.dims; ++i) {
        info.dim[i] = static_cast<u16>(tensor->dims->data[i]);
    }
    info.scale = tensor->params.scale;
    info.zeroPoint = tensor->params.zero_point;
    return info;
}

}  // namespace

TflmEngine::~TflmEngine() { destroyInterpreter(); }

void TflmEngine::destroyInterpreter() {
    if (!hasInterpreter_) return;
    reinterpret_cast<Interpreter*>(interpreterStorage_)->~MicroInterpreter();
    hasInterpreter_ = false;
}

Status TflmEngine::open(void* arena, size_t arenaBytes) {
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

void TflmEngine::close() {
    destroyInterpreter();
    arena_ = nullptr;
    arenaBytes_ = 0;
    ready_ = false;
}

Status TflmEngine::load(const void* model, size_t modelBytes) {
    HYDRA_UNUSED(modelBytes);

    if (!ready_) {
        error_ = "silnik nie jest otwarty";
        return fail(Err::NotInitialized);
    }
    if (model == nullptr) {
        error_ = "brak modelu";
        return fail(Err::BadArgument);
    }
    if (resolver_ == nullptr) {
        // Lista operatorów należy do aplikacji — patrz nagłówek. Bez niej
        // interpreter nie ma czym wykonać ani jednej warstwy.
        error_ = "nie podano resolvera operatorów (setOpResolver)";
        return fail(Err::NotInitialized);
    }

    // Ponowne wczytanie to podmiana modelu bez restartu — poprzedni
    // interpreter musi zniknąć, bo trzyma wskaźniki w arenie.
    destroyInterpreter();

    const tflite::Model* parsed = tflite::GetModel(model);
    if (parsed == nullptr || parsed->version() != TFLITE_SCHEMA_VERSION) {
        // Najczęstsza przyczyna: plik `.tflite` z nowszego konwertera niż
        // wersja TFLM w drzewie. Komunikat mówi to wprost, bo objawem jest
        // inaczej „model się nie wczytał" bez wskazówki.
        HYDRA_LOGE("wersja schematu modelu inna niż %d — przekonwertuj model ponownie",
                   TFLITE_SCHEMA_VERSION);
        error_ = "niezgodna wersja schematu modelu";
        return fail(Err::NotSupported);
    }

    auto* resolver = static_cast<const tflite::MicroOpResolver*>(resolver_);
    auto* interpreter = new (interpreterStorage_) Interpreter(
        parsed, *resolver, static_cast<uint8_t*>(arena_), arenaBytes_);
    hasInterpreter_ = true;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        // Dwie przyczyny i obie warto rozróżnić w logu: za mała arena albo
        // operator, którego nie ma w resolverze. TFLM mówi o tym na swoim
        // wyjściu diagnostycznym, więc tu dokładamy tylko wskazówkę.
        HYDRA_LOGE("AllocateTensors nie powiodło się — za mała arena (%u B) "
                   "albo brak operatora w resolverze",
                   static_cast<unsigned>(arenaBytes_));
        destroyInterpreter();
        error_ = "nie udało się przydzielić tensorów";
        return fail(Err::OutOfMemory);
    }

    HYDRA_LOGI("model wczytany: %u wejść, %u wyjść, arena %u/%u B",
               static_cast<unsigned>(interpreter->inputs_size()),
               static_cast<unsigned>(interpreter->outputs_size()),
               static_cast<unsigned>(interpreter->arena_used_bytes()),
               static_cast<unsigned>(arenaBytes_));
    error_ = "";
    return ok();
}

u8 TflmEngine::inputCount() const {
    if (!hasInterpreter_) return 0;
    const auto* interpreter = reinterpret_cast<const Interpreter*>(interpreterStorage_);
    return static_cast<u8>(const_cast<Interpreter*>(interpreter)->inputs_size());
}

u8 TflmEngine::outputCount() const {
    if (!hasInterpreter_) return 0;
    const auto* interpreter = reinterpret_cast<const Interpreter*>(interpreterStorage_);
    return static_cast<u8>(const_cast<Interpreter*>(interpreter)->outputs_size());
}

TensorInfo TflmEngine::input(u8 index) const {
    if (!hasInterpreter_) return TensorInfo{};
    auto* interpreter = const_cast<Interpreter*>(
        reinterpret_cast<const Interpreter*>(interpreterStorage_));
    return describe(interpreter->input(index));
}

TensorInfo TflmEngine::output(u8 index) const {
    if (!hasInterpreter_) return TensorInfo{};
    auto* interpreter = const_cast<Interpreter*>(
        reinterpret_cast<const Interpreter*>(interpreterStorage_));
    return describe(interpreter->output(index));
}

Status TflmEngine::setInput(u8 index, const void* data, size_t bytes) {
    if (!hasInterpreter_) {
        error_ = "brak wczytanego modelu";
        return fail(Err::NotInitialized);
    }

    auto* interpreter = reinterpret_cast<Interpreter*>(interpreterStorage_);
    TfLiteTensor* tensor = interpreter->input(index);
    if (tensor == nullptr) {
        error_ = "nie ma takiego wejścia";
        return fail(Err::OutOfRange);
    }
    if (bytes != tensor->bytes) {
        // Co do bajta — model o innym kształcie niż dane to nie jest sytuacja
        // do domknięcia obcięciem albo dopełnieniem zerami.
        error_ = "rozmiar wejścia nie zgadza się z kształtem modelu";
        return fail(Err::BadArgument);
    }

    // Kopiujemy do bufora tensora w arenie. Podmiana wskaźnika byłaby szybsza,
    // ale TFLM zakłada, że bufor wejścia leży w arenie i tam go planuje —
    // wskaźnik z zewnątrz przeżyłby tylko do pierwszej realokacji.
    memcpy(tensor->data.raw, data, bytes);
    return ok();
}

Status TflmEngine::invoke() {
    if (!hasInterpreter_) {
        error_ = "brak wczytanego modelu";
        return fail(Err::NotInitialized);
    }

    auto* interpreter = reinterpret_cast<Interpreter*>(interpreterStorage_);
    if (interpreter->Invoke() != kTfLiteOk) {
        error_ = "Invoke zwrócił błąd";
        return fail(Err::Internal);
    }
    return ok();
}

Status TflmEngine::readOutput(u8 index, void* data, size_t bytes) const {
    if (!hasInterpreter_) return fail(Err::NotInitialized);

    auto* interpreter = const_cast<Interpreter*>(
        reinterpret_cast<const Interpreter*>(interpreterStorage_));
    const TfLiteTensor* tensor = interpreter->output(index);
    if (tensor == nullptr) return fail(Err::OutOfRange);
    if (bytes != tensor->bytes) return fail(Err::BadArgument);

    memcpy(data, tensor->data.raw, bytes);
    return ok();
}

u32 TflmEngine::arenaUsedBytes() const {
    if (!hasInterpreter_) return 0;
    const auto* interpreter = reinterpret_cast<const Interpreter*>(interpreterStorage_);
    return static_cast<u32>(interpreter->arena_used_bytes());
}

}  // namespace infer
}  // namespace hydra

#endif  // HYDRA_ENABLE_INFER_TFLM
