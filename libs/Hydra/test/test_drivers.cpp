/**
 * Testy sterowników referencyjnych na atrapie magistrali (rozdz. 8).
 *
 * Sprawdzają to, co da się sprawdzić bez sprzętu: identyfikację układu,
 * sekwencję konfiguracji, składanie słów z rejestrów, przeliczenia o znanym
 * wyniku i propagację błędów. Zgodność bezwzględna kompensacji BME280 wymaga
 * porównania z fizycznym układem i należy do testów HIL.
 */

#include "hydra_test.hpp"

#include "hydra/drivers/sense/As5600.hpp"
#include "hydra/drivers/sense/Bme280.hpp"
#include "hydra/drivers/sense/Ina219.hpp"
#include "hydra/hal/Mock.hpp"

using namespace hydra;
using namespace hydra::sense;
using namespace hydra::drivers;

namespace {

hal::mock::Backend& freshBus() {
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    return hal::mock::backend();
}

/** Wpisuje do atrapy komplet współczynników kalibracyjnych BME280. */
void loadBme280Calibration(hal::mock::MockI2c& bus, u8 addr) {
    // Wartości z typowego egzemplarza — istotne jest, by były niezerowe
    // i miały poprawne znaki, bo na tym opiera się arytmetyka kompensacji.
    const u8 block1[26] = {
        0x70, 0x6B,              // T1 = 27504
        0x43, 0x67,              // T2 = 26435
        0x18, 0xFC,              // T3 = -1000
        0x7D, 0x8E,              // P1 = 36477
        0x43, 0xD6,              // P2 = -10687
        0xD0, 0x0B,              // P3 = 3024
        0x27, 0x0B,              // P4 = 2855
        0x8C, 0x00,              // P5 = 140
        0xF9, 0xFF,              // P6 = -7
        0x8C, 0x3C,              // P7 = 15500
        0xF8, 0xC6,              // P8 = -14600
        0x70, 0x17,              // P9 = 6000
        0x00, 0x4B,              // rezerwa, H1 = 75
    };
    const u8 block2[7] = {
        0x62, 0x01,  // H2 = 354
        0x00,        // H3 = 0
        0x13,        // H4 górne bity
        0x00,        // wspólny bajt H4/H5
        0x1E,        // H5 górne bity
        0x1E,        // H6 = 30
    };

    for (u8 i = 0; i < sizeof(block1); ++i) bus.setReg(addr, 0x88 + i, block1[i]);
    for (u8 i = 0; i < sizeof(block2); ++i) bus.setReg(addr, 0xE1 + i, block2[i]);
    bus.setReg(addr, Bme280::RegChipId, Bme280::kChipId);
}

/** Wpisuje surowe zliczenia przetwornika w rejestry danych BME280. */
void setBme280Raw(hal::mock::MockI2c& bus, u8 addr, u32 press, u32 temp, u16 hum) {
    bus.setReg(addr, 0xF7, static_cast<u8>(press >> 12));
    bus.setReg(addr, 0xF8, static_cast<u8>((press >> 4) & 0xFF));
    bus.setReg(addr, 0xF9, static_cast<u8>((press & 0x0F) << 4));
    bus.setReg(addr, 0xFA, static_cast<u8>(temp >> 12));
    bus.setReg(addr, 0xFB, static_cast<u8>((temp >> 4) & 0xFF));
    bus.setReg(addr, 0xFC, static_cast<u8>((temp & 0x0F) << 4));
    bus.setReg(addr, 0xFD, static_cast<u8>(hum >> 8));
    bus.setReg(addr, 0xFE, static_cast<u8>(hum & 0xFF));
}

}  // namespace

// ---------------------------------------------------------------------------
// AS5600
// ---------------------------------------------------------------------------

TEST("AS5600: brak układu na magistrali wychodzi już w probe()") {
    freshBus();
    As5600 enc;
    CHECK(enc.probe().error() == Err::NotFound);
}

