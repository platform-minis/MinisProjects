/** Testy warstwy HAL na backendzie atrapowym (rozdz. 5). */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/hal/Hal.hpp"
#include "hydra/hal/Mock.hpp"

using namespace hydra;
using namespace hydra::hal;

namespace {

/** Przywraca atrapy do stanu wyjściowego przed każdym przypadkiem. */
mock::Backend& fresh() {
    Hal::reset();
    mock::backend().clear();
    mock::install();
    return mock::backend();
}

constexpr PinNum kLed  = 2;
constexpr PinNum kBtn  = 3;
constexpr PinNum kCs   = 5;
constexpr PinNum kServo = 7;
constexpr PinNum kBatt = 1;

}  // namespace

// ---------------------------------------------------------------------------
// Rejestr
// ---------------------------------------------------------------------------

TEST("Hal: backend instaluje się leniwie przy pierwszym użyciu") {
    Hal::reset();
    CHECK(!Hal::ready());

    // Samo sięgnięcie po sterownik uruchamia instalację — nie ma kroku,
    // o którym aplikacja mogłaby zapomnieć.
    Hal::gpio();
    CHECK(Hal::ready());
    CHECK_STR(Hal::backendName(), "mock");
    CHECK(Hal::resetReason() == ResetReason::PowerOn);
}

TEST("Hal: brakujący sterownik zwraca obiekt pusty, nie nullptr") {
    Hal::reset();
    Drivers empty;
    empty.name = "pusty";
    REQUIRE(Hal::install(empty).has_value());

    CHECK(!Hal::hasGpio());
    CHECK(!Hal::hasI2c(0));
    CHECK(!Hal::hasAdc());

    // Operacje kończą się czytelnym błędem zamiast wywrócenia programu.
    CHECK(Hal::gpio().write(1, true).error() == Err::NotSupported);
    CHECK(Hal::adc().readRaw(1).error() == Err::NotSupported);
    CHECK(Hal::storage().begin("x", false).error() == Err::NotSupported);
    CHECK(Hal::time().epochSec().error() == Err::NotInitialized);

    auto r = Hal::i2c(0).transaction([](II2cBus::Session& s) { return s.ping(0x42); });
    CHECK(r.error() == Err::NotSupported);

    fresh();
}

TEST("Hal: indeks poza zakresem daje obiekt pusty zamiast błędu pamięci") {
    fresh();
    CHECK(!Hal::hasI2c(9));
    CHECK(!Hal::hasUart(9));
    CHECK(Hal::uart(9).begin(UartConfig{}).error() == Err::NotSupported);
}

// ---------------------------------------------------------------------------
// GPIO
// ---------------------------------------------------------------------------

TEST("GPIO: zapis wymaga wcześniejszej konfiguracji pinu") {
    auto& mockHal = fresh();

    CHECK(Hal::gpio().write(kLed, true).error() == Err::NotInitialized);

    REQUIRE(Hal::gpio().configure(kLed, PinMode::Output).has_value());
    REQUIRE(Hal::gpio().write(kLed, true).has_value());
    CHECK(mockHal.gpio.state(kLed).level);
    CHECK(mockHal.gpio.state(kLed).mode == PinMode::Output);

    REQUIRE(Hal::gpio().toggle(kLed).has_value());
    CHECK(!mockHal.gpio.state(kLed).level);
    CHECK_EQ(static_cast<int>(mockHal.gpio.state(kLed).writes), 2);
}

TEST("GPIO: nakładki OutputPin i InputPin") {
    auto& mockHal = fresh();

    OutputPin led(kLed);
    REQUIRE(led.begin(false).has_value());
    REQUIRE(led.high().has_value());
    CHECK(mockHal.gpio.state(kLed).level);
    REQUIRE(led.low().has_value());
    CHECK(!mockHal.gpio.state(kLed).level);

    InputPin btn(kBtn);
    REQUIRE(btn.begin(PinMode::InputPullUp).has_value());
    mockHal.gpio.setInputLevel(kBtn, true);
    auto v = btn.read();
    REQUIRE(v.has_value());
    CHECK(*v);

    // Pin nieobsadzony to poprawna konfiguracja, ale operacje na nim są błędem.
    OutputPin none(kNoPin);
    CHECK(none.begin().error() == Err::BadArgument);
}

