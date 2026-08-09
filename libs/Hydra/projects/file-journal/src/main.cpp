/**
 * file-journal — dziennik zdarzeń na plikach.
 *
 * Nie „przykład zapisu pliku", tylko pełny cykl życia danych na nośniku:
 * przy starcie czytamy poprzedni przebieg, w trakcie dopisujemy wpisy,
 * a gdy plik urośnie — przerzucamy go na kopię i zaczynamy od nowa.
 * Dokładnie to robi każdy rejestrator, i dokładnie tam wychodzą rzeczy,
 * których pojedynczy `write()` nie pokazuje.
 *
 * Cztery decyzje, które warto zobaczyć:
 *
 *  1. **Dopisywanie to inny tryb otwarcia**, nie `seek()` na koniec.
 *     `OpenMode::Write` skraca plik do zera przy otwarciu — jednorazowa
 *     pomyłka kasuje cały dziennik.
 *  2. **Plik jest otwierany na każdy wpis i zamykany.** Uchwyt trzymany
 *     między wpisami oznacza dane w buforze systemu, których nie ma na
 *     nośniku w chwili zaniku zasilania — czyli dziennik bez ostatnich
 *     minut, a właśnie one są interesujące.
 *  3. **Rotacja przez `rename()`**, a nie przez kopiowanie i kasowanie.
 *     Zmiana nazwy jest niepodzielna; kopia z kasowaniem ma stan pośredni,
 *     w którym istnieją dwa pliki albo żaden.
 *  4. **Odczyt czyta ogon, nie całość.** Dziennik rośnie bez ograniczeń,
 *     a bufor na stosie nie — więc `seek()` na koniec minus rozmiar bufora.
 *
 * Na celu `podglad` nośnikiem jest katalog, z którego uruchomiono program:
 * backend hostowy rejestruje go w HAL-u sam. Na układzie takiego wyboru nie
 * ma — karta czy flash to decyzja urządzenia — i tam implementację wnosi
 * projekt.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include <stdio.h>
#include <string.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/hal/Hal.hpp"

HYDRA_LOG_MODULE("journal")

using namespace hydra;

#if !HYDRA_PLAT_HOST
/**
 * Nośnik dostarcza projekt urządzenia.
 *
 * Poza anonimową przestrzenią nazw: w środku deklaracja odnosiłaby się do
 * symbolu wewnętrznego tej jednostki translacji, którego nikt nie miałby jak
 * zdefiniować — a konsolidator zgłosiłby to dopiero na końcu budowy.
 */
hydra::hal::IFileSystem& projectFileSystem();
#endif

namespace {

constexpr const char* kJournal  = "journal.txt";
constexpr const char* kPrevious = "journal.1.txt";

/** Powyżej tego rozmiaru dziennik idzie na kopię. */
constexpr size_t kRotateAtBytes = 2048;

/** Ile ostatnich bajtów czytamy przy starcie. */
constexpr size_t kTailBytes = 512;

constexpr u32 kEntryPeriodMs = 2000;

hal::IFileSystem& storage() {
#if HYDRA_PLAT_HOST
    return hal::Hal::fileSystem();
#else
    return projectFileSystem();
#endif
}

// ---------------------------------------------------------------------------

/** Dopisuje wiersz. Otwarcie i zamknięcie na każdy wpis — powód wyżej. */
Status append(hal::IFileSystem& fs, const char* line) {
    auto opened = fs.open(kJournal, hal::OpenMode::Append);
    if (!opened) return fail(opened.error());

    hal::IFile* file = *opened;
    const auto written = file->write(
        CByteSpan{reinterpret_cast<const u8*>(line), strlen(line)});

    // Zamknięcie także po nieudanym zapisie: pula uchwytów jest stała
    // i zapomniany plik zabiera jedno miejsce do końca życia programu.
    const Status closed = file->close();
    if (!written) return fail(written.error());
    return closed;
}

/**
 * Czyta ogon dziennika do bufora wołającego.
 *
 * Zwraca liczbę bajtów. Pierwszy wiersz bywa ucięty w połowie — to cena
 * czytania od pozycji, a nie od początku, i wołający musi się tego spodziewać.
 */
Result<size_t> readTail(hal::IFileSystem& fs, char* out, size_t capacity) {
    auto opened = fs.open(kJournal, hal::OpenMode::Read);
    if (!opened) return unexpected(opened.error());

    hal::IFile* file = *opened;
    const size_t size = file->size();
    const size_t want = capacity - 1 < size ? capacity - 1 : size;

    if (size > want) {
        if (auto r = file->seek(size - want); !r) {
            file->close();
            return unexpected(r.error());
        }
    }

    const auto got = file->read(ByteSpan{reinterpret_cast<u8*>(out), want});
    file->close();
    if (!got) return unexpected(got.error());

    out[*got] = '\0';
    return *got;
}

/**
 * Przerzuca dziennik na kopię, gdy urósł ponad limit.
 *
 * Poprzednia kopia jest kasowana jawnie: `rename()` na istniejący plik
 * zachowuje się różnie na różnych systemach plików, a różnica ujawnia się
 * dopiero po tygodniu pracy urządzenia.
 */
Status rotateIfNeeded(hal::IFileSystem& fs) {
    auto size = fs.fileSize(kJournal);
    if (!size || *size < kRotateAtBytes) return ok();

    if (fs.exists(kPrevious)) (void)fs.remove(kPrevious);
    HYDRA_CHECK(fs.rename(kJournal, kPrevious));

    HYDRA_LOGI("dziennik urósł do %lu B — przerzucony na %s",
               static_cast<unsigned long>(*size), kPrevious);
    return ok();
}

/** Liczba wierszy w tekście — do raportu przy starcie. */
u16 countLines(const char* text) {
    u16 lines = 0;
    for (const char* p = text; *p != '\0'; ++p) if (*p == '\n') ++lines;
    return lines;
}

// ---------------------------------------------------------------------------

class JournalModule : public ModuleBase {
public:
    JournalModule() : ModuleBase("journal") {}

protected:
    Status onInit() override {
        if (!available()) {
            // Brak nośnika jest stanem, nie awarią — ale musi być widoczny
            // od razu. Urządzenie zapisujące w próżnię dowiaduje się o tym
            // dopiero wtedy, gdy ktoś sięgnie po dane.
            HYDRA_LOGW("brak systemu plików — dziennik nie powstanie");
            return ok();
        }
        return ok();
    }

