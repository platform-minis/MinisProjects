/**
 * Lc29hSensor.cpp — MinisLib
 * Implementacja drivera Quectel LC29H (UART, NMEA 0183).
 */

#include "Lc29hSensor.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace minis {

namespace {

/// Bezpieczne pobranie pola (nigdy nullptr).
inline const char* fld(char* f[], int n, int i) {
    return (i >= 0 && i < n && f[i]) ? f[i] : "";
}
inline bool has(char* f[], int n, int i) {
    return fld(f, n, i)[0] != '\0';
}

} // namespace

// ---------------------------------------------------------------- lifecycle

bool Lc29hSensor::begin() {
    if (!_cfg.port) return false;
    if (!_cfg.millisFn) _cfg.millisFn = &platformMillis;
    if (!_cfg.delayFn)  _cfg.delayFn  = &platformDelay;

    if (!_cfg.port->open(_cfg.baudRate, _cfg.rxPin, _cfg.txPin)) return false;

    // pełny reset stanu parsera i danych
    _data       = LocationData{};
    _pending    = LocationData{};
    _stats      = Stats{};
    _lineLen    = 0;
    _inSentence = false;
    _epochOpen  = false;
    _gotGGA = _gotRMC = false;
    _epochStartMs = 0;
    _talker[0]  = '\0';
    _ackCmd     = 0xFFFF;
    _ackResult  = -1;

    // moduł po otwarciu UART potrzebuje chwili na boot, inaczej komendy
    // konfiguracyjne wypadają w próżnię
    if (_cfg.bootDelayMs) _cfg.delayFn(_cfg.bootDelayMs);
    _cfg.port->flushInput();

    if (_cfg.rateMs != 1000) {
        // przy wysokim rate pełne NMEA nie mieści się w 115200 baud
        if (_cfg.rateMs < 500 && _cfg.baudRate <= 115200) enableGsv(false);
        setUpdateRate(_cfg.rateMs);
    }
    return true;
}

void Lc29hSensor::end() {
    if (_cfg.port) _cfg.port->close();
    // stan musi zniknąć razem z portem, żeby hasFix() nie kłamał po end()
    _data       = LocationData{};
    _pending    = LocationData{};
    _lineLen    = 0;
    _inSentence = false;
    _epochOpen  = false;
    _gotGGA = _gotRMC = false;
    _talker[0]  = '\0';
}

void Lc29hSensor::update() {
    if (!_cfg.port) return;

    while (_cfg.port->available() > 0) {
        int c = _cfg.port->read();
        if (c < 0) break;
        feed(static_cast<char>(c));
    }

    maybeFlush(); // domknij epokę, gdy brakujące zdanie nie przyszło

    // unieważnij fix, gdy moduł zamilkł
    if (_data.valid && (now() - _data.lastUpdateMs) > _cfg.staleMs) {
        _data.valid    = false;
        _data.quality  = FixQuality::NoFix;
        _data.rtcmAgeS = -1.0f;
    }
}

bool Lc29hSensor::hasFix() const {
    return _data.valid &&
           _data.quality != FixQuality::NoFix &&
           (now() - _data.lastUpdateMs) <= _cfg.staleMs;
}

// ---------------------------------------------------------------- TX

size_t Lc29hSensor::writeRtcm(const uint8_t* buf, size_t len, uint32_t timeoutMs) {
    if (!_cfg.port || !buf || len == 0) return 0;

    const uint32_t start = now();
    size_t sent = 0;

    while (sent < len) {
        int room = _cfg.port->availableForWrite();
        if (room <= 0) {
            // bufor TX pełny — nie blokujemy loop() bez wyraźnej zgody
            if (timeoutMs == 0 || (now() - start) >= timeoutMs) break;
            _cfg.delayFn(1);
            continue;
        }
        size_t chunk = len - sent;
        if (chunk > static_cast<size_t>(room)) chunk = static_cast<size_t>(room);
        size_t w = _cfg.port->write(buf + sent, chunk);
        if (w == 0) break;
        sent += w;
    }

    _stats.rtcmBytesWritten += static_cast<uint32_t>(sent);
    return sent;
}

