#pragma once
/**
 * Hydra — punkt wejścia aplikacji (rozdz. 4.1).
 *
 * Ukrywa największą niespójność między platformami: moment startu schedulera.
 * Na ESP32 i arduino-pico scheduler działa, zanim setup() zostanie wywołane;
 * na stm32duino trzeba go wystartować ręcznie, po czym loop() nigdy się nie
 * wykona. Kod użytkownika wygląda wszędzie tak samo:
 *
 *   #include <Hydra.h>
 *
 *   void setup() {
 *       hydra::App::config()
 *           .name("rover-01")
 *           .add(myModule);
 *       hydra::App::begin();
 *   }
 *   void loop() { }   // nieużywane; na STM32 martwe z definicji
 *
 * App::begin() wykonuje kolejno: inicjalizację logów i magistrali zdarzeń,
 * init() wszystkich modułów w kolejności rejestracji, start() w tej samej
 * kolejności, utworzenie taska core.house, a na końcu — tylko na STM32 —
 * start schedulera.
 */

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Events.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/IModule.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {

class App {
public:
    /** Budowniczy konfiguracji — wszystkie metody zwracają *this do łańcuchowania. */
    class Config {
    public:
        /** Nazwa urządzenia: identyfikator w logach, telemetrii i mDNS. */
        Config& name(const char* n) { name_ = n; return *this; }

        Config& logLevel(LogLevel l) { logLevel_ = l; return *this; }
        Config& logMode(Log::Mode m) { logMode_ = m; return *this; }
        Config& logSink(ILogSink& s) { logSink_ = &s; return *this; }

        /** Okres taska core.house (watchdog, statystyki, drenaż logów). */
        Config& housekeepingMs(u32 ms) { houseMs_ = ms; return *this; }

        /** Wyłącza publikację SysHeartbeat (np. przy bardzo ciasnym budżecie RAM). */
        Config& heartbeat(bool on) { heartbeat_ = on; return *this; }

        /**
         * Karmiciel watchdoga wołany z core.house — tylko gdy predykat zdrowia
         * zwróci true. Brak karmienia = reset sprzętowy (rozdz. 13).
         */
        Config& watchdog(Delegate<void()> feeder) { wdtFeed_ = feeder; return *this; }
        Config& healthCheck(Delegate<bool()> check) { health_ = check; return *this; }

        /** Rejestruje moduł. Kolejność rejestracji = kolejność init/start. */
        Config& add(IModule& m);

        const char* deviceName() const { return name_; }

    private:
        friend class App;
        const char*       name_      = "hydra";
        LogLevel          logLevel_  = LogLevel::Info;
        Log::Mode         logMode_   = Log::Mode::Sync;
        ILogSink*         logSink_   = nullptr;
        u32               houseMs_   = 1000;
        bool              heartbeat_ = true;
        Delegate<void()>  wdtFeed_{};
        Delegate<bool()>  health_{};
        IModule*          modules_[HYDRA_MAX_MODULES] = {};
        u8                moduleCount_ = 0;
    };

    /** Konfiguracja globalna aplikacji. Modyfikować wyłącznie przed begin(). */
    static Config& config();

    /**
     * Startuje aplikację. Na STM32 nie wraca (przejmuje kontrolę scheduler).
     * Na pozostałych platformach wraca po uruchomieniu wszystkich modułów.
     */
    static Status begin();

    /** Zatrzymuje moduły w kolejności odwrotnej do rejestracji i gasi core.house. */
    static void stop();

    static bool        running();
    static const char* deviceName();
    static const char* platform() { return HYDRA_PLATFORM_NAME; }
    static Millis      uptimeMs();
    static u8          moduleCount();
    static IModule*    module(u8 index);
    /** Wyszukuje moduł po nazwie — dla shella diagnostycznego. */
    static IModule*    findModule(const char* name);

    /**
     * Jedna iteracja czynności porządkowych: drenaż kolejki ISR EventBusa,
     * drenaż logów, statystyki, watchdog. Normalnie woła ją task core.house;
     * wystawiona publicznie na potrzeby testów i buildów bez własnego taska.
     */
    static void housekeeping();

    /** Zeruje stan aplikacji. Wyłącznie do testów jednostkowych. */
    static void reset();
};

}  // namespace hydra