TEST("AS5600: kąt liczony z 12 bitów rejestru") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(As5600::kDefaultAddress).has_value());
    mockHal.i2c.setReg(As5600::kDefaultAddress, As5600::RegStatus,
                       As5600::kStatusMagnetDetected);

    As5600 enc;
    REQUIRE(enc.probe().has_value());
    REQUIRE(enc.configure(SensorCfg{}).has_value());
    CHECK(enc.magnetOk());

    // 2048 zliczeń z 4096 to dokładnie połowa obrotu.
    mockHal.i2c.setReg(As5600::kDefaultAddress, As5600::RegAngle, 0x08);
    mockHal.i2c.setReg(As5600::kDefaultAddress, As5600::RegAngle + 1, 0x00);

    Sample s;
    REQUIRE(enc.read(s).has_value());
    CHECK_EQ(static_cast<int>(s.n), 1);
    CHECK(s.value[0] > 179.9f && s.value[0] < 180.1f);
    CHECK(s.q == Quality::Good);

    // Górne cztery bity są nieużywane i muszą zostać odrzucone.
    mockHal.i2c.setReg(As5600::kDefaultAddress, As5600::RegAngle, 0xF8);
    REQUIRE(enc.read(s).has_value());
    CHECK(s.value[0] > 179.9f && s.value[0] < 180.1f);
}

TEST("AS5600: brak magnesu obniża jakość, ale nie blokuje odczytu") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(As5600::kDefaultAddress).has_value());
    mockHal.i2c.setReg(As5600::kDefaultAddress, As5600::RegStatus, 0x00);

    As5600 enc;
    REQUIRE(enc.configure(SensorCfg{}).has_value());
    CHECK(!enc.magnetOk());

    Sample s;
    REQUIRE(enc.read(s).has_value());
    CHECK(s.q == Quality::Suspect);
}

TEST("AS5600: błąd magistrali propaguje się do wołającego") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(As5600::kDefaultAddress).has_value());

    As5600 enc;
    REQUIRE(enc.configure(SensorCfg{}).has_value());

    mockHal.i2c.failNext(1, Err::Timeout);
    Sample s;
    CHECK(enc.read(s).error() == Err::Timeout);
}

// ---------------------------------------------------------------------------
// INA219
// ---------------------------------------------------------------------------

TEST("INA219: konfiguracja zapisuje rejestr kalibracji i tryb pracy") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Ina219::kDefaultAddress).has_value());
    mockHal.i2c.setWordRegisters(Ina219::kDefaultAddress);

    Ina219 meter;
    REQUIRE(meter.probe().has_value());
    REQUIRE(meter.configure(SensorCfg{}).has_value());

    // Waga bitu prądu: 3,2 A na 15 bitach → 3200 mA / 32768.
    CHECK(meter.currentLsbMa() > 0.0976f && meter.currentLsbMa() < 0.0977f);

    // cal = 0,04096 / (LSB[A] * R) = 0,04096 / (0,00009766 * 0,1) ≈ 4194
    auto cal = mockHal.i2c.getReg16(Ina219::kDefaultAddress, Ina219::RegCalibration);
    REQUIRE(cal.has_value());
    CHECK(*cal >= 4190 && *cal <= 4198);

    auto cfg = mockHal.i2c.getReg16(Ina219::kDefaultAddress, Ina219::RegConfig);
    REQUIRE(cfg.has_value());
    CHECK_EQ(static_cast<int>(*cfg), 0x399F);
}

TEST("INA219: przeliczenia napięcia, prądu i mocy") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Ina219::kDefaultAddress).has_value());
    mockHal.i2c.setWordRegisters(Ina219::kDefaultAddress);

    Ina219 meter;
    REQUIRE(meter.configure(SensorCfg{}).has_value());

    // Napięcie szyny: wartość zaczyna się od bitu 3, waga 4 mV.
    // 1024 × 4 mV = 4,096 V → rejestr = 1024 << 3 = 0x2000.
    mockHal.i2c.setReg16(Ina219::kDefaultAddress, Ina219::RegBusVolt, 0x2000);
    // Prąd: 1024 × 0,09766 mA ≈ 100 mA.
    mockHal.i2c.setReg16(Ina219::kDefaultAddress, Ina219::RegCurrent, 1024);
    mockHal.i2c.setReg16(Ina219::kDefaultAddress, Ina219::RegPower, 100);

    Sample s;
    REQUIRE(meter.read(s).has_value());
    CHECK_EQ(static_cast<int>(s.n), 3);
    CHECK(s.value[0] > 4.09f && s.value[0] < 4.10f);
    CHECK(s.value[1] > 99.9f && s.value[1] < 100.1f);
    CHECK(s.value[2] > 195.0f && s.value[2] < 196.0f);  // 100 × LSB × 20
    CHECK(s.q == Quality::Good);
}

