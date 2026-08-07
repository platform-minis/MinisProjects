#pragma once
/**
 * Hydra — regulator PID (rozdz. 9).
 *
 * Szablon po typie liczbowym, nie klasa na float. Dzięki temu ten sam kod
 * pracuje na float tam, gdzie jest FPU, i na Q16.16 na RP2040 — a testy
 * sprawdzają obie ścieżki na hoście, zamiast wierzyć, że stałoprzecinkowa
 * wersja zachowa się tak samo.
 *
 * Dwa mechanizmy odróżniają go od podręcznikowego wzoru i oba wynikają
 * z praktyki, nie z teorii:
 *
 * **Ograniczenie całkowania.** Gdy wyjście stoi na nasyceniu, dalsze
 * gromadzenie całki niczego nie poprawia, za to po ustąpieniu przyczyny
 * regulator przez długi czas jedzie na zapasie z przeszłości. Klasyczny objaw:
 * robot dociśnięty do przeszkody rusza gwałtownie w chwili jej usunięcia.
 * Całka jest tu wstrzymywana, gdy wyjście jest nasycone i błąd pcha je dalej
 * w tę samą stronę.
 *
 * **Filtrowana pochodna.** Człon różniczkujący liczony wprost z różnicy próbek
 * wzmacnia szum enkodera — przy okresie 1 ms nawet pojedyncze zliczenie daje
 * ogromną pochodną. Filtr dolnoprzepustowy pierwszego rzędu tłumi to, kosztem
 * niewielkiego opóźnienia reakcji.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/core/Fixed.hpp"
#include "hydra/motion/MotionTypes.hpp"

namespace hydra {
namespace motion {

template <typename T>
class Pid {
public:
    struct Gains {
        T kp = T{};
        T ki = T{};
        T kd = T{};
    };

    struct Limits {
        /** Zakres wyjścia; przekroczenie wstrzymuje całkowanie. */
        T outMin = T{};
        T outMax = T{};
        /** Górna granica samego członu całkującego; zero wyłącza ograniczenie. */
        T integralMax = T{};
    };

    struct Diagnostics {
        T    lastError      = T{};
        T    integral       = T{};
        T    derivative     = T{};
        T    output         = T{};
        u32  saturatedCount = 0;  ///< iteracje z wyjściem na ograniczeniu
        u32  holdCount      = 0;  ///< iteracje ze wstrzymanym całkowaniem
    };

    void setGains(const Gains& gains) { gains_ = gains; }
    void setLimits(const Limits& limits) { limits_ = limits; }

    /**
     * Stała czasowa filtru pochodnej wyrażona jako współczynnik wygładzania
     * z zakresu (0, 1]. Jedynka wyłącza filtrowanie.
     */
    void setDerivativeFilter(T alpha) { dAlpha_ = alpha; }

    const Gains&  gains() const { return gains_; }
    const Limits& limits() const { return limits_; }
    Diagnostics   diagnostics() const { return diag_; }

    /** Zeruje stan wewnętrzny; wołać po każdym zatrzymaniu napędu. */
    void reset() {
        integral_    = T{};
        lastError_   = T{};
        filteredD_   = T{};
        primed_      = false;
        diag_        = Diagnostics{};
    }

    /**
     * Jeden krok regulacji. dt w sekundach; wartość niedodatnia jest
     * odrzucana i zwraca poprzednie wyjście.
     */
    T update(T setpoint, T measured, T dt) {
        if (!(dt > T{})) return diag_.output;

        const T error = setpoint - measured;

        // Człon różniczkujący liczymy z pomiaru, a nie z uchybu: skokowa zmiana
        // zadania dałaby inaczej impuls na wyjściu („derivative kick"), który
        // w napędzie objawia się szarpnięciem przy każdej nowej komendzie.
        T derivative = T{};
        if (primed_) {
            const T raw = (lastMeasured_ - measured) / dt;
            filteredD_  = filteredD_ + dAlpha_ * (raw - filteredD_);
            derivative  = filteredD_;
        } else {
            primed_ = true;
        }
        lastMeasured_ = measured;

        const T proportional = gains_.kp * error;
        const T candidateI   = integral_ + gains_.ki * error * dt;

        // Wyjście próbne z nową całką — dopiero ono mówi, czy wolno ją przyjąć.
        T candidate = proportional + candidateI + gains_.kd * derivative;

        const bool hasRange  = limits_.outMax > limits_.outMin;
        const bool saturated = hasRange && (candidate > limits_.outMax ||
                                            candidate < limits_.outMin);
        // Całkę wstrzymujemy tylko wtedy, gdy błąd pcha wyjście dalej poza
        // ograniczenie. Przy błędzie przeciwnego znaku całkowanie musi działać,
        // inaczej regulator nie wyszedłby z nasycenia.
        const bool pushingOut = saturated && ((candidate > limits_.outMax && error > T{}) ||
                                              (candidate < limits_.outMin && error < T{}));

        if (pushingOut) {
            ++diag_.holdCount;
        } else {
            integral_ = candidateI;
            if (limits_.integralMax > T{}) {
                integral_ = clampValue(integral_, T{} - limits_.integralMax,
                                       limits_.integralMax);
            }
        }

        T output = proportional + integral_ + gains_.kd * derivative;
        if (hasRange) {
            const T limited = clampValue(output, limits_.outMin, limits_.outMax);
            if (limited != output) ++diag_.saturatedCount;
            output = limited;
        }

        lastError_        = error;
        diag_.lastError   = error;
        diag_.integral    = integral_;
        diag_.derivative  = derivative;
        diag_.output      = output;
        return output;
    }

private:
    static T clampValue(T value, T lo, T hi) {
        if (value < lo) return lo;
        if (value > hi) return hi;
        return value;
    }

    Gains  gains_{};
    Limits limits_{};
    T      dAlpha_ = T{};

    T    integral_     = T{};
    T    lastError_    = T{};
    T    lastMeasured_ = T{};
    T    filteredD_    = T{};
    bool primed_       = false;

    Diagnostics diag_{};
};

/** Regulator w typie właściwym dla platformy. */
using PidR = Pid<real_t>;

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
