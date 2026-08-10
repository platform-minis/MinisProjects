#pragma once
/**
 * Hydra — dostarczanie skryptu przez sieć jako rozszerzenie MyCastle.
 *
 * Do tej pory skrypt wchodził na urządzenie dwiema drogami: jako stała
 * w pamięci programu albo wpisany z palca przez `script reload` w shellu.
 * Obie wymagają kabla. To rozszerzenie dokłada trzecią — przez to samo łącze,
 * którym idzie telemetria — i dopiero ono czyni ze skryptu rzecz, którą da się
 * zmienić w urządzeniu wiszącym pod sufitem.
 *
 * Protokół, temat `ext/script/req`:
 *
 *     {"op":"begin",  "params":{"size":1234,"sha256":"<64 znaki hex>","name":"=v7"}}
 *     {"op":"chunk",  "params":{"seq":0,"data":"<base64>"}}
 *     {"op":"chunk",  "params":{"seq":1,"data":"<base64>"}}
 *     {"op":"commit", "params":{}}
 *     {"op":"abort",  "params":{}}
 *     {"op":"status", "params":{}}
 *
 * Fragmentowanie nie jest wyborem: `HYDRA_MINIS_TX_BUFFER` to 512 bajtów,
 * a obraz waży kilka–kilkadziesiąt kilobajtów.
 *
 * **Trzy zabezpieczenia, każde na inny sposób awarii.**
 *
 * 1. *Skrót przed przełączeniem.* Obraz uszkodzony w drodze nigdy nie zostaje
 *    uruchomiony — `commit` liczy SHA-256 zebranych bajtów i porównuje
 *    z zapowiedzią z `begin`. To chroni przed zerwanym połączeniem.
 *
 * 2. *Okres próbny.* Skrót nie mówi nic o tym, czy skrypt działa — obraz może
 *    być przesłany bezbłędnie i wywracać się w `loop()`. Po podmianie moduł
 *    jest obserwowany przez `trialMs`; jeśli w tym czasie `loop()` zostanie
 *    wyłączona po serii błędów, magazyn wraca do poprzedniej wersji. To jest
 *    dokładnie ta sama myśl, co tryb próbny w OTA, i zaczyna być potrzebna
 *    w tej samej chwili: gdy obraz przychodzi z sieci, a nie z warsztatu.
 *
 * 3. *Odmowa w trakcie próby.* Dopóki nowa wersja się nie potwierdzi, kolejny
 *    transfer dostaje `busy`. Inaczej trzecia wersja wyparłaby jedyną, o której
 *    wiadomo, że wstaje.
 *
 * Błąd już w `setup()` albo odrzucenie obrazu przez silnik cofa zmianę
 * natychmiast, bez czekania na koniec okresu próbnego — nie ma czego obserwować.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT && HYDRA_ENABLE_MINIS

#include "hydra/core/IModule.hpp"
#include "hydra/core/Secret.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/minis/MinisModule.hpp"
#include "hydra/script/ImageFile.hpp"
#include "hydra/script/ImageStore.hpp"
#include "hydra/script/ScriptModule.hpp"

/**
 * Największy fragment przyjmowany w jednym `chunk`.
 *
 * Bufory na zdekodowane bajty i na tekst base64 są polami obiektu, więc ta
 * liczba to realny koszt w RAM-ie: fragment plus jedna trzecia na kodowanie.
 * Powyżej MTU łącza nie ma sensu — nadawca i tak nie prześle więcej naraz.
 */
#ifndef HYDRA_SCRIPT_CHUNK_MAX
#  define HYDRA_SCRIPT_CHUNK_MAX 512
#endif

namespace hydra {
namespace script {

class ScriptDelivery : public ModuleBase {
public:
    /** Typ rozszerzenia widziany przez serwer. */
    static constexpr const char* kExtType = "script";

    struct Config {
        minis::MinisIotModule* minis  = nullptr;
        ScriptModule*          script = nullptr;
        ImageStore*            store  = nullptr;