    Status onStart() override {
        if (available()) {
            reportPrevious();
            (void)rotateIfNeeded(storage());
            (void)append(storage(), "--- start ---\n");
        }

        Task::Cfg cfg;
        cfg.name = "journal.write";
        cfg.prio = Prio::Low;
        return task_.startPeriodic(cfg, kEntryPeriodMs, [this] { tick(); });
    }

    void onStop() override {
        task_.stopAndWait();
        if (available()) (void)append(storage(), "--- stop ---\n");
    }

private:
    static bool available() {
#if HYDRA_PLAT_HOST
        return hal::Hal::hasFileSystem();
#else
        return storage().mounted();
#endif
    }

    /** Co zostało po poprzednim przebiegu. */
    void reportPrevious() {
        hal::IFileSystem& fs = storage();
        if (!fs.exists(kJournal)) {
            HYDRA_LOGI("nowy dziennik na %s", hal::board::name);
            return;
        }

        char tail[kTailBytes];
        auto got = readTail(fs, tail, sizeof(tail));
        if (!got) {
            HYDRA_LOGW("nie udało się odczytać dziennika: %s", toString(got.error()));
            return;
        }

        HYDRA_LOGI("poprzedni przebieg: %lu B ogona, %u wierszy",
                   static_cast<unsigned long>(*got),
                   static_cast<unsigned>(countLines(tail)));

        // Ostatni pełny wiersz. Pierwszy bywa ucięty, bo czytamy od pozycji.
        char* last = nullptr;
        for (char* line = strtok(tail, "\n"); line != nullptr;
             line = strtok(nullptr, "\n")) {
            last = line;
        }
        if (last != nullptr) HYDRA_LOGI("  ostatni wpis: %s", last);
    }

    void tick() {
        if (!available()) return;

        // Sprawdzenie przed **każdym** wpisem, nie co dziesiąty.
        //
        // Wariant „co dziesiąty" wyglądał na oszczędność, a dawał plik
        // przerastający limit o tyle, ile zdąży urosnąć między sprawdzeniami —
        // przy krótkim przebiegu rotacja nie zachodziła w ogóle. Jedno
        // `fileSize()` jest i tak tańsze niż otwarcie, zapis i zamknięcie,
        // które robimy tuż obok.
        (void)rotateIfNeeded(storage());

        // Czas i numer wpisu. Na urządzeniu dokłada się tu zwykle wolną
        // stertę i siłę sygnału — na hoście oba są zerami i wpis wyglądałby
        // na uszkodzony, mimo że jest poprawny.
        char line[96];
        snprintf(line, sizeof(line), "%08lu  wpis #%lu  %s\n",
                 static_cast<unsigned long>(App::uptimeMs()),
                 static_cast<unsigned long>(++entries_),
                 hal::board::name);

        if (auto r = append(storage(), line); !r) {
            // Nośnik pełny albo wyjęty. Liczymy i idziemy dalej — zatrzymanie
            // urządzenia z powodu dziennika byłoby lekarstwem gorszym
            // od choroby.
            ++errors_;
            if (errors_ == 1) {
                HYDRA_LOGE("zapis do dziennika nieudany: %s", toString(r.error()));
            }
            return;
        }
    }

    Task task_{};
    u32  entries_ = 0;
    u32  errors_  = 0;
};

JournalModule gJournal;

#if HYDRA_PLAT_HOST
StdoutLogSink gConsole;
#else
UartLogSink   gConsole;
#endif

void run() {
    App::config()
        .name("file-journal")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gJournal);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

}  // namespace

#if HYDRA_PLAT_HOST

int main() {
    run();

    // Kilkanaście sekund pracy, żeby dziennik urósł i było co obejrzeć.
    // Na urządzeniu tej pętli nie ma — tam program po prostu chodzi dalej.
    const u64 startMs = App::uptimeMs();
    while (App::uptimeMs() - startMs < 12000) rtos::delayMs(100);

    App::stop();
    return 0;
}

#else

void setup() { run(); }

void loop() {
    // Cała praca dzieje się w tasku journal.write.
}

#endif
