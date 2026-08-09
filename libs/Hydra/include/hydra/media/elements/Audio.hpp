#pragma once
/**
 * Hydra — elementy audio na sprzęcie (etap 2).
 *
 * Dwie rodziny, bo peryferia dzielą się na dwie i różnią się wszystkim:
 *
 * **Blokowe (I2S).** Kontroler bierze bufor i oddaje go po przerwaniu.
 * Element nie czeka — w jednym `process()` odbiera to, co gotowe, i podaje
 * nowe. To jest ten przypadek, dla którego etap 1 dostał asynchroniczne
 * przekazywanie własności; gdyby `Element` musiał oddać wynik w tym samym
 * wywołaniu, I2S dałoby się wpiąć tylko przez blokadę na semaforze.
 *
 * **Próbkowe (PWM, DAC, ADC).** Nie ma DMA i nie ma buforów: jest jeden
 * rejestr, do którego trzeba trafiać równo w rytm próbkowania. Element
 * wystawia więc tyle próbek, ile minęło czasu — licząc budżet z `nowUs`,
 * a nie z liczby wywołań. Dzięki temu drganie okresu taska rozjeżdża
 * chwilowe tempo, a nie długoterminową wysokość dźwięku.
 *
 * Górna granica tej drugiej rodziny wynika wprost z okresu domeny: przy
 * kroku co 1 ms i buforze na 64 próbki wychodzi 64 kHz teoretycznie, ale
 * każde spóźnienie taska słychać. Powyżej kilku kiloherców trzeba timera
 * sprzętowego, a nie krótszego okresu — i to jest ograniczenie sprzętu,
 * nie tego kodu.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/hal/IAdc.hpp"
#include "hydra/hal/IDac.hpp"
#include "hydra/hal/II2s.hpp"
#include "hydra/hal/IPwm.hpp"
#include "hydra/media/Pipeline.hpp"

/** Ile buforów element I2S może mieć naraz u sterownika. */
#ifndef HYDRA_MEDIA_I2S_INFLIGHT
#  define HYDRA_MEDIA_I2S_INFLIGHT 4
#endif

namespace hydra {
namespace media {

/**
 * Bufory oddane sprzętowi.
 *
 * Sterownik zwraca `ByteSpan`, a my musimy z tego odzyskać `Block`, żeby go
 * zwolnić do puli. Tablica jest krótka i przeszukiwana liniowo — przy czterech
 * wpisach mapa byłaby wolniejsza od pętli.
 */
class InFlightTable {
public:
    bool  add(const Block& block, size_t bytes);
    /** Wyjmuje wpis o danym adresie danych. */
    bool  take(const u8* data, Block& out);
    u8    count() const { return count_; }
    bool  full() const { return count_ >= HYDRA_MEDIA_I2S_INFLIGHT; }
    /** Oddaje kolejny wpis bez dopasowywania — do sprzątania przy stopie. */
    bool  drain(Block& out);

private:
    struct Entry { Block block; const u8* data = nullptr; bool used = false; };
    Entry entries_[HYDRA_MEDIA_I2S_INFLIGHT];
    u8    count_ = 0;
};

// ---------------------------------------------------------------------------
// I2S
// ---------------------------------------------------------------------------

/** Wyjście audio na I2S — kodek, wzmacniacz klasy D, słuchawki. */
class I2sSink : public Element {
public:
    explicit I2sSink(hal::II2s& i2s) : Element("i2s-out"), i2s_(i2s) {}

    /**
     * Konfiguracja sprzętu. Częstotliwość i liczba kanałów muszą się zgadzać
     * z formatem negocjowanym w potoku — rozjazd jest zgłaszany przy
     * `prepare()`, a nie słyszany jako dźwięk o złej wysokości.
     */
    void configure(const hal::I2sConfig& cfg) { cfg_ = cfg; }

    u8 inputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    Status onPrepare(Pipeline& pipeline) override;
    Status onStart() override;
    void   onStop() override;
    void   process(u64 nowUs) override;

    u32 submitted() const { return submitted_; }
    u32 xruns() const { return i2s_.xruns(); }

private:
    hal::II2s&    i2s_;
    hal::I2sConfig cfg_{};
    Pipeline*     pipeline_ = nullptr;
    InFlightTable inFlight_;
    u32           submitted_ = 0;
};

/** Wejście audio z I2S — mikrofon MEMS, przetwornik ADC, kodek. */
class I2sSource : public Element {
public:
    explicit I2sSource(hal::II2s& i2s) : Element("i2s-in"), i2s_(i2s) {}

    void configure(const hal::I2sConfig& cfg) { cfg_ = cfg; }
    /** Ile ramek w bloku. Wprost wyznacza opóźnienie wejścia. */
    void setFramesPerBlock(u16 frames) { framesPerBlock_ = frames; }

    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    Status onStart() override;
    void   onStop() override;
    void   process(u64 nowUs) override;

    u64 framesCaptured() const { return frames_; }
    u32 xruns() const { return i2s_.xruns(); }

private:
    hal::II2s&     i2s_;
    hal::I2sConfig cfg_{};
    u16            framesPerBlock_ = 128;
    Pipeline*      pipeline_ = nullptr;
    BlockPool*     pool_ = nullptr;
    InFlightTable  inFlight_;
    u64            frames_ = 0;
};

// ---------------------------------------------------------------------------
// Peryferia próbkowe
// ---------------------------------------------------------------------------

/**
 * Wspólna część wyjść próbkowych.
 *
 * Trzyma bieżący blok i pozycję w nim, liczy budżet próbek z upływu czasu
 * i woła `writeSample()` klasy pochodnej. Sama nie wie nic o PWM ani o DAC —
 * różnica między nimi to jedna metoda.
 */
class SampleSink : public Element {
public:
    explicit SampleSink(const char* name) : Element(name) {}