void Lc29hSensor::sendCommand(const char* body) {
    if (!_cfg.port || !body) return;
    uint8_t ck = 0;
    for (const char* p = body; *p; ++p) ck ^= static_cast<uint8_t>(*p);
    char out[128];
    int n = snprintf(out, sizeof(out), "$%s*%02X\r\n", body, ck);
    if (n <= 0) return;
    if (static_cast<size_t>(n) >= sizeof(out)) n = static_cast<int>(sizeof(out)) - 1;
    _cfg.port->write(reinterpret_cast<const uint8_t*>(out), static_cast<size_t>(n));
}

bool Lc29hSensor::setUpdateRate(uint16_t ms) {
    if (ms < 100)  ms = 100;
    if (ms > 1000) ms = 1000;
    char body[24];
    snprintf(body, sizeof(body), "PAIR050,%u", static_cast<unsigned>(ms));
    sendCommand(body);
    _cfg.rateMs = ms;
    return waitAck(50, _cfg.ackTimeoutMs);
}

bool Lc29hSensor::enableGsv(bool on) {
    // PAIR062,<typ>,<rate>; typ 3 = GSV, rate 0 = off
    char body[24];
    snprintf(body, sizeof(body), "PAIR062,3,%d", on ? 1 : 0);
    sendCommand(body);
    return waitAck(62, _cfg.ackTimeoutMs);
}

bool Lc29hSensor::waitAck(uint16_t cmdId, uint16_t timeoutMs) {
    if (timeoutMs == 0 || !_cfg.port) return true; // weryfikacja wyłączona
    _ackCmd    = 0xFFFF;
    _ackResult = -1;
    const uint32_t start = now();
    while ((now() - start) < timeoutMs) {
        while (_cfg.port->available() > 0) {
            int c = _cfg.port->read();
            if (c < 0) break;
            feed(static_cast<char>(c));
            if (_ackCmd == cmdId) return _ackResult == 0;
        }
        _cfg.delayFn(1);
    }
    return false;
}

// ---------------------------------------------------------------- RX / NMEA

void Lc29hSensor::feed(char c) {
    if (c == '$') { // start zdania (również po urwanym poprzednim)
        _inSentence = true;
        _lineLen = 0;
        _line[_lineLen++] = c;
        return;
    }
    if (!_inSentence) return;

    if (c == '\n' || c == '\r') {
        if (_lineLen > 6) {
            _line[_lineLen] = '\0';
            if (checksumOk(_line, _lineLen)) {
                _stats.sentencesOk++;
                parseSentence();
            } else {
                _stats.checksumErrors++;
            }
        }
        _inSentence = false;
        _lineLen = 0;
        return;
    }

    if (_lineLen < LINE_MAX - 1) {
        _line[_lineLen++] = c;
    } else { // przepełnienie — porzuć zdanie i policz zdarzenie
        _stats.overflows++;
        _inSentence = false;
        _lineLen = 0;
    }
}

bool Lc29hSensor::checksumOk(const char* line, size_t len) {
    if (len < 4 || line[0] != '$') return false;
    const char* star = strchr(line, '*');
    if (!star) return false;
    // po '*' muszą być dokładnie dwie cyfry hex
    if (static_cast<size_t>(star - line) + 3 > len) return false;
    if (!isxdigit(static_cast<unsigned char>(star[1])) ||
        !isxdigit(static_cast<unsigned char>(star[2]))) return false;

    uint8_t ck = 0;
    for (const char* p = line + 1; p < star; ++p) ck ^= static_cast<uint8_t>(*p);
    return ck == static_cast<uint8_t>(strtol(star + 1, nullptr, 16));
}

bool Lc29hSensor::acceptTalker(const char* hdr) {
    // hdr: "$GNGGA" -> talker "GN"
    const char t0 = hdr[1], t1 = hdr[2];
    if (_talker[0] == '\0') {
        _talker[0] = t0; _talker[1] = t1; _talker[2] = '\0';
        return true;
    }
    if (_talker[0] == t0 && _talker[1] == t1) return true;
    // upgrade na GN (rozwiązanie wielosystemowe ma pierwszeństwo)
    if (t0 == 'G' && t1 == 'N') {
        _talker[0] = 'G'; _talker[1] = 'N';
        flushEpoch(false);
        return true;
    }
    return false; // inny talker — ignoruj, by nie mieszać epok
}

