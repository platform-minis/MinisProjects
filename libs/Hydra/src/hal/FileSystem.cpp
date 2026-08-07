/**
 * Hydra — nakładki wspólne dla wszystkich systemów plików.
 *
 * Backend implementuje operacje elementarne; to, co da się z nich złożyć,
 * mieszka tutaj i nie powiela się w każdej implementacji. Ten sam podział
 * co w `IStorage`.
 */

#include "hydra/hal/IFileSystem.hpp"

#include <string.h>

namespace hydra {
namespace hal {

namespace {

/**
 * Nazwa pliku tymczasowego dla zapisu atomowego.
 *
 * Przyrostek, nie osobny katalog: `rename()` między katalogami bywa na małych
 * systemach plików operacją kopiującą albo w ogóle niedozwoloną, a wtedy
 * atomowość, dla której to robimy, znika.
 */
bool makeTempPath(const char* path, char* out, size_t cap) {
    const size_t len = strlen(path);
    const char   suffix[] = ".tmp";
    if (len + sizeof(suffix) > cap) return false;
    memcpy(out, path, len);
    memcpy(out + len, suffix, sizeof(suffix));
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// IFile

Result<size_t> IFile::readAll(ByteSpan out) {
    const size_t total = size();
    if (total > out.size()) return unexpected(Err::OutOfRange);
    if (auto positioned = seek(0); !positioned) return unexpected(positioned.error());
    return read(ByteSpan{out.data(), total});
}

Status IFile::writeText(const char* text) {
    if (!text) return fail(Err::BadArgument);
    const size_t len = strlen(text);
    if (len == 0) return ok();

    auto written = write(CByteSpan{reinterpret_cast<const u8*>(text), len});
    if (!written) return fail(written.error());

    // Zapis krótszy niż żądany to brak miejsca, a nie sukces — bez tego
    // sprawdzenia obcięty plik wygląda jak poprawnie zapisany.
    return *written == len ? ok() : fail(Err::IoError);
}

// ---------------------------------------------------------------------------
// IFileSystem

Result<size_t> IFileSystem::readFile(const char* path, ByteSpan out) {
    auto file = open(path, OpenMode::Read);
    if (!file) return unexpected(file.error());

    auto result = (*file)->readAll(out);
    (*file)->close();
    return result;
}

Status IFileSystem::writeFile(const char* path, CByteSpan data) {
    if (!path) return fail(Err::BadArgument);

    char temp[kPathMax + 1];
    if (!makeTempPath(path, temp, sizeof(temp))) return fail(Err::OutOfRange);

    auto file = open(temp, OpenMode::Write);
    if (!file) return fail(file.error());

    auto written = (*file)->write(data);
    const Status closed = (*file)->close();

    if (!written || *written != data.size()) {
        remove(temp);   // nie zostawiamy śmiecia po nieudanym zapisie
        return fail(written ? Err::IoError : written.error());
    }
    if (!closed) {
        remove(temp);
        return closed;
    }

    // Podmiana nazwy jest tym miejscem, w którym zapis staje się widoczny.
    // Część systemów plików nie nadpisuje istniejącego celu, więc kasujemy go
    // tuż przed — okno, w którym pliku nie ma, jest krótsze niż zapis całości.
    if (exists(path)) {
        if (auto removed = remove(path); !removed) return removed;
    }
    return rename(temp, path);
}

}  // namespace hal
}  // namespace hydra
