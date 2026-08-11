#pragma once
/**
 * Lc29hSensor.h — MinisLib
 * Driver modułu GNSS Quectel LC29H (L1+L5, RTK) po UART.
 * Parsuje NMEA 0183: GGA, RMC, GSA, GST (talker GN/GP/BD/GA/GL/QZ).
 *
 * Zakres (etap 1 — sam moduł):
 *  - pozycja, fix/RTK status, HDOP/PDOP/VDOP, hAcc/vAcc (GST), satelity, czas UTC
 *  - diagnostyka RTK: wiek poprawek RTCM + ID stacji bazowej (GGA p.13/14)
 *  - konfiguracja rate komendami PAIR (z opcjonalnym oczekiwaniem na ACK)
 *  - wejście korekcji RTCM: writeRtcm() — nieblokujące, chunkowane;
 *    źródło korekcji (NTRIP) poza zakresem tego etapu.
 *
 * Model epoki:
 *  Epoka jest publikowana gdy przyjdą GGA i RMC, albo po upływie
 *  Config::epochTimeoutMs od pierwszego zdania epoki (wtedy publikujemy
 *  to, co mamy — dzięki temu wyłączony/zgubiony RMC nie blokuje fixa).
 *
 * Wielokrotne talkery: LC29H może nadawać $GNGGA i $GPGGA jednocześnie.
 * Driver blokuje się na jednym talkerze (preferowany "GN"), by nie mieszać epok.
 */

#include "LocationSensor.h"
#include "SerialPort.h"

namespace minis {

class Lc29hSensor : public LocationSensor {
public:
    /**
     * Konfiguracja. Ustawiana metodami with*() (bez designated initializers,
     * które są C++20 i wymuszają kolejność pól).
     */
    struct Config {
        ISerialPort* port      = nullptr;
        int          rxPin     = -1;      ///< GPIO MCU <- TX modułu
        int          txPin     = -1;      ///< GPIO MCU -> RX modułu
        uint32_t     baudRate  = 115200;  ///< fabrycznie LC29H: 115200
        uint16_t     rateMs    = 1000;    ///< okres epoki (100–1000 ms)
        uint32_t     staleMs   = 3000;    ///< brak danych dłużej => fix nieaktualny
        uint16_t     epochTimeoutMs = 400;///< max czas składania jednej epoki
        uint16_t     bootDelayMs = 100;   ///< odczekanie po otwarciu UART przed komendami
        uint16_t     ackTimeoutMs = 300;  ///< 0 = nie czekaj na ACK komend PAIR
        MillisFn     millisFn  = &platformMillis;
        DelayFn      delayFn   = &platformDelay;

        Config& withPort(ISerialPort& p)      { port = &p; return *this; }
        Config& withPins(int rx, int tx)      { rxPin = rx; txPin = tx; return *this; }
        Config& withBaudRate(uint32_t b)      { baudRate = b; return *this; }
        Config& withRateMs(uint16_t ms)       { rateMs = ms; return *this; }
        Config& withStaleMs(uint32_t ms)      { staleMs = ms; return *this; }
        Config& withEpochTimeoutMs(uint16_t m){ epochTimeoutMs = m; return *this; }
        Config& withAckTimeoutMs(uint16_t m)  { ackTimeoutMs = m; return *this; }
        Config& withClock(MillisFn m, DelayFn d) { millisFn = m; delayFn = d; return *this; }
    };

    /// Licznik zdarzeń diagnostycznych (do logów / MQTT).
    struct Stats {
        uint32_t sentencesOk      = 0;
        uint32_t checksumErrors   = 0;
        uint32_t overflows        = 0; ///< zdania dłuższe niż LINE_MAX
        uint32_t unknownSentences = 0;
        uint32_t epochsPublished  = 0;
        uint32_t epochsTimedOut   = 0; ///< opublikowane bez kompletu GGA+RMC
        uint32_t rtcmBytesWritten = 0;
    };

    explicit Lc29hSensor(const Config& cfg) : _cfg(cfg) {}

    // ---- LocationSensor -------------------------------------------------
    bool begin() override;
    void end() override;
    void update() override;
    const LocationData& data() const override { return _data; }
    bool hasFix() const override;

    // ---- RTK -----------------------------------------------------------
    /// RTK fixed lub float (i dane nieprzestarzałe).
    bool hasRtk() const { return hasFix() && _data.hasRtk(); }

    /**
     * Surowe korekcje RTCM3 do modułu — NIE blokuje pętli.
     * Zapisuje tyle, ile zmieści się w buforze TX; zwraca liczbę zapisanych
     * bajtów. Resztę należy podać ponownie w kolejnym wywołaniu.
     * timeoutMs > 0 => dopuszczalne krótkie oczekiwanie na miejsce w buforze.
     */
    size_t writeRtcm(const uint8_t* buf, size_t len, uint32_t timeoutMs = 0);

    // ---- Konfiguracja modułu -------------------------------------------
    /// Komenda PQTM/PAIR z automatycznym checksumem, bez "$" i "*hh".
    void sendCommand(const char* body);

    /**
     * Ustaw okres epoki w ms (PAIR050). Zwraca true gdy moduł potwierdził
     * ($PAIR001,050,0) lub gdy ackTimeoutMs == 0 (brak weryfikacji).
     * Uwaga: rate < 1000 ms przy pełnym NMEA wymaga baudRate > 115200.
     */
    bool setUpdateRate(uint16_t ms);

    /// Włącz/wyłącz zdania GSV (oszczędność pasma przy wysokim rate).
    bool enableGsv(bool on);

    // ---- Diagnostyka ----------------------------------------------------
    const Stats& stats() const { return _stats; }
    void resetStats() { _stats = Stats{}; }

    /// Zaobserwowany talker epoki, np. "GN" ("" gdy jeszcze brak danych).
    const char* talker() const { return _talker; }

    /// Wstrzyknięcie bajtu do parsera — używane w testach jednostkowych.
    void feedByte(char c) { feed(c); }

private:
    void feed(char c);
    void parseSentence();
    void parseGGA(char* f[], int n);
    void parseRMC(char* f[], int n);
    void parseGSA(char* f[], int n);
    void parseGST(char* f[], int n);
    void parsePairAck(char* f[], int n);

    void openEpoch();
    void flushEpoch(bool complete);
    void maybeFlush();
    bool acceptTalker(const char* hdr);
    bool waitAck(uint16_t cmdId, uint16_t timeoutMs);
    uint32_t now() const { return _cfg.millisFn ? _cfg.millisFn() : 0; }

    static bool   checksumOk(const char* line, size_t len);
    static double parseCoord(const char* val, const char* hemi);
    static float  knotsToMs(float kn) { return kn * 0.514444f; }
    static void   applyTime(LocationData& d, const char* hhmmss);

    Config       _cfg;
    Stats        _stats;
    LocationData _data;
    LocationData _pending;

    bool     _epochOpen    = false;
    bool     _gotGGA       = false;
    bool     _gotRMC       = false;
    uint32_t _epochStartMs = 0;
    char     _talker[3]    = {0, 0, 0};

    // ostatnie ACK komendy PAIR
    uint16_t _ackCmd    = 0xFFFF;
    int8_t   _ackResult = -1;

    static constexpr size_t LINE_MAX   = 160; ///< GSV L1+L5 bywa > 82 B
    static constexpr int    MAX_FIELDS = 32;
    char   _line[LINE_MAX];
    size_t _lineLen    = 0;
    bool   _inSentence = false;
};

} // namespace minis