TEST("GPIO: przerwanie woła zarejestrowany handler z argumentem") {
    auto& mockHal = fresh();

    static int  hits = 0;
    static int* seen = nullptr;
    int         arg  = 42;
    hits = 0;
    seen = nullptr;

    REQUIRE(Hal::gpio()
                .attachInterrupt(kBtn, Edge::Falling,
                                 [](void* a) {
                                     ++hits;
                                     seen = static_cast<int*>(a);
                                 },
                                 &arg)
                .has_value());

    CHECK(mockHal.gpio.triggerInterrupt(kBtn));
    CHECK_EQ(hits, 1);
    CHECK(seen == &arg);
    CHECK(mockHal.gpio.state(kBtn).edge == Edge::Falling);

    REQUIRE(Hal::gpio().detachInterrupt(kBtn).has_value());
    CHECK(!mockHal.gpio.triggerInterrupt(kBtn));
    CHECK_EQ(hits, 1);
}

// ---------------------------------------------------------------------------
// I2C
// ---------------------------------------------------------------------------

TEST("I2C: transfery rejestrowe wewnątrz sesji") {
    auto& mockHal = fresh();
    REQUIRE(mockHal.i2c.addDevice(0x68).has_value());
    mockHal.i2c.setReg(0x68, 0x75, 0x71);  // WHO_AM_I

    u8 who = 0;
    auto r = Hal::i2c(0).transaction([&](II2cBus::Session& s) -> Status {
        HYDRA_TRY(const u8 v, s.readReg8(0x68, 0x75));
        who = v;
        return s.writeReg8(0x68, 0x6B, 0x01);  // wybudzenie układu
    });

    REQUIRE(r.has_value());
    CHECK_EQ(static_cast<int>(who), 0x71);

    auto stored = mockHal.i2c.getReg(0x68, 0x6B);
    REQUIRE(stored.has_value());
    CHECK_EQ(static_cast<int>(*stored), 0x01);
}

TEST("I2C: odczyt wielobajtowy zwraca kolejne rejestry") {
    auto& mockHal = fresh();
    REQUIRE(mockHal.i2c.addDevice(0x76).has_value());
    mockHal.i2c.setReg(0x76, 0xF7, 0x11);
    mockHal.i2c.setReg(0x76, 0xF8, 0x22);
    mockHal.i2c.setReg(0x76, 0xF9, 0x33);

    u8 buf[3] = {};
    auto r = Hal::i2c(0).transaction([&](II2cBus::Session& s) {
        return s.readReg(0x76, 0xF7, ByteSpan{buf, sizeof(buf)});
    });

    REQUIRE(r.has_value());
    CHECK_EQ(static_cast<int>(buf[0]), 0x11);
    CHECK_EQ(static_cast<int>(buf[1]), 0x22);
    CHECK_EQ(static_cast<int>(buf[2]), 0x33);
}

TEST("I2C: skan znajduje wyłącznie obecne układy") {
    auto& mockHal = fresh();
    REQUIRE(mockHal.i2c.addDevice(0x3C).has_value());
    REQUIRE(mockHal.i2c.addDevice(0x68).has_value());

    u8 found[8] = {};
    auto count = Hal::i2c(0).scan(found, sizeof(found));
    REQUIRE(count.has_value());
    CHECK_EQ(static_cast<int>(*count), 2);
    CHECK_EQ(static_cast<int>(found[0]), 0x3C);
    CHECK_EQ(static_cast<int>(found[1]), 0x68);

    mockHal.i2c.removeDevice(0x3C);
    count = Hal::i2c(0).scan(found, sizeof(found));
    REQUIRE(count.has_value());
    CHECK_EQ(static_cast<int>(*count), 1);
}

