/**
 * Hydra — backend Arduino: pamięć trwała, czas, przyczyna resetu, montaż backendu.
 *
 * Pamięć trwała to miejsce, w którym platformy różnią się najmocniej. ESP32 ma
 * pełnoprawny magazyn klucz→wartość (NVS przez Preferences). RP2 i STM32 mają
 * tylko emulowany EEPROM — czyli surowy bufor bajtów. Dla nich implementujemy
 * tu minimalny magazyn o stałej liczbie slotów: prosty, przewidywalny i bez
 * alokacji, kosztem braku defragmentacji.
 */

#include "ArduinoBackend.hpp"

#if !HYDRA_PLAT_HOST

#include <string.h>

#include "hydra/hal/Board.hpp"

#if HYDRA_PLAT_ESP32
#  include <Preferences.h>
#  include <esp_system.h>
#else
#  include <EEPROM.h>
#endif

#if HYDRA_PLAT_RP2 && __has_include(<hardware/watchdog.h>)
#  include <hardware/watchdog.h>
#  define HYDRA_HAS_RP2_WATCHDOG 1
#endif

namespace hydra {
namespace hal {
namespace arduino {

// ---------------------------------------------------------------------------
// Pamięć trwała
// ---------------------------------------------------------------------------

#if HYDRA_PLAT_ESP32

namespace {
Preferences& prefs() {
    static Preferences p;
    return p;
}
}  // namespace

Status ArduinoStorage::begin(const char* nameSpace, bool readOnly) {
    if (!nameSpace) return fail(Err::BadArgument);
    if (open_) prefs().end();
    if (!prefs().begin(nameSpace, readOnly)) return fail(Err::IoError);
    open_ = true;
    return ok();
}

void ArduinoStorage::end() {
    if (open_) prefs().end();
    open_ = false;
}

Status ArduinoStorage::setBlob(const char* key, CByteSpan data) {
    if (!open_) return fail(Err::NotInitialized);
    if (!key || strlen(key) > kStorageKeyMax) return fail(Err::BadArgument);
    const size_t written = prefs().putBytes(key, data.data(), data.size());
    return written == data.size() ? ok() : fail(Err::IoError);
}

Result<size_t> ArduinoStorage::getBlob(const char* key, ByteSpan out) {
    if (!open_) return unexpected(Err::NotInitialized);
    if (!prefs().isKey(key)) return unexpected(Err::NotFound);
    const size_t stored = prefs().getBytesLength(key);
    prefs().getBytes(key, out.data(), out.size());
    return stored;
}

Status ArduinoStorage::erase(const char* key) {
    if (!open_) return fail(Err::NotInitialized);
    return prefs().remove(key) ? ok() : fail(Err::NotFound);
}

Status ArduinoStorage::eraseAll() {
    if (!open_) return fail(Err::NotInitialized);
    return prefs().clear() ? ok() : fail(Err::IoError);
}

bool ArduinoStorage::has(const char* key) { return open_ && prefs().isKey(key); }

Status ArduinoStorage::commit() { return ok(); }  // NVS zapisuje natychmiast

#else  // ---------------------------------------------------------------------

/**
 * Dostęp do emulowanego EEPROM-u. Dwa rdzenie, dwa różne API.
 *
 * arduino-pico trzyma całą zawartość w RAM-ie i zapisuje ją do flasha dopiero
 * przy commit() — dokładnie tak, jak zakłada ten magazyn.
 *
 * STM32duino udostępnia dwa poziomy. Wygodne EEPROM.write() wywołuje pod
 * spodem eeprom_buffer_flush() po **każdym bajcie**, a na G4 nie ma prawdziwej
 * pamięci EEPROM — flush oznacza skasowanie i przepisanie całej strony flasha.
 * Zapis 64-bajtowej wartości kosztowałby 64 kasowania strony: wolno i kosztem
 * żywotności pamięci. Dlatego sięgamy po warstwę buforowaną i kasujemy stronę
 * raz, przy commit().
 */
namespace {

#if HYDRA_PLAT_RP2

inline void eepromLoad(size_t size)          { EEPROM.begin(size); }
inline u8   eepromRead(size_t i)             { return EEPROM.read(static_cast<int>(i)); }
inline void eepromWrite(size_t i, u8 v)      { EEPROM.write(static_cast<int>(i), v); }
inline bool eepromFlush()                    { return EEPROM.commit(); }

#else  // STM32

inline void eepromLoad(size_t)               { eeprom_buffer_fill(); }
inline u8   eepromRead(size_t i)             { return eeprom_buffered_read_byte(static_cast<uint32_t>(i)); }
inline void eepromWrite(size_t i, u8 v)      { eeprom_buffered_write_byte(static_cast<uint32_t>(i), v); }
inline bool eepromFlush()                    { eeprom_buffer_flush(); return true; }

#endif

}  // namespace


namespace {

/**
 * Magazyn klucz→wartość nad emulowanym EEPROM-em.
 * Układ: [magic:u32][slot 0][slot 1]…, slot = [used:u8][key:16][len:u8][value:64].
 */
constexpr u32    kMagic     = 0x48594452;  // "HYDR"
constexpr size_t kKeyBytes  = kStorageKeyMax + 1;
constexpr size_t kValBytes  = 64;
constexpr size_t kSlotBytes = 1 + kKeyBytes + 1 + kValBytes;
constexpr size_t kHeader    = sizeof(u32);

size_t slotCount() { return (HYDRA_BOARD_EEPROM_SIZE - kHeader) / kSlotBytes; }
size_t slotOffset(size_t i) { return kHeader + i * kSlotBytes; }

bool slotUsed(size_t i) { return eepromRead(slotOffset(i)) == 1; }

void readKey(size_t i, char* out) {
    const size_t base = slotOffset(i) + 1;
    for (size_t k = 0; k < kKeyBytes; ++k) out[k] = static_cast<char>(eepromRead(base + k));
    out[kKeyBytes - 1] = '\0';
}

/** Indeks slotu z danym kluczem albo -1. */
int findSlot(const char* key) {
    char buf[kKeyBytes];
    for (size_t i = 0; i < slotCount(); ++i) {
        if (!slotUsed(i)) continue;
        readKey(i, buf);
        if (strncmp(buf, key, kStorageKeyMax) == 0) return static_cast<int>(i);
    }
    return -1;
}

int findFreeSlot() {
    for (size_t i = 0; i < slotCount(); ++i) {
        if (!slotUsed(i)) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

Status ArduinoStorage::begin(const char* nameSpace, bool) {
    HYDRA_UNUSED(nameSpace);
    eepromLoad(HYDRA_BOARD_EEPROM_SIZE);

    // Pierwsze uruchomienie albo pamięć po innym programie — czyścimy nagłówek.
    u32 magic = 0;
    for (size_t b = 0; b < sizeof(magic); ++b) {
        reinterpret_cast<u8*>(&magic)[b] = eepromRead(b);
    }
    if (magic != kMagic) {
        for (size_t i = 0; i < slotCount(); ++i) eepromWrite(slotOffset(i), 0);
        const u32 magicValue = kMagic;
        for (size_t b = 0; b < sizeof(magicValue); ++b) {
            eepromWrite(b, reinterpret_cast<const u8*>(&magicValue)[b]);
        }
        eepromFlush();
    }
    open_ = true;
    return ok();
}

void ArduinoStorage::end() {
    eepromFlush();
    open_ = false;
}

Status ArduinoStorage::setBlob(const char* key, CByteSpan data) {
    if (!open_) return fail(Err::NotInitialized);
    if (!key || strlen(key) > kStorageKeyMax) return fail(Err::BadArgument);
    if (data.size() > kValBytes) return fail(Err::OutOfRange);

    int slot = findSlot(key);
    if (slot < 0) slot = findFreeSlot();
    if (slot < 0) return fail(Err::OutOfMemory);

    size_t off = slotOffset(static_cast<size_t>(slot));
    eepromWrite(off++, 1);
    for (size_t k = 0; k < kKeyBytes; ++k) {
        eepromWrite(off + k, k < strlen(key) ? static_cast<u8>(key[k]) : 0);
    }
    off += kKeyBytes;
    eepromWrite(off++, static_cast<u8>(data.size()));
    for (size_t b = 0; b < data.size(); ++b) eepromWrite(off + b, data[b]);
    return ok();
}

Result<size_t> ArduinoStorage::getBlob(const char* key, ByteSpan out) {
    if (!open_) return unexpected(Err::NotInitialized);
    const int slot = findSlot(key);
    if (slot < 0) return unexpected(Err::NotFound);

    size_t off = slotOffset(static_cast<size_t>(slot)) + 1 + kKeyBytes;
    const size_t stored = eepromRead(off++);
    const size_t n = stored < out.size() ? stored : out.size();
    for (size_t b = 0; b < n; ++b) out[b] = eepromRead(off + b);
    return stored;
}

Status ArduinoStorage::erase(const char* key) {
    if (!open_) return fail(Err::NotInitialized);
    const int slot = findSlot(key);
    if (slot < 0) return fail(Err::NotFound);
    eepromWrite(slotOffset(static_cast<size_t>(slot)), 0);
    return ok();
}

Status ArduinoStorage::eraseAll() {
    if (!open_) return fail(Err::NotInitialized);
    for (size_t i = 0; i < slotCount(); ++i) eepromWrite(slotOffset(i), 0);
    return ok();
}

bool ArduinoStorage::has(const char* key) { return open_ && findSlot(key) >= 0; }

Status ArduinoStorage::commit() {
    if (!open_) return fail(Err::NotInitialized);
    return eepromFlush() ? ok() : fail(Err::IoError);
}

#endif  // HYDRA_PLAT_ESP32

// ---------------------------------------------------------------------------
// Czas kalendarzowy
// ---------------------------------------------------------------------------

Result<u64> ArduinoTime::epochSec() const {
    if (!synced_) return unexpected(Err::NotInitialized);
    // Zegar kalendarzowy prowadzimy na czasie monotonicznym: RTC bywa nieobecny
    // albo bez podtrzymania, a moduł net i tak resynchronizuje przez NTP.
    return epoch_ + (rtos::nowMs() - setAtMs_) / 1000u;
}

Status ArduinoTime::setEpochSec(u64 epoch) {
    epoch_   = epoch;
    setAtMs_ = rtos::nowMs();
    synced_  = true;
    return ok();
}

// ---------------------------------------------------------------------------
// Przyczyna resetu
// ---------------------------------------------------------------------------

ResetReason readResetReason() {
#if HYDRA_PLAT_ESP32
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return ResetReason::PowerOn;
        case ESP_RST_SW:       return ResetReason::Software;
        case ESP_RST_PANIC:    return ResetReason::Panic;
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:      return ResetReason::Watchdog;
        case ESP_RST_BROWNOUT: return ResetReason::Brownout;
        case ESP_RST_DEEPSLEEP: return ResetReason::DeepSleep;
        default:               return ResetReason::Unknown;
    }
#elif defined(HYDRA_HAS_RP2_WATCHDOG)
    return watchdog_caused_reboot() ? ResetReason::Watchdog : ResetReason::PowerOn;
#elif HYDRA_PLAT_STM32 && defined(__HAL_RCC_GET_FLAG)
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) || __HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) {
        return ResetReason::Watchdog;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))  return ResetReason::Software;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST))  return ResetReason::Brownout;
    return ResetReason::PowerOn;
#else
    return ResetReason::Unknown;
#endif
}

