Programowanie Raspberry Pi Pico za pomocą drugiego Pico (Picoprobe)
Co potrzebujesz
Pico-A — programator (zostaje na stałe jako debugger)
Pico-B — target (ten, który programujesz)
Kabel USB, kilka przewodów

Krok 1: Wgraj firmware Picoprobe na Pico-A

Pobierz picoprobe.uf2 z: https://github.com/raspberrypi/picoprobe/releases
Przytrzymaj przycisk BOOTSEL na Pico-A, podłącz USB → pojawi się jako dysk USB
Skopiuj picoprobe.uf2 na dysk → Pico-A restartuje się jako programator

Krok 2: Podłączenie Pico-A ↔ Pico-B

Pico-A (Picoprobe)	Pico-B (target)
GP2 (SWD CLK)	SWCLK (pin 24)
GP3 (SWD IO)	SWDIO (pin 23)
GND	GND
VSYS (opcjonalnie)	VSYS (zasilanie)

Krok 3: Oprogramowanie na PC

OpenOCD + GDB (darmowe, open-source):


# Ubuntu/Debian
sudo apt install openocd gdb-multiarch

# Uruchom OpenOCD z Picoprobe
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg
Alternatywa — VS Code (najprostsze):

Zainstaluj rozszerzenie Raspberry Pi Pico (oficjalne)
Zainstaluj Cortex-Debug
W ustawieniach wybierz debugger: picoprobe (CMSIS-DAP)
Flash i debug jednym kliknięciem
Pico 2 (RP2350)
Działa tak samo, ale potrzebujesz nowszego firmware:

Picoprobe v2+ obsługuje RP2350
OpenOCD ≥ 0.12 z target/rp2350.cfg
Bonus: SWD umożliwia też debugowanie
Breakpointy, podgląd rejestrów, watchpointy — wszystko za darmo bez dodatkowego sprzętu (J-Link kosztuje ~1000 zł).
