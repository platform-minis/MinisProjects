/**
 * @file Audio.hpp
 * @brief Wyjście dźwiękowe celu natywnego.
 *
 * Rozdzielenie sekwencera od wyjścia opisuje `Tones.hpp`. Tutaj jest druga
 * połowa: konkretna karta dźwiękowa hosta, podłączana pod ujście domyślne
 * wszystkich sekwencerów. Runtime robi to sam, więc niezmieniona gra dostaje
 * dźwięk bez jednej dopisanej linijki.
 *
 * Na układzie tego pliku nie ma — tam brzęczyk podłącza projekt, bo tylko on
 * wie, na której nóżce siedzi i czy w ogóle istnieje.
 */
#pragma once

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY && HYDRA_PLAT_HOST

#include "hydra/arduboy/Tones.hpp"
#include "hydra/core/Expected.hpp"

namespace hydra {
namespace arduboy {

/**
 * Otwiera kartę dźwiękową i podpina ją pod sekwencery.
 *
 * Zwraca `Err::NotSupported`, gdy zbudowano bez SDL albo gdy w systemie nie
 * ma urządzenia dźwiękowego. Nie jest to powód, żeby nie uruchamiać gry —
 * runtime zgłasza to ostrzeżeniem i gra dalej, niemo.
 */
Status startAudio();

/** Wycisza i zamyka kartę. */
void stopAudio();

/** Czy dźwięk faktycznie gra. */
bool audioRunning();

}  // namespace arduboy
}  // namespace hydra

#endif  // HYDRA_ENABLE_ARDUBOY && HYDRA_PLAT_HOST