TEST("INA219: prąd ujemny to liczba ze znakiem, nie ogromna dodatnia") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Ina219::kDefaultAddress).has_value());
    mockHal.i2c.setWordRegisters(Ina219::kDefaultAddress);

    Ina219 meter;
    REQUIRE(meter.configure(SensorCfg{}).has_value());
    mockHal.i2c.setReg16(Ina219::kDefaultAddress, Ina219::RegCurrent,
                         static_cast<u16>(-1024));

    Sample s;
    REQUIRE(meter.read(s).has_value());
    CHECK(s.value[1] < -99.9f && s.value[1] > -100.1f);
}

TEST("INA219: flaga przepełnienia obniża jakość próbki") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Ina219::kDefaultAddress).has_value());
    mockHal.i2c.setWordRegisters(Ina219::kDefaultAddress);

    Ina219 meter;
    REQUIRE(meter.configure(SensorCfg{}).has_value());
    mockHal.i2c.setReg16(Ina219::kDefaultAddress, Ina219::RegBusVolt, 0x2001);

    Sample s;
    REQUIRE(meter.read(s).has_value());
    CHECK(s.q == Quality::Suspect);
}

TEST("INA219: bezsensowny bocznik jest odrzucany przy konfiguracji") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Ina219::kDefaultAddress).has_value());

    Ina219::Setup bad;
    bad.shuntOhms = 0.0f;
    Ina219 meter(bad);
    CHECK(meter.configure(SensorCfg{}).error() == Err::BadArgument);
}

// ---------------------------------------------------------------------------
// BME280
// ---------------------------------------------------------------------------

TEST("BME280: rozróżnia własny układ od BMP280") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Bme280::kDefaultAddress).has_value());

    Bme280 sensor;

    // BMP280 zwraca 0x58 i nie ma kanału wilgotności.
    mockHal.i2c.setReg(Bme280::kDefaultAddress, Bme280::RegChipId, 0x58);
    CHECK(sensor.probe().error() == Err::NotFound);

    mockHal.i2c.setReg(Bme280::kDefaultAddress, Bme280::RegChipId, Bme280::kChipId);
    CHECK(sensor.probe().has_value());
}

TEST("BME280: pusta pamięć kalibracyjna jest wykrywana") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Bme280::kDefaultAddress).has_value());
    mockHal.i2c.setReg(Bme280::kDefaultAddress, Bme280::RegChipId, Bme280::kChipId);

    // Wszystkie rejestry kalibracyjne zerowe — T1 = 0 unieważnia przeliczenia.
    Bme280 sensor;
    CHECK(sensor.configure(SensorCfg{}).error() == Err::Protocol);
}

TEST("BME280: konfiguracja ustawia rejestry w wymaganej kolejności") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Bme280::kDefaultAddress).has_value());
    loadBme280Calibration(mockHal.i2c, Bme280::kDefaultAddress);

    Bme280 sensor;
    REQUIRE(sensor.configure(SensorCfg{}).has_value());

    auto hum = mockHal.i2c.getReg(Bme280::kDefaultAddress, Bme280::RegCtrlHum);
    REQUIRE(hum.has_value());
    CHECK_EQ(static_cast<int>(*hum), 0x01);

    // ctrl_meas musi zostać zapisany po ctrl_hum, inaczej oversampling
    // wilgotności nie wejdzie w życie.
    auto meas = mockHal.i2c.getReg(Bme280::kDefaultAddress, Bme280::RegCtrlMeas);
    REQUIRE(meas.has_value());
    CHECK_EQ(static_cast<int>(*meas), 0x27);
}

