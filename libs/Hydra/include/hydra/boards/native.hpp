#pragma once
/**
 * Płytka: native — build hostowy (PC).
 *
 * Cel `native` nie jest atrapą testową, tylko pełnoprawnym celem budowy:
 * ta sama aplikacja, ten sam rdzeń, te same taski, tylko backend HAL to atrapy
 * hostowe, a scheduler stoi na pthreadach. Służy dwóm rzeczom naraz —
 * uruchomieniu interfejsu w oknie SDL bez sprzętu i debugowaniu logiki
 * aplikacji pod sanitizerami.
 *
 * Peryferia sprzętowe są wyłączone celowo. Na hoście nie ma I2C ani SPI,
 * a włączona magistrala oznaczałaby atrapę, która milczy — czyli błąd
 * wykrywany dopiero w środku aplikacji, zamiast przy starcie.
 *
 * Rozmiar okna nie należy do płytki: podaje go cel w pliku `.hydra`
 * (sekcja `native`), a Studio przekazuje flagami HYDRA_NATIVE_WINDOW_*.
 */

#define HYDRA_BOARD_NAME "native"

// Diody nie ma; blink-task loguje zamiast migać — tak samo jak na płytce
// z diodą adresowalną.
#define HYDRA_BOARD_LED ::hydra::hal::kNoPin

#define HYDRA_BOARD_I2C0_ENABLE 0
#define HYDRA_BOARD_SPI0_ENABLE 0

// Konsola to standardowe wyjście procesu. Prędkość nie ma tu znaczenia,
// ale pole musi istnieć, bo czyta je diagnostyka.
#define HYDRA_BOARD_UART0_ENABLE 1
#define HYDRA_BOARD_UART0_BAUD   115200

#define HYDRA_BOARD_STORAGE_NS "hydra"
