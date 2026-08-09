#pragma once
/**
 * Hydra — warstwa konfiguracji kompilacyjnej (rozdz. 13.1 specyfikacji).
 *
 * Pierwsza z trzech warstw konfiguracji: stałe kompilacji.
 * Kolejne to plik płytki (katalog boards/) i konfiguracja runtime w IStorage.
 *
 * Ten nagłówek nie może zawierać żadnego #include z Arduino ani FreeRTOS —
 * jest włączany także w buildzie hostowym.
 */

// ---------------------------------------------------------------------------
// Detekcja platformy
// ---------------------------------------------------------------------------

#define HYDRA_PLAT_HOST     0
#define HYDRA_PLAT_ESP32S3  0
#define HYDRA_PLAT_ESP32C3  0
#define HYDRA_PLAT_RP2040   0
#define HYDRA_PLAT_RP2350   0
#define HYDRA_PLAT_STM32    0

#if defined(HYDRA_FORCE_HOST)
#  undef  HYDRA_PLAT_HOST
#  define HYDRA_PLAT_HOST 1
#  define HYDRA_PLATFORM_NAME "host"
#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV)
#  undef  HYDRA_PLAT_ESP32S3
#  define HYDRA_PLAT_ESP32S3 1
#  define HYDRA_PLATFORM_NAME "esp32s3"
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ESP32C3_DEV)
#  undef  HYDRA_PLAT_ESP32C3
#  define HYDRA_PLAT_ESP32C3 1
#  define HYDRA_PLATFORM_NAME "esp32c3"
#elif defined(PICO_RP2350) || defined(ARDUINO_ARCH_RP2350)
#  undef  HYDRA_PLAT_RP2350
#  define HYDRA_PLAT_RP2350 1
#  define HYDRA_PLATFORM_NAME "rp2350"
#elif defined(ARDUINO_ARCH_RP2040)
#  undef  HYDRA_PLAT_RP2040
#  define HYDRA_PLAT_RP2040 1
#  define HYDRA_PLATFORM_NAME "rp2040"
#elif defined(ARDUINO_ARCH_STM32)
#  undef  HYDRA_PLAT_STM32
#  define HYDRA_PLAT_STM32 1
#  define HYDRA_PLATFORM_NAME "stm32"
#else
#  undef  HYDRA_PLAT_HOST
#  define HYDRA_PLAT_HOST 1
#  define HYDRA_PLATFORM_NAME "host"
#endif

#define HYDRA_PLAT_ESP32 (HYDRA_PLAT_ESP32S3 || HYDRA_PLAT_ESP32C3)
#define HYDRA_PLAT_RP2   (HYDRA_PLAT_RP2040  || HYDRA_PLAT_RP2350)

// ---------------------------------------------------------------------------
// Właściwości platformy
// ---------------------------------------------------------------------------

/** Sprzętowa jednostka zmiennoprzecinkowa. RP2040 (M0+) jej nie ma — rozdz. 15. */
#ifndef HYDRA_HAS_FPU
#  if HYDRA_PLAT_RP2040
#    define HYDRA_HAS_FPU 0
#  else
#    define HYDRA_HAS_FPU 1
#  endif
#endif

/** Wielordzeniowy scheduler FreeRTOS (pinowanie tasków ma sens). */
#ifndef HYDRA_HAS_SMP
#  if HYDRA_PLAT_ESP32S3 || HYDRA_PLAT_RP2
#    define HYDRA_HAS_SMP 1
#  else
#    define HYDRA_HAS_SMP 0
#  endif
#endif

/**
 * Scheduler trzeba wystartować ręcznie (vTaskStartScheduler) — rozdz. 4.1.
 * Na ESP32 i arduino-pico scheduler działa zanim setup() zostanie wywołane.
 */
#ifndef HYDRA_MANUAL_SCHEDULER
#  if HYDRA_PLAT_STM32
#    define HYDRA_MANUAL_SCHEDULER 1
#  else
#    define HYDRA_MANUAL_SCHEDULER 0
#  endif
#endif

/** Pamięć zewnętrzna PSRAM (bufory UI i sieci lądują tam automatycznie — rozdz. 11). */
#ifndef HYDRA_HAS_PSRAM
#  if HYDRA_PLAT_ESP32S3
#    define HYDRA_HAS_PSRAM 1
#  else
#    define HYDRA_HAS_PSRAM 0
#  endif
#endif

// ---------------------------------------------------------------------------
// Moduły opcjonalne (rozdz. 1: "moduł opcjonalny albo nie istnieje")
// ---------------------------------------------------------------------------

#ifndef HYDRA_ENABLE_UI
#  define HYDRA_ENABLE_UI 0
#endif
#ifndef HYDRA_ENABLE_NET
#  define HYDRA_ENABLE_NET 0
#endif
#ifndef HYDRA_ENABLE_SENSE
#  define HYDRA_ENABLE_SENSE 0
#endif
#ifndef HYDRA_ENABLE_MOTION
#  define HYDRA_ENABLE_MOTION 0
#endif
/**
 * Moduł skryptowy z osadzonym interpreterem Lua.
 *
 * Wyłączony nie kosztuje nic: bez tej flagi nie powstaje ani `ScriptModule`,
 * ani komendy shella, a nieużywane jednostki osadzonego Lua usuwa konsolidator
 * przez --gc-sections. Sam `script::Interp` da się użyć bez tej flagi — wtedy
 * płaci się za interpreter dokładnie wtedy, gdy się go woła.
 */
