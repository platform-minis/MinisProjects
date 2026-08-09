#pragma once
/**
 * Hydra — rejestr sterowników sprzętowych (rozdz. 5).
 *
 * Kontrakt interfejsów należy do Hydry, a nie do Arduino — backend jest tylko
 * ich implementacją. Dzięki temu obok backendu Arduino mogą istnieć backendy
 * natywne (ESP-IDF, Pico SDK) tam, gdzie Arduino ogranicza wydajność, oraz
 * backend atrapowy, na którym testujemy logikę modułów na PC.
 *
 * Zasada zależności dla całej warstwy hal/: wolno jej używać wyłącznie
 * fundamentów rdzenia (Types, Expected, Delegate, Rtos, Config). Sięganie po
 * App, EventBus, IModule czy Log jest zabronione i sprawdzane w CI —
 * inaczej HAL przestałby dać się użyć w izolacji.
 *
 * Brakujący sterownik nie jest błędem krytycznym: akcesor zwraca obiekt pusty,
 * który każdą operację kwituje Err::NotSupported. Kod aplikacji nie musi
 * sprawdzać wskaźników, a build bez np. ADC dalej się linkuje.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/hal/IAdc.hpp"
#include "hydra/hal/IBus.hpp"
#include "hydra/hal/IFileSystem.hpp"
#include "hydra/hal/IGpio.hpp"
#include "hydra/hal/ICamera.hpp"
#include "hydra/hal/IDac.hpp"
#include "hydra/hal/II2s.hpp"
#include "hydra/hal/IPwm.hpp"
#include "hydra/hal/IStorage.hpp"
#include "hydra/hal/ITime.hpp"

namespace hydra {
namespace hal {

/** Komplet sterowników dostarczany przez backend. */
struct Drivers {
    IGpio*    gpio    = nullptr;
    II2cBus*  i2c[2]  = {};
    ISpiBus*  spi[2]  = {};
    IUart*    uart[3] = {};
    II2s*     i2s     = nullptr;
    IPwm*     pwm     = nullptr;
    IDac*     dac     = nullptr;
    ICamera*  camera  = nullptr;
    IAdc*     adc     = nullptr;
    IStorage* storage = nullptr;
    /** System plików; na układzie dostarcza go projekt, na hoście — backend. */
    IFileSystem* fs = nullptr;
    ITime*    time    = nullptr;

    ResetReason resetReason = ResetReason::Unknown;
    /** Nazwa backendu do logów: "arduino", "mock", "esp-idf". */
    const char* name = "none";
};

class Hal {
public:
    /** Rejestruje sterowniki. Wołane przez backend, zwykle z installDefaultBackend(). */
    static Status install(const Drivers& drivers);

    /** Czy jakikolwiek backend jest zainstalowany. */
    static bool ready();
    static const char* backendName();
    static ResetReason resetReason();

    // Akcesory nigdy nie zwracają nullptra — brak sterownika daje obiekt pusty.
    static IGpio&    gpio();
    static II2cBus&  i2c(u8 index = 0);
    static ISpiBus&  spi(u8 index = 0);
    static IUart&    uart(u8 index = 0);
    static II2s&     i2s();
    static IPwm&     pwm();
    static IDac&     dac();
    static ICamera&  camera();
    static IAdc&     adc();
    static IStorage& storage();
    static IFileSystem& fileSystem();
    static ITime&    time();

    // Czy backend dostarczył realny sterownik, czy akcesor zwróci zaślepkę.
    static bool hasGpio();
    static bool hasI2c(u8 index = 0);
    static bool hasSpi(u8 index = 0);
    static bool hasUart(u8 index = 0);
    static bool hasI2s();
    static bool hasPwm();
    static bool hasDac();
    static bool hasCamera();
    static bool hasAdc();
    static bool hasStorage();
    static bool hasFileSystem();
    static bool hasTime();

    /** Usuwa rejestrację. Wyłącznie do testów jednostkowych. */
    static void reset();
};

/**
 * Instaluje backend właściwy dla platformy. Definiuje ją dokładnie jeden
 * plik backendu wchodzący do buildu (Arduino albo atrapa hostowa), więc
 * aplikacja nie musi wiedzieć, który to.
 *
 * Wywoływana leniwie przy pierwszym użyciu Hal — jawne wywołanie jest
 * potrzebne tylko wtedy, gdy chcemy poznać wynik instalacji.
 */
Status installDefaultBackend();

}  // namespace hal
}  // namespace hydra
