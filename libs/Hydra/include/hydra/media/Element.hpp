#pragma once
/**
 * Hydra — element potoku.
 *
 * Element ma pady, format i jedną metodę roboczą. Metoda **nie musi** wyprodukować
 * wyjścia w tym samym wywołaniu, w którym dostała wejście — i to jest cała
 * różnica między tym API a naiwnym `process(Block&) → Block`.
 *
 * Powód jest sprzętowy. Enkoder H.264 w ESP32-P4, kontroler I2S z DMA
 * i kamera na MIPI-CSI działają tak samo: przyjmują bufor, oddają go po
 * przerwaniu kilkanaście milisekund później. Przy interfejsie synchronicznym
 * jedynym sposobem na wpięcie ich byłoby zablokowanie się na semaforze —
 * czyli zamiana akceleratora na najdroższy możliwy sposób czekania.
 *
 * Element synchroniczny (wzmocnienie, mieszanie) jest przypadkiem szczególnym:
 * bierze blok z wejścia i oddaje na wyjście w tym samym `process()`. Element
 * sprzętowy oddaje go z `onComplete()`, wołanego z obsługi przerwania. Ten sam
 * interfejs, dwie implementacje — odwrotnie się nie da.
 *
 * **Cykl życia** odpowiada cyklowi modułów Hydry:
 *
 *     negotiate()  ← jaki format wychodzi, skoro wchodzi taki
 *     memoryRequest() ← ile bloków i jakich
 *     onPrepare()  ← pule są już gotowe, można zapamiętać wskaźniki
 *     onStart()    ← uruchomienie sprzętu
 *     process()    ← w kółko
 *     onStop()
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/media/Block.hpp"
#include "hydra/media/Pad.hpp"

/** Ile padów wejściowych i wyjściowych może mieć element. */
#ifndef HYDRA_MEDIA_MAX_PADS
#  define HYDRA_MEDIA_MAX_PADS 4
#endif

namespace hydra {
namespace media {

class Pipeline;

class Element : NonCopyable {
public:
    explicit Element(const char* name) : name_(name) {}
    virtual ~Element() = default;

    const char* name() const { return name_; }

    virtual u8 inputCount() const  { return 0; }
    virtual u8 outputCount() const { return 0; }

    Pad&       input(u8 index) { return inputs_[index]; }
    OutputPad& output(u8 index) { return outputs_[index]; }

    /**
     * Jaki format wychodzi tym padem, skoro wejściem wchodzi `in`.
     *
     * Dla źródeł `in` jest nieważny. Zwrócenie `NotSupported` przerywa
     * przygotowanie potoku z komunikatem wskazującym element — a nie
     * z ciszą na wyjściu, którą trzeba potem rozplątywać.
     */
    virtual Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) {
        HYDRA_UNUSED(outPad);
        return in;
    }

    /** Czego element potrzebuje od pamięci dla swojego padu wyjściowego. */
    virtual MemReq memoryRequest(u8 outPad) const {
        HYDRA_UNUSED(outPad);
        return {};
    }

    virtual Status onPrepare(Pipeline& pipeline) { HYDRA_UNUSED(pipeline); return ok(); }
    virtual Status onStart() { return ok(); }
    virtual void   onStop() {}

    /** Jeden krok pracy. Bez blokowania — potok ma stały budżet czasu. */
    virtual void process(u64 nowUs) = 0;

    /** Indeks w potoku; ustawia go `Pipeline::add()`. Używany w zdarzeniach. */
    u8   index() const { return index_; }
    void setIndex(u8 index) { index_ = index; }

protected:
    /**
     * Wysyła blok wyjściem.
     *
     * Zwraca `false`, gdy blok nie został przyjęty i **nadal należy do
     * wołającego** — trzeba go wtedy zwolnić albo spróbować później.
     * `evicted` dostaje blok wyrzucony przez politykę kolejki; też trzeba go
     * zwolnić. Rozdzielenie tych dwóch przypadków jest tu celowe: pomylenie
     * ich oznacza albo wyciek bloków, albo zwolnienie cudzego.
     */
    bool emit(u8 outPad, const Block& block, Block& evicted);

    /** Bierze blok z wejścia. `false` = nie ma nic do roboty. */
    bool take(u8 inPad, Block& out) { return inputs_[inPad].pop(out); }

    Pad       inputs_[HYDRA_MEDIA_MAX_PADS];
    OutputPad outputs_[HYDRA_MEDIA_MAX_PADS];

private:
    const char* name_;
    u8          index_ = 0;
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
