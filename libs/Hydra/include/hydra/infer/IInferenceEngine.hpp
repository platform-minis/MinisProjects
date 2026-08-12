#pragma once
/**
 * Hydra — silnik inferencji za interfejsem.
 *
 * Ten sam szew, co `IScriptEngine`: moduł wołający model nie wie, czy pod
 * spodem stoi TensorFlow Lite Micro, coś innego, czy atrapa napisana na
 * potrzeby testu. Powód też ten sam — TFLM to kilkadziesiąt tysięcy wierszy
 * i osobna biblioteka, a bez interfejsu jedynym sposobem na jej wpięcie
 * byłoby `#if` w środku elementu potoku i dwie kopie logiki cyklu życia.
 *
 * ## Pamięć
 *
 * Silnik dostaje **arenę** — jeden ciągły bufor przydzielony przed startem,
 * z którego bierze wszystko: tensory pośrednie, plan wykonania, stan
 * operatorów. Tak działa TFLM i tak samo działa `Heap` w module skryptowym,
 * więc reguła Hydry „po `App::begin()` nie wolno alokować" zostaje spełniona
 * bez wyjątków dla tej ścieżki.
 *
 * Model **nie jest kopiowany do areny**. Wskaźnik musi przeżyć silnik, bo
 * TFLM czyta z niego wagi w trakcie wykonania — kopia oznaczałaby drugi raz
 * tę samą pamięć, a bufor modelu jest zwykle największą rzeczą w projekcie.
 *
 * ## Czas
 *
 * `invoke()` jest **blokujące** i to jest świadome. Inferencja na MCU trwa
 * od ułamka milisekundy do kilkudziesięciu i nie ma w środku miejsca, w którym
 * dałoby się ją wywłaszczyć — inaczej niż skrypt, który ma pułapkę instrukcji.
 * Za budżet odpowiada wołający: element potoku mierzy czas i zgłasza
 * przekroczenie, zamiast udawać, że model da się przerwać.
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/EventBus.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace infer {

/**
 * Typ elementu tensora.
 *
 * Krótka lista, bo tyle naprawdę wychodzi z konwersji modeli na MCU: `F32`
 * dla modeli bez kwantyzacji, `S8` dla skwantyzowanych (te dominują, bo są
 * cztery razy mniejsze i liczą się szybciej), `U8` dla starszych.
 */
enum class TensorType : u8 { None = 0, F32, S8, U8, S16, S32 };

constexpr u8 bytesPerElement(TensorType t) {
    switch (t) {
        case TensorType::F32: return 4;
        case TensorType::S32: return 4;
        case TensorType::S16: return 2;
        case TensorType::S8:  return 1;
        case TensorType::U8:  return 1;
        case TensorType::None: return 0;
    }
    return 0;
}

constexpr const char* toString(TensorType t) {
    switch (t) {
        case TensorType::F32: return "f32";
        case TensorType::S8:  return "s8";
        case TensorType::U8:  return "u8";
        case TensorType::S16: return "s16";
        case TensorType::S32: return "s32";
        case TensorType::None: return "none";
    }
    return "?";
}

/** Ile wymiarów opisujemy. Cztery starczają na obraz (N,H,W,C) i na okno próbek. */
#ifndef HYDRA_INFER_MAX_DIMS
#  define HYDRA_INFER_MAX_DIMS 4
#endif

/**
 * Kształt i skala tensora.
 *
 * `scale` i `zeroPoint` niosą kwantyzację. Bez nich liczba `-42` z wyjścia
 * modelu skwantyzowanego nie znaczy nic: dopiero `(-42 - zeroPoint) * scale`
 * jest wielkością, o którą pytał użytkownik. Zwracanie surowych bajtów
 * i zostawianie tego wołającemu oznaczałoby, że każdy wołający robi to sam —
 * i że któryś zrobi inaczej.
 */
struct TensorInfo {
    TensorType type = TensorType::None;
    u8         dims = 0;
    u16        dim[HYDRA_INFER_MAX_DIMS] = {0, 0, 0, 0};
    /** Mnożnik kwantyzacji; 0 dla tensorów zmiennoprzecinkowych. */
    float      scale = 0.0f;
    i32        zeroPoint = 0;

