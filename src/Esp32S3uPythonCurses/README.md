# Kurs MicroPython — ESP32-S3 Pico

Praktyczny kurs programowania mikrokontrolera **ESP32-S3** w języku **MicroPython**. Każda lekcja to osobny szkic — program wgrywany bezpośrednio na płytkę. Lekcje ułożone są od najprostszych do bardziej zaawansowanych i stopniowo wprowadzają nowe elementy elektroniczne oraz techniki programowania.

---

## Wymagania wstępne

- Płytka **ESP32-S3 Pico** z wgranym firmware MicroPython
- Środowisko **MyCastle** (lub dowolny klient REPL np. Thonny / mpremote)
- Podstawowa znajomość Pythona (zmienne, pętle, instrukcje warunkowe)

---

## Słownik pojęć

| Pojęcie | Opis |
| ------- | ---- |
| **GPIO** | General Purpose Input/Output — pin mikrokontrolera, który może pełnić funkcję wejścia lub wyjścia cyfrowego |
| **Pin.OUT** | Tryb wyjściowy pinu — mikrokontroler steruje napięciem (0 V lub 3,3 V) |
| **Pin.IN** | Tryb wejściowy pinu — mikrokontroler odczytuje stan zewnętrznego sygnału |
| **ADC** | Analog-to-Digital Converter — przetwornik analogowo-cyfrowy; zamienia napięcie (0–3,3 V) na liczbę całkowitą |
| **ATTN_11DB** | Ustawienie tłumienia ADC pozwalające mierzyć napięcia do ~3,9 V zamiast domyślnych ~1,1 V |
| **LED** | Light Emitting Diode — dioda elektroluminescencyjna; świeci gdy przez nią płynie prąd |
| **Rezystor ograniczający** | Element szeregowy z LED zapobiegający przepaleniu — typowo 220–330 Ω |
| **Przycisk taktowy** | Mechaniczny przełącznik zwierający dwa punkty obwodu po wciśnięciu |
| **Rezystor pull-down** | Rezystor (zwykle 10 kΩ) łączący pin z masą (GND), gwarantujący stan LOW gdy przycisk jest zwolniony |
| **Fotoopornik (LDR)** | Light Dependent Resistor — rezystor zmieniający swoją rezystancję zależnie od natężenia światła; ciemniej = wyższy opór |
| **Dzielnik napięcia** | Układ dwóch rezystorów szeregowo między zasilaniem a masą; na ich styku pobiera się napięcie proporcjonalne do proporcji oporów |
| **setup()** | Funkcja inicjalizacyjna wywoływana jednorazowo po starcie programu |
| **loop()** | Funkcja głównej pętli wywoływana wielokrotnie bez przerwy |
| **sleep_ms(n)** | Wstrzymanie wykonania programu na *n* milisekund |
| **REPL** | Read-Eval-Print Loop — interaktywna konsola MicroPythona |

---

## Przegląd użytych pinów

| Pin | Tryb | Komponent | Lekcje |
| --- | ---- | --------- | ------ |
| 7 | ADC wejście | Fotoopornik (LDR) | Lekcja 6 |
| 11 | Wyjście cyfrowe | Dioda LED | Lekcja 1, 2, 4 |
| 12 | Wyjście cyfrowe | Dioda LED (czerwona) | Lekcja 3 |
| 13 | Wyjście cyfrowe | Dioda LED (żółta) | Lekcja 3 |
| 14 | Wyjście cyfrowe | Dioda LED (zielona) | Lekcja 3 |
| 16 | Wejście cyfrowe | Przycisk taktowy | Lekcja 4 |

---

## Schematy połączeń

### Połączenie diody LED (Lekcje 1, 2, 4)

```text
ESP32-S3 Pico
┌──────────────┐
│         GND  ├──────────────────────────────────┐
│              │                                  │
│        GP11  ├──[ R 330Ω ]──┤▶├──(cathode)    GND
│              │               LED    (anoda → R)
└──────────────┘
```

> **Ważne:** Zawsze łącz LED przez rezystor ograniczający (220–330 Ω). Bez niego prąd może przekroczyć dopuszczalny poziom i uszkodzić zarówno diodę, jak i pin GPIO.

Schemat (widok praktyczny):

```text
GP11 ──── Rezystor 330Ω ──── Anoda LED (+)
                                   │
                              Katoda LED (−)
                                   │
GND ───────────────────────────────┘
```

---

### Połączenie trzech diod LED (Lekcja 3)