TEST("I2C: błąd transferu propaguje się z sesji do wołającego") {
    auto& mockHal = fresh();
    REQUIRE(mockHal.i2c.addDevice(0x68).has_value());
    mockHal.i2c.failNext(1, Err::IoError);

    auto r = Hal::i2c(0).transaction([](II2cBus::Session& s) -> Status {
        HYDRA_TRY(const u8 v, s.readReg8(0x68, 0x00));
        HYDRA_UNUSED(v);
        return ok();
    });
    CHECK(!r.has_value());
    CHECK(r.error() == Err::IoError);

    // Kolejna transakcja przechodzi — awaria była jednorazowa.
    CHECK(Hal::i2c(0)
              .transaction([](II2cBus::Session& s) { return s.ping(0x68); })
              .has_value());
}

TEST("I2C: układ nieobecny zgłasza NotFound, nie zawiesza magistrali") {
    fresh();

    auto r = Hal::i2c(0).transaction([](II2cBus::Session& s) { return s.ping(0x50); });
    CHECK(r.error() == Err::NotFound);

    // Blokada została zwolniona mimo błędu — druga transakcja nie czeka.
    auto r2 = Hal::i2c(0).transaction([](II2cBus::Session&) { return ok(); }, 50);
    CHECK(r2.has_value());
}

TEST("I2C: puste ciało transakcji jest odrzucane") {
    fresh();
    CHECK(Hal::i2c(0).transaction(II2cBus::Body{}).error() == Err::BadArgument);
    CHECK(Hal::i2c(0).scan(nullptr, 4).error() == Err::BadArgument);
}

// ---------------------------------------------------------------------------
// SPI
// ---------------------------------------------------------------------------

TEST("SPI: transakcja opuszcza i podnosi CS") {
    auto& mockHal = fresh();
    REQUIRE(Hal::gpio().configure(kCs, PinMode::Output).has_value());
    REQUIRE(Hal::gpio().write(kCs, true).has_value());

    const u8 response[] = {0xAB, 0xCD};
    mockHal.spi.queueResponse(CByteSpan{response, sizeof(response)});

    SpiConfig cfg;
    cfg.clockHz = 8000000;
    cfg.mode    = 3;

    u8 rx[2] = {};
    bool csLowInside = true;

    auto r = Hal::spi(0).transaction(kCs, cfg, [&](ISpiBus::Session& s) -> Status {
        csLowInside = !mockHal.gpio.state(kCs).level;
        const u8 tx[] = {0x9F, 0x00};
        return s.transfer(CByteSpan{tx, sizeof(tx)}, ByteSpan{rx, sizeof(rx)});
    });

    REQUIRE(r.has_value());
    CHECK(csLowInside);
    CHECK(mockHal.gpio.state(kCs).level);  // CS z powrotem w górze
    CHECK_EQ(static_cast<int>(rx[0]), 0xAB);
    CHECK_EQ(static_cast<int>(rx[1]), 0xCD);
    CHECK_EQ(static_cast<int>(mockHal.spi.lastConfig().mode), 3);
    CHECK_EQ(static_cast<int>(mockHal.spi.captured()[0]), 0x9F);
}

TEST("SPI: CS wraca w górę także po błędzie ciała") {
    auto& mockHal = fresh();
    REQUIRE(Hal::gpio().configure(kCs, PinMode::Output).has_value());
    REQUIRE(Hal::gpio().write(kCs, true).has_value());

    auto r = Hal::spi(0).transaction(kCs, SpiConfig{},
                                    [](ISpiBus::Session&) { return fail(Err::Protocol); });

    CHECK(r.error() == Err::Protocol);
    // Zapomniana deselekcja zablokowałaby magistralę pozostałym układom.
    CHECK(mockHal.gpio.state(kCs).level);
}