    bool valid() const { return type != TensorType::None && dims > 0; }

    /** Liczba elementów — iloczyn wymiarów. */
    u32 elements() const {
        if (dims == 0) return 0;
        u32 n = 1;
        for (u8 i = 0; i < dims; ++i) n *= dim[i];
        return n;
    }

    u32 bytes() const { return elements() * bytesPerElement(type); }

    /** Surowa wartość → wielkość fizyczna. Dla `F32` przejście tożsamościowe. */
    float dequantize(i32 raw) const {
        return scale > 0.0f ? (static_cast<float>(raw - zeroPoint) * scale)
                            : static_cast<float>(raw);
    }
};

/**
 * Silnik inferencji.
 *
 * Cykl życia odpowiada cyklowi modułów Hydry: `open()` przy starcie,
 * `load()` po wczytaniu modelu, potem `invoke()` w kółko, `close()` na końcu.
 * Wymiana modelu bez restartu to ponowne `load()` — tak samo jak podmiana
 * skryptu w locie.
 */
class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;

    /** Nazwa do logów i do diagnostyki: "tflm", "mock". */
    virtual const char* name() const = 0;

    /**
     * Przejmuje arenę. Bufor musi przeżyć silnik — nie jest kopiowany.
     */
    virtual Status open(void* arena, size_t arenaBytes) = 0;
    virtual void   close() = 0;
    virtual bool   ready() const = 0;

    /**
     * Wczytuje model. Bufor **musi przeżyć** do `close()` albo kolejnego
     * `load()`: silnik czyta z niego wagi w trakcie wykonania.
     */
    virtual Status load(const void* model, size_t modelBytes) = 0;

    virtual u8 inputCount() const = 0;
    virtual u8 outputCount() const = 0;
    virtual TensorInfo input(u8 index) const = 0;
    virtual TensorInfo output(u8 index) const = 0;

    /**
     * Wpisuje dane wejściowe. Rozmiar musi zgadzać się z `input(index).bytes()`
     * co do bajta — model o innym kształcie niż dane to nie jest sytuacja,
     * którą wolno domknąć obcięciem albo dopełnieniem zerami.
     */
    virtual Status setInput(u8 index, const void* data, size_t bytes) = 0;

    /** Wykonanie. Blokujące — patrz nagłówek pliku. */
    virtual Status invoke() = 0;

    virtual Status readOutput(u8 index, void* data, size_t bytes) const = 0;

    /**
     * Ile areny naprawdę zajęte po `load()`.
     *
     * Arena dobiera się doświadczalnie: za mała i model się nie wczyta, za
     * duża i zabiera pamięć pętli sterowania. Bez tej liczby jedynym sposobem
     * jest zgadywanie w górę.
     */
    virtual u32 arenaUsedBytes() const = 0;

    /** Ostatni błąd w postaci nadającej się do logu. */
    virtual const char* error() const = 0;
};

// ---------------------------------------------------------------------------
// Zdarzenia
// ---------------------------------------------------------------------------

/**
 * Werdykt modelu.
 *
 * Wynik inferencji jest **zdarzeniem, nie strumieniem**, i dlatego nie płynie
 * padem wyjściowym. „Wykryto anomalię z pewnością 0,87" zdarza się rzadko
 * i nieregularnie, a odbiorcą jest logika sterowania albo łącze do serwera —
 * nie kolejny element potoku. Ta sama droga, którą idzie `SensorAnomaly`.
 */
struct InferenceReady {
    /** Nazwa elementu, który liczył — jeden potok może mieć kilka modeli. */
    const char* element;
    /** Indeks największego wyjścia; dla modelu z jednym wyjściem zawsze 0. */
    u8    top;
    /** Wartość tego wyjścia, już po odkwantyzowaniu. */
    float score;
    /** Ile trwało `invoke()`. Bez tego nie da się dobrać budżetu. */
    u32   elapsedUs;
};

}  // namespace infer
}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::infer::InferenceReady, "infer.ready")
