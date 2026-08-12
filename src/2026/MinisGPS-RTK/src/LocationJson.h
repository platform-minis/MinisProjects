#pragma once
/**
 * LocationJson.h — MinisLib
 * Serializacja LocationData do JSON (payload MQTT).
 * Bez alokacji — zapis do bufora użytkownika.
 */

#include "LocationSensor.h"
#include <stdio.h>

namespace minis {

/**
 * Zapisuje stan do bufora jako JSON.
 * @return liczba zapisanych znaków (bez '\0') lub 0 przy błędzie/za małym buforze.
 * Pola opcjonalne (hAcc/vAcc/pdop/vdop/rtcmAge) pomijane, gdy niedostępne.
 */
inline size_t toJson(const LocationData& d, char* buf, size_t len) {
    if (!buf || len == 0) return 0;

    int n = snprintf(buf, len,
        "{\"lat\":%.8f,\"lon\":%.8f,\"alt\":%.2f,"
        "\"speed\":%.2f,\"course\":%.1f,"
        "\"quality\":\"%s\",\"sats\":%u,\"hdop\":%.2f,"
        "\"valid\":%s,"
        "\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02uZ\"",
        d.latitude, d.longitude, static_cast<double>(d.altitude),
        static_cast<double>(d.speed), static_cast<double>(d.course),
        fixQualityName(d.quality), static_cast<unsigned>(d.satellites),
        static_cast<double>(d.hdop),
        d.valid ? "true" : "false",
        static_cast<unsigned>(d.year), static_cast<unsigned>(d.month),
        static_cast<unsigned>(d.day), static_cast<unsigned>(d.hour),
        static_cast<unsigned>(d.minute), static_cast<unsigned>(d.second));

    if (n < 0 || static_cast<size_t>(n) >= len) return 0;
    size_t used = static_cast<size_t>(n);

    const auto append = [&](const char* fmt, double v) {
        int k = snprintf(buf + used, len - used, fmt, v);
        if (k > 0 && static_cast<size_t>(k) < len - used) used += static_cast<size_t>(k);
    };

    if (d.hAcc >= 0.0f)     append(",\"hAcc\":%.3f",   static_cast<double>(d.hAcc));
    if (d.vAcc >= 0.0f)     append(",\"vAcc\":%.3f",   static_cast<double>(d.vAcc));
    if (d.pdop >= 0.0f)     append(",\"pdop\":%.2f",   static_cast<double>(d.pdop));
    if (d.vdop >= 0.0f)     append(",\"vdop\":%.2f",   static_cast<double>(d.vdop));
    if (d.rtcmAgeS >= 0.0f) append(",\"rtcmAge\":%.1f", static_cast<double>(d.rtcmAgeS));
    if (d.baseStation != 0) append(",\"baseStation\":%.0f", static_cast<double>(d.baseStation));

    if (used + 2 > len) return 0;
    buf[used++] = '}';
    buf[used]   = '\0';
    return used;
}

} // namespace minis
