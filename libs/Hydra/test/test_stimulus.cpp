/**
 * Testy bodźca — warstwy, która oddziela „co się dzieje w świecie" od „jak to
 * wywołać w tym środowisku".
 *
 * Sprawdzian jest w każdym teście ten sam: bodziec podaje **zjawisko**, a skutek
 * odczytujemy przez zwykłe API HAL i sterowników, nigdy przez atrapę. Gdyby test
 * zaglądał do atrapy, sprawdzałby własne ustawienie zamiast zachowania — i nie
 * dałoby się go przenieść na stanowisko ze sprzętem.
 */

#include "hydra_test.hpp"

#include "hydra/core/App.hpp"
#include "hydra/drivers/sense/Bme280.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/stim/MockStimulus.hpp"

using namespace hydra;
using namespace hydra::stim;

namespace {

constexpr hal::PinNum kBtn = 4;
constexpr hal::PinNum kSense = 7;
constexpr u8          kBme = drivers::Bme280::kDefaultAddress;

/** Czy układ odpowiada — przez zwykłą magistralę, nie przez atrapę. */
bool responds(u8 address) {
    return hal::Hal::i2c(0)
        .transaction([&](hal::II2cBus::Session& s) { return s.ping(address); })
        .has_value();
}

void resetStim() {
    App::reset();
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    EventBus::reset();
    EventBus::init();
    Log::init(LogLevel::Off, Log::Mode::Sync);
}

/**
 * Stanowisko umiejące tylko poruszać pinami — takie, jakim bywa prosta ława
 * testowa z jednym przekaźnikiem.
 *
 * Istnieje w testach po to, żeby sprawdzić, że częściowe środowisko jest
 * przewidziane przez interfejs, a nie że trzeba je udawać.
 */
class PinsOnlyStimulus : public IStimulus {
public:
    const char* name() const override { return "pins-only"; }

    bool supports(Phenomenon phenomenon) const override {
        return phenomenon == Phenomenon::DigitalInput;
    }

    Status digitalInput(hal::PinNum pin, bool high) override {
        lastPin = pin;
        lastLevel = high;
        return ok();
    }

    hal::PinNum lastPin = -1;
    bool        lastLevel = false;
};

}  // namespace

// ---------------------------------------------------------------------------
// Zjawiska elektryczne
// ---------------------------------------------------------------------------

TEST("Bodziec: stan wejścia widać przez zwykły odczyt pinu") {
    resetStim();
    MockStimulus stim;

    REQUIRE(hal::Hal::gpio().configure(kBtn, hal::PinMode::Input).has_value());
    REQUIRE(stim.digitalInput(kBtn, true).has_value());

    auto level = hal::Hal::gpio().read(kBtn);
    REQUIRE(level.has_value());
    CHECK(*level);
}

TEST("Bodziec: zbocze budzi przerwanie urządzenia") {
    resetStim();
    MockStimulus stim;

    static int fired = 0;
    fired = 0;
    REQUIRE(hal::Hal::gpio().configure(kBtn, hal::PinMode::Input).has_value());
    REQUIRE(hal::Hal::gpio()
                .attachInterrupt(kBtn, hal::Edge::Falling,
                                 [](void*) { ++fired; }, nullptr)
                .has_value());

    REQUIRE(stim.edge(kBtn).has_value());
    CHECK_EQ(fired, 1);
}

TEST("Bodziec: zbocze bez słuchacza nie jest błędem") {
    // Na stole impuls pojawia się niezależnie od tego, czy urządzenie akurat
    // go słucha. Odmowa w tym miejscu byłaby własnością atrapy, nie świata.
    resetStim();
    MockStimulus stim;

    CHECK(stim.edge(kBtn).has_value());
}

TEST("Bodziec: napięcie widać przez odczyt przetwornika") {
    resetStim();
    MockStimulus stim;

    hal::AdcConfig cfg;
    REQUIRE(hal::Hal::adc().configure(kSense, cfg).has_value());
    REQUIRE(stim.analogInput(kSense, 1650).has_value());

    auto mv = hal::Hal::adc().readPinMv(kSense);
    REQUIRE(mv.has_value());
    CHECK_EQ(static_cast<int>(*mv), 1650);
}

