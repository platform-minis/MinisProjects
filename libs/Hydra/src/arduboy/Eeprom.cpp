#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY

#include "hydra/arduboy/Eeprom.hpp"

#include "hydra/core/Rtos.hpp"

namespace hydra {
namespace arduboy {

Status EepromClass::begin(hal::IFileSystem& fs, const char* path) {
    fs_   = &fs;
    path_ = path;

    if (!fs.exists(path)) {
        // Pierwsze uruchomienie. Zawartość 0xFF to stan skasowanej pamięci —
        // ten sam, który gra zobaczyłaby na nowym urządzeniu, a wiele gier
        // rozpoznaje po nim „brak zapisanego rekordu".
        return ok();
    }

    auto opened = fs.open(path, hal::OpenMode::Read);
    if (!opened) return fail(opened.error());

    hal::IFile* file = *opened;
    const auto got = file->read(ByteSpan{data_, sizeof(data_)});
    const Status closed = file->close();
    if (!got) return fail(got.error());
    return closed;
}

void EepromClass::markDirty() {
    if (!dirty_) dirtySinceMs_ = static_cast<u32>(rtos::nowMs());
    dirty_ = true;
}

u8 EepromClass::read(int address) const {
    if (address < 0 || address >= static_cast<int>(kEepromBytes)) return 0xFF;
    return data_[address];
}

void EepromClass::write(int address, u8 value) {
    if (address < 0 || address >= static_cast<int>(kEepromBytes)) return;
    if (data_[address] == value) return;
    data_[address] = value;
    markDirty();
}

void EepromClass::update(int address, u8 value) {
    write(address, value);
}

Status EepromClass::commit() {
    if (!dirty_) return ok();
    if (fs_ == nullptr || path_ == nullptr) {
        // Bez nośnika dane żyją do zamknięcia programu. Znacznik kasujemy,
        // żeby nie próbować zapisu w kółko przy każdym `tick()`.
        dirty_ = false;
        return ok();
    }

    auto opened = fs_->open(path_, hal::OpenMode::Write);
    if (!opened) return fail(opened.error());

    hal::IFile* file = *opened;
    const auto written = file->write(CByteSpan{data_, sizeof(data_)});
    const Status closed = file->close();

    if (!written) return fail(written.error());
    dirty_ = false;
    return closed;
}

void EepromClass::tick() {
    if (!dirty_) return;

    const u32 now = static_cast<u32>(rtos::nowMs());
    if (now - dirtySinceMs_ < kFlushDelayMs) return;

    // Wynik celowo pomijany: gra nie ma jak zareagować na nieudany zapis
    // rekordu, a przerwanie jej działania byłoby gorsze od utraty wyniku.
    (void)commit();
}

EepromClass& eeprom() {
    static EepromClass instance;
    return instance;
}

}  // namespace arduboy
}  // namespace hydra

#endif  // HYDRA_ENABLE_ARDUBOY