// ---------------------------------------------------------------------------
// UART
// ---------------------------------------------------------------------------

TEST("UART: zapis i odczyt przez atrapę") {
    auto& mockHal = fresh();

    UartConfig cfg;
    cfg.baud = 9600;
    REQUIRE(Hal::uart(0).begin(cfg).has_value());
    CHECK_EQ(static_cast<int>(mockHal.uart.config().baud), 9600);

    const char msg[] = "hydra";
    const size_t sent =
        Hal::uart(0).write(CByteSpan{reinterpret_cast<const u8*>(msg), 5});
    CHECK_EQ(static_cast<int>(sent), 5);
    CHECK_EQ(static_cast<int>(mockHal.uart.sent().size()), 5);
    CHECK_EQ(static_cast<int>(mockHal.uart.sent()[0]), static_cast<int>('h'));

    const u8 incoming[] = {'o', 'k'};
    mockHal.uart.inject(CByteSpan{incoming, sizeof(incoming)});
    CHECK_EQ(static_cast<int>(Hal::uart(0).available()), 2);

    u8 buf[2] = {};
    CHECK_EQ(static_cast<int>(Hal::uart(0).read(ByteSpan{buf, sizeof(buf)})), 2);
    CHECK_EQ(static_cast<int>(buf[0]), static_cast<int>('o'));
}

// ---------------------------------------------------------------------------
// PWM
// ---------------------------------------------------------------------------

TEST("PWM: wypełnienie w promilach") {
    auto& mockHal = fresh();

    REQUIRE(Hal::pwm().configure(kServo, 1000, 10).has_value());
    REQUIRE(Hal::pwm().setDutyPermille(kServo, 250).has_value());
    CHECK_EQ(static_cast<int>(mockHal.pwm.channel(kServo).permille), 250);
    CHECK_EQ(static_cast<int>(Hal::pwm().frequencyHz(kServo)), 1000);

    CHECK(Hal::pwm().setDutyPermille(kServo, 1001).error() == Err::OutOfRange);
    CHECK(Hal::pwm().setDutyPermille(40, 500).error() == Err::NotInitialized);
}

TEST("PWM: szerokość impulsu serwa przelicza się na wypełnienie") {
    auto& mockHal = fresh();
    REQUIRE(Hal::pwm().configure(kServo, kServoFreqHz, 12).has_value());

    // 50 Hz → okres 20 ms. Impuls 1500 µs to 7,5% wypełnienia.
    REQUIRE(Hal::pwm().writeMicroseconds(kServo, kServoCenterUs).has_value());
    CHECK_EQ(static_cast<int>(mockHal.pwm.channel(kServo).permille), 75);

    REQUIRE(Hal::pwm().writeMicroseconds(kServo, kServoMaxUs).has_value());
    CHECK_EQ(static_cast<int>(mockHal.pwm.channel(kServo).permille), 100);

    // Impuls dłuższy niż okres to błąd konfiguracji, nie „prawie działa".
    CHECK(Hal::pwm().writeMicroseconds(kServo, 25000).error() == Err::OutOfRange);
    // Kanał nieskonfigurowany nie ma częstotliwości, więc nie da się przeliczyć.
    CHECK(Hal::pwm().writeMicroseconds(41, 1500).error() == Err::NotInitialized);
}

TEST("PWM: zwolnienie kanału udostępnia go ponownie") {
    fresh();
    REQUIRE(Hal::pwm().configure(kServo, 1000, 10).has_value());
    REQUIRE(Hal::pwm().release(kServo).has_value());
    CHECK_EQ(static_cast<int>(Hal::pwm().frequencyHz(kServo)), 0);
    CHECK(Hal::pwm().release(kServo).error() == Err::NotFound);
}

// ---------------------------------------------------------------------------
// ADC
// ---------------------------------------------------------------------------