void Lc29hSensor::parseSentence() {
    char* star = strchr(_line, '*');
    if (star) *star = '\0';

    if (strlen(_line) < 6) { _stats.unknownSentences++; return; } // "$xxYYY"

    // tokenizacja po ',' z zachowaniem pustych pól
    char* fields[MAX_FIELDS];
    int n = 0;
    char* p = _line;
    fields[n++] = p;
    while (*p && n < MAX_FIELDS) {
        if (*p == ',') { *p = '\0'; fields[n++] = p + 1; }
        ++p;
    }
    if (n < 2) { _stats.unknownSentences++; return; }

    const char* hdr = fields[0];

    // komunikaty własne Quectela: $PAIR001,<cmd>,<res>
    if (hdr[1] == 'P') {
        if (strncmp(hdr + 1, "PAIR001", 7) == 0) parsePairAck(fields, n);
        return;
    }

    if (!acceptTalker(hdr)) return;

    const char* type = hdr + 3; // za "$GN"/"$GP"/"$BD"...
    if      (strncmp(type, "GGA", 3) == 0) parseGGA(fields, n);
    else if (strncmp(type, "RMC", 3) == 0) parseRMC(fields, n);
    else if (strncmp(type, "GSA", 3) == 0) parseGSA(fields, n);
    else if (strncmp(type, "GST", 3) == 0) parseGST(fields, n);
    else _stats.unknownSentences++;
}

// $PAIR001,<cmdId>,<result>
void Lc29hSensor::parsePairAck(char* f[], int n) {
    if (n < 3) return;
    _ackCmd    = static_cast<uint16_t>(atoi(fld(f, n, 1)));
    _ackResult = static_cast<int8_t>(atoi(fld(f, n, 2)));
}

void Lc29hSensor::applyTime(LocationData& d, const char* hhmmss) {
    if (!hhmmss[0]) return;
    long t = strtol(hhmmss, nullptr, 10); // hhmmss (ułamek pomijamy)
    d.hour   = static_cast<uint8_t>(t / 10000);
    d.minute = static_cast<uint8_t>((t / 100) % 100);
    d.second = static_cast<uint8_t>(t % 100);
}

void Lc29hSensor::openEpoch() {
    if (_epochOpen) return;
    _epochOpen    = true;
    _epochStartMs = now();
    _pending      = LocationData{}; // czysty start: zero pól z poprzedniej epoki
    _gotGGA = _gotRMC = false;
}

// $xxGGA,time,lat,N,lon,E,quality,sats,hdop,alt,M,geoid,M,age,staId
void Lc29hSensor::parseGGA(char* f[], int n) {
    if (n < 10) return;
    openEpoch();

    applyTime(_pending, fld(f, n, 1));

    _pending.quality    = static_cast<FixQuality>(atoi(fld(f, n, 6)));
    _pending.satellites = static_cast<uint8_t>(atoi(fld(f, n, 7)));
    if (has(f, n, 8))  _pending.hdop     = strtof(fld(f, n, 8), nullptr);
    if (has(f, n, 9))  _pending.altitude = strtof(fld(f, n, 9), nullptr);
    if (has(f, n, 11)) _pending.geoidSep = strtof(fld(f, n, 11), nullptr);

    // diagnostyka RTK: wiek poprawek i ID stacji bazowej
    _pending.rtcmAgeS    = has(f, n, 13) ? strtof(fld(f, n, 13), nullptr) : -1.0f;
    _pending.baseStation = static_cast<uint16_t>(atoi(fld(f, n, 14)));

    if (_pending.quality != FixQuality::NoFix && has(f, n, 2) && has(f, n, 4)) {
        _pending.latitude  = parseCoord(fld(f, n, 2), fld(f, n, 3));
        _pending.longitude = parseCoord(fld(f, n, 4), fld(f, n, 5));
    } else {
        // brak fixa => zeruj pozycję, żeby konsument nie dostał starych
        // współrzędnych podanych jako aktualne
        _pending.latitude  = 0.0;
        _pending.longitude = 0.0;
        _pending.altitude  = 0.0f;
    }

    _gotGGA = true;
    maybeFlush();
}

