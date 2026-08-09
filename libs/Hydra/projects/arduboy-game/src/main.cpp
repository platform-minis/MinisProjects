/**
 * Jaskinia — gra na Arduboya.
 *
 * ══════════════════════════════════════════════════════════════════════════
 *  UWAGA: w tym pliku nie ma **ani jednego** odwołania do Hydry.
 *
 *  Nie ma `#include <Hydra.h>`, nie ma `hydra::`, nie ma `App::begin()`,
 *  nie ma `main()`. Jest dokładnie to, co byłoby w źródle gry napisanej na
 *  oryginalny sprzęt: `<Arduboy2.h>`, `setup()` i `loop()`.
 *
 *  I to jest cały sens tego projektu — jeśli ten plik wymagałby choćby jednej
 *  linijki dopisanej „pod Hydrę", moduł `arduboy` nie spełniałby obietnicy,
 *  bo tej linijki nie da się dopisać w tysiącu istniejących gier.
 * ══════════════════════════════════════════════════════════════════════════
 *
 * Zasady: statek leci w prawo przez zwężającą się jaskinię. A albo góra
 * podnosi, brak nacisku opuszcza. Zderzenie ze ścianą kończy przelot.
 * Wynik to przebyty dystans; najlepszy zapisuje się w EEPROM-ie.
 */

#include <Arduboy2.h>
#include <ArduboyTones.h>
#include <EEPROM.h>

Arduboy2 arduboy;
ArduboyTones sound(arduboy.audio.enabled);

// ── Stałe gry ───────────────────────────────────────────────────────────────

constexpr uint8_t  SHIP_X      = 24;
constexpr uint8_t  SHIP_W      = 7;
constexpr uint8_t  SHIP_H      = 5;

/** Grawitacja i ciąg w jednostkach 1/16 piksela na klatkę — bez liczb zmiennoprzecinkowych. */
constexpr int8_t   GRAVITY     = 2;
constexpr int8_t   THRUST      = -5;
constexpr int8_t   MAX_FALL    = 24;
constexpr int8_t   MAX_RISE    = -24;

/** Adres rekordu w EEPROM-ie. Poniżej 16 oryginał trzymał ustawienia systemowe. */
constexpr int      EE_HISCORE  = 64;
/** Znacznik „to nasze dane", żeby nie odczytać śmieci po innej grze. */
constexpr uint16_t EE_MAGIC    = 0xA7B3;
constexpr int      EE_MAGIC_AT = 62;

/** Szerokość kolumny ściany w pikselach. */
constexpr uint8_t  COL_W       = 4;
constexpr uint8_t  COLUMNS     = WIDTH / COL_W + 2;

// ── Melodie ─────────────────────────────────────────────────────────────────

const uint16_t crashTune[] PROGMEM = {
    NOTE_A3, 80, NOTE_F3, 80, NOTE_D3, 200, TONES_END
};

const uint16_t pointTune[] PROGMEM = {
    NOTE_E6, 30, TONES_END
};

// ── Stan ────────────────────────────────────────────────────────────────────

enum class Mode : uint8_t { Title, Flying, Dead };

Mode     mode = Mode::Title;
int16_t  shipY16;          // pozycja w 1/16 piksela
int8_t   velocity;
uint16_t distance;
uint16_t hiScore;

/** Górna i dolna krawędź jaskini dla każdej kolumny. */
uint8_t  ceilingAt[COLUMNS];
uint8_t  floorAt[COLUMNS];
uint8_t  scroll;           // przesunięcie w pikselach wewnątrz kolumny
uint8_t  head;             // indeks najstarszej kolumny w buforze cyklicznym

uint8_t  gap;              // bieżąca szerokość przejścia
int8_t   drift;            // w którą stronę wędruje jaskinia

// ── Rekord ──────────────────────────────────────────────────────────────────

void loadHiScore() {
    uint16_t magic = 0;
    EEPROM.get(EE_MAGIC_AT, magic);
    if (magic != EE_MAGIC) {
        // Pierwsze uruchomienie albo dane po innej grze — zaczynamy od zera.
        hiScore = 0;
        return;
    }
    EEPROM.get(EE_HISCORE, hiScore);
}

void saveHiScore() {
    EEPROM.put(EE_MAGIC_AT, EE_MAGIC);
    EEPROM.put(EE_HISCORE, hiScore);
}

// ── Jaskinia ────────────────────────────────────────────────────────────────

/** Dokłada kolejną kolumnę na końcu, przesuwając bufor cykliczny. */
void pushColumn() {
    const uint8_t prevCeiling = ceilingAt[(head + COLUMNS - 1) % COLUMNS];

    int16_t ceiling = prevCeiling + drift;

    // Zmiana kierunku co jakiś czas, żeby jaskinia falowała zamiast biec skosem.
    if (random(10) == 0) drift = -drift;

    // Odbicie od krawędzi ekranu z zapasem na przejście.
    if (ceiling < 1) {
        ceiling = 1;
        drift = 1;
    }
    if (ceiling + gap > HEIGHT - 1) {
        ceiling = HEIGHT - 1 - gap;
        drift = -1;
    }

    ceilingAt[head] = static_cast<uint8_t>(ceiling);
    floorAt[head]   = static_cast<uint8_t>(ceiling + gap);
    head = (head + 1) % COLUMNS;
}

void resetCave() {
    gap   = 40;
    drift = 1;
    head  = 0;

    for (uint8_t i = 0; i < COLUMNS; ++i) {
        ceilingAt[i] = 12;
        floorAt[i]   = 12 + gap;
    }
    scroll = 0;
}

void startGame() {
    resetCave();
    shipY16  = (HEIGHT / 2) * 16;
    velocity = 0;
    distance = 0;
    mode     = Mode::Flying;
}