        /**
         * Trwałość między restartami. Bez niej urządzenie po zaniku zasilania
         * wraca do skryptu wkompilowanego w firmware — a wszystko, co wgrano
         * przez sieć, przepada.
         */
        ImageFile* persist = nullptr;

        /**
         * Klucz potwierdzający autentyczność obrazu.
         *
         * Pusty oznacza sprawdzanie **wyłącznie spójności**: skrót mówi, że
         * obraz nie uległ uszkodzeniu w drodze, ale nie że pochodzi od
         * właściciela urządzenia — napastnik policzy go równie dobrze.
         * Dopuszczalne w sieci zamkniętej, nie w Internecie.
         *
         * Z kluczem `begin` musi nieść pole `hmac`, a obraz bez podpisu jest
         * odrzucany przed transferem.
         */
        SecretString<64> hmacKey;

        /**
         * Jak długo obserwować nową wersję, zanim zostanie uznana za dobrą.
         * Musi z zapasem przekraczać `maxConsecutiveErrors` przebiegów modułu
         * skryptowego — inaczej wersja psująca się w `loop()` zdąży zostać
         * potwierdzona, zanim się wywali.
         */
        u32 trialMs = 10000;

        /** Co ile sprawdzać stan okresu próbnego. */
        u32 periodMs = 500;
        Prio priority = Prio::Low;
    };

    struct Stats {
        u32 begins    = 0;
        u32 unsigned_ = 0;   ///< odrzucone z powodu braku albo złego podpisu
        u32 chunks    = 0;
        u32 commits   = 0;   ///< obrazy przełączone i wczytane
        u32 rejects   = 0;   ///< odrzucone przy weryfikacji skrótu
        u32 rollbacks = 0;
        u32 confirms  = 0;
    };

    ScriptDelivery() : ModuleBase("script.ota") {}

    Status configure(const Config& cfg);

    /** Czy trwa obserwacja świeżo wgranej wersji. */
    bool  inTrial() const { return trialActive_; }
    Stats stats() const { return stats_; }

    /** Jeden krok obserwacji. Normalnie woła go task; publiczne dla testów. */
    void step(Millis now);

    /** Obsługa żądania z sieci. Publiczne dla testów; normalnie woła je Minis. */
    void onRequest(const char* id, const char* op, json::JsonView params);

protected:
    Status onInit() override;
    Status onStart() override;
    void   onStop() override;

private:
    void opBegin(const char* id, json::JsonView params);
    /** Czy obraz w slocie ma podpis zgodny z kluczem. */
    bool verifySignature() const;
    void opChunk(const char* id, json::JsonView params);
    void opCommit(const char* id);
    void opAbort(const char* id);
    void opStatus(const char* id);

    /** Wczytuje obraz do modułu. Zwraca false, gdy skrypt nie wstał. */
    bool applyImage(const ImageRef& image);
    void rollbackNow(const char* reason);

    /** Zapisuje aktywny obraz do pamięci trwałej, jeśli jest gdzie. */
    void persistActive();
    /** Odtwarza obraz z pamięci trwałej przy starcie. */
    void restorePersisted();

    void respondOk(const char* id, const char* dataJson = nullptr);
    void respondErr(const char* id, const char* code, const char* message);

    Config cfg_{};
    Task   task_{};
    Stats  stats_{};

    Millis trialUntilMs_ = 0;
    bool   trialActive_  = false;

    /**
     * Nazwa obrazu w komunikatach błędów. Pole obiektu, bo `ScriptModule`
     * zapamiętuje sam wskaźnik — napis musi przeżyć podmianę.
     */
    char imageName_[24] = {};

    /** Podpis zapowiedziany w `begin`; sprawdzany przy `commit`. */
    u8   wantHmac_[util::kSha256Size] = {};
    bool haveHmac_ = false;

    u8   chunk_[HYDRA_SCRIPT_CHUNK_MAX] = {};
    char b64_[((HYDRA_SCRIPT_CHUNK_MAX + 2) / 3) * 4 + 4] = {};
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT && HYDRA_ENABLE_MINIS
