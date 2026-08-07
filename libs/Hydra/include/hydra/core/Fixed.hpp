#pragma once
/**
 * Hydra — arytmetyka stałoprzecinkowa Q16.16 i alias real_t (rozdz. 9, 15).
 *
 * Na RP2040 (Cortex-M0+, brak FPU) regulatory pracują na Q16.16; implementacja
 * regulatorów jest wspólna, a typ liczbowy wybierany per platforma aliasem real_t.
 * Wszystkie operacje są constexpr i wolne od dzielenia tam, gdzie się da.
 */

#include "hydra/core/Types.hpp"

namespace hydra {

/** Liczba stałoprzecinkowa Q16.16: 16 bitów części całkowitej, 16 ułamkowej. */
class Fixed {
public:
    static constexpr int   kFracBits = 16;
    static constexpr i32   kOne      = 1 << kFracBits;

    constexpr Fixed() = default;
    /** Konstrukcja z liczby całkowitej. */
    constexpr explicit Fixed(int v) : raw_(static_cast<i32>(v) << kFracBits) {}
    /** Konstrukcja z float — dozwolona w kodzie inicjalizacyjnym, nie w pętli RT. */
    constexpr explicit Fixed(float v)
        : raw_(static_cast<i32>(v * static_cast<float>(kOne) + (v >= 0 ? 0.5f : -0.5f))) {}
    constexpr explicit Fixed(double v)
        : raw_(static_cast<i32>(v * static_cast<double>(kOne) + (v >= 0 ? 0.5 : -0.5))) {}

    /** Budowa z surowej reprezentacji Q16.16. */
    static constexpr Fixed fromRaw(i32 raw) { Fixed f; f.raw_ = raw; return f; }
    constexpr i32 raw() const { return raw_; }

    constexpr float toFloat() const { return static_cast<float>(raw_) / static_cast<float>(kOne); }
    constexpr int   toInt()   const { return raw_ >> kFracBits; }  // zaokrąglenie w dół
    /** Zaokrąglenie do najbliższej liczby całkowitej. */
    constexpr int   round()   const { return (raw_ + (kOne / 2)) >> kFracBits; }

    constexpr Fixed operator-() const { return fromRaw(-raw_); }
    constexpr Fixed operator+(Fixed o) const { return fromRaw(raw_ + o.raw_); }
    constexpr Fixed operator-(Fixed o) const { return fromRaw(raw_ - o.raw_); }

    constexpr Fixed operator*(Fixed o) const {
        const i64 p = static_cast<i64>(raw_) * static_cast<i64>(o.raw_);
        return fromRaw(static_cast<i32>(p >> kFracBits));
    }

    constexpr Fixed operator/(Fixed o) const {
        if (o.raw_ == 0) return fromRaw(raw_ >= 0 ? INT32_MAX : INT32_MIN);
        const i64 n = static_cast<i64>(raw_) << kFracBits;
        return fromRaw(static_cast<i32>(n / o.raw_));
    }

    constexpr Fixed& operator+=(Fixed o) { raw_ += o.raw_; return *this; }
    constexpr Fixed& operator-=(Fixed o) { raw_ -= o.raw_; return *this; }
    constexpr Fixed& operator*=(Fixed o) { *this = *this * o; return *this; }
    constexpr Fixed& operator/=(Fixed o) { *this = *this / o; return *this; }

    constexpr bool operator==(Fixed o) const { return raw_ == o.raw_; }
    constexpr bool operator!=(Fixed o) const { return raw_ != o.raw_; }
    constexpr bool operator< (Fixed o) const { return raw_ <  o.raw_; }
    constexpr bool operator<=(Fixed o) const { return raw_ <= o.raw_; }
    constexpr bool operator> (Fixed o) const { return raw_ >  o.raw_; }
    constexpr bool operator>=(Fixed o) const { return raw_ >= o.raw_; }

private:
    i32 raw_ = 0;
};

constexpr Fixed abs(Fixed v)              { return v < Fixed(0) ? -v : v; }
constexpr Fixed min(Fixed a, Fixed b)     { return a < b ? a : b; }
constexpr Fixed max(Fixed a, Fixed b)     { return a > b ? a : b; }
constexpr Fixed clamp(Fixed v, Fixed lo, Fixed hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float abs(float v)                 { return v < 0.0f ? -v : v; }
constexpr float min(float a, float b)     { return a < b ? a : b; }
constexpr float max(float a, float b)     { return a > b ? a : b; }
constexpr float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/**
 * Konwersje na float dostępne dla obu typów niezależnie od platformy.
 * Bez wariantu dla Fixed kod mieszający obie arytmetyki — strojenie
 * regulatora, telemetria, testy porównujące ścieżki — nie miałby jak
 * wyrazić wyniku w jednostkach fizycznych.
 */
constexpr float toFloat(Fixed v) { return v.toFloat(); }

/**
 * Typ liczbowy pętli czasu rzeczywistego. Na platformach z FPU to float,
 * na RP2040 — Q16.16. Kod regulatorów jest identyczny w obu przypadkach.
 */
#if HYDRA_HAS_FPU
using real_t = float;
constexpr real_t real(float v) { return v; }
constexpr float toFloat(float v) { return v; }
#else
using real_t = Fixed;
constexpr real_t real(float v) { return Fixed(v); }
#endif

}  // namespace hydra
