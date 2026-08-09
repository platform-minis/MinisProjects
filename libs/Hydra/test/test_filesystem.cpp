/**
 * Hydra — testy warstwy plików (rozdz. 5, 13).
 *
 * Testy celują w miejsca, w których taka warstwa zwykle zawodzi: pula
 * uchwytów o stałym rozmiarze, atomowość zapisu całego pliku i granica
 * korzenia. Zwykłe „zapisz i odczytaj" sprawdzamy raz — to nie tam siedzą
 * błędy.
 */

#include <string.h>

#include "hydra/hal/HostFileSystem.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra_test.hpp"

using namespace hydra;
using namespace hydra::hal;

namespace {

/** Katalog roboczy testów — kasowany przez `format()` przy każdym przypadku. */
constexpr const char* kRoot = "build/fs-test";

CByteSpan text(const char* s) {
    return CByteSpan{reinterpret_cast<const u8*>(s), strlen(s)};
}

/** Zamontowany, pusty system plików. */
struct Fixture {
    HostFileSystem fs{kRoot};
    Fixture() {
        (void) fs.mount();
        (void) fs.format();
    }
    ~Fixture() { fs.unmount(); }
};

}  // namespace

TEST("FileSystem: zapis i odczyt pliku") {
    Fixture f;

    CHECK(static_cast<bool>(f.fs.writeFile("nota.txt", text("dane"))));
    CHECK(f.fs.exists("nota.txt"));

    u8 buffer[16] = {};
    auto read = f.fs.readFile("nota.txt", ByteSpan{buffer, sizeof(buffer)});
    CHECK(static_cast<bool>(read));
    CHECK_EQ(*read, static_cast<size_t>(4));
    CHECK(memcmp(buffer, "dane", 4) == 0);

    auto size = f.fs.fileSize("nota.txt");
    CHECK(static_cast<bool>(size));
    CHECK_EQ(*size, static_cast<size_t>(4));
}

TEST("FileSystem: brakujący plik to NotFound, nie pusty odczyt") {
    Fixture f;

    u8 buffer[8] = {};
    auto read = f.fs.readFile("nie-ma.txt", ByteSpan{buffer, sizeof(buffer)});
    CHECK(!static_cast<bool>(read));
    CHECK(read.error() == Err::NotFound);

    // Odróżnienie „pusty" od „nie istnieje" jest tu istotne: konfiguracja
    // o zerowej długości to co innego niż jej brak.
    CHECK(!f.fs.exists("nie-ma.txt"));
}

TEST("FileSystem: zbyt mały bufor jest odrzucany zamiast obcinać") {
    Fixture f;
    CHECK(static_cast<bool>(f.fs.writeFile("dlugi.txt", text("0123456789"))));

    u8 buffer[4] = {};
    auto read = f.fs.readFile("dlugi.txt", ByteSpan{buffer, sizeof(buffer)});
    CHECK(!static_cast<bool>(read));
    CHECK(read.error() == Err::OutOfRange);
}

TEST("FileSystem: nadpisanie nie zostawia pliku tymczasowego") {
    Fixture f;

    CHECK(static_cast<bool>(f.fs.writeFile("konfig.bin", text("stare"))));
    CHECK(static_cast<bool>(f.fs.writeFile("konfig.bin", text("nowe"))));

    u8 buffer[16] = {};
    auto read = f.fs.readFile("konfig.bin", ByteSpan{buffer, sizeof(buffer)});
    CHECK(static_cast<bool>(read));
    CHECK_EQ(*read, static_cast<size_t>(4));
    CHECK(memcmp(buffer, "nowe", 4) == 0);

    // Zapis atomowy idzie przez plik tymczasowy — musi po sobie posprzątać,
    // inaczej po kilkuset zapisach nośnik zapełniają śmieci.
    CHECK(!f.fs.exists("konfig.bin.tmp"));
}

