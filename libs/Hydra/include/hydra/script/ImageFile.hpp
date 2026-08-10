#pragma once
/**
 * Hydra — obraz skryptu zapisany w pamięci trwałej.
 *
 * Bez tego urządzenie po restarcie wraca do skryptu wkompilowanego w firmware,
 * a wszystko, co wgrano przez sieć, przepada. Przy zdalnej aktualizacji to nie
 * jest niedogodność, tylko wywrotka: urządzenie po zaniku zasilania zaczyna
 * zachowywać się inaczej niż wczoraj i nikt nie wie dlaczego.
 *
 * **Osobna klasa, nie pole w `ImageStore`.** Magazyn zajmuje się slotami
 * w RAM-ie i weryfikacją przy odbiorze; to jest inna odpowiedzialność, w innym
 * cyklu życia i z inną drogą awarii. Zapisanie obu w jednym typie znaczyłoby,
 * że `ImageStore` nie da się już przetestować bez systemu plików — a dziś się da.
 *
 * ## Format
 *
 *     magic    4 B   "HSI1"
 *     bytes    4 B   długość obrazu, little-endian
 *     sha256  32 B   skrót obrazu
 *     obraz    n B
 *
 * Skrót zapisujemy razem z obrazem i sprawdzamy przy odczycie. Pamięć trwała
 * bywa uszkodzona przez zanik zasilania w trakcie zapisu, a obraz odtworzony
 * z połowicznego pliku jest gorszy od braku obrazu: braku widać od razu,
 * a uszkodzony wywraca się w losowym momencie.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/core/Expected.hpp"
#include "hydra/hal/IFileSystem.hpp"
#include "hydra/util/Sha256.hpp"

namespace hydra {
namespace script {

class ImageFile {
public:
    /** Nagłówek pliku. Stała długość, żeby odczyt szedł jednym `read`. */
    static constexpr size_t kHeaderSize = 4 + 4 + util::kSha256Size;

    struct Config {
        hal::IFileSystem* fs = nullptr;
        /** Ścieżka obrazu. Musi przeżyć obiekt — zwykle stała w pamięci programu. */
        const char* path = "/script.img";
    };

    Status configure(const Config& cfg);

    /** Czy trwałość jest w ogóle włączona (podano system plików). */
    bool enabled() const { return cfg_.fs != nullptr; }

    /**
     * Zapisuje obraz razem ze skrótem.
     *
     * Zapis idzie do pliku tymczasowego i dopiero potem podmienia właściwy —
     * zanik zasilania w trakcie zostawia wtedy nietkniętą poprzednią wersję,
     * zamiast pliku obciętego w połowie.
     */
    Status save(CByteSpan image);

    /**
     * Wczytuje obraz do podanego bufora i sprawdza skrót.
     *
     * `Err::NotFound`, gdy pliku nie ma — to nie jest awaria, tylko urządzenie,
     * na które jeszcze nic nie wgrano. `Err::BadArgument`, gdy skrót się nie
     * zgadza; plik jest wtedy kasowany, żeby nie próbować go w nieskończoność
     * przy każdym rozruchu.
     */
    Result<size_t> load(ByteSpan out);

    /** Usuwa zapisany obraz. Brak pliku nie jest błędem. */
    Status clear();

private:
    Config cfg_{};
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
