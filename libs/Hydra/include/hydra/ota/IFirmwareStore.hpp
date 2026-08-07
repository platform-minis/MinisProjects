#pragma once
/**
 * Hydra — miejsce, w którym ląduje nowy obraz oprogramowania (rozdz. 7.2).
 *
 * Każda platforma robi to inaczej i te różnice są nie do ukrycia:
 *   - ESP32 ma natywne partycje OTA i przełącza wskaźnik rozruchu,
 *   - STM32 z podwójnym bankiem zamienia banki miejscami,
 *   - RP2 zapisuje obraz obok i pozwala programowi rozruchowemu go przenieść.
 *
 * Wspólne jest to, co naprawdę istotne: obraz zapisuje się fragmentami,
 * a przełączenie na niego to osobna, jawna decyzja. Ta separacja jest tu
 * kluczowa — zapis może się nie udać w połowie i wtedy urządzenie ma nadal
 * działać na starym obrazie.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_OTA

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace ota {

class IFirmwareStore {
public:
    virtual ~IFirmwareStore() = default;

    /** Nazwa do logów: "esp-ota", "stm32-dualbank", "mock". */
    virtual const char* name() const = 0;

    /** Ile bajtów zmieści się w miejscu przeznaczonym na nowy obraz. */
    virtual size_t capacity() const = 0;

    /**
     * Otwiera zapis obrazu o znanym rozmiarze. Zwraca Err::OutOfRange,
     * gdy obraz się nie mieści — lepiej dowiedzieć się o tym przed
     * pobraniem kilkuset kilobajtów niż po.
     */
    virtual Status begin(size_t imageSize) = 0;

    /** Zapisuje kolejny fragment. Kolejność wywołań wyznacza układ obrazu. */
    virtual Status write(CByteSpan chunk) = 0;

    /** Domyka zapis. Po tym obraz jest kompletny, ale jeszcze nieaktywny. */
    virtual Status finish() = 0;

    /** Porzuca rozpoczęty zapis i zwalnia zasoby. */
    virtual void abort() = 0;

    /**
     * Przełącza urządzenie na nowy obraz. Zaczyna obowiązywać po restarcie.
     * To jedyny moment, w którym cokolwiek staje się nieodwracalne — i dlatego
     * jest osobną decyzją, a nie skutkiem ubocznym zakończenia zapisu.
     */
    virtual Status commit() = 0;

    /**
     * Czy działający obraz czeka na potwierdzenie sprawności.
     *
     * Po przełączeniu na nowy obraz urządzenie startuje z niego w trybie
     * próbnym. Jeśli nowy obraz nie potwierdzi, że działa, kolejny rozruch
     * wróci do poprzedniego. To zabezpieczenie przed zamurowaniem urządzenia
     * aktualizacją, która wprawdzie się wgrała, ale nie wstaje.
     */
    virtual bool pendingVerify() const = 0;

    /** Potwierdza sprawność działającego obrazu — kończy tryb próbny. */
    virtual Status markValid() = 0;

    /** Wraca do poprzedniego obrazu przy najbliższym rozruchu. */
    virtual Status rollback() = 0;

    /** Ile bajtów zapisano w bieżącej sesji. */
    virtual size_t written() const = 0;
};

/**
 * Magazyn właściwy dla platformy. Definiuje go backend wchodzący do buildu,
 * więc aplikacja nie musi wiedzieć, czy pod spodem są partycje OTA, drugi
 * bank Flasha czy atrapa.
 */
IFirmwareStore& defaultFirmwareStore();

}  // namespace ota
}  // namespace hydra

#endif  // HYDRA_ENABLE_OTA