```text
GP14 ── R 330Ω ──┤▶├── GND    (zielona)
GP13 ── R 330Ω ──┤▶├── GND    (żółta)
GP12 ── R 330Ω ──┤▶├── GND    (czerwona)
```

Schemat blokowy sekwencji:

```text
Zielona (GP14) ─── 3 s ON ─── OFF ───────────────────────────▶
Żółta   (GP13) ───────────── 1 s ON ─── OFF ─────────────────▶
Czerwona(GP12) ──────────────────────── 3 s ON ─── OFF ──────▶
              0s              3s         4s          7s   ...
```

---

### Połączenie przycisku (Lekcja 4)

```text
3,3 V ──── Przycisk ──── GP16
                  │
              R 10kΩ (pull-down)
                  │
                 GND
```

Gdy przycisk **zwolniony**: GP16 podciągnięty do GND przez rezystor → stan LOW (0)
Gdy przycisk **wciśnięty**: GP16 połączony z 3,3 V → stan HIGH (1)

```text
               ┌─────────┐
3V3 ──[BTN]──┤  GP16   │  ESP32-S3
              │         │
             [10kΩ]     │
              │         │
GND ──────────┘         │
                        │
                   odczyt .value()
```

---

### Połączenie fotoopornika — dzielnik napięcia (Lekcja 6)

```text
3,3 V ──── LDR (fotoopornik) ──┬──── GP7 (ADC)
                               │
                           R 10kΩ (stały)
                               │
                              GND
```

Zasada działania: fotoopornik i rezystor tworzą dzielnik napięcia. Gdy jest **ciemno**, opór LDR rośnie → napięcie na GP7 spada → odczyt ADC mały. Gdy jest **jasno**, opór LDR maleje → napięcie na GP7 rośnie → odczyt ADC duży.

```text
Natężenie światła:  │░░░░░░░░░│▒▒▒▒▒▒▒▒▒│█████████│
Odczyt ADC:         0       1000       2500      4095
Klasyfikacja:      CIEMNO   NORMALNIE    JASNO
```

---

## Lekcje

---

### Lekcja 1 — Zapalenie diody LED

**Cel:** Zrozumienie podstawowej konfiguracji pinu jako wyjścia i jednorazowego ustawienia stanu HIGH.

**Co się dzieje:**
Program konfiguruje pin 11 jako wyjście, a następnie w funkcji `setup()` ustawia go w stan HIGH (3,3 V). Dioda zapala się i pozostaje zapalona przez cały czas działania programu. Pętla `loop()` jest pusta — nic się nie zmienia po inicjalizacji.

**Schemat połączenia:** jak w sekcji *Połączenie diody LED*, pin GP11.

**Czego uczysz się:**

- Importowanie modułu `machine`
- Tworzenie obiektu `Pin` w trybie `Pin.OUT`
- Metoda `.value(1)` — ustawienie stanu HIGH
- Struktura `setup()` / `loop()`

---

### Lekcja 2 — Migająca dioda LED (blink)

**Cel:** Wprowadzenie opóźnień czasowych i cyklicznego zmieniania stanu pinu.

**Co się dzieje:**
Dioda na pinie 11 włącza się na 1 sekundę, wyłącza się na 1 sekundę — i tak w kółko bez końca. Efekt to charakterystyczne „miganie".

**Diagram czasowy:**

```text
GP11:  ___________         ___________         _____
      |           |       |           |       |
      |   1000ms  |       |   1000ms  |       |
______|           |_______|           |_______|
      ↑ ON        ↑ OFF   ↑ ON        ↑ OFF
```

**Czego uczysz się:**

- Moduł `time` i funkcja `sleep_ms()`
- Cykliczne wykonywanie kodu w pętli `loop()`
- Zmiana stanu pinu między 0 a 1

---

### Lekcja 3 — Sygnalizacja trzema diodami LED

**Cel:** Sterowanie wieloma pinami, sekwencje z różnymi czasami, wizualne naśladowanie sygnalizatora świetlnego.

**Co się dzieje:**
Trzy diody zapalają się jedna po drugiej w określonej kolejności:

1. Zielona (GP14) świeci 3 sekundy
2. Żółta (GP13) świeci 1 sekundę
3. Czerwona (GP12) świeci 3 sekundy
4. Cykl wraca do punktu 1

**Schemat połączenia:** jak w sekcji *Połączenie trzech diod LED*.

**Czego uczysz się:**

- Deklaracja wielu pinów wyjściowych
- Sekwencyjne sterowanie wieloma komponentami
- Planowanie czasów w pętli

