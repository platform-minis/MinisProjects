#pragma once
/**
 * Hydra — SHA-256 i HMAC-SHA256.
 *
 * Implementacja własna, nie zapożyczona z biblioteki kryptograficznej,
 * z jednego powodu: aktualizacja oprogramowania musi dać się zweryfikować
 * na każdej platformie docelowej, także tam, gdzie mbedTLS nie wchodzi
 * do budżetu pamięci. Sto linii kodu i 104 bajty stanu to cena, którą
 * warto zapłacić za brak zależności w ścieżce, od której zależy, czy
 * urządzenie po aktualizacji w ogóle wstanie.
 *
 * Poprawność sprawdzana jest wektorami testowymi z FIPS 180-4 i RFC 4231.
 */

#include "hydra/core/Types.hpp"

namespace hydra {
namespace util {

/** Długość skrótu w bajtach. */
constexpr size_t kSha256Size = 32;
/** Rozmiar bloku przetwarzania — potrzebny przy HMAC. */
constexpr size_t kSha256Block = 64;

class Sha256 {
public:
    Sha256() { reset(); }

    void reset();
    /** Dokłada kolejny fragment danych. Wolno wołać dowolnie wiele razy. */
    void update(CByteSpan data);
    /** Domyka obliczenia i zapisuje skrót. Po tym wymagany jest reset(). */
    void finish(u8 out[kSha256Size]);

    /** Skrót jednym wywołaniem. */
    static void hash(CByteSpan data, u8 out[kSha256Size]);

    /** Zamienia skrót na zapis szesnastkowy z terminatorem (65 znaków). */
    static void toHex(const u8 digest[kSha256Size], char* out, size_t capacity);
    /** Odczytuje skrót z zapisu szesnastkowego. */
    static bool fromHex(const char* text, u8 out[kSha256Size]);

    /**
     * Porównanie odporne na pomiar czasu.
     *
     * Zwykłe memcmp kończy pracę na pierwszej różnicy, więc czas wykonania
     * zdradza, ile początkowych bajtów się zgadza — to wystarcza, by odgadnąć
     * poprawny skrót bajt po bajcie. Tutaj czas nie zależy od danych.
     */
    static bool equal(const u8 a[kSha256Size], const u8 b[kSha256Size]);

private:
    void processBlock(const u8* block);

    u32    state_[8]              = {};
    u8     buffer_[kSha256Block]  = {};
    size_t bufferLength_          = 0;
    u64    totalBits_             = 0;
};

/**
 * HMAC-SHA256 — potwierdzenie autentyczności kluczem współdzielonym.
 *
 * Sam skrót mówi wyłącznie o tym, że plik nie uległ uszkodzeniu w drodze;
 * nie chroni przed podstawieniem cudzego obrazu, bo napastnik policzy skrót
 * równie dobrze. HMAC wymaga znajomości klucza, więc podmiana wymaga jego
 * zdobycia.
 *
 * Ograniczenie: klucz jest ten sam po obu stronach, więc każde urządzenie
 * w parku potrafi podpisać aktualizację dla pozostałych. Podpis asymetryczny
 * usuwa ten problem, ale wymaga biblioteki kryptograficznej — patrz
 * ISignatureVerifier.
 */
class HmacSha256 {
public:
    void begin(CByteSpan key);
    void update(CByteSpan data);
    void finish(u8 out[kSha256Size]);

    static void compute(CByteSpan key, CByteSpan data, u8 out[kSha256Size]);

private:
    Sha256 inner_;
    u8     outerKey_[kSha256Block] = {};
};

}  // namespace util
}  // namespace hydra
