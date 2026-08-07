/**
 * Hydra — sterownik BME280.
 *
 * Formuły kompensacyjne są transkrypcją wariantów całkowitoliczbowych
 * z dokumentacji Boscha (BME280 datasheet, rozdz. 4.2.3). Testy jednostkowe
 * sprawdzają obsługę układu (identyfikacja, sekwencja konfiguracji, składanie
 * słów z rejestrów, propagacja błędów) oraz własności kompensacji, których da
 * się dowieść bez sprzętu — monotoniczność i punkt odniesienia. Zgodność
 * bezwzględna wymaga porównania z fizycznym układem i należy do testów HIL.
 */

#include "hydra/drivers/sense/Bme280.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/hal/Hal.hpp"

namespace hydra {
namespace drivers {
namespace {

u16 le16(const u8* p)  { return static_cast<u16>(static_cast<u16>(p[1]) << 8 | p[0]); }
i16 le16s(const u8* p) { return static_cast<i16>(le16(p)); }

}  // namespace

const char* Bme280::unit(u8 channel) const {
    switch (channel) {
        case 0: return "degC";
        case 1: return "hPa";
        case 2: return "%RH";
        default: return "";
    }
}

Status Bme280::probe() {
    return hal::Hal::i2c(bus_).transaction([this](hal::II2cBus::Session& s) -> Status {
        HYDRA_TRY(const u8 id, s.readReg8(addr_, RegChipId));
        // Ten sam rejestr w BMP280 zwraca 0x58 — rozróżnienie jest istotne,
        // bo BMP280 nie ma wilgotności i zwracałby śmieci na trzecim kanale.
        return id == kChipId ? ok() : fail(Err::NotFound);
    });
}

Status Bme280::readCalibration() {
    u8 block1[26] = {};
    u8 block2[7]  = {};

    HYDRA_CHECK(hal::Hal::i2c(bus_).transaction([&](hal::II2cBus::Session& s) -> Status {
        HYDRA_CHECK(s.readReg(addr_, RegCalib00, ByteSpan{block1, sizeof(block1)}));
        return s.readReg(addr_, RegCalib26, ByteSpan{block2, sizeof(block2)});
    }));

    calib_.t1 = le16(&block1[0]);
    calib_.t2 = le16s(&block1[2]);
    calib_.t3 = le16s(&block1[4]);

    calib_.p1 = le16(&block1[6]);
    calib_.p2 = le16s(&block1[8]);
    calib_.p3 = le16s(&block1[10]);
    calib_.p4 = le16s(&block1[12]);
    calib_.p5 = le16s(&block1[14]);
    calib_.p6 = le16s(&block1[16]);
    calib_.p7 = le16s(&block1[18]);
    calib_.p8 = le16s(&block1[20]);
    calib_.p9 = le16s(&block1[22]);

    calib_.h1 = block1[25];
    calib_.h2 = le16s(&block2[0]);
    calib_.h3 = block2[2];
    // H4 i H5 dzielą jeden bajt: młodsze cztery bity należą do H4,
    // starsze do H5. To najczęstsze miejsce pomyłki przy tym układzie.
    calib_.h4 = static_cast<i16>((static_cast<i16>(static_cast<i8>(block2[3])) << 4) |
                                 (block2[4] & 0x0F));
    calib_.h5 = static_cast<i16>((static_cast<i16>(static_cast<i8>(block2[5])) << 4) |
                                 (block2[4] >> 4));
    calib_.h6 = static_cast<i8>(block2[6]);

    // T1 równe zeru oznacza, że pamięć kalibracyjna nie została odczytana —
    // dalsze przeliczenia dzieliłyby przez zero albo dawały bezsens.
    return calib_.t1 != 0 ? ok() : fail(Err::Protocol);
}

Status Bme280::configure(const sense::SensorCfg& cfg) {
    if (cfg.address != 0) addr_ = cfg.address;
    bus_ = cfg.busIndex;

    HYDRA_CHECK(readCalibration());

    return hal::Hal::i2c(bus_).transaction([this](hal::II2cBus::Session& s) -> Status {
        // Kolejność ma znaczenie: ctrl_hum wchodzi w życie dopiero po zapisie
        // ctrl_meas (dokumentacja, rozdz. 5.4.3).
        HYDRA_CHECK(s.writeReg8(addr_, RegCtrlHum, 0x01));   // oversampling x1
        HYDRA_CHECK(s.writeReg8(addr_, RegCtrlMeas, 0x27));  // T x1, P x1, tryb ciągły
        return s.writeReg8(addr_, RegConfig, 0xA0);          // standby 1 s, filtr wyłączony
    });
}

// ---------------------------------------------------------------------------
// Kompensacja (transkrypcja z dokumentacji producenta)
// ---------------------------------------------------------------------------

i32 Bme280::compensateTemp(i32 adc) {
    const i32 var1 = ((((adc >> 3) - (static_cast<i32>(calib_.t1) << 1))) *
                      static_cast<i32>(calib_.t2)) >> 11;
    const i32 var2 = (((((adc >> 4) - static_cast<i32>(calib_.t1)) *
                        ((adc >> 4) - static_cast<i32>(calib_.t1))) >> 12) *
                      static_cast<i32>(calib_.t3)) >> 14;
    tFine_ = var1 + var2;
    return (tFine_ * 5 + 128) >> 8;
}

u32 Bme280::compensatePressure(i32 adc) const {
    i32 var1 = (tFine_ >> 1) - 64000;
    i32 var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * static_cast<i32>(calib_.p6);
    var2     = var2 + ((var1 * static_cast<i32>(calib_.p5)) << 1);
    var2     = (var2 >> 2) + (static_cast<i32>(calib_.p4) << 16);
    var1     = (((static_cast<i32>(calib_.p3) * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) +
            ((static_cast<i32>(calib_.p2) * var1) >> 1)) >> 18;
    var1     = ((32768 + var1) * static_cast<i32>(calib_.p1)) >> 15;

    if (var1 == 0) return 0;  // dzielenie przez zero przy nieustawionej kalibracji

    u32 p = static_cast<u32>((static_cast<i32>(1048576) - adc) - (var2 >> 12)) * 3125u;
    if (p < 0x80000000u) {
        p = (p << 1) / static_cast<u32>(var1);
    } else {
        p = (p / static_cast<u32>(var1)) * 2u;
    }

    var1 = (static_cast<i32>(calib_.p9) *
            static_cast<i32>(((p >> 3) * (p >> 3)) >> 13)) >> 12;
    var2 = (static_cast<i32>(p >> 2) * static_cast<i32>(calib_.p8)) >> 13;
    return static_cast<u32>(static_cast<i32>(p) +
                            ((var1 + var2 + static_cast<i32>(calib_.p7)) >> 4));
}

u32 Bme280::compensateHumidity(i32 adc) const {
    i32 v = tFine_ - 76800;

    v = (((((adc << 14) - (static_cast<i32>(calib_.h4) << 20) -
            (static_cast<i32>(calib_.h5) * v)) + 16384) >> 15) *
         (((((((v * static_cast<i32>(calib_.h6)) >> 10) *
              (((v * static_cast<i32>(calib_.h3)) >> 11) + 32768)) >> 10) + 2097152) *
               static_cast<i32>(calib_.h2) + 8192) >> 14));

    v = v - (((((v >> 15) * (v >> 15)) >> 7) * static_cast<i32>(calib_.h1)) >> 4);

    if (v < 0) v = 0;
    if (v > 419430400) v = 419430400;  // odpowiada 100 %RH
    return static_cast<u32>(v >> 12);
}

// ---------------------------------------------------------------------------

Status Bme280::read(sense::Sample& out) {
    u8 raw[8] = {};

    HYDRA_CHECK(hal::Hal::i2c(bus_).transaction([this, &raw](hal::II2cBus::Session& s) {
        // Odczyt jednym transferem: rozdzielenie go groziłoby zmieszaniem
        // danych z dwóch różnych konwersji.
        return s.readReg(addr_, RegData, ByteSpan{raw, sizeof(raw)});
    }));

    const i32 adcP = (static_cast<i32>(raw[0]) << 12) | (static_cast<i32>(raw[1]) << 4) |
                     (raw[2] >> 4);
    const i32 adcT = (static_cast<i32>(raw[3]) << 12) | (static_cast<i32>(raw[4]) << 4) |
                     (raw[5] >> 4);
    const i32 adcH = (static_cast<i32>(raw[6]) << 8) | raw[7];

    // Temperatura musi być policzona pierwsza — ustawia tFine_, z którego
    // korzystają obie pozostałe kompensacje.
    const i32 tempCenti = compensateTemp(adcT);
    const u32 pressPa   = compensatePressure(adcP);
    const u32 humQ22    = compensateHumidity(adcH);

    out.value[0] = static_cast<float>(tempCenti) / 100.0f;
    out.value[1] = static_cast<float>(pressPa) / 100.0f;   // Pa → hPa
    out.value[2] = static_cast<float>(humQ22) / 1024.0f;   // Q22.10 → %RH
    out.n        = 3;

    // Wartość 0x80000 na wszystkich kanałach oznacza, że układ nie zakończył
    // jeszcze pierwszej konwersji po starcie.
    out.q = (adcT == 0x80000) ? Quality::Stale : Quality::Good;
    return ok();
}

}  // namespace drivers
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
