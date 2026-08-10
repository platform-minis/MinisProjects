/**
 * Hydra — obraz skryptu w pamięci trwałej.
 *
 * Zapis przez plik tymczasowy i podmianę: to jedyny sposób, żeby zanik
 * zasilania w trakcie zapisu zostawił poprzednią wersję nietkniętą zamiast
 * pliku obciętego w połowie.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/ImageFile.hpp"

#include <string.h>

#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("script.img")

namespace hydra {
namespace script {

namespace {

constexpr char kMagic[4] = {'H', 'S', 'I', '1'};

/** Ścieżka pliku tymczasowego — ta sama co docelowa z dopiskiem. */
constexpr const char* kTempSuffix = ".new";

void writeU32(u8* out, u32 value) {
    out[0] = static_cast<u8>(value & 0xFF);
    out[1] = static_cast<u8>((value >> 8) & 0xFF);
    out[2] = static_cast<u8>((value >> 16) & 0xFF);
    out[3] = static_cast<u8>((value >> 24) & 0xFF);
}

u32 readU32(const u8* in) {
    return static_cast<u32>(in[0]) | (static_cast<u32>(in[1]) << 8) |
           (static_cast<u32>(in[2]) << 16) | (static_cast<u32>(in[3]) << 24);
}

/** Składa ścieżkę pliku tymczasowego. */
bool tempPath(const char* path, char* out, size_t capacity) {
    const size_t n = strlen(path);
    if (n + strlen(kTempSuffix) + 1 > capacity) return false;
    memcpy(out, path, n);
    strcpy(out + n, kTempSuffix);
    return true;
}

}  // namespace

Status ImageFile::configure(const Config& cfg) {
    if (cfg.path == nullptr || cfg.path[0] == '\0') return fail(Err::BadArgument);
    cfg_ = cfg;
    return ok();
}

Status ImageFile::save(CByteSpan image) {
    if (!enabled()) return fail(Err::NotSupported);
    if (image.data() == nullptr || image.size() == 0) return fail(Err::BadArgument);

    char temp[hal::kPathMax + 8] = {};
    if (!tempPath(cfg_.path, temp, sizeof(temp))) return fail(Err::OutOfRange);

    u8 header[kHeaderSize] = {};
    memcpy(header, kMagic, sizeof(kMagic));
    writeU32(header + 4, static_cast<u32>(image.size()));
    util::Sha256::hash(image, header + 8);

    auto file = cfg_.fs->open(temp, hal::OpenMode::Write);
    if (!file) return fail(file.error());

    auto* f = file.value();
    auto  written = f->write(CByteSpan{header, sizeof(header)});
    if (written) written = f->write(image);
    const bool flushed = written && f->flush().has_value();
    (void)f->close();

    if (!flushed) {
        (void)cfg_.fs->remove(temp);
        HYDRA_LOGE("zapis obrazu nieudany");
        return fail(Err::IoError);
    }

    // Podmiana jest ostatnim krokiem i dopiero ona czyni nowy obraz aktualnym.
    if (cfg_.fs->exists(cfg_.path)) (void)cfg_.fs->remove(cfg_.path);
    HYDRA_CHECK(cfg_.fs->rename(temp, cfg_.path));

    HYDRA_LOGI("obraz zapisany, %u B", static_cast<u32>(image.size()));
    return ok();
}

Result<size_t> ImageFile::load(ByteSpan out) {
    if (!enabled()) return unexpected(Err::NotSupported);
    if (!cfg_.fs->exists(cfg_.path)) return unexpected(Err::NotFound);

    auto file = cfg_.fs->open(cfg_.path, hal::OpenMode::Read);
    if (!file) return unexpected(file.error());
    auto* f = file.value();

    u8   header[kHeaderSize] = {};
    auto head = f->read(ByteSpan{header, sizeof(header)});
    if (!head || head.value() != sizeof(header)) {
        (void)f->close();
        (void)clear();
        return unexpected(Err::BadArgument);
    }

    if (memcmp(header, kMagic, sizeof(kMagic)) != 0) {
        (void)f->close();
        (void)clear();
        HYDRA_LOGE("obraz ma obcy naglowek");
        return unexpected(Err::BadArgument);
    }

    const u32 bytes = readU32(header + 4);
    if (bytes == 0 || bytes > out.size()) {
        (void)f->close();
        HYDRA_LOGE("zapisany obraz nie miesci sie w buforze (%u B)", bytes);
        return unexpected(Err::OutOfRange);
    }

    auto body = f->read(ByteSpan{out.data(), bytes});
    (void)f->close();
    if (!body || body.value() != bytes) {
        (void)clear();
        return unexpected(Err::IoError);
    }

    u8 got[util::kSha256Size] = {};
    util::Sha256::hash(CByteSpan{out.data(), bytes}, got);
    if (!util::Sha256::equal(got, header + 8)) {
        // Plik uszkodzony — kasujemy, żeby nie próbować go przy każdym
        // rozruchu. Obraz, o którym wiadomo, że jest zły, nie ma prawa
        // wracać w nieskończoność.
        (void)clear();
        HYDRA_LOGE("zapisany obraz nie zgadza sie ze skrotem — usuniety");
        return unexpected(Err::BadArgument);
    }

    return static_cast<size_t>(bytes);
}

Status ImageFile::clear() {
    if (!enabled()) return fail(Err::NotSupported);
    if (!cfg_.fs->exists(cfg_.path)) return ok();
    return cfg_.fs->remove(cfg_.path);
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
