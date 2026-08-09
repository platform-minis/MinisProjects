#pragma once
/**
 * Hydra — bodziec, czyli sposób wywołania zjawiska w świecie wokół urządzenia.
 *
 * Warstwy HAL, sterowników i modułów są symetryczne: ten sam kod działa na
 * atrapach i na sprzęcie, bo pod spodem podmienia się backend. Bodziec
 * symetryczny nie jest i nigdy nie będzie — na hoście „czujnik zgłasza 25 °C"
 * znaczy podmianę wartości rejestru, a na stole trzeba fizycznie zmienić świat:
 * grzałką, przekaźnikiem, drugą płytką udającą układ.
 *
 * Dlatego scenariusz testowy ma mówić o **zjawisku**, a nie o sposobie. Gdyby
 * zapisywał sposób (`setReg(0x76, 0xFA, …)`), przestałby być przenośny w chwili,
 * w której trafi na prawdziwą płytkę — a to jest cała stawka tego interfejsu.
 *
 * Środowisko odpowiada za tłumaczenie zjawiska na to, co potrafi:
 *
 *     stim.digitalInput(kBtn, true);      // host: atrapa GPIO
 *                                         // farma: sterownik przekaźnika
 *     stim.busFault(0, 3, Err::IoError);  // host: failNext na atrapie
 *                                         // farma: rozłączenie SDA
 *
 * Żadne środowisko nie potrafi wszystkiego i to jest normalne, a nie usterka:
 * host nie zna fizyki, a farma nie przyspieszy czasu. Stąd `supports()` —
 * pytanie bez skutków ubocznych, zadawane **przed** wykonaniem, żeby scenariusz
 * mógł odróżnić „sprawdzone" od „pominięte". Bez tego rozróżnienia zielony
 * wynik na hoście zaczyna być mylony z działającym urządzeniem.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/hal/Pin.hpp"

namespace hydra {
namespace stim {

/**
 * Rodzaje zjawisk.
 *
 * Lista jest wąska świadomie: obejmuje to, co da się wywołać po obu stronach —
 * na atrapach i na stanowisku ze sprzętem. Zjawiska zależne od konkretnego
 * układu (`temperatura 25 °C` dla BME280) tu nie wchodzą, bo kodowanie wartości
 * w rejestrach jest wiedzą sterownika, nie środowiska. Potrzebują emulatora
 * układu stojącego *nad* tym interfejsem, a nie kolejnej metody w nim.
 */
enum class Phenomenon : u8 {
    /** Poziom na wejściu cyfrowym — przycisk, krańcówka, sygnał z zewnątrz. */
    DigitalInput,
    /** Zbocze na wejściu: impuls enkodera, przerwanie od układu. */
    Edge,
    /** Napięcie na wejściu analogowym. */
    AnalogInput,
    /** Obecność układu na magistrali — wpięty albo nie. */
    DevicePresence,
    /** Zawartość rejestru układu; sposób, w jaki układ przedstawia świat. */
    DeviceRegister,
    /** Awaria magistrali: zwarcie, przerwa, układ przestający odpowiadać. */
    BusFault,
};

constexpr const char* toString(Phenomenon p) {
    switch (p) {
        case Phenomenon::DigitalInput:   return "digital-input";
        case Phenomenon::Edge:           return "edge";
        case Phenomenon::AnalogInput:    return "analog-input";
        case Phenomenon::DevicePresence: return "device-presence";
        case Phenomenon::DeviceRegister: return "device-register";
        case Phenomenon::BusFault:       return "bus-fault";
    }
    return "unknown";
}

/**
 * Środowisko, w którym wykonywany jest scenariusz.
 *
 * Domyślne implementacje odmawiają, więc nowe stanowisko implementuje tylko to,
 * co naprawdę potrafi — a nie musi udawać reszty. Kontrakt: `supports()` i wynik
 * wywołania mówią to samo, i nic poza tym nie odróżnia zjawiska niewspieranego
 * od nieudanego.
 */
class IStimulus {
public:
    virtual ~IStimulus() = default;

    /** Nazwa stanowiska do dziennika i raportu — „mock", „ława-3". */
    virtual const char* name() const = 0;

    /** Czy to środowisko umie wywołać takie zjawisko. Bez skutków ubocznych. */
    virtual bool supports(Phenomenon phenomenon) const = 0;

    virtual Status digitalInput(hal::PinNum pin, bool high) {
        HYDRA_UNUSED(pin); HYDRA_UNUSED(high);
        return fail(Err::NotSupported);
    }

    /** Zbocze na wejściu; kierunek wynika z konfiguracji pinu w urządzeniu. */
    virtual Status edge(hal::PinNum pin) {
        HYDRA_UNUSED(pin);
        return fail(Err::NotSupported);
    }

    virtual Status analogInput(hal::PinNum pin, u16 millivolts) {
        HYDRA_UNUSED(pin); HYDRA_UNUSED(millivolts);
        return fail(Err::NotSupported);
    }

    virtual Status devicePresence(u8 bus, u8 address, bool present) {
        HYDRA_UNUSED(bus); HYDRA_UNUSED(address); HYDRA_UNUSED(present);
        return fail(Err::NotSupported);
    }

    /**
     * Zawartość rejestru układu. `width` to 1 albo 2 bajty — szerokość jest
     * cechą układu, nie zjawiska, ale bez niej nie da się wskazać rejestru
     * jednoznacznie.
     */
    virtual Status deviceRegister(u8 bus, u8 address, u8 reg, u16 value, u8 width = 1) {
        HYDRA_UNUSED(bus); HYDRA_UNUSED(address);
        HYDRA_UNUSED(reg); HYDRA_UNUSED(value); HYDRA_UNUSED(width);
        return fail(Err::NotSupported);
    }

    /**
     * Awaria magistrali na najbliższych `count` transferach.
     *
     * Liczba transferów, a nie czas: scenariusz opisuje, ile prób ma się nie
     * udać, i to samo znaczy na hoście i na stanowisku, gdzie przekaźnik trzyma
     * linię rozwartą przez tyle wymian.
     */
    virtual Status busFault(u8 bus, u32 count, Err error = Err::IoError) {
        HYDRA_UNUSED(bus); HYDRA_UNUSED(count); HYDRA_UNUSED(error);
        return fail(Err::NotSupported);
    }

    /** Przywraca świat do stanu wyjściowego między scenariuszami. */
    virtual void reset() {}
};

}  // namespace stim
}  // namespace hydra
