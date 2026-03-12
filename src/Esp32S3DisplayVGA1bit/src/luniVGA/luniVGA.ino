#include "luniVGA.h"

// ---------------------------------------------------------------------------
// Waveshare VGA PS2 Board — tryb mono (1-bit efektywny, 3 piny danych)
//
// Używa tylko MSB każdego kanału koloru (r[4], g[5], b[4]).
// Piksele: 0xFF = biały, 0x00 = czarny.
//
// Połączenia: ESP32-S3 GPIO → złącze 16BitI/O na breakoucie → sygnał VGA
//
//   GPIO   | 16BitI/O | Sygnał VGA | Rola
//   -------+----------+------------+---------------------------
//   GPIO6  | pin 15   | VGA_R2     | r[4] — MSB kanału R
//   GPIO9  | pin 11   | VGA_G2     | g[5] — MSB kanału G
//   GPIO11 | pin 18   | VGA_B1     | b[4] — MSB kanału B
//   GPIO1  | pin 7    | HSYNC      | hSync
//   GPIO2  | pin 8    | VSYNC      | vSync
//   GND    | GND      | GND        |
//   3V3    | 3V3      | 3V3        |
//
// Pozostałe piny danych (VGA_R0, R1, G0, G1, B0) pozostają niepodłączone.
//
// Jak to działa w bibliotece (tryb 8-bit R3G3B2):
//   Pixel = 0xFF (11111111): bit2=r[4]=1, bit5=g[5]=1, bit7=b[4]=1 → biały
//   Pixel = 0x00 (00000000): bit2=r[4]=0, bit5=g[5]=0, bit7=b[4]=0 → czarny
// ---------------------------------------------------------------------------
const PinConfig pins(
    -1, -1, -1, -1, 6,      // r[4]=GPIO6  (tylko MSB R, reszta -1)
    -1, -1, -1, -1, -1, 9,  // g[5]=GPIO9  (tylko MSB G, reszta -1)
    -1, -1, -1, -1, 11,     // b[4]=GPIO11 (tylko MSB B, reszta -1)
    1, 2                     // hSync=GPIO1, vSync=GPIO2
);

// Rysuje piksel monochromatyczny: on=true → biały, on=false → czarny
static inline void vgaDotMono(int x, int y, bool on)
{
    vgaDot(x, y, on ? 255 : 0, on ? 255 : 0, on ? 255 : 0);
}

void setup()
{
    VGAMode mode = MODE_640x480x60;
    if(!vgaInit(pins, mode, 8)) while(1) delay(1);

    // Szachownica 8×8 pikseli (wzorzec testowy)
    for(int y = 0; y < mode.vRes; y++)
        for(int x = 0; x < mode.hRes; x++)
            vgaDotMono(x, y, ((x >> 3) ^ (y >> 3)) & 1);

    vgaShow();
    vgaStart();
}

void loop()
{
}