// ---------------------------------------------------------------------------
// Magistrala i układy
// ---------------------------------------------------------------------------

TEST("Bodziec: nieobecny układ nie odpowiada na magistrali") {
    resetStim();
    MockStimulus stim;

    REQUIRE(stim.devicePresence(0, kBme, true).has_value());
    CHECK(responds(kBme));

    // Wypięcie układu to zjawisko, nie usunięcie wpisu z mapy testu.
    REQUIRE(stim.devicePresence(0, kBme, false).has_value());
    CHECK(!responds(kBme));
}

TEST("Bodziec: rejestr układu dociera do sterownika") {
    resetStim();
    MockStimulus stim;

    // Rejestr identyfikacyjny — po nim sterownik poznaje, że to jego układ.
    REQUIRE(stim.deviceRegister(0, kBme, drivers::Bme280::RegChipId,
                                drivers::Bme280::kChipId).has_value());

    drivers::Bme280 sensor;
    CHECK(sensor.probe().has_value());
}

TEST("Bodziec: ustawienie rejestru samo wprowadza układ na magistralę") {
    // Inaczej scenariusz opisujący czujnik, którego nikt wcześniej nie „wpiął",
    // po cichu nie robiłby nic.
    resetStim();
    MockStimulus stim;

    REQUIRE(stim.deviceRegister(0, 0x40, 0x00, 0x1234, 2).has_value());
    CHECK(responds(0x40));
}

TEST("Bodziec: awaria magistrali propaguje się do sterownika") {
    resetStim();
    MockStimulus stim;

    REQUIRE(stim.deviceRegister(0, kBme, drivers::Bme280::RegChipId,
                                drivers::Bme280::kChipId).has_value());
    REQUIRE(stim.busFault(0, 2, Err::IoError).has_value());

    drivers::Bme280 sensor;
    // Rozpoznanie układu pada — dokładnie tak, jak przy przerwanej linii SDA.
    CHECK(!sensor.probe().has_value());
}

TEST("Bodziec: druga magistrala nie udaje pierwszej") {
    // Ciche przekierowanie sprawiłoby, że scenariusz pisany pod dwie magistrale
    // „przechodzi" na hoście, sprawdzając dwa razy tę samą.
    resetStim();
    MockStimulus stim;

    CHECK(!stim.devicePresence(1, kBme, true).has_value());
    CHECK(!stim.busFault(1, 1).has_value());
}

TEST("Bodziec: szerokość rejestru inna niż 1 lub 2 bajty jest błędem") {
    resetStim();
    MockStimulus stim;

    CHECK(!stim.deviceRegister(0, kBme, 0xD0, 0x60, 3).has_value());
}

// ---------------------------------------------------------------------------
// Granice środowiska
// ---------------------------------------------------------------------------

TEST("Bodziec: atrapy deklarują wszystkie zjawiska z listy") {
    MockStimulus stim;

    for (auto phenomenon : {Phenomenon::DigitalInput, Phenomenon::Edge,
                            Phenomenon::AnalogInput, Phenomenon::DevicePresence,
                            Phenomenon::DeviceRegister, Phenomenon::BusFault}) {
        CHECK(stim.supports(phenomenon));
    }
}

TEST("Bodziec: częściowe stanowisko mówi wprost, czego nie potrafi") {
    PinsOnlyStimulus stim;

    CHECK(stim.supports(Phenomenon::DigitalInput));
    CHECK(!stim.supports(Phenomenon::BusFault));

    // Deklaracja i zachowanie mówią to samo — scenariusz może polegać na
    // jednym albo drugim i dostanie ten sam obraz środowiska.
    REQUIRE(stim.digitalInput(kBtn, true).has_value());
    auto refused = stim.busFault(0, 1);
    REQUIRE(!refused.has_value());
    CHECK(refused.error() == Err::NotSupported);
}

TEST("Bodziec: nazwa stanowiska trafia do wyniku") {
    // Raport z farmy bez nazwy ławy nie mówi, gdzie szukać płytki.
    MockStimulus mock;
    PinsOnlyStimulus pins;

    CHECK_STR(mock.name(), "mock");
    CHECK_STR(pins.name(), "pins-only");
}
