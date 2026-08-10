#pragma once
/**
 * @file ProgramReceiver.hpp
 * @brief Składanie programu z kawałków przychodzących z sieci.
 *
 * Moduł `.wasm` ma kilka–kilkadziesiąt kilobajtów, a jedna wiadomość MQTT
 * na urządzeniu z 320 KB RAM to zwykle kilkaset bajtów. Program przychodzi
 * więc w kawałkach i ktoś musi je złożyć — z pilnowaniem, że przyszły
 * wszystkie, we właściwej kolejności i nienaruszone.
 *
 * ## Transport nie jest tu wymieniony ani razu
 *
 * Klasa nie zna MQTT, WebSocketu ani nośnika. Dostaje `(offset, bajty)`
 * i tyle. Dzięki temu ta sama logika obsługuje wgranie z brokera, z shella
 * i z karty pamięci — a testy sprawdzają ją bez ani jednego gniazda.
 * Adapter transportu jest zadaniem projektu i ma kilkanaście linijek.
 *
 * ## Bufor należy do wołającego
 *
 * Odbiornik nie alokuje. Bufor podaje projekt i to on decyduje, jak duży
 * program dopuszcza — a przy okazji rozwiązuje to wymóg z `loadModule()`,
 * że bajty muszą przeżyć moduł: bufor projektu jest statyczny, więc żyje.
 *
 * ## Użycie
 *
 *     static u8 gImage[32 * 1024];
 *     ProgramReceiver receiver;
 *     receiver.begin(gImage);
 *
 *     // z obsługi wiadomości:
 *     HYDRA_CHECK(receiver.expect(totalBytes, crc));
 *     HYDRA_CHECK(receiver.chunk(offset, payload));
 *     if (receiver.complete()) {
 *         HYDRA_CHECK(script.loadModule(receiver.image()));
 *         receiver.reset();
 *     }
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace script {

class ProgramReceiver {
public:
    struct Stats {
        u32 chunks     = 0;   ///< przyjęte kawałki
        u32 duplicates = 0;   ///< kawałki przysłane powtórnie
        u32 rejected   = 0;   ///< odrzucone: zły zakres albo brak zapowiedzi
    };

    /** Podpina bufor. Musi przeżyć program, który z niego powstanie. */
    Status begin(ByteSpan buffer);

    /**
     * Zapowiada transfer: ile bajtów i jaka suma kontrolna całości.
     *
     * Zapowiedź jest obowiązkowa, bo bez znanego rozmiaru nie da się
     * stwierdzić, że transfer się skończył — a program złożony w połowie
     * wygląda jak poprawny, dopóki nie sięgnie się do brakującej części.
     *
     * Powtórna zapowiedź o innych parametrach zaczyna transfer od nowa.
     * Tak wygląda ponowienie po zerwaniu połączenia i nie jest to błąd.
     */
    Status expect(u32 totalBytes, u16 crc16);

    /** Przyjmuje kawałek. `Err::OutOfRange`, gdy wychodzi poza zapowiedziany rozmiar. */
    Status chunk(u32 offset, CByteSpan data);

    /** Czy przyszły wszystkie bajty **i** suma kontrolna się zgadza. */
    bool complete() const;

    /**
     * Złożony program.
     *
     * Pusty, dopóki `complete()` nie zwróci `true` — oddanie niekompletnego
     * obrazu byłoby zaproszeniem do wczytania go „na próbę".
     */
    CByteSpan image() const;

    /** Ile bajtów już przyszło. */
    u32 received() const { return received_; }
    /** Zapowiedziany rozmiar; 0 przed zapowiedzią. */
    u32 expected() const { return expected_; }

    /** Suma kontrolna złożonego obrazu — do porównania z zapowiedzianą. */
    u16 checksum() const;

    Stats stats() const { return stats_; }

    /**
     * Przyjmuje kawałek w postaci ramki transportowej.
     *
     * Jedno wywołanie dla całej wiadomości z sieci: rozbiera nagłówek, zgłasza
     * zapowiedź i przyjmuje dane. Adapter transportu sprowadza się dzięki temu
     * do przekazania ładunku — bez rozbierania protokołu w każdym projekcie.
     *
     * `Err::Protocol`, gdy znacznika nie ma albo ramka jest krótsza od
     * nagłówka. Cudzy ruch na temacie jest wtedy odrzucany, a nie brany za
     * uszkodzony program.
     */
    Status feed(CByteSpan frame);

    /** Porzuca transfer i zwalnia znaczniki. Bufor zostaje podpięty. */
    void reset();

private:
    /** Ile fragmentów śledzimy — patrz uwaga przy `covered_` w implementacji. */
    static constexpr u8 kMaxRanges = 16;

    struct Range {
        u32 from = 0;
        u32 to   = 0;   ///< wyłącznie
    };

    ByteSpan buffer_{};
    u32      expected_ = 0;
    u16      crc_      = 0;
    u32      received_ = 0;

    Range covered_[kMaxRanges]{};
    u8    rangeCount_ = 0;
    Stats stats_{};
};

/**
 * Ramka transportowa programu.
 *
 * `ProgramReceiver` sam w sobie nie zna formatu — bierze `(offset, bajty)`.
 * Ale te trzy liczby muszą jakoś przejechać przez sieć, a bez ustalonego
 * kształtu każdy projekt wymyśliłby własny i moduł zbudowany dla jednego
 * nie dałby się wgrać drugim.
 *
 * Nagłówek ma **dwanaście bajtów**, wszystkie liczby grubszym końcem naprzód
 * (tak jak reszta protokołów sieciowych Hydry):
 *
 * ```
 *   0..3   'H' 'W' 'A' 'M'     znacznik — odsiewa cudzy ruch na temacie
 *   4..7   total   (u32)       pełny rozmiar programu
 *   8..9   crc16   (u16)       suma kontrolna całości
 *  10..11  offset  (u16)       przesunięcie tego kawałka
 *  12..    dane
 * ```
 *
 * Przesunięcie jest szesnastobitowe, więc jeden transfer obejmuje do 64 KB.
 * Program większy nie mieści się i tak w pamięci urządzeń, do których to
 * jest pisane; szerokie pole kosztowałoby dwa bajty w każdym pakiecie.
 *
 * Zapowiedź jedzie **w każdym kawałku**, a nie w osobnej wiadomości. Wygląda
 * to na marnotrawstwo dwunastu bajtów, ale znosi problem kolejności: kawałek,
 * który dotarł pierwszy, niesie komplet informacji potrzebnych do przyjęcia
 * reszty. Osobna zapowiedź, która zginęła albo przyszła po danych, wymagałaby
 * buforowania „sierot" — czyli drugiego mechanizmu obok tego, który już jest.
 */
constexpr size_t kProgramFrameHeader = 12;

/** Największy program, jaki obejmie jeden transfer. */
constexpr u32 kProgramMaxBytes = 65535;

/** CRC-16/CCITT-FALSE — ten sam wielomian, co w ramkowaniu `minis`. */
u16 crc16Ccitt(CByteSpan data);

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
