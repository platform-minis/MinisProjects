/**
 * @file Arduboy2.h
 * @brief Nagłówek zgodności — to jego widzi niezmieniony kod gry.
 *
 * Gra na Arduboya zaczyna się od `#include <Arduboy2.h>` i od tej chwili
 * zakłada nazwy globalne: typ `Arduboy2`, stałe `WIDTH`, `BLACK`, `A_BUTTON`.
 * Ten plik je dostarcza, przekierowując wszystko do `hydra::arduboy`.
 *
 * Katalog `include/compat/arduboy` jest osobnym korzeniem włączeń, dodawanym
 * do ścieżki tylko wtedy, gdy projekt włączył moduł `arduboy`. Nazwy globalne
 * bez przestrzeni to cena zgodności ze źródłami, których nie piszemy — i cena
 * ta ma być płacona wyłącznie przez projekty, które o to poprosiły. Reszta
 * Hydry nie widzi stąd niczego.
 *
 * ## Użycie
 *
 *     #include <Arduboy2.h>
 *     Arduboy2 arduboy;
 *
 *     void setup() {
 *         arduboy.begin();
 *         arduboy.setFrameRate(60);
 *     }
 *
 *     void loop() {
 *         if (!arduboy.nextFrame()) return;
 *         arduboy.pollButtons();
 *         arduboy.clear();
 *         arduboy.setCursor(0, 0);
 *         arduboy.print(F("Hello"));
 *         arduboy.display();
 *     }
 *
 * `setup()` i `loop()` woła runtime modułu — na celu natywnym z własnego
 * `main()`, na układzie z pętli Arduino.
 */
#ifndef HYDRA_COMPAT_ARDUBOY2_H
#define HYDRA_COMPAT_ARDUBOY2_H

#include "hydra/core/Config.hpp"

#if !HYDRA_ENABLE_ARDUBOY
#  error "Wlacz modul arduboy (HYDRA_ENABLE_ARDUBOY), zanim dolaczysz <Arduboy2.h>"
#endif

// Na układzie prawdziwe Arduino dostarcza millis(), delay(), random() i PROGMEM.
// Na hoście nie ma ich skąd wziąć, więc wnosi je nasza atrapa.
#if HYDRA_PLAT_HOST
#  include "ArduinoCompat.h"
#else
#  include <Arduino.h>
#endif

#include "hydra/arduboy/Arduboy2.hpp"

// ── Typy ────────────────────────────────────────────────────────────────────

using Arduboy2Base  = hydra::arduboy::Arduboy2Base;
using Arduboy2      = hydra::arduboy::Arduboy2;
using Arduboy2Audio = hydra::arduboy::Audio;
using Point         = hydra::arduboy::Point;
using Rect          = hydra::arduboy::Rect;

using hydra::arduboy::collide;

// ── Ekran ───────────────────────────────────────────────────────────────────

#define WIDTH  128
#define HEIGHT 64

// Makra, nie stałe: gry używają ich w rozmiarach tablic i w `#if`, gdzie
// zmienna typu `constexpr i16` nie zawsze przechodzi przez preprocesor.
#define BLACK  0
#define WHITE  1
#define INVERT 2

/** Liczba bajtów bufora obrazu — gry deklarują na tym własne bufory. */
#define ARDUBOY_BUFFER_SIZE ((WIDTH) * (HEIGHT) / 8)

// ── Przyciski ───────────────────────────────────────────────────────────────

#define LEFT_BUTTON  0x20
#define RIGHT_BUTTON 0x40
#define UP_BUTTON    0x80
#define DOWN_BUTTON  0x10
#define A_BUTTON     0x08
#define B_BUTTON     0x04

/** Znaczniki `begin()` z oryginału — przyjmowane, choć nic tu nie zmieniają. */
#define ARDUBOY_NO_USB 0

#endif  // HYDRA_COMPAT_ARDUBOY2_H