TEST("FileSystem: pula uchwytów jest skończona i zwalniana") {
    Fixture f;
    CHECK(static_cast<bool>(f.fs.writeFile("a.txt", text("x"))));

    IFile* opened[kHostMaxOpenFiles] = {};
    for (size_t i = 0; i < kHostMaxOpenFiles; ++i) {
        auto file = f.fs.open("a.txt", OpenMode::Read);
        CHECK(static_cast<bool>(file));
        opened[i] = *file;
    }

    // Wyczerpanie puli to błąd do obsłużenia, a nie awaria (rozdz. 11).
    auto excess = f.fs.open("a.txt", OpenMode::Read);
    CHECK(!static_cast<bool>(excess));
    CHECK(excess.error() == Err::OutOfMemory);

    CHECK(static_cast<bool>(opened[0]->close()));
    auto reused = f.fs.open("a.txt", OpenMode::Read);
    CHECK(static_cast<bool>(reused));

    for (size_t i = 1; i < kHostMaxOpenFiles; ++i) (void) opened[i]->close();
    (void) (*reused)->close();
}

TEST("FileSystem: dopisywanie nie kasuje istniejącej treści") {
    Fixture f;
    CHECK(static_cast<bool>(f.fs.writeFile("log.txt", text("pierwszy\n"))));

    auto file = f.fs.open("log.txt", OpenMode::Append);
    CHECK(static_cast<bool>(file));
    CHECK(static_cast<bool>((*file)->writeText("drugi\n")));
    CHECK(static_cast<bool>((*file)->close()));

    u8 buffer[32] = {};
    auto read = f.fs.readFile("log.txt", ByteSpan{buffer, sizeof(buffer)});
    CHECK(static_cast<bool>(read));
    CHECK_EQ(*read, static_cast<size_t>(15));
}

TEST("FileSystem: tryb Write skraca do zera") {
    Fixture f;
    CHECK(static_cast<bool>(f.fs.writeFile("plik.txt", text("dluga-tresc"))));

    auto file = f.fs.open("plik.txt", OpenMode::Write);
    CHECK(static_cast<bool>(file));
    CHECK(static_cast<bool>((*file)->writeText("ab")));
    CHECK(static_cast<bool>((*file)->close()));

    auto size = f.fs.fileSize("plik.txt");
    CHECK(static_cast<bool>(size));
    CHECK_EQ(*size, static_cast<size_t>(2));
}

TEST("FileSystem: ścieżka nie wychodzi poza korzeń") {
    Fixture f;

    // Bez tej blokady test operujący na katalogu tymczasowym mógłby skasować
    // coś obok — a ten sam kod na urządzeniu sięgnąłby poza partycję danych.
    auto escaped = f.fs.open("../poza.txt", OpenMode::Write);
    CHECK(!static_cast<bool>(escaped));
    CHECK(escaped.error() == Err::BadArgument);

    CHECK(!static_cast<bool>(f.fs.remove("../poza.txt")));
    CHECK(!f.fs.exists("../poza.txt"));
}

TEST("FileSystem: przeglądanie katalogu pomija . i ..") {
    Fixture f;
    CHECK(static_cast<bool>(f.fs.writeFile("jeden.txt", text("1"))));
    CHECK(static_cast<bool>(f.fs.writeFile("dwa.txt", text("2"))));

    auto dir = f.fs.openDir("");
    CHECK(static_cast<bool>(dir));

    int count = 0;
    bool sawDots = false;
    DirEntry entry;
    while ((*dir)->next(entry)) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) sawDots = true;
        ++count;
    }
    (*dir)->close();

    CHECK_EQ(count, 2);
    CHECK(!sawDots);
}

