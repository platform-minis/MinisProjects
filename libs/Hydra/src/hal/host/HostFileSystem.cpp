/**
 * Hydra — implementacja systemu plików na katalogu hosta.
 *
 * Zwykłe POSIX-owe stdio. Jedyna nieoczywista część to `resolve()`, które
 * pilnuje, żeby ścieżka nie wyszła poza korzeń — test operujący na katalogu
 * tymczasowym nie może przez pomyłkę sięgnąć obok.
 */

#include "hydra/hal/HostFileSystem.hpp"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hydra {
namespace hal {

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
    DIR* handle = nullptr;

    bool next(DirEntry& out) override {
        if (!handle) return false;
        for (;;) {
            const dirent* entry = readdir(handle);
            if (!entry) return false;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            const size_t len = strlen(entry->d_name);
            if (len > kPathMax) continue;   // nazwa nie mieści się w API — pomijamy
            memcpy(out.name, entry->d_name, len + 1);
            out.isDirectory = entry->d_type == DT_DIR;
            out.size        = 0;
            return true;
        }
    }

    void close() override {
        if (handle) closedir(handle);
        handle = nullptr;
    }
};

// ---------------------------------------------------------------------------

HostFileSystem::HostFileSystem(const char* root) : root_(root) {}

HostFileSystem::~HostFileSystem() { unmount(); }

Status HostFileSystem::mount() {
    if (mounted_) return fail(Err::AlreadyExists);
    if (!root_ || !*root_) return fail(Err::BadArgument);

    // Pula otwartych uchwytów powstaje raz, przy montowaniu (rozdz. 11).
    files_ = new Slot[kHostMaxOpenFiles];
    dirs_  = new DirSlot[kHostMaxOpenDirs];

    ::mkdir(root_, 0755);   // istniejący katalog to nie błąd
    struct stat info{};
    if (stat(root_, &info) != 0 || !S_ISDIR(info.st_mode)) {
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

    DIR* dir = opendir(root_);
    if (!dir) return fail(Err::IoError);
    while (const dirent* entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char full[kFullPathMax];
        if (!resolve(entry->d_name, full, sizeof(full))) continue;
        ::remove(full);
    }
    closedir(dir);
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
        if (!dirs_[i].handle) { slot = &dirs_[i]; break; }
    }
    if (!slot) return unexpected(Err::OutOfMemory);

    slot->handle = opendir(full);
    if (!slot->handle) return unexpected(Err::NotFound);
    return static_cast<IDirectory*>(slot);
}

bool HostFileSystem::exists(const char* path) {
    char full[kFullPathMax];
    if (!mounted_ || !resolve(path, full, sizeof(full))) return false;
    struct stat info{};
    return stat(full, &info) == 0;
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
    if (::mkdir(full, 0755) != 0) return fail(Err::AlreadyExists);
    return ok();
}

Result<size_t> HostFileSystem::fileSize(const char* path) {
    char full[kFullPathMax];
    if (!mounted_) return unexpected(Err::NotInitialized);
    if (!resolve(path, full, sizeof(full))) return unexpected(Err::BadArgument);
    struct stat info{};
    if (stat(full, &info) != 0) return unexpected(Err::NotFound);
    return static_cast<size_t>(info.st_size);
}

Result<u64> HostFileSystem::totalBytes() const {
    // Host nie narzuca limitu; podajemy wartość umowną, żeby kod decydujący
    // „czy się zmieści" miał na czym pracować także w testach.
    return static_cast<u64>(16u * 1024u * 1024u);
}

Result<u64> HostFileSystem::usedBytes() const {
    if (!mounted_) return unexpected(Err::NotInitialized);

    DIR* dir = opendir(root_);
    if (!dir) return unexpected(Err::IoError);

    u64 total = 0;
    while (const dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        char full[kFullPathMax];
        if (!resolve(entry->d_name, full, sizeof(full))) continue;
        struct stat info{};
        if (stat(full, &info) == 0 && S_ISREG(info.st_mode)) {
            total += static_cast<u64>(info.st_size);
        }
    }
    closedir(dir);
    return total;
}

}  // namespace hal
}  // namespace hydra