// ---------------------------------------------------------------------------
// Montaż backendu
// ---------------------------------------------------------------------------

namespace {

/**
 * Nazwy pól celowo nie brzmią i2c0/spi0/uart0.
 *
 * pico-sdk definiuje dokładnie takie **makra** (`#define i2c0 (&i2c0_inst)`),
 * więc pole o tej nazwie rozwija się w środku deklaracji struktury i daje
 * błędy wskazujące na nagłówki pico-sdk, nie na to miejsce. Przyrostek Bus
 * usuwa kolizję i przy okazji czyta się lepiej.
 */
struct Backend {
    ArduinoGpio    gpio;
    ArduinoPwm     pwm;
    ArduinoAdc     adc;
    ArduinoI2c     i2cBus0;
    ArduinoSpi     spiBus0;
    ArduinoUart    uartBus0{&Serial};
    ArduinoStorage storage;
    ArduinoTime    time;
};

Backend& backend() {
    static Backend b;
    return b;
}

}  // namespace

}  // namespace arduino

Status installDefaultBackend() {
    auto& b = arduino::backend();

    Drivers d;
    d.gpio = &b.gpio;
    d.pwm  = &b.pwm;
    d.adc  = &b.adc;
    d.time = &b.time;

#if HYDRA_PLAT_ESP32
    // I2S rejestrujemy, ale nie uruchamiamy: piny i częstotliwość zależą od
    // przetwornika, którego HAL nie zna. Otwiera go `media::I2sSink` przy
    // starcie potoku, na podstawie tego, co uzgodniły elementy.
    d.i2s = &arduino::i2sBackend();
#endif

#if HYDRA_BOARD_I2C0_ENABLE
    if (b.i2cBus0.begin(0, HYDRA_BOARD_I2C0_SDA, HYDRA_BOARD_I2C0_SCL, HYDRA_BOARD_I2C0_HZ)) {
        d.i2c[0] = &b.i2cBus0;
    }
#endif
#if HYDRA_BOARD_SPI0_ENABLE
    if (b.spiBus0.begin(HYDRA_BOARD_SPI0_SCK, HYDRA_BOARD_SPI0_MISO, HYDRA_BOARD_SPI0_MOSI)) {
        d.spi[0] = &b.spiBus0;
    }
#endif
#if HYDRA_BOARD_UART0_ENABLE
    {
        UartConfig cfg;
        cfg.baud = HYDRA_BOARD_UART0_BAUD;
        cfg.rx   = HYDRA_BOARD_UART0_RX;
        cfg.tx   = HYDRA_BOARD_UART0_TX;
        if (b.uartBus0.begin(cfg)) d.uart[0] = &b.uartBus0;
    }
#endif

    if (b.storage.begin(HYDRA_BOARD_STORAGE_NS, false)) d.storage = &b.storage;

    d.resetReason = arduino::readResetReason();
    d.name        = "arduino/" HYDRA_BOARD_NAME;
    return Hal::install(d);
}

}  // namespace hal
}  // namespace hydra

#endif  // !HYDRA_PLAT_HOST
