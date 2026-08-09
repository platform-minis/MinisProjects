#pragma once
/**
 * Hydra — formaty i zdarzenia potoku multimedialnego.
 *
 * Moduł `media` jest tym, czym GStreamer dla Linuksa: grafem elementów, przez
 * który płyną dane. Różnice wynikają wprost z reguł frameworka i są tu
 * najważniejszą treścią:
 *
 *  • **Format jest strukturą, nie napisem.** GStreamer negocjuje po tekstowych
 *    „capsach"; tutaj to POD porównywany polami. Parsowanie
 *    „audio/x-raw,rate=44100" na układzie bez sterty nie miałoby jak działać,
 *    a `switch` po wyliczeniu wyłapuje przypadek, o którym ktoś zapomniał.
 *  • **Graf jest zamrażany.** Elementy i połączenia deklaruje się przed
 *    `App::begin()`, potem topologia nie zmienia się nigdy — bo po starcie nic
 *    się nie alokuje. Przełączenie źródła robi się drugim, gotowym potokiem.
 *  • **Cisza jest błędem.** Zgubiony blok, spóźnione źródło i pusty bufor
 *    wyjścia są zdarzeniami na magistrali, a nie przerwą w dźwięku, o której
 *    nikt się nie dowie.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace media {

/** Rodzina danych płynących przez pad. */
enum class MediaKind : u8 { None = 0, Audio, Video };

/**
 * Układ próbek audio.
 *
 * Tylko to, co naprawdę wychodzi z przetworników i wchodzi do nich. `S16` jest
 * walutą całego modułu: I2S go daje, PWM go przyjmuje, a mieszanie i filtry
 * mieszczą się w 32 bitach bez utraty. `F32` istnieje dla efektów liczonych
 * na FPU, `U8` — dla wewnętrznego DAC-a i ADC-a.
 */
enum class SampleFormat : u8 { None = 0, U8, S16, S24, S32, F32 };

constexpr u8 bytesPerSample(SampleFormat f) {
    switch (f) {
        case SampleFormat::U8:  return 1;
        case SampleFormat::S16: return 2;
        case SampleFormat::S24: return 3;
        case SampleFormat::S32: return 4;
        case SampleFormat::F32: return 4;
        case SampleFormat::None: return 0;
    }
    return 0;
}

constexpr const char* toString(SampleFormat f) {
    switch (f) {
        case SampleFormat::U8:   return "u8";
        case SampleFormat::S16:  return "s16";
        case SampleFormat::S24:  return "s24";
        case SampleFormat::S32:  return "s32";
        case SampleFormat::F32:  return "f32";
        case SampleFormat::None: return "none";
    }
    return "none";
}

/**
 * Postać obrazu.
 *
 * `Jpeg` stoi obok formatów surowych świadomie: kamera na ESP32 oddaje gotowy
 * JPEG, a przepuszczenie go przez potok bez dekodowania jest najczęstszym
 * i najtańszym przypadkiem. Format „skompresowany" jako osobna oś oznaczałby
 * drugą negocjację obok tej, która już jest.
 */
enum class FrameFormat : u8 {
    None = 0,
    Gray8,
    Rgb565,     ///< bajt starszy pierwszy — układ paneli SPI, ten sam co w gfx
    Rgb888,
    Yuv422,     ///< YUYV, natywny format większości sensorów
    Jpeg,       ///< strumień skompresowany; szerokość i wysokość opisowe
};

constexpr const char* toString(FrameFormat f) {
    switch (f) {
        case FrameFormat::Gray8:  return "gray8";
        case FrameFormat::Rgb565: return "rgb565";
        case FrameFormat::Rgb888: return "rgb888";
        case FrameFormat::Yuv422: return "yuv422";
        case FrameFormat::Jpeg:   return "jpeg";
        case FrameFormat::None:   return "none";
    }
    return "none";
}

/** Bity na piksel; 0 dla formatów o zmiennej długości. */
constexpr u8 bitsPerPixel(FrameFormat f) {
    switch (f) {
        case FrameFormat::Gray8:  return 8;
        case FrameFormat::Rgb565: return 16;
        case FrameFormat::Rgb888: return 24;
        case FrameFormat::Yuv422: return 16;
        case FrameFormat::Jpeg:   return 0;
        case FrameFormat::None:   return 0;
    }
    return 0;
}

/**
 * Format danych na padzie.
 *
 * Jedna struktura na dźwięk i obraz, bo pad ma jeden format, a unia
 * oznaczałaby pole `kind`, które trzeba sprawdzić przed każdym dostępem —
 * i którego ktoś kiedyś nie sprawdzi. Nieużywane pola są zerami.
 */