// $xxRMC,time,status,lat,N,lon,E,speed(kn),course,date,...
void Lc29hSensor::parseRMC(char* f[], int n) {
    if (n < 10) return;
    openEpoch();

    applyTime(_pending, fld(f, n, 1));
    _pending.valid = (fld(f, n, 2)[0] == 'A');
    if (has(f, n, 7)) _pending.speed  = knotsToMs(strtof(fld(f, n, 7), nullptr));
    if (has(f, n, 8)) _pending.course = strtof(fld(f, n, 8), nullptr);
    if (has(f, n, 9)) {
        long d = strtol(fld(f, n, 9), nullptr, 10); // ddmmyy
        _pending.day   = static_cast<uint8_t>(d / 10000);
        _pending.month = static_cast<uint8_t>((d / 100) % 100);
        _pending.year  = static_cast<uint16_t>(2000 + d % 100);
    }

    _gotRMC = true;
    maybeFlush();
}

// $xxGSA,mode,fixType,sv1..sv12,pdop,hdop,vdop
void Lc29hSensor::parseGSA(char* f[], int n) {
    if (n < 18) return;
    openEpoch();
    if (has(f, n, 15)) _pending.pdop = strtof(fld(f, n, 15), nullptr);
    if (has(f, n, 17)) _pending.vdop = strtof(fld(f, n, 17), nullptr);
}

// $xxGST,time,rmsRes,semiMajor,semiMinor,orient,latStd,lonStd,altStd
void Lc29hSensor::parseGST(char* f[], int n) {
    if (n < 9) return;
    openEpoch();
    if (has(f, n, 6) && has(f, n, 7)) {
        const float latStd = strtof(fld(f, n, 6), nullptr);
        const float lonStd = strtof(fld(f, n, 7), nullptr);
        // błąd poziomy jako norma z obu składowych (1-sigma)
        _pending.hAcc = sqrtf(latStd * latStd + lonStd * lonStd);
    }
    if (has(f, n, 8)) _pending.vAcc = strtof(fld(f, n, 8), nullptr);
}

// ---------------------------------------------------------------- epoka

void Lc29hSensor::maybeFlush() {
    if (!_epochOpen) return;

    if (_gotGGA && _gotRMC) {          // komplet
        flushEpoch(true);
        return;
    }
    // timeout — publikujemy to, co przyszło (np. RMC wyłączony w konfiguracji)
    if ((now() - _epochStartMs) >= _cfg.epochTimeoutMs) {
        flushEpoch(false);
    }
}

void Lc29hSensor::flushEpoch(bool complete) {
    if (!_epochOpen) return;
    if (!_gotGGA && !_gotRMC) { _epochOpen = false; return; }

    // Bez RMC nie ma pola status — fix z GGA traktujemy jako wiarygodny.
    if (!_gotRMC && _pending.quality != FixQuality::NoFix) _pending.valid = true;

    _pending.lastUpdateMs = now();
    _data = _pending;

    _epochOpen = false;
    _gotGGA = _gotRMC = false;

    _stats.epochsPublished++;
    if (!complete) _stats.epochsTimedOut++;

    emit(_data);
}

// "ddmm.mmmmm" / "dddmm.mmmmm" + hemisfera -> stopnie dziesiętne
double Lc29hSensor::parseCoord(const char* val, const char* hemi) {
    const double raw = strtod(val, nullptr);
    const double deg = static_cast<double>(static_cast<int>(raw / 100.0));
    const double min = raw - deg * 100.0;
    double out = deg + min / 60.0;
    if (hemi && (hemi[0] == 'S' || hemi[0] == 'W')) out = -out;
    return out;
}

} // namespace minis
