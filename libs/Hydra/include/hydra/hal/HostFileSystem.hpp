#pragma once
/**
 * Hydra — system plików oparty o katalog na hoście.
 *
 * Backend testowy i zarazem drugi port warstwy plików. Istnieje po to, żeby
 * `IFileSystem` miał od pierwszego dnia dwie implementacje: bez tego interfejs
 * kształtuje się pod jedną platformę i przy drugiej trzeba go przepisywać.
 *
 * Wszystkie ścieżki są doklejane do korzenia podanego przy konstrukcji;
 * wyjście poza niego (`..`) jest odrzucane, żeby test nie mógł skasować
 * czegoś obok katalogu roboczego.
 */

#include "hydra/hal/IFileSystem.hpp"

namespace hydra {
namespace hal {

/** Liczba jednocześnie otwartych plików. Pula stała — rozdz. 11. */
constexpr size_t kHostMaxOpenFiles = 4;
constexpr size_t kHostMaxOpenDirs  = 2;

class HostFileSystem : public IFileSystem {
public:
    /** `root` musi żyć tak długo jak obiekt — zwykle literał albo pole testu. */
    explicit HostFileSystem(const char* root);
    ~HostFileSystem() override;

    Status mount() override;
    void   unmount() override;
    bool   mounted() const override { return mounted_; }
    Status format() override;

    Result<IFile*>      open(const char* path, OpenMode mode) override;
    Result<IDirectory*> openDir(const char* path) override;

    bool   exists(const char* path) override;
    Status remove(const char* path) override;
    Status rename(const char* from, const char* to) override;
    Status mkdir(const char* path) override;

    Result<size_t> fileSize(const char* path) override;
    Result<u64>    totalBytes() const override;
    Result<u64>    usedBytes() const override;

private:
    /** Skleja korzeń ze ścieżką i odrzuca próby wyjścia poza korzeń. */
    bool resolve(const char* path, char* out, size_t cap) const;

    const char* root_    = nullptr;
    bool        mounted_ = false;

    struct Slot;
    struct DirSlot;
    Slot*    files_ = nullptr;
    DirSlot* dirs_  = nullptr;
};

}  // namespace hal
}  // namespace hydra
