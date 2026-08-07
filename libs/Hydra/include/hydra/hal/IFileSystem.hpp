#pragma once
/**
 * Hydra — pliki (rozdz. 5, 13).
 *
 * Uzupełnienie `IStorage`, a nie jego zamiennik. Podział przebiega po sposobie
 * użycia, nie po wielkości danych: konfiguracja to kilkadziesiąt nazwanych
 * wartości czytanych przy starcie i zapisywanych rzadko — do tego jest
 * `IStorage` z NVS-em, który sam dba o zużycie komórek. Pliki to strumienie
 * bajtów, których liczby i nazw nie znamy z góry: wsad OTA, dziennik zdarzeń,
 * nagranie przebiegów, zasoby ekranu.
 *
 * Backendy: LittleFS (RP2, ESP32), FATFS na karcie SD, katalog na hoście.
 *
 * Czego tu celowo nie ma: uprawnień, dowiązań, czasów dostępu i przesuwania
 * kursora w zapisie. Systemy plików na mikrokontrolerach albo tego nie mają,
 * albo emulują to kosztem, którego nie widać w API — a każda taka metoda
 * musiałaby zwracać `Err::NotSupported` na połowie platform.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace hal {

/**
 * Maksymalna długość ścieżki.
 *
 * LittleFS domyślnie dopuszcza 255 znaków na nazwę, ale bufor tej wielkości
 * na stosie taska bywa całym jego zapasem. 63 znaki mieszczą realne ścieżki
 * (`/log/2026-08-07.txt`), a bufor da się trzymać lokalnie bez liczenia stosu.
 */
constexpr size_t kPathMax = 63;

/** Tryb otwarcia. Zapis zawsze dopisuje na końcu albo skraca — patrz IFile. */
enum class OpenMode : u8 {
    Read,      ///< Plik musi istnieć.
    Write,     ///< Tworzy albo skraca do zera.
    Append,    ///< Tworzy albo dopisuje na końcu.
};

/** Wpis katalogu zwracany przez `IDirectory::next()`. */
struct DirEntry {
    char   name[kPathMax + 1] = {};
    size_t size               = 0;
    bool   isDirectory        = false;
};

/**
 * Otwarty plik.
 *
 * Czas życia jest jawny: `close()` zwalnia uchwyt backendu. Destruktor też go
 * zwalnia, więc zapomnienie nie przecieka — ale wynik zapisu przepada, bo
 * destruktor nie ma jak zgłosić błędu. Kod, któremu zależy na danych, zamyka
 * plik sam i sprawdza wynik.
 */
class IFile {
public:
    virtual ~IFile() = default;

    virtual Result<size_t> read(ByteSpan out)      = 0;
    virtual Result<size_t> write(CByteSpan data)   = 0;

    /** Ustawia pozycję odczytu. Zapis jest sekwencyjny — patrz nagłówek. */
    virtual Status seek(size_t position) = 0;
    virtual size_t position() const      = 0;
    virtual size_t size() const          = 0;

    /** Wypycha bufory backendu. Bez tego dane mogą czekać w RAM-ie. */
    virtual Status flush() = 0;

    /** Zamyka plik i zgłasza błędy zapisu. Powtórne wywołanie jest bezpieczne. */
    virtual Status close() = 0;

    virtual bool isOpen() const = 0;

    // --- nakładki ----------------------------------------------------------

    /** Czyta cały plik do bufora. Błąd `OutOfRange`, gdy się nie mieści. */
    Result<size_t> readAll(ByteSpan out);

    /** Zapisuje napis bez terminatora. */
    Status writeText(const char* text);
};

/** Przeglądanie katalogu. Kolejność wpisów zależy od backendu i nie jest ustalona. */
class IDirectory {
public:
    virtual ~IDirectory() = default;

    /** Kolejny wpis. `false` oznacza koniec, nie błąd. */
    virtual bool next(DirEntry& out) = 0;
    virtual void close()             = 0;
};

/**
 * System plików.
 *
 * `open()` i `openDir()` zwracają wskaźniki na obiekty należące do backendu.
 * Backend trzyma je w puli o stałym rozmiarze ustalonej przy `mount()` —
 * zgodnie z zakazem przydziału pamięci po `App::begin()` (rozdz. 11).
 * Wyczerpanie puli to `Err::OutOfMemory`, a nie awaria.
 */
class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    virtual Status mount()   = 0;
    virtual void   unmount() = 0;
    virtual bool   mounted() const = 0;

    /**
     * Formatuje nośnik. Osobno od `mount()`, bo automatyczne formatowanie po
     * nieudanym montowaniu kasuje dane przy pierwszym błędzie odczytu — awarii,
     * z której często dałoby się wyjść.
     */
    virtual Status format() = 0;

    virtual Result<IFile*>      open(const char* path, OpenMode mode) = 0;
    virtual Result<IDirectory*> openDir(const char* path)             = 0;

    virtual bool   exists(const char* path)   = 0;
    virtual Status remove(const char* path)   = 0;
    virtual Status rename(const char* from, const char* to) = 0;
    virtual Status mkdir(const char* path)    = 0;

    /** Rozmiar pliku bez otwierania go. */
    virtual Result<size_t> fileSize(const char* path) = 0;

    /** Pojemność i zajętość w bajtach — do decyzji „czy zmieści się wsad OTA". */
    virtual Result<u64> totalBytes() const = 0;
    virtual Result<u64> usedBytes() const  = 0;

    // --- nakładki ----------------------------------------------------------

    /** Czyta cały plik. Skrót dla konfiguracji i małych zasobów. */
    Result<size_t> readFile(const char* path, ByteSpan out);

    /**
     * Zapisuje plik w całości, atomowo względem czytelników.
     *
     * Zapis idzie do pliku tymczasowego i dopiero `rename()` podmienia nazwę.
     * Bez tego przerwa w zasilaniu w połowie zapisu zostawia plik skrócony
     * do zera — czyli traci się nie nowe dane, tylko także stare.
     */
    Status writeFile(const char* path, CByteSpan data);
};

}  // namespace hal
}  // namespace hydra
