# RPiPico2DisplayVGA

**Wyświetlanie na urządzeniach VGA z obsługą klawiatury PS/2.**
Raspberry Pi Pico 2 (RP2350) wyświetla tekst na monitorze VGA w rozdzielczości 640×480 1bit (monochrom)
i czyta znaki z klawiatury PS/2. Terminal 80×60 znaków z czcionką 8×8 px.

---

## Sprzęt

### Mikrokontroler

- Raspberry Pi Pico 2 (RP2350)

### Breakout

- **Waveshare VGA PS2 Board** — płytka rozszerzająca z wbudowanymi rezystorami DAC VGA
  i gniazdami PS/2. Łączy się z Pico przez 16-pinowy (16BitI/O) złącze.

### Wyprowadzenia — Pico 2 ↔ Waveshare 16BitI/O

Rezystory DAC i kondensatory pull-up PS/2 są wbudowane w breakout — brak ręcznego lutowania.

#### VGA (R3G3B2)

| GPIO Pico 2 | Pin 16BitI/O | Sygnał VGA |
|-------------|--------------|------------|
| GP0         | 19           | R0         |
| GP1         | 17           | R1         |
| GP2         | 15           | R2         |
| GP3         | 13           | G0         |
| GP4         | 14           | G1         |
| GP5         | 11           | G2         |
| GP6         | 20           | B0         |
| GP7         | 18           | B1         |
| GP8         | 7            | HSYNC      |
| GP9         | 8            | VSYNC      |
| GND         | GND          | GND        |

> Pin 16 (VGA_B2) na breakoucie pozostaje niepodłączony — biblioteka używa R3G3B2 (8-bit),
> nie R3G3B3.

#### Klawiatura PS/2

| GPIO Pico 2 | Pin 16BitI/O | Sygnał PS/2 |
|-------------|--------------|-------------|
| GP20        | 5            | DATA        |
| GP21        | 6            | CLK         |
| 3V3         | 3V3          | zasilanie   |
| GND         | GND          | GND         |

> **Uwaga:** DATA i CLK muszą być na kolejnych GPIO (CLK = DATA + 1) — wymaganie biblioteki ps2kbd.
> Pico 2 pracuje na 3,3 V. Waveshare breakout ma wbudowane rezystory pull-up i ograniczniki
> napięcia dla PS/2.

---

## Architektura

### Taktowanie VGA

| Parametr     | Wartość                 |
|--------------|-------------------------|
| Pixel clock  | 25,175 MHz              |
| Standard     | VGA 640×480 @ 60 Hz     |
| Sygnały sync | Negatywne HSYNC i VSYNC |

### Framebuffer

```text
Szerokość:   640 pikseli
Wysokość:    480 linii
Format:      FORM_GRAPH1 — 1 bit/piksel (8 pikseli w bajcie)
Rozmiar:     640 × 480 / 8 = 38 400 B
Stride:      80 bajtów / linię
Bit order:   MSB = lewy piksel w bajcie (bit 7 = piksel x=0)
```

### Terminal tekstowy

```text
Kolumny: 640 / 8 = 80
Wiersze: 480 / 8 = 60
Czcionka: 8×8 px, public domain (IBM VGA, via dhepper/font8x8)
Scrollowanie: memmove() — wiersze 1–59 → 0–58, ostatni wiersz czyszczony
Obsługiwane znaki sterujące: \n (nowa linia), \r (powrót karetki), \b (backspace)
```

### Architektura dwurdzeniowa i PIO

```text
Core 0 (main):
  ├── multicore_launch_core1(core1_entry)
  ├── Video(DEV_VGA, RES_VGA, FORM_GRAPH1, framebuf, NULL)  ← PIO0
  ├── kbd_init(1, PS2_DAT_GPIO)                             ← PIO1
  └── pętla główna — odczyt PS/2, renderowanie tekstu

Core 1 (VgaCore):
  └── VgaCore()  — generuje VGA przez PIO0 + DMA (blokuje na zawsze)
```

> **Ważne:** `multicore_launch_core1()` musi być wywołane **przed** `Video()`.
> PIO0 jest zajęty przez PicoVGA — klawiatura PS/2 musi używać PIO1.

### Przepływ danych

```text
Klawiatura PS/2
  (GP20/GP21)
      │  PIO1 — dekoduje protokół PS/2 → ASCII
      ▼
  kbd_getc() / kbd_ready()
      │
      ▼
  fb_put_char()  →  framebuf[y*80 + x/8]  (1bpp)
                         │
                         ▼
                   PicoVGA (PIO0 + DMA)    ← Core 1
                         │  R3G3B2 serializer
                         ▼
                      GP0–GP9
                         │
                         ▼
                    Monitor VGA
```

