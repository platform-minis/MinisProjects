#pragma once
/**
 * LocationSensor.h — MinisLib
 * Abstrakcyjny interfejs sensora lokalizacji (GNSS) dla integracji IoT.
 * Implementacje: Lc29hSensor (Quectel LC29H, UART/NMEA).
 *
 * Konfiguracja kompilacji:
 *   MINIS_NO_STD_FUNCTION — wyłącza callback std::function (zostaje wariant
 *                           surowy: wskaźnik na funkcję + kontekst). Przydatne
 *                           na małych MCU bez sterty / z wyłączonym RTTI.
 */

#include <stdint.h>
#include <stddef.h>

#ifndef MINIS_NO_STD_FUNCTION
#include <functional>
#include <utility>
#endif

namespace minis {

/// Jakość fixa wg NMEA GGA (pole 6)
enum class FixQuality : uint8_t {
    NoFix    = 0,
    Gps      = 1,  ///< autonomiczny GNSS
    Dgps     = 2,  ///< SBAS / DGPS
    PpsFix   = 3,
    RtkFixed = 4,  ///< RTK fixed (cm)
    RtkFloat = 5,  ///< RTK float (dm)
    DeadReck = 6,  ///< nawigacja inercyjna / DR
};

/// Nazwa jakości fixa (stabilne identyfikatory pod JSON/MQTT)
inline const char* fixQualityName(FixQuality q) {
    switch (q) {
        case FixQuality::Gps:      return "gps";
        case FixQuality::Dgps:     return "dgps";
        case FixQuality::PpsFix:   return "pps";
        case FixQuality::RtkFixed: return "rtk_fixed";
        case FixQuality::RtkFloat: return "rtk_float";
        case FixQuality::DeadReck: return "dead_reckoning";
        case FixQuality::NoFix:    return "none";
    }
    return "none";
}

/**
 * Aktualny stan lokalizacji — POD, tani w kopiowaniu.
 * Konwencja: wartości ujemne w polach *Acc / rtcmAgeS oznaczają "brak danych".
 */
struct LocationData {
    double   latitude    = 0.0;   ///< stopnie, WGS84, +N
    double   longitude   = 0.0;   ///< stopnie, WGS84, +E
    float    altitude    = 0.0f;  ///< m n.p.m. (MSL)
    float    geoidSep    = 0.0f;  ///< separacja geoidy [m] (alt. elipsoidalna = altitude + geoidSep)
    float    speed       = 0.0f;  ///< m/s (grunt)
    float    course      = 0.0f;  ///< stopnie, true
    float    hdop        = 99.9f;
    float    pdop        = -1.0f; ///< z GSA, <0 = brak
    float    vdop        = -1.0f; ///< z GSA, <0 = brak
    float    hAcc        = -1.0f; ///< 1-sigma pozioma [m] z GST, <0 = brak
    float    vAcc        = -1.0f; ///< 1-sigma pionowa [m] z GST, <0 = brak
    float    rtcmAgeS    = -1.0f; ///< wiek poprawek RTCM [s] (GGA p.13), <0 = brak
    uint16_t baseStation = 0;     ///< ID stacji referencyjnej (GGA p.14)
    uint8_t  satellites  = 0;
    FixQuality quality   = FixQuality::NoFix;
    bool     valid       = false; ///< RMC status == 'A'
    // Czas UTC z odbiornika
    uint8_t  hour = 0, minute = 0, second = 0;
    uint8_t  day  = 0, month  = 0;
    uint16_t year = 0;
    uint32_t lastUpdateMs = 0;    ///< millis() ostatniej aktualizacji

    bool hasRtk() const {
        return quality == FixQuality::RtkFixed || quality == FixQuality::RtkFloat;
    }
};

/**
 * Interfejs sensora lokalizacji.
 * Model użycia: begin() raz, update() w pętli (nieblokujące),
 * odczyt przez data() lub callback onUpdate/onUpdateRaw.
 */
class LocationSensor {
public:
    /// Callback bez alokacji: wskaźnik na funkcję + kontekst użytkownika.
    using UpdateFn = void (*)(const LocationData&, void* ctx);

    virtual ~LocationSensor() = default;

    virtual bool begin() = 0;
    virtual void end() = 0;

    /// Nieblokujące przetwarzanie danych z modułu; wołać często w loop/task.
    virtual void update() = 0;

    virtual const LocationData& data() const = 0;
    virtual bool hasFix() const = 0;

    /// Callback surowy (bez sterty) — wołany po skompletowaniu epoki.
    void onUpdateRaw(UpdateFn fn, void* ctx = nullptr) { _fn = fn; _ctx = ctx; }

#ifndef MINIS_NO_STD_FUNCTION
    using UpdateCallback = std::function<void(const LocationData&)>;
    /// Callback wygodny (lambda z przechwytywaniem) — wołany po epoce.
    void onUpdate(UpdateCallback cb) { _callback = std::move(cb); }
#endif

protected:
    void emit(const LocationData& d) {
        if (_fn) _fn(d, _ctx);
#ifndef MINIS_NO_STD_FUNCTION
        if (_callback) _callback(d);
#endif
    }

    void clearCallbacks() {
        _fn = nullptr;
        _ctx = nullptr;
#ifndef MINIS_NO_STD_FUNCTION
        _callback = nullptr;
#endif
    }

private:
    UpdateFn _fn  = nullptr;
    void*    _ctx = nullptr;
#ifndef MINIS_NO_STD_FUNCTION
    UpdateCallback _callback;
#endif
};

} // namespace minis
