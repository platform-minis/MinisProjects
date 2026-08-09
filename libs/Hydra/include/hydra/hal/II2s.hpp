#pragma once
/**
 * Hydra — I2S: strumień audio z przekazywaniem własności buforów.
 *
 * Jedyny peryferiał w HAL-u, który **nie** ma metod `read()` i `write()`
 * kopiujących dane, i to jest w nim najważniejsze. Powód jest ten sam, dla
 * którego `media::Element` nie musi oddawać wyniku w tym samym wywołaniu:
 * I2S chodzi z DMA. Bufor oddaje się kontrolerowi i dostaje z powrotem po
 * przerwaniu, kilka milisekund później.
 *
 *     submit(buffer)     → bufor należy do sterownika
 *     …DMA…
 *     reclaim(buffer, n) → bufor wraca do wołającego
 *
 * Interfejs z `read(out, timeout)` wymuszałby na warstwie wyżej czekanie —
 * a task audio, który czeka, jest taskiem, który nie liczy. Przy 16 kHz
 * i blokach po 64 ramki to cztery milisekundy blokady co cztery milisekundy,
 * czyli połowa dostępnego czasu oddana za nic.
 *
 * **Backend kopiujący jest dopuszczalny.** ESP-IDF nie daje własności buforów
 * DMA — daje `i2s_channel_write()`, które kopiuje do swojego pierścienia.
 * Taki backend przyjmuje bufor w `submit()`, kopiuje i oddaje go w najbliższym
 * `reclaim()`. Warstwa wyżej nie widzi różnicy, bo nigdy nie zakłada, kiedy
 * bufor wróci — zakłada tylko, że wróci.
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/hal/Pin.hpp"

namespace hydra {
namespace hal {

/** Kierunek pracy kanału. */
enum class I2sDirection : u8 { Rx = 0, Tx, Duplex };

/**
 * Ułożenie danych na magistrali.
 *
 * `Philips` to standard i domyślna wartość. `LeftJustified` zdarza się
 * w starszych przetwornikach, a `Pdm` to zupełnie inny protokół obsługiwany
 * przez ten sam kontroler — mikrofony MEMS na ESP32 są prawie zawsze PDM.
 */
enum class I2sStandard : u8 { Philips = 0, LeftJustified, Pdm };

struct I2sConfig {
    u32          sampleRate    = 16000;
    /** 16, 24 albo 32. Przetworniki 24-bitowe nadają w słowach 32-bitowych. */
    u8           bitsPerSample = 16;
    u8           channels      = 2;
    I2sDirection direction     = I2sDirection::Tx;
    I2sStandard  standard      = I2sStandard::Philips;

    PinNum bclk = kNoPin;
    PinNum ws   = kNoPin;   ///< LRCLK / WS
    PinNum dout = kNoPin;
    PinNum din  = kNoPin;
    /**
     * Zegar główny dla przetwornika. `kNoPin`, gdy układ generuje go sam —
     * większość kodeków tego wymaga, część tanich wzmacniaczy klasy D nie.
     */
    PinNum mclk = kNoPin;

    bool master = true;

    u32 frameBytes() const {
        return static_cast<u32>(bitsPerSample / 8) * channels;
    }
};

class II2s {
public:
    virtual ~II2s() = default;

    virtual Status begin(const I2sConfig& cfg) = 0;
    virtual void   end() = 0;
    virtual bool   running() const = 0;

    /**
     * Oddaje bufor sprzętowi.
     *
     * Dla `Tx` bufor musi być wypełniony; dla `Rx` — pusty, sterownik go
     * zapełni. Po powrocie bufor należy do sterownika i **nie wolno go
     * dotykać** aż do `reclaim()`.
     *
     * `Busy` oznacza pełną kolejkę sprzętu, a nie błąd: wołający ma spróbować
     * w następnym kroku, bo bufory wracają w tempie strumienia.
     */
    virtual Status submit(ByteSpan buffer) = 0;

    /**
     * Odbiera bufor, którego transfer się zakończył.
     *
     * `bytes` mówi, ile bajtów zostało naprawdę przeniesione — dla `Rx` bywa
     * mniej niż pojemność, gdy strumień się kończy. `false` = nic gotowego.
     */
    virtual bool reclaim(ByteSpan& buffer, u32& bytes) = 0;

    /** Ile buforów sterownik przyjmie naraz. Poniżej dwóch nie ma sensu. */
    virtual u8 queueDepth() const = 0;

    /**
     * Ile razy sprzęt nie miał czego nadać (Tx) albo nie miał gdzie zapisać
     * (Rx). Licznik, nie zdarzenie — bo rośnie w przerwaniu.
     */
    virtual u32 xruns() const = 0;

    /** Faktyczna częstotliwość próbkowania; dzielnik zegara rzadko trafia idealnie. */
    virtual u32 actualSampleRate() const = 0;
};

}  // namespace hal
}  // namespace hydra
