#pragma once
/**
 * Hydra — pad: punkt wejścia albo wyjścia elementu.
 *
 * Pad wejściowy trzyma kolejkę bloków czekających na przetworzenie; pad
 * wyjściowy jest tylko wskaźnikiem na pad wejściowy sąsiada. Kolejka jest po
 * stronie odbiorcy, bo to odbiorca wie, ile jest w stanie przyjąć.
 *
 * Ten sam mechanizm obsługuje połączenie wewnątrz domeny i między domenami.
 * Wewnątrz jest to zwykły bufor cykliczny; między domenami — ten sam bufor,
 * ale z jednym producentem i jednym konsumentem w różnych wątkach, czyli
 * kolejka SPSC. Poprawność bierze się z tego, że indeksy są atomowe i że
 * producent rusza tylko `head`, a konsument tylko `tail`. Nie ma tu zamka
 * i nie ma go być: zamek na ścieżce audio to priorytetowa inwersja czekająca
 * na okazję.
 *
 * **Polityka przepełnienia jest jawna.** Kolejka pełna to nie wyjątek, tylko
 * stan, który w potoku czasu rzeczywistego prędzej czy później nastąpi.
 * Trzeba wtedy zdecydować, co jest mniej złe — i ta decyzja należy do miejsca
 * w grafie, nie do frameworka:
 *
 *   • `DropOldest` — podgląd z kamery: świeża klatka jest warta więcej niż
 *     stara, którą i tak nikt nie zobaczy;
 *   • `DropNewest` — zapis do pliku: kolejność ma znaczenie, dziura na końcu
 *     jest lepsza niż dziura w środku;
 *   • `Reject`     — źródło ma wstrzymać produkcję zamiast cokolwiek gubić.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include <atomic>

#include "hydra/media/Block.hpp"

/** Głębokość kolejki pojedynczego padu wejściowego. */
#ifndef HYDRA_MEDIA_PAD_DEPTH
#  define HYDRA_MEDIA_PAD_DEPTH 8
#endif

namespace hydra {
namespace media {

enum class OverflowPolicy : u8 { DropOldest = 0, DropNewest, Reject };

class Pad {
public:
    /** Ustala format i politykę. Wołane w fazie negocjacji, przed startem. */
    void configure(const MediaFormat& format, OverflowPolicy policy = OverflowPolicy::DropOldest) {
        format_ = format;
        policy_ = policy;
    }

    const MediaFormat& format() const { return format_; }
    OverflowPolicy     policy() const { return policy_; }

    /**
     * Wkłada blok do kolejki.
     *
     * Zwraca blok **do zwolnienia** w `evicted`, gdy polityka kazała coś
     * wyrzucić — pad nie zna puli i nie może zwolnić go sam. Zwrócenie
     * `false` oznacza, że wołający zatrzymuje własność swojego bloku.
     */
    bool push(const Block& block, Block& evicted);

    /** Wyjmuje najstarszy blok. `false` = kolejka pusta. */
    bool pop(Block& out);

    /** Podgląd bez wyjmowania — dla elementów sprawdzających znacznik czasu. */
    bool peek(Block& out) const;

    u8   depth() const;
    bool empty() const { return depth() == 0; }
    bool full() const { return depth() >= HYDRA_MEDIA_PAD_DEPTH; }

    /** Największe zajęcie od startu — pokazuje, czy głębokość jest dobrana. */
    u8  highWater() const { return highWater_; }
    u32 dropped() const { return dropped_; }

    /** Opróżnia kolejkę do `out`, po jednym bloku, żeby dało się je zwolnić. */
    bool drain(Block& out) { return pop(out); }

private:
    MediaFormat    format_{};
    OverflowPolicy policy_ = OverflowPolicy::DropOldest;

    Block                 ring_[HYDRA_MEDIA_PAD_DEPTH];
    std::atomic<u8>       head_{0};   ///< rusza wyłącznie producent
    std::atomic<u8>       tail_{0};   ///< rusza wyłącznie konsument
    u8                    highWater_ = 0;
    u32                   dropped_ = 0;
};

/**
 * Wyjście elementu — wskaźnik na pad wejściowy sąsiada.
 *
 * Niepodłączone wyjście nie jest błędem: element diagnostyczny bywa wpięty
 * tylko czasem, a potok bez tej gałęzi ma działać. Bloki wysyłane w próżnię
 * są liczone, żeby dało się to zauważyć.
 */
class OutputPad {
public:
    void connect(Pad& peer) { peer_ = &peer; }
    bool connected() const { return peer_ != nullptr; }
    Pad* peer() const { return peer_; }

    const MediaFormat& format() const { return format_; }
    void setFormat(const MediaFormat& format) { format_ = format; }

    u32 sent() const { return sent_; }
    u32 discarded() const { return discarded_; }

    void countSent() { ++sent_; }
    void countDiscarded() { ++discarded_; }

private:
    Pad*        peer_ = nullptr;
    MediaFormat format_{};
    u32         sent_ = 0;
    u32         discarded_ = 0;
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