TEST("ADC: kalibracja uwzględnia dzielnik, wzmocnienie i offset") {
    auto& mockHal = fresh();

    REQUIRE(Hal::adc().configure(kBatt, AdcConfig{}).has_value());
    mockHal.adc.setPinMv(kBatt, 1650);

    // Bez kalibracji: napięcie na pinie.
    auto plain = Hal::adc().readMv(kBatt);
    REQUIRE(plain.has_value());
    CHECK_EQ(static_cast<int>(*plain), 1650);

    // Dzielnik 1:2 — mierzymy połowę napięcia baterii.
    AdcCalibration cal;
    cal.dividerNum = 2;
    cal.dividerDen = 1;
    REQUIRE(Hal::adc().setCalibration(kBatt, cal).has_value());

    auto battery = Hal::adc().readMv(kBatt);
    REQUIRE(battery.has_value());
    CHECK_EQ(static_cast<int>(*battery), 3300);

    // Korekta wzmocnienia i przesunięcia zera.
    cal.gainPermille = 990;
    cal.offsetMv     = -30;
    REQUIRE(Hal::adc().setCalibration(kBatt, cal).has_value());

    auto trimmed = Hal::adc().readMv(kBatt);
    REQUIRE(trimmed.has_value());
    CHECK_EQ(static_cast<int>(*trimmed), 3300 * 990 / 1000 - 30);
}

TEST("ADC: zerowy mianownik dzielnika jest odrzucany") {
    fresh();
    AdcCalibration bad;
    bad.dividerDen = 0;
    CHECK(Hal::adc().setCalibration(kBatt, bad).error() == Err::BadArgument);
}

TEST("ADC: surowe zliczenia i pin nieskonfigurowany") {
    auto& mockHal = fresh();
    REQUIRE(Hal::adc().configure(kBatt, AdcConfig{}).has_value());
    mockHal.adc.setPinMv(kBatt, 3300);

    auto raw = Hal::adc().readRaw(kBatt);
    REQUIRE(raw.has_value());
    CHECK_EQ(static_cast<int>(*raw), 4095);
    CHECK_EQ(static_cast<int>(Hal::adc().resolutionBits()), 12);

    CHECK(Hal::adc().readPinMv(44).error() == Err::NotInitialized);
}

// ---------------------------------------------------------------------------
// Pamięć trwała
// ---------------------------------------------------------------------------

TEST("Storage: zapis i odczyt wartości typowanych") {
    fresh();
    auto& s = Hal::storage();
    REQUIRE(s.begin("test", false).has_value());

    REQUIRE(s.setU32("count", 12345).has_value());
    REQUIRE(s.setI32("offset", -42).has_value());
    REQUIRE(s.setBool("enabled", true).has_value());
    REQUIRE(s.setFloat("gain", 1.25f).has_value());
    REQUIRE(s.setString("ssid", "domowa-siec").has_value());

    auto count = s.getU32("count");
    REQUIRE(count.has_value());
    CHECK_EQ(static_cast<int>(*count), 12345);

    auto offset = s.getI32("offset");
    REQUIRE(offset.has_value());
    CHECK_EQ(*offset, -42);

    auto enabled = s.getBool("enabled");
    REQUIRE(enabled.has_value());
    CHECK(*enabled);

    auto gain = s.getFloat("gain");
    REQUIRE(gain.has_value());
    CHECK(*gain > 1.24f && *gain < 1.26f);

    char ssid[32] = {};
    auto len = s.getString("ssid", ssid, sizeof(ssid));
    REQUIRE(len.has_value());
    CHECK_STR(ssid, "domowa-siec");
    CHECK_EQ(static_cast<int>(*len), 11);
}

TEST("Storage: brak klucza zwraca wartość domyślną, nie błąd") {
    fresh();
    auto& s = Hal::storage();
    REQUIRE(s.begin("test", false).has_value());

    auto missing = s.getU32("nieistnieje", 777);
    REQUIRE(missing.has_value());
    CHECK_EQ(static_cast<int>(*missing), 777);
    CHECK(!s.has("nieistnieje"));

    // Ale odczyt napisu bez klucza to już jawny brak.
    char buf[8];
    CHECK(s.getString("nieistnieje", buf, sizeof(buf)).error() == Err::NotFound);
}

