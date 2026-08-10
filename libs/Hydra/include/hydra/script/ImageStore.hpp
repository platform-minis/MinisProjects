#pragma once
/**
 * Hydra — magazyn obrazów skryptu przychodzących z sieci.
 *
 * Powtarza dyscyplinę `IFirmwareStore` (rozdz. 7.2) na mniejszej skali: obraz
 * przyjmuje się fragmentami, weryfikuje **przed** przełączeniem, a samo
 * przełączenie jest osobną, jawną decyzją. Różnica jest taka, że tutaj nie ma
 * partycji ani restartu — obraz to kilka kilobajtów, które muszą zostać
 * w RAM-ie, bo `ScriptModule` trzyma na nie wskaźnik przez cały czas pracy.
 *
 * **Dwa sloty, nie jeden.** Po podmianie stary obraz musi przeżyć, bo nowy
 * jest jeszcze niesprawdzony. Slot zwalnia się dopiero wtedy, gdy okres próbny
 * potwierdzi nową wersję — do tej chwili oba są zajęte i kolejny transfer
 * dostaje `Err::Busy`. To nie jest ograniczenie do obejścia: przyjmowanie
 * trzeciej wersji, gdy druga jeszcze nie dowiodła, że wstaje, oznaczałoby
 * utratę jedynego obrazu, o którym wiadomo, że działa.
 *
 * **Obraz wbudowany** — ten z `ScriptModule::Config::source`, leżący w pamięci
 * programu — nie zajmuje slotu i jest naturalnym celem pierwszego wycofania.
 *
 * Magazyn nie zna ani sieci, ani modułu skryptowego: dostaje bajty i oddaje
 * zweryfikowany obraz. Dzięki temu testuje się go bez łącza i bez interpretera.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/util/Sha256.hpp"

namespace hydra {
namespace script {

/** Opis obrazu — gdzie leży, ile waży i czym się legitymuje. */
struct ImageRef {
    const u8* data  = nullptr;
    size_t    bytes = 0;
    u8        sha[util::kSha256Size] = {};
    /** Slot magazynu albo -1 dla obrazu wbudowanego w pamięć programu. */
    i8        slot  = -1;

    bool valid() const { return data != nullptr; }
    CByteSpan span() const { return CByteSpan{data, bytes}; }
};

class ImageStore : NonCopyable {
public:
    struct Config {
        /**
         * Dwa bufory na obrazy z sieci. Muszą być równe co do rozmiaru
         * i przeżyć magazyn — zwykle są tablicami statycznymi.
         */
        ByteSpan slotA{};
        ByteSpan slotB{};
    };

    struct Stats {
        u32 transfers  = 0;  ///< rozpoczęte transfery
        u32 commits    = 0;  ///< obrazy zweryfikowane i przełączone
        u32 rejects    = 0;  ///< odrzucone przy weryfikacji skrótu
        u32 rollbacks  = 0;
        u32 aborts     = 0;
    };

    Status configure(const Config& cfg);

    /** Pojemność jednego slotu — największy obraz, jaki magazyn przyjmie. */
    size_t capacity() const { return slots_[0].size(); }

    // --- odbiór ------------------------------------------------------------

    /**
     * Otwiera transfer o znanym rozmiarze i skrócie.
     *
     * `Err::OutOfRange`, gdy obraz nie mieści się w slocie — lepiej wiedzieć
     * przed przesłaniem niż po. `Err::Busy`, gdy oba sloty trzyma para
     * aktywny/poprzedni, czyli w trakcie okresu próbnego.
     */
    Status beginTransfer(size_t totalBytes, const u8 expectedSha[util::kSha256Size]);

    /**
     * Dokłada kolejny fragment. Fragmenty muszą przychodzić po kolei — `seq`
     * jest numerem porządkowym liczonym od zera i służy do wykrycia zgubionej
     * albo powtórzonej paczki, a nie do składania ich w dowolnej kolejności.
     */
    Status appendChunk(u32 seq, CByteSpan data);

    /**
     * Domyka transfer: liczy skrót zebranych bajtów i porównuje z zapowiedzią.
     * Obraz **nie** staje się jeszcze aktywny — to robi `activateStaged()`.
     */
    Status verifyStaged();

    /**
     * Przełącza na zweryfikowany obraz, zachowując poprzedni do wycofania.
     * Wolno wołać wyłącznie po `verifyStaged()`.
     */
    Result<ImageRef> activateStaged();

    /** Porzuca transfer i zwalnia slot. */
    void abortTransfer();

    // --- okres próbny ------------------------------------------------------

    /** Czy istnieje obraz, do którego da się wrócić. */
    bool canRollback() const { return previous_.valid(); }

    /** Wraca do poprzedniego obrazu i zwalnia slot nieudanej wersji. */
    Result<ImageRef> rollback();

    /** Potwierdza aktywny obraz: poprzedni przestaje być potrzebny. */
    void confirm();

    // --- stan --------------------------------------------------------------

    /**
     * Ustawia obraz wbudowany jako aktywny, bez zajmowania slotu.
     * Wołane raz, przy starcie, żeby pierwsze wycofanie miało cel.
     */
    void adoptBuiltin(CByteSpan image);

    const ImageRef& active() const { return active_; }
    const ImageRef& previous() const { return previous_; }

    bool   receiving() const { return staging_ >= 0; }
    size_t received() const { return received_; }
    size_t expected() const { return expected_; }
    /** Czy zebrany obraz przeszedł weryfikację i czeka na przełączenie. */
    bool   staged() const { return verified_; }

    Stats stats() const { return stats_; }

private:
    /** Slot nienależący ani do aktywnego, ani do poprzedniego obrazu. */
    i8 pickFreeSlot() const;

    ByteSpan slots_[2]{};

    ImageRef active_{};
    ImageRef previous_{};

    i8     staging_  = -1;
    size_t expected_ = 0;
    size_t received_ = 0;
    u32    nextSeq_  = 0;
    bool   verified_ = false;
    u8     wantSha_[util::kSha256Size] = {};

    Stats stats_{};
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
