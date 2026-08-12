/**
 * test_nmea_parser.cpp — testy parsera NMEA drivera LC29H (build native).
 * Uruchomienie: pio test -e native
 *
 * Testy pokrywają regresje z przeglądu kodu:
 *  - pozycja nie "przecieka" z poprzedniej epoki po utracie fixa,
 *  - epoka publikowana także bez RMC (timeout),
 *  - drugi talker nie miesza epok,
 *  - krótkie/obce zdania nie czytają poza nagłówkiem,
 *  - pola RTK: wiek poprawek + ID stacji, GST -> hAcc/vAcc.
 */

#include <unity.h>

#include <string.h>
#include <string>
#include <vector>

#include "Lc29hSensor.h"
#include "LocationJson.h"

using namespace minis;

// ------------------------------------------------------------ fake port/clock

static uint32_t g_now = 0;
static uint32_t fakeMillis() { return g_now; }
static void     fakeDelay(uint32_t ms) { g_now += ms; }

class FakePort final : public ISerialPort {
public:
    bool open(uint32_t, int, int) override { opened = true; return true; }
    void close() override { opened = false; }
    int  available() override { return static_cast<int>(rx.size() - rxPos); }
    int  read() override { return rxPos < rx.size() ? rx[rxPos++] : -1; }
    size_t write(const uint8_t* b, size_t n) override {
        if (n > static_cast<size_t>(txRoom)) n = static_cast<size_t>(txRoom);
        tx.insert(tx.end(), b, b + n);
        txRoom -= static_cast<int>(n);
        return n;
    }
    int  availableForWrite() override { return txRoom; }
    void flushInput() override { rx.clear(); rxPos = 0; }

    void push(const char* s) { rx.insert(rx.end(), s, s + strlen(s)); }

    bool opened = false;
    int  txRoom = 1024;
    std::vector<uint8_t> rx, tx;
    size_t rxPos = 0;
};

static FakePort     port;
static Lc29hSensor* gnss = nullptr;

static Lc29hSensor::Config cfg() {
    Lc29hSensor::Config c;
    c.withPort(port).withPins(17, 18).withRateMs(1000)
     .withClock(&fakeMillis, &fakeDelay)
     .withAckTimeoutMs(0)           // bez weryfikacji ACK w testach
     .withEpochTimeoutMs(400);
    return c;
}

void setUp() {
    g_now = 1000;
    port = FakePort{};
    delete gnss;
    gnss = new Lc29hSensor(cfg());
    TEST_ASSERT_TRUE(gnss->begin());
}

void tearDown() {
    delete gnss;
    gnss = nullptr;
}

// ------------------------------------------------------------ dane testowe

static const char* GGA_RTK =
    "$GNGGA,123519.00,4807.038000,N,01131.000000,E,4,12,0.60,545.4,M,46.9,M,1.2,0123*4E\r\n";
static const char* RMC_OK =
    "$GNRMC,123519.00,A,4807.038000,N,01131.000000,E,022.4,084.4,230326,,,R*4C\r\n";
static const char* GGA_NOFIX =
    "$GNGGA,123520.00,,,,,0,00,99.99,,M,,M,,*56\r\n";
static const char* RMC_VOID =
    "$GNRMC,123520.00,V,,,,,,,230326,,,N*67\r\n";
static const char* GST_OK =
    "$GNGST,123519.00,0.010,0.012,0.009,0.0,0.008,0.006,0.020*4B\r\n";
static const char* GGA_GP_OTHER =
    "$GPGGA,123521.00,5000.000000,N,02000.000000,E,1,08,1.20,200.0,M,40.0,M,,*5B\r\n";

static void feedStr(const char* s) {
    for (const char* p = s; *p; ++p) gnss->feedByte(*p);
}

// ------------------------------------------------------------ testy

static void test_gga_rmc_epoch() {
    feedStr(GGA_RTK);
    feedStr(RMC_OK);

    const auto& d = gnss->data();
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_EQUAL(static_cast<int>(FixQuality::RtkFixed), static_cast<int>(d.quality));
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 48.1173, d.latitude);
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 11.5166666, d.longitude);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 545.4f, d.altitude);
    TEST_ASSERT_EQUAL(12, d.satellites);
    TEST_ASSERT_EQUAL(1, gnss->stats().epochsPublished);
    TEST_ASSERT_EQUAL(0, gnss->stats().epochsTimedOut);
}

/// Regresja #1: po utracie fixa stara pozycja NIE może zostać w data().
static void test_nofix_clears_position() {
    feedStr(GGA_RTK);
    feedStr(RMC_OK);
    TEST_ASSERT_TRUE(gnss->data().latitude != 0.0);

    feedStr(GGA_NOFIX);
    feedStr(RMC_VOID);

    const auto& d = gnss->data();
    TEST_ASSERT_FALSE(d.valid);
    TEST_ASSERT_EQUAL(static_cast<int>(FixQuality::NoFix), static_cast<int>(d.quality));
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, d.latitude);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, d.longitude);
    TEST_ASSERT_FALSE(gnss->hasFix());
}

/// Regresja #2: brak RMC nie może blokować publikacji epoki na zawsze.
static void test_epoch_published_without_rmc() {
    feedStr(GGA_RTK);
    TEST_ASSERT_EQUAL(0, gnss->stats().epochsPublished); // czeka na RMC

    g_now += 500;           // przekroczony epochTimeoutMs
    gnss->update();

    TEST_ASSERT_EQUAL(1, gnss->stats().epochsPublished);
    TEST_ASSERT_EQUAL(1, gnss->stats().epochsTimedOut);
    TEST_ASSERT_TRUE(gnss->hasFix());    // fix z GGA uznany za wiarygodny
    TEST_ASSERT_TRUE(gnss->hasRtk());
}