TEST("Storage: kasowanie pojedynczego klucza i całej przestrzeni") {
    fresh();
    auto& s = Hal::storage();
    REQUIRE(s.begin("test", false).has_value());

    REQUIRE(s.setU32("a", 1).has_value());
    REQUIRE(s.setU32("b", 2).has_value());
    CHECK(s.has("a"));

    REQUIRE(s.erase("a").has_value());
    CHECK(!s.has("a"));
    CHECK(s.has("b"));
    CHECK(s.erase("a").error() == Err::NotFound);

    REQUIRE(s.eraseAll().has_value());
    CHECK(!s.has("b"));
}

TEST("Storage: operacje przed begin() są odrzucane") {
    fresh();
    auto& s = Hal::storage();
    CHECK(s.setU32("x", 1).error() == Err::NotInitialized);
    CHECK(s.getBlob("x", ByteSpan{}).error() == Err::NotInitialized);
}

TEST("Storage: odczyt typem innym niż zapisany jest wykrywany") {
    fresh();
    auto& s = Hal::storage();
    REQUIRE(s.begin("test", false).has_value());

    REQUIRE(s.setBool("flag", true).has_value());
    // Zapisano bajt, czytamy cztery — to nie jest cicha konwersja.
    CHECK(s.getU32("flag").error() == Err::Protocol);
}

// ---------------------------------------------------------------------------
// Czas
// ---------------------------------------------------------------------------

TEST("Time: zegar kalendarzowy wymaga synchronizacji") {
    fresh();
    CHECK(!Hal::time().synchronized());
    CHECK(Hal::time().epochSec().error() == Err::NotInitialized);

    REQUIRE(Hal::time().setEpochSec(1700000000).has_value());
    CHECK(Hal::time().synchronized());

    auto now = Hal::time().epochSec();
    REQUIRE(now.has_value());
    CHECK(*now >= 1700000000);

    auto dt = Hal::time().utc();
    REQUIRE(dt.has_value());
    CHECK_EQ(static_cast<int>(dt->year), 2023);
    CHECK_EQ(static_cast<int>(dt->month), 11);
    CHECK_EQ(static_cast<int>(dt->day), 14);
}

TEST("Time: czas monotoniczny jest niezależny od kalendarzowego") {
    fresh();
    const Millis t0 = Hal::time().monotonicMs();
    rtos::delayMs(5);
    const Millis t1 = Hal::time().monotonicMs();
    CHECK(t1 >= t0 + 4);

    // Cofnięcie zegara kalendarzowego nie rusza czasu monotonicznego.
    REQUIRE(Hal::time().setEpochSec(1000).has_value());
    CHECK(Hal::time().monotonicMs() >= t1);
}

TEST("Time: konwersje epoki i daty są odwracalne") {
    DateTime dt = toDateTime(0);
    CHECK_EQ(static_cast<int>(dt.year), 1970);
    CHECK_EQ(static_cast<int>(dt.month), 1);
    CHECK_EQ(static_cast<int>(dt.day), 1);
    CHECK_EQ(static_cast<int>(dt.hour), 0);

    const u64 stamps[] = {0, 1000000, 1700000000ull, 951782400ull /* 29.02.2000 */};
    for (u64 s : stamps) {
        CHECK_EQ(static_cast<long long>(toEpochSec(toDateTime(s))),
                 static_cast<long long>(s));
    }

    DateTime leap = toDateTime(951782400ull);
    CHECK_EQ(static_cast<int>(leap.year), 2000);
    CHECK_EQ(static_cast<int>(leap.month), 2);
    CHECK_EQ(static_cast<int>(leap.day), 29);
}