struct MediaFormat {
    MediaKind kind = MediaKind::None;

    // --- audio ---
    u32          sampleRate = 0;
    SampleFormat sampleFormat = SampleFormat::None;
    u8           channels = 0;

    // --- wideo ---
    FrameFormat frameFormat = FrameFormat::None;
    u16         width  = 0;
    u16         height = 0;
    /** Klatek na sekundę × 1000; 0 = źródło nieregularne. */
    u32         frameRateMilli = 0;

    bool valid() const { return kind != MediaKind::None; }

    static MediaFormat audio(u32 rate, SampleFormat format, u8 channelCount) {
        MediaFormat f;
        f.kind = MediaKind::Audio;
        f.sampleRate = rate;
        f.sampleFormat = format;
        f.channels = channelCount;
        return f;
    }

    static MediaFormat video(FrameFormat format, u16 w, u16 h, u32 fpsMilli = 0) {
        MediaFormat f;
        f.kind = MediaKind::Video;
        f.frameFormat = format;
        f.width = w;
        f.height = h;
        f.frameRateMilli = fpsMilli;
        return f;
    }

    bool equals(const MediaFormat& other) const;

    /** Bajty jednej ramki audio (wszystkie kanały) albo jednego piksela. */
    u32 unitBytes() const {
        return kind == MediaKind::Audio
                   ? static_cast<u32>(bytesPerSample(sampleFormat)) * channels
                   : static_cast<u32>(bitsPerPixel(frameFormat)) / 8u;
    }

    /** Bajty pełnej klatki obrazu; 0 dla formatów o zmiennej długości. */
    u32 frameBytes() const {
        if (kind != MediaKind::Video) return 0;
        return static_cast<u32>(width) * height * bitsPerPixel(frameFormat) / 8u;
    }
};

// ---------------------------------------------------------------------------
// Zdarzenia
// ---------------------------------------------------------------------------

/** Stan potoku. Nazwy jak w GStreamerze, bo znaczą to samo. */
enum class PipelineState : u8 {
    Idle = 0,   ///< przed prepare()
    Ready,      ///< formaty uzgodnione, bufory przydzielone
    Running,
    Paused,
};

constexpr const char* toString(PipelineState s) {
    switch (s) {
        case PipelineState::Idle:    return "idle";
        case PipelineState::Ready:   return "ready";
        case PipelineState::Running: return "running";
        case PipelineState::Paused:  return "paused";
    }
    return "unknown";
}

/** Dlaczego blok nie dotarł tam, gdzie miał. */
enum class MediaFault : u8 {
    Underrun = 0,   ///< ujście chciało danych, a ich nie było
    Overrun,        ///< kolejka pełna, blok odrzucony
    PoolEmpty,      ///< źródło nie miało z czego wziąć bufora
    FormatMismatch, ///< pady połączone, ale formaty się nie zgadzają
    TooLarge,       ///< blok większy niż bufor odbiorcy
};

constexpr const char* toString(MediaFault f) {
    switch (f) {
        case MediaFault::Underrun:       return "underrun";
        case MediaFault::Overrun:        return "overrun";
        case MediaFault::PoolEmpty:      return "pool-empty";
        case MediaFault::FormatMismatch: return "format-mismatch";
        case MediaFault::TooLarge:       return "too-large";
    }
    return "unknown";
}

struct MediaStateChanged {
    PipelineState from;
    PipelineState to;
    u8            elements;
    u8            domains;
};

/**
 * Zakłócenie w potoku.
 *
 * Publikowane, a nie logowane: przerwa w dźwięku trwa trzy milisekundy
 * i w logu ginie, a licznik na wykresie pokazuje, że urządzenie nie wyrabia.
 * To ten sam wybór, co przy spóźnieniach tasków.
 */
struct MediaFaultRaised {
    MediaFault fault;
    u8         element;   ///< indeks elementu w potoku
    u8         pad;
    u32        total;     ///< łączna liczba zakłóceń tego rodzaju
};

/** Koniec strumienia doszedł do ujścia — plik odtworzony do końca. */
struct MediaEndOfStream {
    u8  element;
    u64 blocks;
};

}  // namespace media
}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::media::MediaStateChanged, "media/state")
HYDRA_DECLARE_EVENT(hydra::media::MediaFaultRaised,  "media/fault")
HYDRA_DECLARE_EVENT(hydra::media::MediaEndOfStream,  "media/eos")

#endif  // HYDRA_ENABLE_MEDIA