---

### Lekcja 4 — Przycisk sterujący diodą LED

**Cel:** Odczytywanie stanu cyfrowego wejścia i reagowanie na działanie użytkownika.

**Co się dzieje:**
Program co 100 ms sprawdza stan pinu 16 (przycisk). Jeśli pin ma stan HIGH (przycisk wciśnięty) — dioda LED na pinie 11 zapala się i w konsoli REPL pojawia się komunikat `Button pressed`. Jeśli przycisk jest zwolniony — dioda gaśnie.

**Diagram logiczny:**

```text
Stan GP16:  0  0  0  1  1  1  1  0  0  1  0
Stan GP11:  0  0  0  1  1  1  1  0  0  1  0
Konsola:              ↑ "Button pressed" × 4    ↑
```

**Schemat połączenia:** jak w sekcji *Połączenie przycisku*, plus LED na GP11.

**Czego uczysz się:**

- Tworzenie pinu w trybie `Pin.IN`
- Metoda `.value()` do odczytu stanu
- Instrukcja warunkowa `if/else` w pętli
- Opóźnienie 100 ms jako prosty debouncing

---

### Lekcja 6 — Pomiar natężenia światła (ADC)

**Cel:** Odczyt sygnału analogowego, klasyfikacja wartości, wypisywanie wyników na konsolę.

**Co się dzieje:**
Co 500 ms program odczytuje wartość z przetwornika ADC na pinie 7 (fotoopornik). Wynik jest liczbą z zakresu 0–4095. Na podstawie wartości program wypisuje jedną z trzech klasyfikacji:

| Odczyt ADC | Klasyfikacja |
| ---------- | ------------ |
| < 1000 | DARK (ciemno) |
| 1000 – 2499 | NORMAL (normalne) |
| ≥ 2500 | BRIGHT (jasno) |

**Przykładowe wyjście REPL:**

```text
Light level2341
NORMAL
Light level891
DARK
Light level3102
BRIGHT
```

**Schemat połączenia:** jak w sekcji *Połączenie fotoopornika*.

**Czego uczysz się:**

- Klasa `ADC` i parametr `atten`
- Różnica między sygnałem cyfrowym a analogowym
- Użycie zmiennych globalnych (`global`)
- Klasyfikacja wartości ciągłych przez progi (`elif`)

---

### Lekcja 7 — Szablon projektu

**Cel:** Punkt startowy do własnych eksperymentów.

**Co się dzieje:**
Lekcja zawiera tylko czysty szablon z pustymi funkcjami `setup()` i `loop()` oraz obsługą wyjątku przy zatrzymaniu (`KeyboardInterrupt`). Brak gotowej implementacji — to miejsce na Twój własny program.

**Czego uczysz się:**

- Standardowa struktura każdego programu MicroPython w tym kursie
- Obsługa wyjątku `KeyboardInterrupt` (Ctrl+C) — bezpieczne zatrzymanie programu

---

## Struktura każdego programu

Każdy szkic w tym kursie ma jednolity schemat:

```python
from machine import Pin   # import modułów sprzętowych
import time               # moduł czasu

# --- Konfiguracja pinów (globalna) ---
_pin_XX = Pin(XX, mode=Pin.OUT)

# --- Inicjalizacja jednorazowa ---
def setup():
    pass  # kod wykonany raz po starcie

# --- Pętla główna ---
def loop():
    pass  # kod wykonywany bez przerwy

# --- Punkt wejścia ---
if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
```

> `try/except` na końcu zapewnia, że naciśnięcie Ctrl+C w konsoli REPL bezpiecznie zatrzyma program zamiast wyrzucić nieobsłużony wyjątek.

---

## Postęp kursu

```text
Lekcja 1  ──  Wyjście cyfrowe (LED ON)
Lekcja 2  ──  Czas i cykl (LED blink)
Lekcja 3  ──  Wiele wyjść + sekwencja (3× LED)
Lekcja 4  ──  Wejście cyfrowe + sterowanie (przycisk → LED)
Lekcja 6  ──  Wejście analogowe ADC (fotoopornik)
Lekcja 7  ──  Szablon do własnych projektów
```

---

## Platforma

- **Mikrokontroler:** ESP32-S3 (moduł `esp32-s3-pico`)
- **Język:** MicroPython
- **Środowisko:** MyCastle / Thonny / mpremote
- **Napięcie logiczne:** 3,3 V
- **Zasilanie:** USB 5 V → regulator na płytce → 3,3 V
