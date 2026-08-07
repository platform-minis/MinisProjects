/**
 * Hydra — przykład: i2c-scan.
 *
 * Drugie kryterium ukończenia etapu M1 (rozdz. 14). Skanuje magistralę I2C
 * i wypisuje adresy odpowiadających układów.
 *
 * Pokazuje wzorzec dostępu do magistrali z rozdz. 5: cały skan odbywa się pod
 * jedną blokadą, a pojedyncze transfery — wyłącznie wewnątrz transaction().
 * Nie ma sposobu, żeby dotknąć magistrali z pominięciem blokady, więc nie ma
 * też sposobu, żeby o niej zapomnieć.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
// Deklaracje setup() i loop(). Potrzebne, bo STM32duino umieszcza je w bloku
// extern "C" — bez tej deklaracji definicje poniżej dostają wiązanie C++
// i konsolidator ich nie znajduje. Na ESP32 i RP2040 deklaracje są zwykłe,
// więc włączenie niczego nie zmienia.
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/hal/Hal.hpp"

HYDRA_LOG_MODULE("i2cscan")

using namespace hydra;

namespace {

class ScanModule : public ModuleBase {
public:
    ScanModule() : ModuleBase("i2cscan") {}

protected:
    Status onInit() override {
        if (!hal::Hal::hasI2c(0)) {
            HYDRA_LOGE("płytka '%s' nie ma skonfigurowanej magistrali I2C",
                       hal::board::name);
            return fail(Err::NotSupported);
        }
        HYDRA_LOGI("magistrala I2C: %lu Hz",
                   static_cast<unsigned long>(hal::Hal::i2c(0).clockHz()));
        return ok();
    }

    Status onStart() override {
        Task::Cfg cfg;
        cfg.name = "i2c.scan";
        cfg.prio = Prio::Normal;
        return task_.startPeriodic(cfg, 3000, [this] { scan(); });
    }

    void onStop() override { task_.stopAndWait(); }

private:
    void scan() {
        u8 found[16];
        auto count = hal::Hal::i2c(0).scan(found, sizeof(found));
        if (!count) {
            HYDRA_LOGE("skan nieudany: %s", toString(count.error()));
            return;
        }
        if (*count == 0) {
            HYDRA_LOGW("brak układów na magistrali");
            return;
        }

        for (u8 i = 0; i < *count; ++i) {
            HYDRA_LOGI("znaleziono układ pod adresem 0x%02X", found[i]);
        }

        // Odczyt rejestru identyfikacyjnego pierwszego znalezionego układu —
        // przykład transferu rejestrowego wewnątrz sesji.
        const u8 addr = found[0];
        auto r = hal::Hal::i2c(0).transaction([addr](hal::II2cBus::Session& s) -> Status {
            HYDRA_TRY(const u8 who, s.readReg8(addr, 0x00));
            HYDRA_LOGI("0x%02X: rejestr 0x00 = 0x%02X", addr, who);
            return ok();
        });
        if (!r) HYDRA_LOGW("odczyt rejestru nieudany: %s", toString(r.error()));
    }

    Task task_;
};

ScanModule  gScan;
UartLogSink gConsole;

}  // namespace

void setup() {
    App::config()
        .name("i2c-scan")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gScan);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

void loop() {}