// ── Rysowanie ───────────────────────────────────────────────────────────────

void drawCave() {
    for (uint8_t i = 0; i < COLUMNS; ++i) {
        const uint8_t slot = (head + i) % COLUMNS;
        const int16_t x = static_cast<int16_t>(i * COL_W) - scroll;
        if (x + COL_W < 0 || x >= WIDTH) continue;

        const uint8_t ceiling = ceilingAt[slot];
        const uint8_t floorY  = floorAt[slot];

        arduboy.fillRect(x, 0, COL_W, ceiling, WHITE);
        arduboy.fillRect(x, floorY, COL_W, HEIGHT - floorY, WHITE);
    }
}

void drawShip(uint8_t y) {
    // Klin: dziób z prawej, dysza z lewej.
    arduboy.fillTriangle(SHIP_X, y,
                         SHIP_X, y + SHIP_H - 1,
                         SHIP_X + SHIP_W, y + SHIP_H / 2, WHITE);

    // Płomień, gdy ciąg pracuje — czytelniejsze niż sam ruch.
    if (velocity < 0 && arduboy.everyXFrames(2)) {
        arduboy.drawFastHLine(SHIP_X - 3, y + SHIP_H / 2, 3, WHITE);
    }
}

/** Krawędź jaskini pod podanym pikselem ekranu. */
void caveAt(int16_t screenX, uint8_t& ceiling, uint8_t& floorY) {
    int16_t index = (screenX + scroll) / COL_W;
    if (index < 0) index = 0;
    if (index >= COLUMNS) index = COLUMNS - 1;

    const uint8_t slot = (head + index) % COLUMNS;
    ceiling = ceilingAt[slot];
    floorY  = floorAt[slot];
}

// ── Ekrany ──────────────────────────────────────────────────────────────────

void title() {
    arduboy.setCursor(28, 12);
    arduboy.setTextSize(2);
    arduboy.print(F("JASKINIA"));
    arduboy.setTextSize(1);

    arduboy.setCursor(16, 36);
    arduboy.print(F("A - start, lec w prawo"));

    arduboy.setCursor(34, 50);
    arduboy.print(F("rekord: "));
    arduboy.print(hiScore);

    if (arduboy.justPressed(A_BUTTON)) startGame();
}

void flying() {
    // Sterowanie: ciąg pod A albo pod strzałką w górę.
    if (arduboy.pressed(A_BUTTON | UP_BUTTON)) {
        velocity += THRUST;
        if (velocity < MAX_RISE) velocity = MAX_RISE;
    } else {
        velocity += GRAVITY;
        if (velocity > MAX_FALL) velocity = MAX_FALL;
    }

    shipY16 += velocity;
    if (shipY16 < 0) {
        shipY16  = 0;
        velocity = 0;
    }
    if (shipY16 > (HEIGHT - SHIP_H) * 16) {
        shipY16  = (HEIGHT - SHIP_H) * 16;
        velocity = 0;
    }

    // Przewijanie jaskini — dwa piksele na klatkę.
    scroll += 2;
    while (scroll >= COL_W) {
        scroll -= COL_W;
        pushColumn();
        ++distance;

        // Co pięćdziesiąt kolumn przejście zwęża się o jeden piksel.
        if (distance % 50 == 0 && gap > 18) --gap;
        if (distance % 25 == 0) sound.tones(pointTune);
    }

    const uint8_t shipY = static_cast<uint8_t>(shipY16 / 16);

    // Kolizja sprawdzana na dziobie i na ogonie, nie w środku: przy locie
    // ukośnym przez zwężenie środek bywa jeszcze w przejściu, gdy dziób jest
    // już w skale.
    uint8_t ceilingFront, floorFront, ceilingBack, floorBack;
    caveAt(SHIP_X + SHIP_W, ceilingFront, floorFront);
    caveAt(SHIP_X, ceilingBack, floorBack);

    const bool hit = shipY < ceilingFront || shipY + SHIP_H > floorFront ||
                     shipY < ceilingBack  || shipY + SHIP_H > floorBack;

    drawCave();
    drawShip(shipY);

    arduboy.setCursor(0, 0);
    arduboy.print(distance);

    if (hit) {
        sound.tones(crashTune);
        if (distance > hiScore) {
            hiScore = distance;
            saveHiScore();
        }
        mode = Mode::Dead;
    }
}

void dead() {
    drawCave();

    // Ramka z komunikatem na tle jaskini — czarne wypełnienie, biały obrys.
    arduboy.fillRect(20, 16, 88, 32, BLACK);
    arduboy.drawRect(20, 16, 88, 32, WHITE);

    arduboy.setCursor(38, 22);
    arduboy.print(F("KONIEC"));

    arduboy.setCursor(28, 32);
    arduboy.print(F("dystans: "));
    arduboy.print(distance);

    if (distance >= hiScore && distance > 0) {
        arduboy.setCursor(30, 40);
        arduboy.print(F("NOWY REKORD"));
    }

    if (arduboy.justPressed(A_BUTTON)) mode = Mode::Title;
    if (arduboy.justPressed(B_BUTTON)) startGame();
}

// ── Szkielet Arduino ────────────────────────────────────────────────────────

void setup() {
    arduboy.begin();
    arduboy.setFrameRate(60);
    arduboy.initRandomSeed();

    loadHiScore();
    resetCave();
}

void loop() {
    if (!arduboy.nextFrame()) return;

    arduboy.pollButtons();
    arduboy.clear();

    switch (mode) {
        case Mode::Title:  title();  break;
        case Mode::Flying: flying(); break;
        case Mode::Dead:   dead();   break;
    }

    sound.update();
    arduboy.display();
}