---

## Zależności

### Biblioteki

| Parametr   | PicoVGA                                                            | PS/2 keyboard                                               |
|------------|--------------------------------------------------------------------|-------------------------------------------------------------|
| Biblioteka | [codaris/picovga-cmake](https://github.com/codaris/picovga-cmake)  | [lurk101/ps2kbd-lib](https://github.com/lurk101/ps2kbd-lib) |
| Wersja     | v1.2.2                                                             | master                                                      |
| Commit     | `f2dc01f6990d81b08e9c31494325aced66f2e20d`                         | `9ef20df0bfded1d8fdddbb8b864202224c7faf50`                  |
| RP2350     | Tak                                                                | Tak (PIO, hardware_pio)                                     |
| Integracja | `FetchContent` + makro `add_picovga()`                             | `FetchContent` + `target_link_libraries`                    |

### Narzędzia

- pico-sdk ≥ 2.0 (wymagany dla RP2350)
- CMake ≥ 3.13
- ARM GCC toolchain (`arm-none-eabi-gcc`)
- Git (wymagany przez FetchContent)
- Zmienna środowiskowa: `PICO_SDK_PATH`
- Połączenie internetowe przy pierwszym buildzie

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
cd src/RPiPico2DisplayVGA

mkdir build && cd build

cmake .. -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350

make -j$(nproc)
```

Przy **pierwszym buildzie** CMake automatycznie sklonuje obie biblioteki
(`codaris/picovga-cmake` i `lurk101/ps2kbd-lib`) do `build/_deps/`.

Wynik: `build/src/rpi_pico2_display_vga.uf2`

### 3. Wgrywanie na Pico 2

```bash
# Metoda 1: UF2 (drag & drop)
# Przytrzymaj BOOTSEL, podłącz USB → Pico pojawi się jako dysk RPI-RP2
cp build/src/rpi_pico2_display_vga.uf2 /media/$USER/RPI-RP2/

# Metoda 2: picotool
picotool load build/src/rpi_pico2_display_vga.uf2 -f
```

### 4. Działanie

Po wgraniu monitor VGA wyświetli:

```text
RPiPico2DisplayVGA
PS/2 keyboard ready. Type below:
>
```

Klawiatura PS/2 jest gotowa do wpisywania tekstu. Każdy znak pojawia się
na ekranie VGA i jest jednocześnie wysyłany przez USB CDC
(widoczny w `minicom` / `screen` na `/dev/ttyACM*`).

---

## Uwagi

- **PIO0 vs PIO1:** PicoVGA używa PIO0 — PS/2 musi być zainicjowany z `kbd_init(1, ...)` (PIO1).
- **Zmiana pinów PS/2:** piny DATA i CLK muszą być zawsze na kolejnych GPIO (CLK = DATA+1).
- **Napięcie PS/2:** standard PS/2 to 5 V; Waveshare breakout ma pull-upy i ograniczniki — nie podłączać PS/2 bez breakouta.
- **Zmiana wersji PicoVGA:** edytuj `GIT_TAG` w głównym `CMakeLists.txt` i usuń `build/` przed kolejnym buildem.
- **Offline build:** po pierwszym buildzie `build/_deps/` jest zachowane i kolejne buildy nie wymagają internetu.

---

## Źródła

- [codaris/picovga-cmake — GitHub](https://github.com/codaris/picovga-cmake)
- [lurk101/ps2kbd-lib — GitHub](https://github.com/lurk101/ps2kbd-lib)
- [PicoVGA oryginał — Panda381 (Miroslav Nemecek)](https://github.com/Panda381/PicoVGA)
- [PicoVGA dokumentacja sprzętowa (DAC, pinout)](https://www.breatharian.eu/hw/picovga/index_en.html)
- [Waveshare VGA PS2 Board — Wiki](https://www.waveshare.com/wiki/VGA_PS2_Board)
- [Waveshare VGA PS2 Board — Schematic PDF](https://www.waveshare.com/w/upload/7/7a/VGA-PS2-Board-Schematic.pdf)
- [font8x8 — public domain IBM VGA font](https://github.com/dhepper/font8x8)
- [VGA Signal Timing 640×480@60Hz](http://tinyvga.com/vga-timing/640x480@60Hz)
- [RP2350 Datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
