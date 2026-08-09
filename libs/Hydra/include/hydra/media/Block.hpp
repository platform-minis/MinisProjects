#pragma once
/**
 * Hydra — blok danych i pula, z której się bierze.
 *
 * **Blok jest uchwytem, nie buforem.** Nosi wskaźnik, długość, znacznik czasu
 * i numer miejsca w puli — ale pamięci nie posiada. To jest ta decyzja, bez
 * której wideo nie ma sensu: klatka 1080p to 4 MB, a bramka przekazująca ją
 * dalej nie może jej kopiować. Przy dźwięku różnica jest mniejsza, ale ta sama
 * — 128 próbek stereo to 512 bajtów kopiowane 344 razy na sekundę na każdym
 * połączeniu w grafie.
 *
 * **Jeden właściciel.** Blok wędruje przez potok jak pałeczka w sztafecie:
 * kto go dostał, ten za niego odpowiada, dopóki nie odda go dalej albo nie
 * zwolni. Kopia uchwytu **nie** zwiększa licznika. Wyjątkiem jest rozgałęzienie
 * (element `Tee`), które musi jawnie wywołać `retain()` — i to jedyne miejsce
 * w module, gdzie licznik odwołań jest czymś więcej niż zerem albo jedynką.
 *
 * Model własności jest ten sam, co w v4l2 i w Zephyrowym API wideo, i z tego
 * samego powodu: element sprzętowy oddaje bufor DMA i dostaje go z powrotem
 * dopiero po przerwaniu, więc synchroniczne „przetwórz i zwróć" nie ma jak
 * zadziałać.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/core/Expected.hpp"
#include "hydra/media/MediaTypes.hpp"

namespace hydra {
namespace media {

/** Numer puli w potoku. */
using PoolId = u8;
constexpr PoolId kNoPool = 0xFF;
constexpr u16    kNoSlot = 0xFFFF;

enum BlockFlags : u8 {
    /** Klatka kluczowa; dla JPEG zawsze, dla strumieni z predykcją — nie. */
    kBlockKeyframe = 0x1,
    /** Ostatni blok strumienia. Ujście publikuje wtedy MediaEndOfStream. */
    kBlockEos = 0x2,
    /**
     * Nieciągłość: przed tym blokiem coś przepadło.
     *
     * Filtry ze stanem (biquad, resampler) muszą się wtedy zresetować, bo ich
     * pamięć odnosi się do próbek, których już nie ma. Bez tej flagi objawem
     * jest trzask po każdym zgubionym bloku.
     */
    kBlockDiscontinuity = 0x4,
    /**
     * Treść zapisana z pominięciem pamięci podręcznej (DMA).
     *
     * Na Cortex-A i ESP32-P4 procesor musi unieważnić linie cache, zanim
     * to przeczyta. Robi to warstwa `Pad` na granicy przekazania; element
     * o tym nie wie i wiedzieć nie musi.
     */
    kBlockCacheDirty = 0x8,
};

struct Block {
    u8* data     = nullptr;
    u32 capacity = 0;
    /** Ile bajtów jest naprawdę wypełnione. */
    u32 length   = 0;
    /** Znacznik czasu w mikrosekundach zegara potoku. */
    u64 pts      = 0;

    PoolId pool  = kNoPool;
    u16    slot  = kNoSlot;
    u8     flags = 0;

    bool valid() const { return data != nullptr && pool != kNoPool; }
    bool has(u8 flag) const { return (flags & flag) != 0; }
    void set(u8 flag) { flags = static_cast<u8>(flags | flag); }
};

/**
 * Czego element potrzebuje od pamięci.
 *
 * Deklaruje się to w `prepare()`, a pule powstają raz, przed startem.
 * Atrybuty nie są ozdobą: bufor dla DMA musi być w pamięci widzianej przez
 * kontroler i wyrównany do linii pamięci podręcznej, a bufor ścieżki czasu
 * rzeczywistego nie może wylądować w PSRAM, bo dostęp do niej ma nieprzewidywalne
 * opóźnienie.
 */
struct MemReq {
    u32  blockSize = 0;
    u8   count     = 0;
    bool dmaCapable   = false;
    bool internalOnly = false;
    u8   alignment    = 4;

    bool valid() const { return blockSize > 0 && count > 0; }
};

/**
 * Pula bloków o stałym rozmiarze.
 *
 * Pamięć dostarcza wołający — pula niczego nie alokuje, tak samo jak
 * `gfx::Framebuffer` i z tego samego powodu. Przydział i zwolnienie są O(1):
 * wolne miejsca leżą na liście jednokierunkowej zapisanej w tablicy indeksów,
 * więc nie ma przeszukiwania i nie ma fragmentacji.
 */
class BlockPool : NonCopyable {
public:
    static constexpr u16 kMaxSlots = 64;

    BlockPool() = default;

    /**
     * Podpina pamięć i dzieli ją na bloki.
     *
     * `storage` musi pomieścić `count × blockSize` po wyrównaniu. Zwraca
     * `OutOfRange`, gdy jest za mało, i `BadArgument` przy zerowych rozmiarach
     * — po cichu mniejsza pula oznaczałaby underrun w losowym momencie.
     */
    Status attach(PoolId id, ByteSpan storage, u32 blockSize, u16 count,
                  u8 alignment = 4);

    /** Blok z puli albo pusty uchwyt, gdy wszystkie są w obiegu. */
    Block acquire();

    /** Oddaje blok. Zwolnienie cudzego albo już zwolnionego jest ignorowane. */
    void release(Block& block);

    /** Dodatkowe odwołanie — wyłącznie dla rozgałęzień. */
    void retain(const Block& block);

    PoolId id() const { return id_; }
    u16    capacityBlocks() const { return count_; }
    u16    available() const { return free_; }
    /** Najmniejsza liczba wolnych bloków od startu — pokazuje zapas. */
    u16    lowWater() const { return lowWater_; }
    u32    blockSize() const { return blockSize_; }
    /** Ile razy `acquire()` wróciło z pustymi rękami. */
    u32    exhausted() const { return exhausted_; }

private:
    u8*    base_ = nullptr;
    u32    blockSize_ = 0;
    u16    count_ = 0;
    u16    free_ = 0;
    u16    lowWater_ = 0;
    PoolId id_ = kNoPool;
    u32    exhausted_ = 0;

    /** Lista wolnych miejsc; `kNoSlot` kończy. */
    u16 nextFree_[kMaxSlots] = {};
    u16 head_ = kNoSlot;
    u8  refs_[kMaxSlots] = {};
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
