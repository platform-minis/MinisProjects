#include "luniVGA.h"

// ---------------------------------------------------------------------------
// Waveshare VGA PS2 Board — 8-bit mode (R3G3B2)
//
// Połączenia: ESP32-S3 GPIO → złącze 16BitI/O na breakoucie → sygnał VGA
//
//   GPIO  | 16BitI/O | Sygnał VGA
//   ------+----------+-----------
//   GPIO4 | pin 19   | VGA_R0   → r[2] (LSB bitów R)
//   GPIO5 | pin 17   | VGA_R1   → r[3]
//   GPIO6 | pin 15   | VGA_R2   → r[4] (MSB bitów R)
//   GPIO7 | pin 13   | VGA_G0   → g[3]
//   GPIO8 | pin 14   | VGA_G1   → g[4]
//   GPIO9 | pin 11   | VGA_G2   → g[5] (MSB bitów G)
//  GPIO10 | pin 20   | VGA_B0   → b[3]
//  GPIO11 | pin 18   | VGA_B1   → b[4] (MSB bitów B)
//   GPIO1 | pin 7    | HSYNC
//   GPIO2 | pin 8    | VSYNC
//   GND   | GND      | GND
//   3V3   | 3V3      | 3V3
//
// W trybie 8-bit biblioteka używa: r[2..4], g[3..5], b[3..4]
// Piny r[0..1], g[0..2], b[0..2] są ignorowane (przekazane jako -1).
// ---------------------------------------------------------------------------
const PinConfig pins(
    -1, -1, 4, 5, 6,      // r[0..4]: używane r[2]=GPIO4, r[3]=GPIO5, r[4]=GPIO6
    -1, -1, -1, 7, 8, 9,  // g[0..5]: używane g[3]=GPIO7, g[4]=GPIO8, g[5]=GPIO9
    -1, -1, -1, 10, 11,   // b[0..4]: używane b[3]=GPIO10, b[4]=GPIO11
    1, 2                   // hSync=GPIO1, vSync=GPIO2
);

void setup()
{
    VGAMode mode = MODE_640x480x60;
    if(!vgaInit(pins, mode, 8)) while(1) delay(1);

    for(int y = 0; y < mode.vRes; y++)
        for(int x = 0; x < mode.hRes; x++)
            vgaDotDit(x, y, x, y, 255-x);

    for(int y = 0; y < 30; y++)
        for(int x = 0; x < 256; x++)
        {
            vgaDot(x, y,      x, 0, 0);
            vgaDot(x, y + 30, 0, x, 0);
            vgaDot(x, y + 60, 0, 0, x);
        }

    vgaShow();
    vgaStart();
}

void loop()
{
}
