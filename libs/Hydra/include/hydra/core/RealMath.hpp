#pragma once
/**
 * Hydra — funkcje trygonometryczne dla typu real_t.
 *
 * Odometria potrzebuje sinusa i cosinusa, a RP2040 nie ma nie tylko jednostki
 * zmiennoprzecinkowej, ale i tablic funkcji przestępnych. Zamiast wykluczać
 * tę platformę albo przeliczać odometrię na float (co kosztowałoby więcej niż
 * cała reszta pętli), Hydra dostarcza przybliżenie stałoprzecinkowe.
 *
 * Użyty wielomian daje błąd poniżej 0,001 na całym zakresie — przy odometrii
 * kołowej, gdzie pojedynczy krok obraca pojazd o ułamek stopnia, jest to
 * o rząd wielkości mniej niż niepewność samego poślizgu kół.
 */

#include <math.h>

#include "hydra/core/Fixed.hpp"

namespace hydra {

constexpr float kPi     = 3.14159265f;
constexpr float kTwoPi  = 6.28318531f;
constexpr float kHalfPi = 1.57079633f;

// --- wariant zmiennoprzecinkowy --------------------------------------------

inline float sinReal(float x) { return sinf(x); }
inline float cosReal(float x) { return cosf(x); }
inline float sqrtReal(float x) { return x > 0.0f ? sqrtf(x) : 0.0f; }

/** Sprowadza kąt do przedziału [-π, π]. */
inline float wrapAngle(float x) {
    while (x > kPi) x -= kTwoPi;
    while (x < -kPi) x += kTwoPi;
    return x;
}

// --- wariant stałoprzecinkowy ----------------------------------------------

/** Sprowadza kąt do przedziału [-π, π]. */
inline Fixed wrapAngle(Fixed x) {
    const Fixed pi(kPi), twoPi(kTwoPi);
    while (x > pi) x -= twoPi;
    while (x < -pi) x += twoPi;
    return x;
}

/**
 * Sinus w arytmetyce Q16.16.
 *
 * Przybliżenie paraboliczne z jednym krokiem poprawki:
 *   s  = 4/π·x − 4/π²·x·|x|
 *   s' = 0,225·(s·|s| − s) + s
 * Pierwszy człon daje błąd rzędu 0,06, poprawka zbija go poniżej 0,001 —
 * i to bez ani jednego dzielenia, którego Cortex-M0+ nie ma w sprzęcie.
 */
inline Fixed sinReal(Fixed x) {
    x = wrapAngle(x);

    constexpr float kB = 1.27323954f;   // 4/π
    constexpr float kC = 0.405284735f;  // 4/π²
    constexpr float kP = 0.225f;

    const Fixed b(kB), c(kC), p(kP);
    Fixed       s = b * x - c * x * abs(x);
    s             = p * (s * abs(s) - s) + s;
    return s;
}

inline Fixed cosReal(Fixed x) { return sinReal(x + Fixed(kHalfPi)); }

/** Pierwiastek kwadratowy metodą Newtona; wartości ujemne dają zero. */
inline Fixed sqrtReal(Fixed x) {
    if (!(x > Fixed(0))) return Fixed(0);

    // Start od wartości rzędu wielkości argumentu — trzy iteracje wystarczają
    // wtedy na zbieżność do rozdzielczości formatu.
    Fixed guess = x > Fixed(1) ? x : Fixed(1);
    for (int i = 0; i < 6; ++i) {
        guess = (guess + x / guess) * Fixed(0.5f);
    }
    return guess;
}

}  // namespace hydra
