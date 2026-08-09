/**
 * @file Tones.hpp
 * @brief Sekwencer melodii — odpowiednik biblioteki ArduboyTones.
 *
 * Oryginał generuje przebiegi prostokątne licznikiem sprzętowym ATmega
 * i przerwaniem przełączającym nóżkę. Tutaj rozdzielamy dwie rzeczy, które
 * tam były zrośnięte:
 *
 *  1. **Co ma brzmieć** — sekwencer: kolejka nut, czasy, powtórki. To jest tu.
 *  2. **Czym to zabrzmi** — nóżka PWM, przetwornik, karta dźwiękowa hosta.
 *     Tego moduł nie zakłada; dostaje ujście przez `setSink()`.
 *
 * Rozdział nie jest ozdobnikiem. Sekwencer musi działać wszędzie, bo gra pyta
 * `playing()` i wstrzymuje na tej podstawie akcję — a wyjście dźwiękowe bywa
 * różne albo nie ma go wcale. Bez ujścia gra chodzi normalnie, tylko cicho.
 *
 * ## Odmierzanie czasu
 *
 * `update()` woła się raz na klatkę. Nie ma tu przerwania ani wątku: melodia
 * przesuwa się o tyle, ile minęło od poprzedniego wywołania. Gra, która
 * przestanie wołać `update()`, zatrzyma melodię na bieżącej nucie — i to jest
 * zachowanie oczekiwane, bo znaczy, że stoi też cała reszta gry.
 */
#pragma once

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace arduboy {

/** Znacznik końca tablicy nut. */
constexpr u16 kTonesEnd = 0x8000;
/** Znacznik powrotu na początek tablicy. */
constexpr u16 kTonesRepeat = 0x8001;
/** Bit ustawiany na częstotliwości: „ta nuta głośniej". */
constexpr u16 kToneHighVolume = 0x8000;

constexpr u8 kVolumeInTone       = 0;
constexpr u8 kVolumeAlwaysNormal = 1;
constexpr u8 kVolumeAlwaysHigh   = 2;

/**
 * Odtwarzacz melodii.
 *
 * Melodia to tablica par (częstotliwość w hercach, czas trwania w ms)
 * zakończona `kTonesEnd`. Częstotliwość 0 to pauza.
 */
class Tones {
public:
    /**
     * Ujście dźwięku: częstotliwość w hercach (0 = cisza) i głośność.
     *
     * Wołane **tylko przy zmianie** nuty, nie co klatkę — sterownik brzęczyka
     * ma ustawić przebieg i o nim zapomnieć, a nie przestawiać go sześćdziesiąt
     * razy na sekundę na tę samą wartość.
     */
    using Sink = Delegate<void(u16 frequencyHz, bool loud)>;

    /** Pytanie o zgodę gracza; zwykle `arduboy.audio.enabled`. */
    using EnabledFn = bool (*)();

    Tones() = default;
    explicit Tones(EnabledFn enabled) : enabled_(enabled) {}

    /** Podpina wyjście dźwiękowe. Bez niego sekwencer działa, ale milczy. */
    void setSink(Sink sink) { sink_ = sink; }

    void tone(u16 frequency, u16 durationMs);
    void tone(u16 f1, u16 d1, u16 f2, u16 d2);
    void tone(u16 f1, u16 d1, u16 f2, u16 d2, u16 f3, u16 d3);

    /** Odtwarza tablicę nut. Tablica musi żyć do końca odtwarzania. */
    void tones(const u16* sequence);
    void tonesInRAM(u16* sequence) { tones(sequence); }

    void noTone();

    void volumeMode(u8 mode) { volumeMode_ = mode; }
    bool playing() const     { return sequence_ != nullptr || remainingMs_ > 0; }

    /** Przesuwa melodię. Woła się raz na klatkę. */
    void update();

    /** Częstotliwość brzmiąca w tej chwili; 0 to cisza. Do testów i diagnostyki. */
    u16 currentFrequency() const { return currentFreq_; }

private:
    void emit(u16 frequency, bool loud);
    void startNote(u16 frequency, u16 durationMs);
    void advance();

    Sink      sink_{};
    EnabledFn enabled_ = nullptr;

    /** Do trzech nut podanych wprost przez `tone()`. */
    u16 inline_[6]     = {0, 0, 0, 0, 0, 0};
    u8  inlineCount_   = 0;
    u8  inlineIndex_   = 0;

    const u16* sequence_ = nullptr;
    const u16* cursor_   = nullptr;

    u32 lastUpdateMs_ = 0;
    i32 remainingMs_  = 0;
    u16 currentFreq_  = 0;
    u8  volumeMode_   = kVolumeInTone;
    bool primed_      = false;
};

}  // namespace arduboy
}  // namespace hydra

#endif  // HYDRA_ENABLE_ARDUBOY