    u8 inputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    Status onPrepare(Pipeline& pipeline) override;
    void   onStop() override;
    void   process(u64 nowUs) override;

    u64 samplesWritten() const { return written_; }
    /** Ile próbek przepadło, bo nie było czego wystawić. */
    u32 starved() const { return starved_; }

protected:
    /** Próbka przeskalowana do 0…`fullScale()`. */
    virtual Status writeSample(u16 value) = 0;
    virtual u16    fullScale() const = 0;

    void releaseCurrent();

    Pipeline* pipeline_ = nullptr;
    Block     current_{};
    u32       offset_ = 0;      ///< bajt w bieżącym bloku
    u32       sampleRate_ = 0;
    u8        channels_ = 1;
    u64       lastUs_ = 0;
    /**
     * Czy `lastUs_` znaczy cokolwiek.
     *
     * Osobna flaga, a nie zero jako wartownik: potok wystartowany w chwili
     * zerowej zegara — a tak wygląda każdy test i każdy start po resecie —
     * nigdy nie wychodził z fazy „pierwsze wywołanie" i nie wystawiał ani
     * jednej próbki.
     */
    bool      primed_ = false;
    u64       written_ = 0;
    u32       starved_ = 0;
    /** Reszta budżetu poniżej jednej próbki — bez niej tempo dryfuje w dół. */
    u64       carryUs_ = 0;
};

/**
 * Audio na PWM.
 *
 * Nośną trzeba ustawić dobrze powyżej pasma sygnału, inaczej filtr RC na
 * wyjściu nie odróżni jej od dźwięku. Regułą jest co najmniej dziesięciokrotność
 * częstotliwości próbkowania; przy 8 kHz oznacza to 80 kHz i rozdzielczość
 * spadającą do dziewięciu bitów na typowym ESP32 — dlatego PWM jest rozwiązaniem
 * dla brzęczyka i komunikatów głosowych, a nie dla muzyki.
 */
class PwmAudioSink : public SampleSink {
public:
    PwmAudioSink(hal::IPwm& pwm, hal::PinNum pin)
        : SampleSink("pwm-out"), pwm_(pwm), pin_(pin) {}

    /**
     * Nośna i rozdzielczość. Domyślnie 10 bitów przy 80 kHz.
     *
     * Kanał PWM **nie** jest zwalniany przy zatrzymaniu — patrz `onStop()`.
     * Gdy pin ma wrócić do innego zastosowania, aplikacja woła `release()`
     * sama, bo tylko ona wie, czy głośnik jest jeszcze podłączony.
     */
    void configure(u32 carrierHz, u8 resolutionBits = 10) {
        carrierHz_ = carrierHz;
        bits_ = resolutionBits;
    }

    Status onStart() override;
    void   onStop() override;

protected:
    Status writeSample(u16 value) override;
    u16    fullScale() const override { return 1000; }   // permile w IPwm

private:
    hal::IPwm&  pwm_;
    hal::PinNum pin_;
    u32         carrierHz_ = 80000;
    u8          bits_ = 10;
};

/** Audio na sprzętowym DAC. Bez filtru nośnej, bo nie ma nośnej. */
class DacAudioSink : public SampleSink {
public:
    DacAudioSink(hal::IDac& dac, u8 channel = 0)
        : SampleSink("dac-out"), dac_(dac), channel_(channel) {}

    Status onStart() override;
    void   onStop() override;

protected:
    Status writeSample(u16 value) override;
    u16    fullScale() const override { return scale_; }

private:
    hal::IDac& dac_;
    u8         channel_;
    u16        scale_ = 255;
};

/**
 * Wejście audio z ADC.
 *
 * Odczyt jest jednorazowy i blokujący na czas konwersji, więc tempo wyznacza
 * ta sama arytmetyka budżetu, co po stronie wyjść. Składowa stała wejścia
 * (zwykle połowa zakresu, bo sygnał jest spolaryzowany) jest odejmowana
 * filtrem górnoprzepustowym pierwszego rzędu — bez tego cały sygnał siedzi
 * przy jednej szynie i po wzmocnieniu wychodzi z zakresu.
 */
class AdcAudioSource : public Element {
public:
    AdcAudioSource(hal::IAdc& adc, hal::PinNum pin)
        : Element("adc-in"), adc_(adc), pin_(pin) {}

    struct Config {
        u32 sampleRate = 8000;
        u16 framesPerBlock = 128;
        /** Czy odejmować składową stałą. Dla czujnika — nie, dla mikrofonu — tak. */
        bool removeDc = true;
    };

    void configure(const Config& cfg) { cfg_ = cfg; }

    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    Status onStart() override;
    void   onStop() override;
    void   process(u64 nowUs) override;

    u64 framesCaptured() const { return frames_; }

private:
    void flush();

    hal::IAdc&  adc_;
    hal::PinNum pin_;
    Config      cfg_{};
    Pipeline*   pipeline_ = nullptr;
    BlockPool*  pool_ = nullptr;
    Block       current_{};
    u32         offset_ = 0;
    u64         lastUs_ = 0;
    bool        primed_ = false;   ///< patrz SampleSink::primed_
    u64         carryUs_ = 0;
    u64         frames_ = 0;
    /** Średnia bieżąca składowej stałej w Q16. */
    i32         dc_ = 0;
    bool        dcPrimed_ = false;
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
