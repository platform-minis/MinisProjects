#pragma once
/**
 * Hydra — sterta skryptu na statycznej puli (rozdz. 11).
 *
 * Lua z założenia alokuje dynamicznie: każdy napis, tabela i domknięcie to
 * osobne żądanie do `lua_Alloc`. Reguła Hydry mówi jednak, że po `App::begin()`
 * nie wolno sięgnąć po stertę systemową — inaczej fragmentacja wywołana przez
 * skrypt uderzyłaby w moduł sieciowy albo w bufory UI, i to po godzinach pracy,
 * w sposób nie do odtworzenia.
 *
 * Rozwiązaniem jest własny alokator nad jedną statyczną tablicą. Skrypt dostaje
 * pulę o rozmiarze ustalonym w chwili linkowania i nie może wyjść poza nią:
 * wyczerpanie puli daje błąd `not enough memory` w skrypcie, a nie brak pamięci
 * w systemie. Zużycie widać co do bajta przez `stats()`, więc „ile RAM-u zjada
 * ten skrypt" jest pytaniem z odpowiedzią, a nie przedmiotem domysłów.
 *
 * Konstrukcja: lista niejawna z tagami granicznymi. Każdy blok zna swój rozmiar
 * i rozmiar poprzednika, więc scalanie wolnych sąsiadów w obie strony jest
 * stałoczasowe. Wolne bloki trzymają wskaźniki listy we własnym ładunku, więc
 * narzut to osiem bajtów na blok niezależnie od tego, ile bloków żyje.
 *
 * Klasa nie zna Lua i nie zna Hydry — da się ją przetestować w izolacji
 * i taka jest testowana (`test_script_heap.cpp`, także pod sanitizerami).
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/script/Profile.hpp"

namespace hydra {
namespace script {

class Heap : NonCopyable {
public:
    /** Narzut na blok: rozmiar własny i rozmiar poprzednika. */
    static constexpr u32 kHeaderSize = 8;
    /** Wyrównanie ładunku. Ósemka wystarcza dla każdego typu, jaki tworzy Lua. */
    static constexpr u32 kAlign = 8;
    /** Najmniejszy blok — musi pomieścić nagłówek i dwa ogniwa listy wolnych. */
    static constexpr u32 kMinBlock = 16;

    struct Stats {
        u32 capacity    = 0;  ///< bajty puli dostępne dla bloków
        u32 used        = 0;  ///< bajty zajęte razem z nagłówkami
        u32 peak        = 0;  ///< maksimum `used` od inicjalizacji
        u32 liveBlocks  = 0;
        u32 freeBlocks  = 0;
        u32 largestFree = 0;  ///< największy spójny wolny blok (miara fragmentacji)
        u32 requests    = 0;  ///< przydziały i zmiany rozmiaru razem
        u32 failures    = 0;  ///< żądania odrzucone z braku miejsca
    };

    Heap() = default;

    /**
     * Przejmuje pulę. Bufor musi żyć dłużej niż sterta i mieć co najmniej
     * kMinBlock + kHeaderSize bajtów. Wołane raz, w fazie inicjalizacji.
     */
    Status init(void* pool, size_t bytes);

    /** Czy sterta jest gotowa do użycia. */
    bool ready() const { return base_ != nullptr; }

    /**
     * Zwraca całą pulę do stanu początkowego. Unieważnia wszystkie wskaźniki —
     * wołać wyłącznie po zamknięciu interpretera.
     */
    void reset();

    void* allocate(size_t bytes);
    void  release(void* ptr);
    /**
     * Zmiana rozmiaru w miejscu, gdy da się to zrobić bez przenoszenia.
     * `oldBytes` pochodzi od Lua i służy wyłącznie do kopiowania przy
     * przenoszeniu — alokator zna prawdziwy rozmiar bloku z nagłówka.
     */
    void* reallocate(void* ptr, size_t oldBytes, size_t newBytes);

    Stats stats() const;

    /**
     * Sprawdza spójność wszystkich bloków i listy wolnych.
     * Kosztowne — dla testów i dla komendy diagnostycznej, nie dla pętli.
     */
    bool validate() const;

private:
    /** Znacznik pustego ogniwa listy. Zero jest poprawnym offsetem, więc nie nadaje się. */
    static constexpr u32 kNil = 0xFFFFFFFFu;

    struct Block {
        u32 size;  ///< cały blok z nagłówkiem; bit 0 ustawiony = zajęty
        u32 prev;  ///< rozmiar bloku poprzedzającego fizycznie; 0 dla pierwszego
    };

    struct Links {
        u32 next;
        u32 prev;
    };

    static constexpr u32 kUsedBit = 1u;

    static u32   blockSize(const Block* b) { return b->size & ~kUsedBit; }
    static bool  isUsed(const Block* b) { return (b->size & kUsedBit) != 0; }
    static void  setSize(Block* b, u32 size, bool used) { b->size = size | (used ? kUsedBit : 0u); }

    Block* at(u32 offset) const;
    u32    offsetOf(const Block* b) const;
    Block* nextPhysical(const Block* b) const;
    Block* prevPhysical(const Block* b) const;
    Links* linksOf(Block* b) const;

    void   listInsert(Block* b);
    void   listRemove(Block* b);
    Block* findFit(u32 need);
    void   splitIfWorthwhile(Block* b, u32 need);

    u8*   base_  = nullptr;
    u32   size_  = 0;      ///< bajty puli objęte blokami
    u32   free_  = kNil;   ///< offset głowy listy wolnych bloków
    Stats stats_{};
};

/**
 * Rozmiar puli sterty skryptu w bajtach.
 *
 * Wartości wzięte z pomiaru, nie z sufitu: pusty interpreter z bibliotekami
 * base/string/math/table zajmuje ok. 12 KB, a typowy skrypt sterujący
 * z kilkunastoma funkcjami i tabelami mieści się poniżej 24 KB. Mały profil
 * zostawia więc zapas rzędu połowy, a duży — kilkukrotny.
 *
 * Podniesienie progu jest jedną flagą kompilacji; obniżenie poniżej ok. 16 KB
 * uniemożliwi otwarcie bibliotek standardowych.
 */
#ifndef HYDRA_SCRIPT_HEAP_BYTES
#  if HYDRA_SCRIPT_LARGE_PROFILE
#    define HYDRA_SCRIPT_HEAP_BYTES (96 * 1024)
#  else
#    define HYDRA_SCRIPT_HEAP_BYTES (36 * 1024)
#  endif
#endif

}  // namespace script
}  // namespace hydra
