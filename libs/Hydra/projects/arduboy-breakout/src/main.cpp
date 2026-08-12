/**
 * ArduBreakout — szkielet szkicu.
 *
 * ══════════════════════════════════════════════════════════════════════════
 *  Gra leży obok, w `ArduBreakout.ino`, i jest **bajt w bajt** tym samym
 *  plikiem, co w repozytorium MLXXXp/Arduboy2. Nie ma w niej ani jednej
 *  naszej poprawki — można to sprawdzić sumą kontrolną:
 *
 *      sha256sum src/ArduBreakout.ino
 *
 *  Ten plik robi jedną rzecz, której nie robi kompilator C++, a robi ją
 *  środowisko Arduino: **generuje prototypy funkcji**. Szkic `.ino` wolno
 *  pisać z wywołaniami przed definicją, bo IDE przed kompilacją skleja
 *  pliki i dokleja na górze deklaracje. C++ tego nie wybacza i zgłasza
 *  „was not declared in this scope".
 *
 *  Prototypy poniżej są wygenerowane z definicji w szkicu — tak samo jak
 *  robi to Arduino, tylko widocznie zamiast niewidocznie.
 * ══════════════════════════════════════════════════════════════════════════
 */

#include <Arduboy2.h>

// ── Prototypy, które doklejałoby środowisko Arduino ─────────────────────────

void movePaddle();
void moveBall();
void drawBall();
void drawPaddle();
void drawGameOver();
void pause();
void Score();
void newLevel();
boolean pollFireButton(int n);
boolean displayHighScores(byte file);
boolean titleScreen();
void enterInitials();
void enterHighScore(byte file);
void playTone(uint16_t count, uint8_t frames);
void playToneTimed(uint16_t count, uint16_t duration);
// ── Właściwy szkic, niezmieniony ────────────────────────────────────────────

#include "ArduBreakout.ino"
