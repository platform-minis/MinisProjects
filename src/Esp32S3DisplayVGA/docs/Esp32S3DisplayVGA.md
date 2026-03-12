# Esp32S3DisplayVGA

**Wyświetlanie na urządzeniach VGA.**
ESP32-S3 wyświetla obraz na monitorze VGA w trybie 640×480 @ 60 Hz, 8-bit kolor (R3G3B2).
Używa sprzętowego bloku LCD_CAM z DMA — brak obciążenia CPU podczas generowania sygnału.
Kod oparty na przykładzie `luniVGA` z repozytorium [bitluni/ESP32-S3-VGA](https://github.com/bitluni/ESP32-S3-VGA).

---

## Sprzęt

### Mikrokontroler

- **ESP32-S3** z **8 MB PSRAM** (wymagane — framebuffer przechowywany w PSRAM)

### Breakout

- **Waveshare VGA PS2 Board** — płytka z wbudowanymi rezystorami DAC VGA i złączem DE-15.
  Podłączona do ESP32-S3 przez 16-pinowy złącze 16BitI/O.
  PS/2 nieużywane w tym projekcie.

### Wyprowadzenia — ESP32-S3 GPIO → Waveshare 16BitI/O

Tryb 8-bit (R3G3B2): biblioteka używa pinów `r[2..4]`, `g[3..5]`, `b[3..4]`.
Piny niższy bitów (`r[0..1]`, `g[0..2]`, `b[0..2]`) przekazane jako `-1` (ignorowane).

| GPIO ESP32-S3 | Pin 16BitI/O | Sygnał VGA | Rola w PinConfig |
|---------------|-------------|------------|------------------|
| GPIO4         | 19          | VGA_R0     | r[2] — LSB R     |
| GPIO5         | 17          | VGA_R1     | r[3]             |
| GPIO6         | 15          | VGA_R2     | r[4] — MSB R     |
| GPIO7         | 13          | VGA_G0     | g[3] — LSB G     |
| GPIO8         | 14          | VGA_G1     | g[4]             |
| GPIO9         | 11          | VGA_G2     | g[5] — MSB G     |
| GPIO10        | 20          | VGA_B0     | b[3] — LSB B     |
| GPIO11        | 18          | VGA_B1     | b[4] — MSB B     |
| GPIO1         | 7           | HSYNC      | hSync            |
| GPIO2         | 8           | VSYNC      | vSync            |
| GND           | GND         | GND        | —                |
| 3V3           | 3V3         | zasilanie  | —                |

> Pin 16 (VGA_B2) na breakoucie pozostaje niepodłączony — tryb 8-bit używa tylko R3G3B2.

---

## Architektura

### Mechanizm generowania VGA

Blok **LCD_CAM** w ESP32-S3 generuje sygnały VGA sprzętowo:

```text
PSRAM framebuffer (640 × 480 × 1 B = 300 KB)
        │
        ▼
  GDMA (TX channel)
        │  DMA → LCD_CAM FIFO (automatycznie, bez CPU)
        ▼
  LCD_CAM (tryb RGB)
        │  generuje HSYNC, VSYNC, DATA
        │  taktowanie: PLL240M / N ≈ 25,175 MHz pixel clock
        ▼
  GPIO4–GPIO11, GPIO1, GPIO2
        │
        ▼
  Waveshare VGA PS2 Board (rezystory DAC)
        │
        ▼
  Monitor VGA
```

### Taktowanie

| Parametr      | Wartość                      |
|---------------|------------------------------|
| Pixel clock   | 25,175 MHz                   |
| Źródło zegara | PLL240M / 10                 |
| Standard      | VGA 640×480 @ 60 Hz          |
| Kolor         | 8-bit R3G3B2 (256 kolorów)   |

### Format piksela (8-bit R3G3B2)

```text
Bit: 7  6  5  4  3  2  1  0
     B1 B0 G2 G1 G0 R2 R1 R0
```

- R: bity [2:0] — 3 bity (8 odcieni)
- G: bity [5:3] — 3 bity (8 odcieni)
- B: bity [7:6] — 2 bity (4 odcienie)

### VGA timing 640×480 @ 60 Hz

```text
Poziomo:
  Active:      640 px
  Front porch:  16 px
  Sync:         96 px  (negatywny)
  Back porch:   48 px
  Total:       800 px

Pionowo:
  Active:      480 linii
  Front porch:  10 linii
  Sync:          2 linie (negatywny)
  Back porch:   33 linie
  Total:       525 linii
```

---

## Kod źródłowy

Szkic Arduino w [src/luniVGA/](../src/luniVGA/):

| Plik               | Opis                                              |
|--------------------|---------------------------------------------------|
| `luniVGA.ino`      | Główny szkic — konfiguracja pinów i wzorzec testowy |
| `luniVGA.h`        | Implementacja VGA: LCD_CAM, DMA, API rysowania    |
| `DMAVideoBuffer.h` | Klasa zarządzająca buforami DMA w PSRAM           |

### Konfiguracja pinów w szkicu

```cpp
const PinConfig pins(
    -1, -1, 4, 5, 6,      // r[0..4]
    -1, -1, -1, 7, 8, 9,  // g[0..5]
    -1, -1, -1, 10, 11,   // b[0..4]
    1, 2                   // hSync, vSync
);
VGAMode mode = MODE_640x480x60;
vgaInit(pins, mode, 8);   // 8 = tryb 8-bit R3G3B2
```

### API rysowania

```cpp
// Narysuj piksel (x, y) kolorem (r, g, b) — każdy składnik 0..255
vgaDot(x, y, r, g, b);

// Narysuj piksel z dithering (losowe szumy poprawiające jakość gradientów)
vgaDotDit(x, y, r, g, b);

// Wyślij framebuffer do DMA (flush PSRAM cache)
vgaShow();

// Uruchom generowanie VGA przez LCD_CAM + DMA
vgaStart();
```

---

## Zależności

| Parametr   | Wartość                                                              |
|------------|----------------------------------------------------------------------|
| Biblioteka | Wbudowana w projekt (pliki skopiowane z repozytorium)                |
| Źródło     | [bitluni/ESP32-S3-VGA — luniVGA](https://github.com/bitluni/ESP32-S3-VGA/tree/main/luniVGA) |
| Platforma  | Arduino (ESP32 Arduino Core ≥ 2.x)                                   |
| PSRAM      | **Wymagane 8 MB PSRAM** (`Octal PSRAM` lub `Quad PSRAM`)             |
| ESP-IDF    | Wbudowany w Arduino Core (komponent `esp_driver_lcd`, `gdma`)        |

---

## Budowanie i wgrywanie

### Arduino IDE

1. Otwórz `src/luniVGA/luniVGA.ino` w Arduino IDE.
2. Wybierz płytkę: **ESP32S3 Dev Module** (lub kompatybilną).
3. Ustaw:
   - **PSRAM:** `OPI PSRAM` lub `Quad PSRAM` (zależy od modułu)
   - **Flash Size:** ≥ 4 MB
   - **Partition Scheme:** domyślna
4. Wgraj: `Sketch → Upload` lub `Ctrl+U`.

### Weryfikacja

Po uruchomieniu monitor VGA powinien wyświetlić:
- Gradient tęczowy z dithering (tło)
- Trzy pasy kolorów w lewym górnym rogu: czerwony, zielony, niebieski

---

## Uwagi

- **8 MB PSRAM jest wymagane** — bez PSRAM `vgaInit()` zwróci `false` i program zawiśnie w pętli.
- **Zmiana trybu:** zamień `MODE_640x480x60` na inny tryb z `luniVGA.h`
  (np. `MODE_800x600x60`) i zmień `8` → `16` w `vgaInit()` dla trybu 16-bit.
  Tryb 16-bit wymaga pełnych 16 pinów danych (R5G6B5).
- **Zmiana pinów:** edytuj tylko `PinConfig pins(...)` w `luniVGA.ino`.
- **`spiram.h`:** plik nagłówkowy dostarczany przez Arduino ESP32 Core — nie wymaga oddzielnej instalacji.

---

## Źródła

- [bitluni/ESP32-S3-VGA — GitHub](https://github.com/bitluni/ESP32-S3-VGA)
- [Waveshare VGA PS2 Board — Wiki](https://www.waveshare.com/wiki/VGA_PS2_Board)
- [Waveshare VGA PS2 Board — Schematic PDF](https://www.waveshare.com/w/upload/7/7a/VGA-PS2-Board-Schematic.pdf)
- [ESP32-S3 Technical Reference Manual — LCD_CAM](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
