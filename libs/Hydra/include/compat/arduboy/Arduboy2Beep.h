/**
 * @file Arduboy2Beep.h
 * @brief Proste piknięcia — `BeepPin1` i `BeepPin2` z oryginału.
 *
 * Lżejsza od `ArduboyTones` warstwa: jedna nuta, czas liczony w klatkach.
 * Gra ustawia dźwięk przez `tone()` i woła `timer()` raz na klatkę; gdy
 * licznik dojdzie do zera, dźwięk gaśnie.
 *
 * Obie nóżki prowadzą do tego samego sekwencera — na oryginalnym sprzęcie
 * były to dwa kanały licznika, tutaj wyjście jest jedno.
 */
#ifndef HYDRA_COMPAT_ARDUBOY2BEEP_H
#define HYDRA_COMPAT_ARDUBOY2BEEP_H

#include "Arduboy2.h"
#include "hydra/arduboy/Tones.hpp"

/**
 * @note `freq()` z oryginału przelicza herce na wartość licznika ATmega.
 *       Tutaj jest tożsamościowe — nie ma licznika, do którego trzeba by
 *       przeliczać, a gra podaje i tak herce.
 */
class BeepPin1 {
public:
    /** Klatki pozostałe do wyciszenia. `inline`, więc bez osobnej definicji. */
    inline static uint8_t duration = 0;

    void begin() {}

    /** Przelicznik z oryginału; u nas herce to herce. */
    static constexpr uint16_t freq(float hz) { return static_cast<uint16_t>(hz); }

    void tone(uint16_t count) { tone(count, 0); }

    void tone(uint16_t count, uint8_t dur) {
        duration = dur;
        tones_.tone(count, dur == 0 ? 0xFFFF : static_cast<uint16_t>(dur * 16));
    }

    void noTone() {
        duration = 0;
        tones_.noTone();
    }

    /** Odlicza jedną klatkę. Woła się raz na obieg pętli gry. */
    void timer() {
        if (duration > 0 && --duration == 0) tones_.noTone();
        tones_.update();
    }

    /** Sekwencer pod spodem — tędy projekt podpina brzęczyk. */
    static hydra::arduboy::Tones& sequencer() { return tones_; }

private:
    inline static hydra::arduboy::Tones tones_{};
};

/** Drugi kanał oryginału; tutaj to ten sam sekwencer. */
using BeepPin2 = BeepPin1;

#endif  // HYDRA_COMPAT_ARDUBOY2BEEP_H
