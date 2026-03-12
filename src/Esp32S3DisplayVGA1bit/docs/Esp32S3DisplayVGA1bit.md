# Esp32S3DisplayVGA1bit

**Wyświetlanie monochromatyczne na urządzeniach VGA.**
ESP32-S3 wyświetla obraz czarno-biały w trybie 640×480 @ 60 Hz.
Minimalne okablowanie: tylko 3 piny danych (MSB każdego kanału) + HSYNC + VSYNC.
Oparty na tej samej bibliotece co [Esp32S3DisplayVGA](../../Esp32S3DisplayVGA/docs/Esp32S3DisplayVGA.md) — różnica tylko w pinoucie i sposobie rysowania pikseli.

---

## Sprzęt

### Mikrokontroler

- **ESP32-S3** z **8 MB PSRAM** (wymagane)

### Breakout

- **Waveshare VGA PS2 Board** — tylko VGA, PS/2 nieużywane.

### Wyprowadzenia — minimalne (5 pinów danych)

W trybie mono podłączone są tylko MSB każdego kanału koloru.
Pozostałe piny danych (VGA_R0, R1, G0, G1, B0) pozostają **niepodłączone**.

| GPIO ESP32-S3 | Pin 16BitI/O | Sygnał VGA | Rola w PinConfig     |
|---------------|-------------|------------|----------------------|
| GPIO6         | 15          | VGA_R2     | r[4] — jedyny pin R  |
| GPIO9         | 11          | VGA_G2     | g[5] — jedyny pin G  |
| GPIO11        | 18          | VGA_B1     | b[4] — jedyny pin B  |
| GPIO1         | 7           | HSYNC      | hSync                |
| GPIO2         | 8           | VSYNC      | vSync                |
| GND           | GND         | GND        | —                    |
| 3V3           | 3V3         | zasilanie  | —                    |

> Porównanie z wersją 8-bit: zamiast 10 połączeń danych wystarczą **3**.

### Zasada działania 1-bit mono

Biblioteka pracuje w trybie 8-bit (R3G3B2). W każdym bajcie piksela:

```text
Bit: 7   6   5   4   3   2   1   0
     b[4] b[3] g[5] g[4] g[3] r[4] r[3] r[2]
      ↑              ↑              ↑
   GPIO11           GPIO9          GPIO6
```

- Pixel `0xFF` → bity 2, 5, 7 = **1** → GPIO6=H, GPIO9=H, GPIO11=H → **biały**
- Pixel `0x00` → bity 2, 5, 7 = **0** → GPIO6=L, GPIO9=L, GPIO11=L → **czarny**

Bity 0, 1, 3, 4, 6 (piny r[2], r[3], g[3], g[4], b[3]) są `-1` w PinConfig
— biblioteka ich nie inicjalizuje.

---

## Architektura

Identyczna z wersją 8-bit — LCD_CAM + GDMA + PSRAM framebuffer.
Różnica: tylko 3 GPIO obsługują dane kolorów.

```text
PSRAM framebuffer (640 × 480 × 1 B = 300 KB)
  każdy bajt: 0x00 (czarny) lub 0xFF (biały)
        │
        ▼
  GDMA → LCD_CAM (8-bit mode)
        │  LCD_DATA_OUT2 → GPIO6  → VGA_R2
        │  LCD_DATA_OUT5 → GPIO9  → VGA_G2
        │  LCD_DATA_OUT7 → GPIO11 → VGA_B1
        │  HSYNC → GPIO1, VSYNC → GPIO2
        ▼
  Waveshare VGA PS2 Board (rezystory R2, G2, B1)
        ▼
  Monitor VGA — obraz mono
```

---

## Kod źródłowy

Szkic Arduino w [src/luniVGA/](../src/luniVGA/):

| Plik               | Opis                                          |
|--------------------|-----------------------------------------------|
| `luniVGA.ino`      | Szkic mono — PinConfig, `vgaDotMono()`, test  |
| `luniVGA.h`        | Biblioteka VGA — bez zmian względem 8-bit      |
| `DMAVideoBuffer.h` | Bufor DMA — bez zmian                         |

### Konfiguracja pinów

```cpp
const PinConfig pins(
    -1, -1, -1, -1, 6,      // r[4] = GPIO6
    -1, -1, -1, -1, -1, 9,  // g[5] = GPIO9
    -1, -1, -1, -1, 11,     // b[4] = GPIO11
    1, 2                     // hSync=GPIO1, vSync=GPIO2
);
vgaInit(pins, MODE_640x480x60, 8);
```

### Rysowanie pikseli

```cpp
// Helper zdefiniowany w szkicu:
static inline void vgaDotMono(int x, int y, bool on)
{
    vgaDot(x, y, on ? 255 : 0, on ? 255 : 0, on ? 255 : 0);
}

// Przykład użycia — szachownica 8×8:
for(int y = 0; y < mode.vRes; y++)
    for(int x = 0; x < mode.hRes; x++)
        vgaDotMono(x, y, ((x >> 3) ^ (y >> 3)) & 1);
```

---

## Zależności

Identyczne z [Esp32S3DisplayVGA](../../Esp32S3DisplayVGA/docs/Esp32S3DisplayVGA.md):

- ESP32-S3 z 8 MB PSRAM
- Arduino ESP32 Core ≥ 2.x
- Biblioteka wbudowana w projekt (skopiowana z bitluni/ESP32-S3-VGA)

---

## Budowanie i wgrywanie

Identyczne z wersją 8-bit — otwórz `src/luniVGA/luniVGA.ino` w Arduino IDE,
wybierz **ESP32S3 Dev Module** z PSRAM, wgraj.

Po uruchomieniu monitor VGA powinien wyświetlić szachownicę 8×8 (biało-czarną).

---

## Porównanie z Esp32S3DisplayVGA

| Cecha                 | Esp32S3DisplayVGA      | Esp32S3DisplayVGA1bit  |
|-----------------------|------------------------|------------------------|
| Tryb koloru           | 8-bit R3G3B2 (256 kolorów) | Mono (2 kolory)    |
| Piny danych VGA       | 8 (GPIO4–GPIO11)       | 3 (GPIO6, GPIO9, GPIO11) |
| Piny sync             | 2 (GPIO1, GPIO2)       | 2 (GPIO1, GPIO2)       |
| Razem GPIO            | 10                     | 5                      |
| Framebuffer           | 300 KB PSRAM           | 300 KB PSRAM           |
| Biblioteka            | ta sama                | ta sama                |
| Wzorzec testowy       | Gradient tęczowy       | Szachownica mono       |

---

## Źródła

- [bitluni/ESP32-S3-VGA — GitHub](https://github.com/bitluni/ESP32-S3-VGA)
- [Waveshare VGA PS2 Board — Schematic PDF](https://www.waveshare.com/w/upload/7/7a/VGA-PS2-Board-Schematic.pdf)