TEST("FileSystem: operacje bez montowania są odrzucane") {
    HostFileSystem fs{kRoot};

    CHECK(!fs.mounted());
    CHECK(!static_cast<bool>(fs.open("cokolwiek.txt", OpenMode::Read)));
    CHECK(!static_cast<bool>(fs.remove("cokolwiek.txt")));
    CHECK(!fs.exists("cokolwiek.txt"));

    // Powtórne montowanie jest błędem, a nie cichym powodzeniem — inaczej
    // pula uchwytów powstawałaby drugi raz i pierwsza przeciekała.
    CHECK(static_cast<bool>(fs.mount()));
    CHECK(!static_cast<bool>(fs.mount()));
    fs.unmount();
}

TEST("FileSystem: usedBytes rośnie wraz z danymi") {
    Fixture f;

    auto empty = f.fs.usedBytes();
    CHECK(static_cast<bool>(empty));
    CHECK_EQ(static_cast<size_t>(*empty), static_cast<size_t>(0));

    CHECK(static_cast<bool>(f.fs.writeFile("dane.bin", text("0123456789"))));
    auto used = f.fs.usedBytes();
    CHECK(static_cast<bool>(used));
    CHECK_EQ(static_cast<size_t>(*used), static_cast<size_t>(10));

    auto total = f.fs.totalBytes();
    CHECK(static_cast<bool>(total));
    CHECK(*total > *used);
}

// ---------------------------------------------------------------------------
// Katalog uruchomienia jako system plików
// ---------------------------------------------------------------------------

TEST("host: wpis katalogu niesie prawdziwy rozmiar pliku") {
    // Wcześniej stało tu zero i każdy wpis wyglądał na pusty plik — a to
    // jedyna liczba, po której widać, czy zapis w ogóle coś zapisał.
    HostFileSystem fs{kRoot};
    REQUIRE(fs.mount().has_value());
    fs.remove("rozmiar.bin");

    const u8 payload[37] = {};
    REQUIRE(fs.writeFile("rozmiar.bin", CByteSpan{payload, sizeof(payload)}).has_value());

    auto dir = fs.openDir("");
    REQUIRE(dir.has_value());

    bool found = false;
    DirEntry entry;
    while ((*dir)->next(entry)) {
        if (strcmp(entry.name, "rozmiar.bin") != 0) continue;
        found = true;
        CHECK_EQ(static_cast<int>(entry.size), 37);
        CHECK(!entry.isDirectory);
    }
    (*dir)->close();
    CHECK(found);

    fs.remove("rozmiar.bin");
}

TEST("host: backend rejestruje katalog uruchomienia w HAL") {
    // Na celu `native` odpowiedź na pytanie „gdzie zapisać plik" jest jedna
    // i nie ma powodu, żeby każda aplikacja pisała ją od nowa.
    Hal::reset();
    mock::backend().clear();
    REQUIRE(mock::install().has_value());

    CHECK(Hal::hasFileSystem());
    CHECK(Hal::fileSystem().mounted());

    // Zapis trafia do katalogu procesu — plik musi być widoczny zwykłą drogą.
    IFileSystem& fs = Hal::fileSystem();
    fs.remove("hydra-test-cwd.tmp");
    const u8 mark[4] = {'H', 'y', 'd', 'r'};
    REQUIRE(fs.writeFile("hydra-test-cwd.tmp", CByteSpan{mark, sizeof(mark)}).has_value());
    CHECK(fs.exists("hydra-test-cwd.tmp"));

    u8 back[8] = {};
    auto got = fs.readFile("hydra-test-cwd.tmp", ByteSpan{back, sizeof(back)});
    REQUIRE(got.has_value());
    CHECK_EQ(static_cast<int>(*got), 4);
    CHECK_EQ(memcmp(back, mark, 4), 0);

    REQUIRE(fs.remove("hydra-test-cwd.tmp").has_value());
}

TEST("host: ten sam obiekt przy każdym wywołaniu — korzeń nie wędruje") {
    // Ścieżka jest ustalana raz. Późniejsze chdir() nie przesuwa korzenia:
    // aplikacja zapisuje tam, gdzie ją uruchomiono, a nie tam, gdzie zawędrowała.
    CHECK(&hostWorkingDirectory() == &hostWorkingDirectory());
}