#ifndef HYDRA_ENABLE_SCRIPT
#  define HYDRA_ENABLE_SCRIPT 0
#endif

/**
 * Moduł IoT platformy MyCastle.
 *
 * Niezależny od HYDRA_ENABLE_NET, choć zwykle chodzą razem: węzeł na końcu
 * magistrali RS-485 nie ma stosu TCP/IP i nie ma powodu, żeby go kompilować.
 * Odwrotnie też — bramka bez encji własnych używa sieci bez tego modułu.
 */
#ifndef HYDRA_ENABLE_MINIS
#  define HYDRA_ENABLE_MINIS 0
#endif

/**
 * Potok multimedialny: audio i pojedyncze klatki obrazu.
 *
 * Wyłączony nie kosztuje nic — a włączony kosztuje tyle, ile pule buforów,
 * które sam zadeklarujesz. Framework nie ma tu żadnej pamięci własnej.
 */
#ifndef HYDRA_ENABLE_MEDIA
#  define HYDRA_ENABLE_MEDIA 0
#endif

/**
 * Warstwa zgodności z Arduboy2 — gry na Arduboya bez zmian w źródle.
 *
 * Kosztuje dwa kilobajty pamięci na bufory obrazu (stronicowy gry i wierszowy
 * dla `gfx`) plus pół kilobajta na czcionkę. Włączenie dokłada też osobny
 * korzeń włączeń `include/compat/arduboy`, skąd biorą się nazwy globalne
 * `Arduboy2`, `WIDTH`, `A_BUTTON` i reszta — bez włączonego modułu nie widzi
 * ich nikt.
 */
#ifndef HYDRA_ENABLE_ARDUBOY
#  define HYDRA_ENABLE_ARDUBOY 0
#endif

// ---------------------------------------------------------------------------
// Rozmiary statyczne (rozdz. 11: brak alokacji po App::begin())
// ---------------------------------------------------------------------------

/** Maksymalny rozmiar ładunku zdarzenia. Większe dane idą wskaźnikiem — rozdz. 4.3. */
#ifndef HYDRA_EVENT_MAX_SIZE
#  define HYDRA_EVENT_MAX_SIZE 32
#endif

/** Maksymalna liczba subskrypcji w całym systemie. */
#ifndef HYDRA_EVENT_MAX_SUBS
#  if HYDRA_PLAT_HOST
#    define HYDRA_EVENT_MAX_SUBS 64
#  else
#    define HYDRA_EVENT_MAX_SUBS 32
#  endif
#endif

/** Głębokość kolejki odroczonych publikacji z ISR. */
#ifndef HYDRA_EVENT_ISR_QUEUE_LEN
#  define HYDRA_EVENT_ISR_QUEUE_LEN 16
#endif

/** Maksymalna liczba modułów rejestrowanych w App. */
#ifndef HYDRA_MAX_MODULES
#  define HYDRA_MAX_MODULES 8
#endif

/** Rozmiar bufora pierścieniowego logów zrzucanego po awarii (rozdz. 13). */
#ifndef HYDRA_LOG_RING_SIZE
#  define HYDRA_LOG_RING_SIZE 2048
#endif

/** Maksymalna długość pojedynczej linii logu. */
#ifndef HYDRA_LOG_LINE_MAX
#  define HYDRA_LOG_LINE_MAX 160
#endif

/** Domyślny rozmiar stosu tasków (w słowach na FreeRTOS, w bajtach na hoście). */
#ifndef HYDRA_DEFAULT_STACK
#  define HYDRA_DEFAULT_STACK 4096
#endif

// ---------------------------------------------------------------------------
// Atrybuty i pomocnicze makra
// ---------------------------------------------------------------------------

#define HYDRA_NODISCARD [[nodiscard]]

/** Funkcja wołana z ISR — na ESP32 trafia do IRAM. */
#if HYDRA_PLAT_ESP32
/**
 * Na ESP32 procedura obsługi przerwania musi leżeć w IRAM — flash bywa
 * niedostępny w chwili zgłoszenia. Atrybut definiuje ESP-IDF w esp_attr.h;
 * sięgamy tam wprost, bo warstwy frameworka nie widzą nagłówków Arduino,
 * przez które ten symbol normalnie się pojawia.
 */
#  if __has_include(<esp_attr.h>)
#    include <esp_attr.h>
#    define HYDRA_ISR_ATTR IRAM_ATTR
#  else
#    define HYDRA_ISR_ATTR __attribute__((section(".iram1.hydra")))
#  endif
#else
#  define HYDRA_ISR_ATTR
#endif

/** Jawne oznaczenie nieużywanego parametru (np. rdzeń SMP na platformie 1-rdzeniowej). */
#define HYDRA_UNUSED(x) (void)(x)
