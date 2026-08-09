#pragma once
/**
 * Hydra — elementy programowe potoku.
 *
 * Cztery sztuki, po jednej na każdy kształt elementu: źródło, filtr,
 * rozgałęzienie i ujście. Razem wystarczają do zbudowania działającego potoku
 * bez ani jednego kawałka sprzętu — a to jest warunek, żeby dało się go
 * przetestować na hoście pod sanitizerami.
 *
 * Wszystkie liczą na liczbach całkowitych. Nie dlatego, że tak ładniej:
 * RP2040 nie ma jednostki zmiennoprzecinkowej, a filtr audio wołany 344 razy
 * na sekundę na emulowanym `float` zjada rdzeń.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/media/Pipeline.hpp"

namespace hydra {
namespace media {

/**
 * Generator tonu — źródło audio bez sprzętu.
 *
 * Istnieje po to, żeby dało się sprawdzić cały łańcuch, zanim pojawi się
 * przetwornik: to samo, czym w studiu jest generator sygnału. Przebieg liczy
 * z tablicy 256 próbek, bez `sin()` — dzięki temu chodzi identycznie na
 * RP2040 i na hoście, a test może porównać wynik co do próbki.
 */
class ToneSource : public Element {
public:
    struct Config {
        MediaFormat format = MediaFormat::audio(16000, SampleFormat::S16, 1);
        u32 frequencyHz = 440;
        i16 amplitude   = 8000;
        /** Ile ramek audio w jednym bloku. Wprost przekłada się na opóźnienie. */
        u16 framesPerBlock = 128;
    };

    ToneSource() : Element("tone") {}

    Status configure(const Config& cfg);

    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

    /** Zatrzymuje generowanie i wysyła blok z flagą końca strumienia. */
    void finish() { finishing_ = true; }

    u64 framesProduced() const { return frames_; }

private:
    Config     cfg_{};
    Pipeline*  pipeline_ = nullptr;
    BlockPool* pool_ = nullptr;
    u32        phase_ = 0;       ///< Q16 w obrębie okresu
    u32        phaseStep_ = 0;
    u64        frames_ = 0;
    bool       finishing_ = false;
    bool       finished_ = false;
};

/**
 * Wzmocnienie — filtr audio S16.
 *
 * Współczynnik w Q8.8: 256 to jeden do jednego, 128 to połowa, 512 to
 * podwojenie. Wynik jest przycinany do zakresu, a nie zawijany — przepełnienie
 * `i16` przy zawinięciu daje trzask o pełnej amplitudzie, czyli najgłośniejszy
 * możliwy objaw najcichszego błędu.
 */
class Gain : public Element {
public:
    Gain() : Element("gain") {}

    /** 256 = bez zmiany. */
    void setGainQ8_8(u16 gain) { gain_ = gain; }
    u16  gainQ8_8() const { return gain_; }

    u8 inputCount() const override { return 1; }
    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

    /** Ile próbek trafiło w ogranicznik — sygnał, że wzmocnienie za duże. */
    u32 clipped() const { return clipped_; }

private:
    Pipeline* pipeline_ = nullptr;
    u16       gain_ = 256;
    u32       clipped_ = 0;
};

/**
 * Rozgałęzienie — jedno wejście, dwa wyjścia.
 *
 * Jedyne miejsce w module, w którym licznik odwołań bloku bywa większy niż
 * jeden. Blok nie jest kopiowany: obie gałęzie dostają ten sam bufor i obie
 * go zwalniają, a pula oddaje miejsce dopiero po drugim zwolnieniu. Przy
 * podglądzie klatki 320×240 różnica to 150 kB na klatkę.
 *
 * Z tego wynika reguła, której trzeba pilnować: **gałąź nie ma prawa pisać
 * po bloku**. Element modyfikujący musi stać za rozgałęzieniem po tej stronie,
 * gdzie ma działać, a nie przed nim.
 */
class Tee : public Element {
public:
    Tee() : Element("tee") {}

    u8 inputCount() const override { return 1; }
    u8 outputCount() const override { return 2; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

private:
    Pipeline* pipeline_ = nullptr;
};

/**
 * Ujście z pomiarem.
 *
 * Kończy potok: zwalnia bloki i mierzy, co przez nie przeszło. Szczyt
 * amplitudy nie jest ozdobą — to jedyny sposób, żeby zdalnie stwierdzić,
 * czy mikrofon w ogóle coś słyszy, bez przesyłania dźwięku.
 */
class MeterSink : public Element {
public:
    MeterSink() : Element("meter") {}

    u8 inputCount() const override { return 1; }
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

    u64 blocks() const { return blocks_; }
    u64 bytes() const { return bytes_; }
    /** Szczyt bezwzględny od ostatniego odczytu; odczyt zeruje. */
    u16 takePeak();
    u64 lastPts() const { return lastPts_; }
    bool sawEos() const { return sawEos_; }

private:
    Pipeline* pipeline_ = nullptr;
    u64       blocks_ = 0;
    u64       bytes_ = 0;
    u64       lastPts_ = 0;
    u16       peak_ = 0;
    bool      sawEos_ = false;
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