TEST("BME280: kompensacja temperatury jest monotoniczna i w rozsądnym zakresie") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Bme280::kDefaultAddress).has_value());
    loadBme280Calibration(mockHal.i2c, Bme280::kDefaultAddress);

    Bme280 sensor;
    REQUIRE(sensor.configure(SensorCfg{}).has_value());

    // Wartość 0x80000 to połowa zakresu przetwornika — okolice temperatury
    // pokojowej dla typowych współczynników kalibracyjnych.
    Sample mid;
    setBme280Raw(mockHal.i2c, Bme280::kDefaultAddress, 0x65000, 0x80000, 0x8000);
    REQUIRE(sensor.read(mid).has_value());
    CHECK_EQ(static_cast<int>(mid.n), 3);
    CHECK(mid.value[0] > -40.0f && mid.value[0] < 85.0f);

    // Większe zliczenie musi dać wyższą temperaturę — to własność kompensacji,
    // którą da się sprawdzić bez fizycznego układu.
    // Przy typowych współczynnikach 0x80000 daje okolice 26 °C.
    CHECK(mid.value[0] > 20.0f && mid.value[0] < 32.0f);

    Sample warmer;
    setBme280Raw(mockHal.i2c, Bme280::kDefaultAddress, 0x65000, 0x88000, 0x8000);
    REQUIRE(sensor.read(warmer).has_value());
    CHECK(warmer.value[0] > mid.value[0]);

    Sample cooler;
    setBme280Raw(mockHal.i2c, Bme280::kDefaultAddress, 0x65000, 0x78000, 0x8000);
    REQUIRE(sensor.read(cooler).has_value());
    CHECK(cooler.value[0] < mid.value[0]);
}

TEST("BME280: ciśnienie i wilgotność mieszczą się w zakresach fizycznych") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Bme280::kDefaultAddress).has_value());
    loadBme280Calibration(mockHal.i2c, Bme280::kDefaultAddress);

    Bme280 sensor;
    REQUIRE(sensor.configure(SensorCfg{}).has_value());

    // 0x65000 odpowiada przy tych współczynnikach ciśnieniu bliskiemu
    // normalnemu na poziomie morza.
    Sample s;
    setBme280Raw(mockHal.i2c, Bme280::kDefaultAddress, 0x65000, 0x80000, 0x8000);
    REQUIRE(sensor.read(s).has_value());

    // Zakresy z dokumentacji układu: 300–1100 hPa, 0–100 %RH.
    CHECK(s.value[1] > 300.0f && s.value[1] < 1100.0f);
    CHECK(s.value[1] > 950.0f && s.value[1] < 1060.0f);
    CHECK(s.value[2] >= 0.0f && s.value[2] <= 100.0f);
    CHECK(s.value[2] > 50.0f && s.value[2] < 80.0f);

    // W formule kompensacyjnej ciśnienie liczone jest od (1048576 - adc_P),
    // więc większe zliczenie oznacza niższe ciśnienie. Odwrócenie tej zależności
    // byłoby najbardziej prawdopodobnym skutkiem błędu transkrypcji.
    Sample higher;
    setBme280Raw(mockHal.i2c, Bme280::kDefaultAddress, 0x60000, 0x80000, 0x8000);
    REQUIRE(sensor.read(higher).has_value());
    CHECK(higher.value[1] > s.value[1]);
}

TEST("BME280: brak pierwszej konwersji oznacza dane nieaktualne") {
    auto& mockHal = freshBus();
    REQUIRE(mockHal.i2c.addDevice(Bme280::kDefaultAddress).has_value());
    loadBme280Calibration(mockHal.i2c, Bme280::kDefaultAddress);

    Bme280 sensor;
    REQUIRE(sensor.configure(SensorCfg{}).has_value());

    // 0x80000 w rejestrach danych to wartość po resecie, przed konwersją.
    setBme280Raw(mockHal.i2c, Bme280::kDefaultAddress, 0x80000, 0x80000, 0x8000);
    Sample s;
    REQUIRE(sensor.read(s).has_value());
    CHECK(s.q == Quality::Stale);
}