/// Regresja #3: obcy talker nie miesza się do epoki wybranego talkera.
static void test_second_talker_ignored() {
    feedStr(GGA_RTK);       // ustala talker "GN"
    feedStr(RMC_OK);
    const double lat = gnss->data().latitude;

    feedStr(GGA_GP_OTHER);  // $GPGGA — musi zostać zignorowane
    g_now += 500;
    gnss->update();

    TEST_ASSERT_EQUAL_STRING("GN", gnss->talker());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, lat, gnss->data().latitude);
    TEST_ASSERT_EQUAL(1, gnss->stats().epochsPublished);
}

/// Regresja #4: krótkie/nietypowe zdania nie mogą wywoływać parsowania.
static void test_short_and_foreign_sentences() {
    feedStr("$GN*13\r\n");                            // za krótkie
    feedStr("$PQTMVERNO,LC29H,V1.0*39\r\n");          // komunikat własny
    feedStr("$GNGGA,123519.00,4807.038000,N*7A\r\n"); // za mało pól
    gnss->update();

    TEST_ASSERT_EQUAL(0, gnss->stats().epochsPublished);
    TEST_ASSERT_FALSE(gnss->hasFix());
}

static void test_checksum_rejected() {
    feedStr("$GNGGA,123519.00,4807.038000,N,01131.000000,E,4,12,0.60,545.4,M,46.9,M,1.2,0123*00\r\n");
    TEST_ASSERT_EQUAL(1, gnss->stats().checksumErrors);
    TEST_ASSERT_EQUAL(0, gnss->stats().epochsPublished);
}

/// Regresja #8/#9: diagnostyka RTK i dokładności z GST.
static void test_rtk_diagnostics_and_gst() {
    feedStr(GST_OK);
    feedStr(GGA_RTK);
    feedStr(RMC_OK);

    const auto& d = gnss->data();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.2f, d.rtcmAgeS);
    TEST_ASSERT_EQUAL(123, d.baseStation);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.01f, d.hAcc);   // hypot(0.008, 0.006)
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.02f, d.vAcc);
}

/// Fix wygasa po staleMs bez danych.
static void test_stale_fix_expires() {
    feedStr(GGA_RTK);
    feedStr(RMC_OK);
    TEST_ASSERT_TRUE(gnss->hasFix());

    g_now += 5000;   // > staleMs (3000)
    gnss->update();

    TEST_ASSERT_FALSE(gnss->hasFix());
    TEST_ASSERT_FALSE(gnss->data().valid);
}

/// Regresja #7: end() nie może zostawiać "żywego" fixa.
static void test_end_clears_state() {
    feedStr(GGA_RTK);
    feedStr(RMC_OK);
    TEST_ASSERT_TRUE(gnss->hasFix());

    gnss->end();
    TEST_ASSERT_FALSE(gnss->hasFix());
    TEST_ASSERT_FALSE(port.opened);
}

/// Regresja #10: writeRtcm chunkuje i nie blokuje przy pełnym buforze TX.
static void test_write_rtcm_nonblocking() {
    std::vector<uint8_t> frame(1000, 0xD3);
    port.tx.clear();
    port.txRoom = 300;

    size_t sent = gnss->writeRtcm(frame.data(), frame.size()); // timeout 0
    TEST_ASSERT_EQUAL(300, sent);          // tylko tyle, ile było miejsca
    TEST_ASSERT_EQUAL(300, gnss->stats().rtcmBytesWritten);

    port.txRoom = 1000;                    // bufor się opróżnił
    sent += gnss->writeRtcm(frame.data() + sent, frame.size() - sent);
    TEST_ASSERT_EQUAL(1000, sent);
}

static void test_send_command_checksum() {
    port.tx.clear();
    gnss->sendCommand("PAIR050,200");
    const std::string out(port.tx.begin(), port.tx.end());
    TEST_ASSERT_TRUE(out.find("$PAIR050,200*") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("\r\n") != std::string::npos);
}

static void test_json_serialization() {
    feedStr(GST_OK);
    feedStr(GGA_RTK);
    feedStr(RMC_OK);

    char buf[320];
    const size_t n = toJson(gnss->data(), buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL('}', buf[n - 1]);
    TEST_ASSERT_TRUE(strstr(buf, "\"quality\":\"rtk_fixed\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"rtcmAge\":1.2") != nullptr);

    char tiny[8];
    TEST_ASSERT_EQUAL(0, toJson(gnss->data(), tiny, sizeof(tiny))); // za mały bufor
}

static void test_coord_south_west() {
    // 33°51.5' S, 151°12.5' W
    feedStr("$GNGGA,010101.00,3351.500000,S,15112.500000,W,1,09,0.90,10.0,M,20.0,M,,*54\r\n");
    g_now += 500;
    gnss->update();

    const auto& d = gnss->data();
    TEST_ASSERT_TRUE(d.latitude < 0.0);
    TEST_ASSERT_TRUE(d.longitude < 0.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, -33.858333, d.latitude);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, -151.208333, d.longitude);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_gga_rmc_epoch);
    RUN_TEST(test_nofix_clears_position);
    RUN_TEST(test_epoch_published_without_rmc);
    RUN_TEST(test_second_talker_ignored);
    RUN_TEST(test_short_and_foreign_sentences);
    RUN_TEST(test_checksum_rejected);
    RUN_TEST(test_rtk_diagnostics_and_gst);
    RUN_TEST(test_stale_fix_expires);
    RUN_TEST(test_end_clears_state);
    RUN_TEST(test_write_rtcm_nonblocking);
    RUN_TEST(test_send_command_checksum);
    RUN_TEST(test_json_serialization);
    RUN_TEST(test_coord_south_west);
    return UNITY_END();
}
