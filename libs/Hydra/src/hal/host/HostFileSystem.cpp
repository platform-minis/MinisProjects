/**
 * Hydra — implementacja systemu plików na katalogu hosta.
 *
 * Katalogi i metadane przez `std::filesystem`, zawartość plików przez stdio.
 * Jeden kod dla Linuksa, macOS i Windows: `dirent.d_type`, `DT_DIR` i
 * dwuargumentowy `mkdir` nie istnieją w mingw, a rozgałęzienia `#ifdef _WIN32`
 * dałyby dwie ścieżki do utrzymania i przetestowania zamiast jednej.
 *
 * Wszędzie warianty z `std::error_code`, nigdy rzucające: ten sam nagłówek
 * bywa kompilowany bez obsługi wyjątków, a błąd systemu plików jest tu
 * zwyczajną wartością zwracaną (`Err::NotFound`), nie sytuacją wyjątkową.
 *
 * Jedyna nieoczywista część to `resolve()`, które pilnuje, żeby ścieżka nie
 * wyszła poza korzeń — test operujący na katalogu tymczasowym nie może przez
 * pomyłkę sięgnąć obok.
 */

#include "hydra/hal/HostFileSystem.hpp"

#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace hydra {
namespace hal {

namespace fs = std::filesystem;

namespace {

/** Bufor na pełną ścieżkę: korzeń + separator + ścieżka względna. */
constexpr size_t kFullPathMax = 256;

}  // namespace

// ---------------------------------------------------------------------------

struct HostFileSystem::Slot : IFile {
    FILE* handle = nullptr;

    Result<size_t> read(ByteSpan out) override {
        if (!handle) return unexpected(Err::NotInitialized);
        const size_t n = fread(out.data(), 1, out.size(), handle);
        // Krótki odczyt to koniec pliku, nie błąd — błąd rozpoznaje ferror().
        if (n < out.size() && ferror(handle)) return unexpected(Err::IoError);
        return n;
    }

    Result<size_t> write(CByteSpan data) override {
        if (!handle) return unexpected(Err::NotInitialized);
        const size_t n = fwrite(data.data(), 1, data.size(), handle);
        if (n != data.size()) return unexpected(Err::IoError);
        return n;
    }

    Status seek(size_t position) override {
        if (!handle) return fail(Err::NotInitialized);
        return fseek(handle, static_cast<long>(position), SEEK_SET) == 0
            ? ok() : fail(Err::IoError);
    }

    size_t position() const override {
        if (!handle) return 0;
        const long p = ftell(handle);
        return p < 0 ? 0 : static_cast<size_t>(p);
    }

    size_t size() const override {
        if (!handle) return 0;
        const long here = ftell(handle);
        if (here < 0) return 0;
        fseek(handle, 0, SEEK_END);
        const long end = ftell(handle);
        fseek(handle, here, SEEK_SET);
        return end < 0 ? 0 : static_cast<size_t>(end);
    }

    Status flush() override {
        if (!handle) return fail(Err::NotInitialized);
        return fflush(handle) == 0 ? ok() : fail(Err::IoError);
    }

    Status close() override {
        if (!handle) return ok();          // powtórne zamknięcie jest bezpieczne
        const int result = fclose(handle);
        handle = nullptr;
        return result == 0 ? ok() : fail(Err::IoError);
    }

    bool isOpen() const override { return handle != nullptr; }
};

struct HostFileSystem::DirSlot : IDirectory {
    /*
     * Iterator zamiast `DIR*`.
     *
     * `directory_iterator` sam pomija „." i „..", więc odpada filtrowanie ich
     * po nazwie. Rodzaj wpisu bierzemy z `is_directory()`, a nie z `d_type`:
     * tamto pole jest rozszerzeniem, którego mingw nie ma, a część systemów
     * plików i tak zwraca w nim `DT_UNKNOWN`.
     */
    fs::directory_iterator it{};
    bool open = false;

