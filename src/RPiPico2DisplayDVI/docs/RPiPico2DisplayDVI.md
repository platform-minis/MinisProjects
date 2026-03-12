# RPiPico2DisplayDVI

**Wyświetlanie na urządzeniach DVI/HDMI.**
Raspberry Pi Pico 2 (RP2350) wyświetla informacje na urządzeniach DVI/HDMI.
W rozdzielczości 640×480 1bit (monochrom).
Używa HSTX DVI (sprzętowy blok HSTX w RP2350).

---

## Sprzęt

### Mikrokontroler
- Raspberry Pi Pico 2 (RP2350)

### Wyprowadzenia DVI (standard breakout: Pimoroni / Adafruit DVI Breakout)

| GPIO | Funkcja HSTX | Para różnicowa | Kolor DVI |
|------|-------------|----------------|-----------|
| GP12 | HSTX lane 0 | D0−            | Blue −    |
| GP13 | HSTX lane 1 | D0+            | Blue +    |
| GP14 | HSTX lane 2 | D1−            | Green −   |
| GP15 | HSTX lane 3 | D1+            | Green +   |
| GP16 | HSTX lane 4 | D2−            | Red −     |
| GP17 | HSTX lane 5 | D2+            | Red +     |
| GP18 | HSTX lane 6 | CLK−           | Clock −   |
| GP19 | HSTX lane 7 | CLK+           | Clock +   |

> Wszystkie piny konfigurowane jako `GPIO_FUNC_HSTX`.

### Schemat połączeń

```
Pico 2            DVI Breakout
GP12 ───────────► D0−
GP13 ───────────► D0+
GP14 ───────────► D1−
GP15 ───────────► D1+
GP16 ───────────► D2−
GP17 ───────────► D2+
GP18 ───────────► CLK−
GP19 ───────────► CLK+
GND  ───────────► GND
3V3  ───────────► (nie podłączany do DVI)
```

---

## Architektura

### Taktowanie

| Parametr           | Wartość                     |
|--------------------|-----------------------------|
| Zegar systemowy    | 252 MHz                     |
| Pixel clock        | 25,2 MHz (252 MHz / 10)     |
| TMDS bit rate      | ≈ 252 Mbit/s per lane       |
| Standard           | DVI 1.0, 640×480 @ 60 Hz   |

### Framebuffer

```
Szerokość:  640 pikseli
Wysokość:   480 linii
Format:     1 bpp (1 bit = 1 piksel, monochromatyczny)
Rozmiar:    640 × 480 / 8 = 38 400 B
Organizacja: tablica uint32_t[480][20]  (20 słów × 32 bity = 640 pikseli/linię)
Bit order:  LSB = lewy piksel w słowie
```

### HSTX EXPAND_TMDS (konwersja 1bpp → TMDS)

Blok HSTX w RP2350 konwertuje dane pikselowe na symbole TMDS sprzętowo, bez udziału CPU.

Konfiguracja dla monochromu (wszystkie 3 kanały czytają ten sam 1 bit):

| Pole               | Wartość | Znaczenie                         |
|--------------------|---------|-----------------------------------|
| `L0_NBITS`         | 0       | 1 bit na symbol (Blue)            |
| `L0_ROT`           | 0       | czyta bit na pozycji 0            |
| `L1_NBITS`         | 0       | 1 bit na symbol (Green)           |
| `L1_ROT`           | 0       | ten sam bit → monochrom           |
| `L2_NBITS`         | 0       | 1 bit na symbol (Red)             |
| `L2_ROT`           | 0       | ten sam bit → monochrom           |
| `N_TMDS_SYM_M1`    | 31      | 32 symbole TMDS na słowo FIFO     |

Efekt: 1 słowo `uint32_t` → 32 piksele → 32 trójki symboli TMDS (Blue=Green=Red).

### DVI timing 640×480 @ 60 Hz

```
Poziomo:
  Active:       640 px
  Front porch:   16 px
  Sync:          96 px
  Back porch:    48 px
  Total:        800 px

Pionowo:
  Active:       480 linii
  Front porch:   10 linii
  Sync:           2 linie
  Back porch:    33 linie
  Total:        525 linii
```

### Przepływ danych

```
framebuf[y][0..19]                    blank_line_*[0..24]
       │                                      │
       ▼                                      ▼
  DMA (DREQ_HSTX) ◄── IRQ przełącza ────────┘
       │
       ▼
  HSTX FIFO
       │  EXPAND_TMDS (sprzętowo)
       ▼
  HSTX serializer
       │  10 bitów/symbol × 3 kanały + CLK
       ▼
  GP12–GP19 (pary różnicowe)
       │
       ▼
  Monitor DVI/HDMI
```

### Obsługa blanking (impulsy synchronizacji)

Podczas wygaszania HSTX musi nadawać symbole kontrolne TMDS (nie dane):

| Stan       | CH0 (Blue) symbol | CH1 (Green) | CH2 (Red) |
|------------|-------------------|-------------|-----------|
| No sync    | `CTRL_00` 0x354   | `CTRL_00`   | `CTRL_00` |
| HSYNC      | `CTRL_01` 0x0AB   | `CTRL_00`   | `CTRL_00` |
| VSYNC      | `CTRL_10` 0x154   | `CTRL_00`   | `CTRL_00` |
| HSYNC+VSYNC| `CTRL_11` 0x2AB   | `CTRL_00`   | `CTRL_00` |

Pre-obliczone bufory `blank_line_*` (25 słów/linię) są podawane przez DMA
w liniach wygaszania zamiast danych z framebuffera.

---

## Zależności

- **pico-sdk** ≥ 2.0 (wymagany dla RP2350 i HSTX)
- CMake ≥ 3.13
- ARM GCC toolchain (`arm-none-eabi-gcc`)
- Zmienne środowiskowe: `PICO_SDK_PATH`

---

## Budowanie

### 1. Wymagania wstępne

```bash
# Ubuntu / Debian
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi git

# Sklonuj pico-sdk (jeśli jeszcze nie masz)
git clone https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
cd ~/pico-sdk && git submodule update --init

export PICO_SDK_PATH=~/pico-sdk
```

### 2. Build

```bash
cd src/RPiPico2DisplayDVI

mkdir build && cd build

cmake .. -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350

make -j$(nproc)
```

Wynik: `build/src/rpi_pico2_display_dvi.uf2`

### 3. Wgrywanie na Pico 2

```bash
# Metoda 1: UF2 (drag & drop)
# Przytrzymaj BOOTSEL, podłącz USB → Pico pojawi się jako dysk RPI-RP2
cp build/src/rpi_pico2_display_dvi.uf2 /media/$USER/RPI-RP2/

# Metoda 2: picotool
picotool load build/src/rpi_pico2_display_dvi.uf2 -f
```

### 4. Weryfikacja

Po wgraniu monitor DVI powinien wyświetlić szachownicę 8×8 pikseli (biało-czarną).
Debugowanie przez USB CDC (`minicom`, `screen`, `putty` na odpowiednim `/dev/ttyACM*`).

---

## Źródła

- [pico-examples / hstx / dvi](https://github.com/raspberrypi/pico-examples/tree/master/hstx/dvi)
- [RP2350 Datasheet — HSTX chapter](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
- [DVI 1.0 Specification](https://www.ddwg.org/lib/dvi_10_doc10.pdf)
