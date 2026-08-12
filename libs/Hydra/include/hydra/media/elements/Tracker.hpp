#pragma once
/**
 * Hydra — moduł Amigi jako źródło potoku.
 *
 * Cztery kanały muzyki trackerowej z pliku `.mod`. Odtwarzacz to pocketmod
 * (`src/pocketmod/`), a ten element jest wyłącznie adapterem: wypełnia bloki
 * potoku tym, co odtwarzacz wyrenderuje, i pilnuje znaczników czasu.
 *
 * Wartość jest w tym, czego tu **nie ma**: syntezatora, obsługi formatu,
 * miksowania kanałów. Cztery kanały z próbkami, efekty Protrackera i pętle
 * wzorców mają już czterdzieści lat i są rozwiązane — a potok Hydry umie
 * przenieść dźwięk na I2S, PWM albo do okna SDL bez wiedzy o tym, skąd się
 * wziął.
 *
 * ## Moduł zostaje w pamięci
 *
 * pocketmod nie kopiuje pliku: czyta z niego próbki instrumentów w trakcie
 * odtwarzania. Bufor musi więc przeżyć element. Na urządzeniu oznacza to
 * wczytanie pliku z VFS do RAM-u albo tablicę w pamięci programu — moduł
 * z lat 90. to zwykle 50–300 kB, więc na małych płytkach jest to decyzja
 * projektowa, a nie szczegół.
 *
 * ## Stereo, zawsze
 *
 * Odtwarzacz produkuje dwa kanały i element ich nie miesza. Zejście do mono
 * to osobna decyzja — uśrednienie brzmi inaczej niż wybór kanału, a moduły
 * z Amigi mają kanały rozrzucone twardo na lewo i prawo, więc uśrednienie
 * bywa jedynym sensownym wyborem, ale nie zawsze.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/media/Element.hpp"

/**
 * Miejsce na kontekst pocketmoda.
 *
 * Odtwarzacz jest konstruowany w miejscu, bo Hydra nie alokuje po starcie.
 * Rozmiar sprawdza `static_assert` w pliku źródłowym: gdyby biblioteka urosła
 * przy aktualizacji, budowa zatrzyma się z komunikatem, ile brakuje, zamiast
 * psuć pamięć obok.
 */
#ifndef HYDRA_MOD_CONTEXT_BYTES
#  define HYDRA_MOD_CONTEXT_BYTES 2048
#endif

namespace hydra {
namespace media {

class ModSource : public Element {
public:
    struct Config {
        /** Częstotliwość próbkowania wyjścia. */
        u32 sampleRate = 44100;
        /** Ile ramek stereo w jednym bloku — wprost przekłada się na opóźnienie. */
        u16 framesPerBlock = 128;
        /**
         * Po ilu przejściach modułu skończyć. Zero znaczy „graj bez końca",
         * co dla muzyki w tle jest stanem normalnym, a nie brakiem decyzji.
         */
        u8  maxLoops = 0;
    };

    ModSource() : Element("mod") {}

    /**
     * Wczytuje moduł. Bufor **musi przeżyć** element — patrz nagłówek.
     *
     * Wołać przed `prepare()`: format wyjścia zależy od częstotliwości, a ta
     * jest ustalana przy wczytaniu.
     */
    Status load(CByteSpan module, const Config& cfg);

    /** Czy moduł jest wczytany i gotowy do grania. */
    bool ready() const { return ready_; }

    /** Ile razy moduł doszedł do końca i zaczął od nowa. */
    u32 loops() const;

    /** Ile ramek wyprodukowano — do sprawdzenia, czy źródło w ogóle gra. */
    u64 framesProduced() const { return frames_; }

    /** Zatrzymuje granie i wysyła blok z flagą końca strumienia. */
    void finish() { finishing_ = true; }

    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

private:
    Config     cfg_{};
    Pipeline*  pipeline_ = nullptr;
    BlockPool* pool_ = nullptr;

    /** Kontekst pocketmoda konstruowany w miejscu — patrz stała wyżej. */
    alignas(8) u8 context_[HYDRA_MOD_CONTEXT_BYTES] = {};
    bool ready_ = false;

    u64  frames_ = 0;
    bool finishing_ = false;
    bool finished_ = false;
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
