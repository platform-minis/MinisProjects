# ESP32-S3 Zero uPython Audio — MAX98357A

Trzy przykłady MicroPython generowania dźwięku na **ESP32-S3 Zero** (Waveshare) z wykorzystaniem wzmacniacza I2S **MAX98357A**. Wszystkie lekcje działają bez zewnętrznych bibliotek — dźwięk generowany jest programowo jako fala sinusoidalna.

## Połączenia

| MAX98357A | ESP32-S3 Zero |
|-----------|---------------|
| BCLK      | GP4           |
| LRC (LRCLK) | GP5         |
| DIN       | GP6           |
| GND       | GND           |
| VIN       | 3.3V lub 5V   |

> Pin `SD` (Shutdown) pozostaw niepodłączony lub podłącz do 3.3V, aby wzmacniacz był zawsze aktywny.  
> LED wbudowany w ESP32-S3 Zero jest na GP21 — nie koliduje z pinami audio.

## Lekcje

### Lesson 40 — Beep

Trzy sygnały dźwiękowe 440 Hz × 500 ms z przerwami 200 ms. Podstawowy test połączenia z wzmacniaczem.

```
440 Hz ▐▀▀▀▀▐ 200ms ▐▀▀▀▀▐ 200ms ▐▀▀▀▀▐ ... 2s pauza
```

### Lesson 41 — Gama C-dur

Sekwencja ośmiu nut gamy C-dur od C4 (262 Hz) do C5 (523 Hz), każda trwająca 400 ms, z odstępem 100 ms między nutami.

```
C4 → D4 → E4 → F4 → G4 → A4 → B4 → C5 ... 1s pauza
```

### Lesson 42 — Twinkle Twinkle

Pełna melodia „Twinkle Twinkle Little Star" z podziałem na ćwierćnuty (400 ms) i półnuty (800 ms).

```
C C G G A A G·· F F E E D D C·· ...
```

## Jak to działa

Generator dźwięku oblicza jeden okres fali sinusoidalnej dla danej częstotliwości, a następnie powtarza ten bufor przez żądany czas:

```python
period = int(RATE / freq)          # próbki na jeden okres
amp = int(32767 * vol)             # amplituda 16-bit
for i in range(period):
    v = int(amp * math.sin(6.2832 * i / period))
    struct.pack_into('<h', buf, i * 2, v)
# wielokrotny zapis bufora przez I2S aż do upłynięcia czasu ms
```

Parametry I2S:
- Sample rate: **22 050 Hz**
- Bit depth: **16-bit signed**
- Format: **Mono**
- Internal buffer: 4096 bajtów

## Bloki Blockly

Po załadowaniu projektu w MyCastle dostępna jest kategoria **MAX98357A Audio** z blokami:

| Blok | Opis |
|------|------|
| `MAX98357A init` | Inicjalizacja I2S (wywołaj raz w setup) |
| `play tone … Hz … ms` | Odtwórz ton o danej częstotliwości |
| `play note … ms` | Odtwórz nutę z listy (C4–C5) |
| `rest … ms` | Cisza przez podany czas |

## Wymagania

- ESP32-S3 Zero (Waveshare) z MicroPython ≥ 1.22
- Moduł MAX98357A
- Głośnik 4–8 Ω
- Brak zewnętrznych bibliotek