    bool next(DirEntry& out) override {
        if (!open) return false;
        const fs::directory_iterator end{};

        while (it != end) {
            const fs::directory_entry entry = *it;
            std::error_code ec;
            it.increment(ec);
            if (ec) { close(); return false; }

            const std::string name = entry.path().filename().string();
            if (name.size() > kPathMax) continue;   // nazwa nie mieści się w API — pomijamy

            memcpy(out.name, name.c_str(), name.size() + 1);
            out.isDirectory = entry.is_directory(ec) && !ec;

            // Rozmiar odczytujemy naprawdę. Wcześniej stało tu zero i każdy
            // wpis wyglądał na pusty plik — a to jest jedyna liczba, po której
            // widać, czy zapis w ogóle coś zapisał. Katalogi zostają na zerze,
            // bo `file_size()` nie jest dla nich określone.
            if (out.isDirectory) {
                out.size = 0;
            } else {
                std::error_code sizeEc;
                const auto bytes = entry.file_size(sizeEc);
                out.size = sizeEc ? 0 : static_cast<size_t>(bytes);
            }
            return true;
        }
        return false;
    }

    void close() override {
        it = fs::directory_iterator{};
        open = false;
    }
};

// ---------------------------------------------------------------------------

IFileSystem& hostWorkingDirectory() {
    // Bufor statyczny, bo HostFileSystem trzyma korzeń wskaźnikiem i wymaga,
    // żeby przeżył obiekt. Ścieżka jest ustalana raz — patrz nagłówek.
    static char root[512] = {};
    static HostFileSystem instance{root};

    if (root[0] == '\0') {
        std::error_code ec;
        const auto cwd = fs::current_path(ec);
        const std::string text = ec ? std::string(".") : cwd.string();
        const size_t n = text.size() < sizeof(root) - 1 ? text.size() : sizeof(root) - 1;
        memcpy(root, text.c_str(), n);
        root[n] = '\0';
    }
    return instance;
}

HostFileSystem::HostFileSystem(const char* root) : root_(root) {}

HostFileSystem::~HostFileSystem() { unmount(); }

Status HostFileSystem::mount() {
    if (mounted_) return fail(Err::AlreadyExists);
    if (!root_ || !*root_) return fail(Err::BadArgument);

    // Pula otwartych uchwytów powstaje raz, przy montowaniu (rozdz. 11).
    files_ = new Slot[kHostMaxOpenFiles];
    dirs_  = new DirSlot[kHostMaxOpenDirs];

    std::error_code ec;
    fs::create_directories(root_, ec);   // istniejący katalog to nie błąd
    if (!fs::is_directory(root_, ec)) {
        unmount();
        return fail(Err::IoError);
    }
    mounted_ = true;
    return ok();
}

void HostFileSystem::unmount() {
    if (files_) {
        for (size_t i = 0; i < kHostMaxOpenFiles; ++i) files_[i].close();
        delete[] files_;
        files_ = nullptr;
    }
    if (dirs_) {
        for (size_t i = 0; i < kHostMaxOpenDirs; ++i) dirs_[i].close();
        delete[] dirs_;
        dirs_ = nullptr;
    }
    mounted_ = false;
}

Status HostFileSystem::format() {
    if (!mounted_) return fail(Err::NotInitialized);

    std::error_code ec;
    fs::directory_iterator it(root_, ec);
    if (ec) return fail(Err::IoError);

    // Najpierw lista, potem usuwanie: modyfikowanie katalogu w trakcie
    // chodzenia po nim iteratorem jest zachowaniem nieokreślonym.
    std::vector<fs::path> entries;
    for (const fs::directory_entry& entry : it) entries.push_back(entry.path());

    for (const fs::path& entry : entries) {
        std::error_code removeEc;
        fs::remove_all(entry, removeEc);
    }
    return ok();
}

bool HostFileSystem::resolve(const char* path, char* out, size_t cap) const {
    if (!path || !root_) return false;

    // Wyjście poza korzeń odrzucamy w całości, zamiast próbować normalizować:
    // normalizacja ścieżek to miejsce, w którym łatwo o pomyłkę, a tu nie ma
    // powodu, żeby jakakolwiek ścieżka Hydry zawierała „..".
    if (strstr(path, "..") != nullptr) return false;

    while (*path == '/') ++path;                    // ścieżki bezwzględne API są względne wobec korzenia
    const int written = snprintf(out, cap, "%s/%s", root_, path);
    return written > 0 && static_cast<size_t>(written) < cap;
}

Result<IFile*> HostFileSystem::open(const char* path, OpenMode mode) {
    if (!mounted_) return unexpected(Err::NotInitialized);

    char full[kFullPathMax];
    if (!resolve(path, full, sizeof(full))) return unexpected(Err::BadArgument);

    Slot* slot = nullptr;
    for (size_t i = 0; i < kHostMaxOpenFiles; ++i) {
        if (!files_[i].isOpen()) { slot = &files_[i]; break; }
    }
    if (!slot) return unexpected(Err::OutOfMemory);

    const char* fmode = mode == OpenMode::Read   ? "rb"
                      : mode == OpenMode::Write  ? "wb+"
                                                 : "ab+";
    slot->handle = fopen(full, fmode);
    if (!slot->handle) {
        return unexpected(mode == OpenMode::Read ? Err::NotFound : Err::IoError);
    }
    return static_cast<IFile*>(slot);
}

Result<IDirectory*> HostFileSystem::openDir(const char* path) {
    if (!mounted_) return unexpected(Err::NotInitialized);

    char full[kFullPathMax];
    if (!resolve(path ? path : "", full, sizeof(full))) return unexpected(Err::BadArgument);

    DirSlot* slot = nullptr;
    for (size_t i = 0; i < kHostMaxOpenDirs; ++i) {
        if (!dirs_[i].open) { slot = &dirs_[i]; break; }
    }
    if (!slot) return unexpected(Err::OutOfMemory);

    std::error_code ec;
    slot->it = fs::directory_iterator(full, ec);
    if (ec) return unexpected(Err::NotFound);
    slot->open = true;
    return static_cast<IDirectory*>(slot);
}

bool HostFileSystem::exists(const char* path) {
    char full[kFullPathMax];
    if (!mounted_ || !resolve(path, full, sizeof(full))) return false;
    std::error_code ec;
    return fs::exists(full, ec) && !ec;
}

Status HostFileSystem::remove(const char* path) {
    char full[kFullPathMax];
    if (!mounted_) return fail(Err::NotInitialized);
    if (!resolve(path, full, sizeof(full))) return fail(Err::BadArgument);
    if (::remove(full) != 0) return fail(Err::NotFound);
    return ok();
}

Status HostFileSystem::rename(const char* from, const char* to) {
    char fullFrom[kFullPathMax];
    char fullTo[kFullPathMax];
    if (!mounted_) return fail(Err::NotInitialized);
    if (!resolve(from, fullFrom, sizeof(fullFrom))) return fail(Err::BadArgument);
    if (!resolve(to, fullTo, sizeof(fullTo)))       return fail(Err::BadArgument);
    return ::rename(fullFrom, fullTo) == 0 ? ok() : fail(Err::IoError);
}

Status HostFileSystem::mkdir(const char* path) {
    char full[kFullPathMax];
    if (!mounted_) return fail(Err::NotInitialized);
    if (!resolve(path, full, sizeof(full))) return fail(Err::BadArgument);
    std::error_code ec;
    // `false` bez błędu znaczy „katalog już był" — to samo, co dotąd zgłaszał
    // niezerowy kod `mkdir` z EEXIST.
    if (!fs::create_directory(full, ec) || ec) return fail(Err::AlreadyExists);
    return ok();
}

Result<size_t> HostFileSystem::fileSize(const char* path) {
    char full[kFullPathMax];
    if (!mounted_) return unexpected(Err::NotInitialized);
    if (!resolve(path, full, sizeof(full))) return unexpected(Err::BadArgument);
    std::error_code ec;
    const auto size = fs::file_size(full, ec);
    if (ec) return unexpected(Err::NotFound);
    return static_cast<size_t>(size);
}

Result<u64> HostFileSystem::totalBytes() const {
    // Host nie narzuca limitu; podajemy wartość umowną, żeby kod decydujący
    // „czy się zmieści" miał na czym pracować także w testach.
    return static_cast<u64>(16u * 1024u * 1024u);
}

Result<u64> HostFileSystem::usedBytes() const {
    if (!mounted_) return unexpected(Err::NotInitialized);

    std::error_code ec;
    fs::directory_iterator it(root_, ec);
    if (ec) return unexpected(Err::IoError);

    u64 total = 0;
    for (const fs::directory_entry& entry : it) {
        // Pliki ukryte pomijamy tak jak dotąd — katalog roboczy hosta bywa
        // dzielony z narzędziami, które trzymają w nim własne `.coś`.
        if (entry.path().filename().string()[0] == '.') continue;

        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc) || entryEc) continue;
        const auto size = entry.file_size(entryEc);
        if (!entryEc) total += static_cast<u64>(size);
    }
    return total;
}

}  // namespace hal
}  // namespace hydra
