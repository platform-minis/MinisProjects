#pragma once
/**
 * Hydra — strumień multimedialny przez sieć (etap 3).
 *
 * Po TCP, przez `net::IClient` — czyli przez to samo gniazdo, na którym stoi
 * MQTT, a po opakowaniu w `WebSocketClient` także przez WebSocket.
 *
 * **Dlaczego TCP, a nie UDP.** Dla dźwięku na żywo UDP byłby lepszy: strata
 * pakietu kosztuje jedno kliknięcie, a retransmisja — kaskadę opóźnień.
 * Ale w Hydrze warstwa transportowa ma dziś tylko `IClient`, czyli strumień
 * TCP, i dołożenie UDP oznaczałoby nowy interfejs HAL plus backendy na trzech
 * platformach. To jest osobna decyzja, nie efekt uboczny etapu 3. Dopóki jej
 * nie ma, ten element nadaje się do zapisu zdalnego, podglądu i przesyłania
 * nagrań — nie do rozmowy dwukierunkowej.
 *
 * **Ramkowanie.** TCP daje strumień bajtów bez granic, a odbiorca musi wiedzieć,
 * gdzie kończy się blok i jaki ma znacznik czasu. Nagłówek jest więc stały
 * i minimalny — szesnaście bajtów:
 *
 *     0   'H' 'M' (magic)
 *     2   wersja | flagi
 *     4   długość ładunku (u32, młodszy bajt pierwszy)
 *     8   znacznik czasu w µs (u64)
 *
 * Bez magii nie da się odróżnić utraty synchronizacji od poprawnych danych:
 * odbiornik, który wszedł w strumień w połowie bloku, czytałby długość ze
 * środka próbek i czekał na dwa gigabajty. Z magią wraca do siebie po jednym
 * bloku.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA && HYDRA_ENABLE_NET

#include "hydra/media/Pipeline.hpp"
#include "hydra/net/ITransport.hpp"

namespace hydra {
namespace media {

/** Nagłówek bloku na łączu strumieniowym. */
constexpr size_t kNetHeaderSize = 16;
constexpr u8     kNetMagic0 = 'H';
constexpr u8     kNetMagic1 = 'M';
constexpr u8     kNetVersion = 1;

/** Zapisuje nagłówek; zwraca liczbę bajtów albo 0, gdy bufor za mały. */
size_t buildNetHeader(ByteSpan out, u32 length, u64 pts, u8 flags);
/** Rozbiera nagłówek. `false` = brak magii albo zła wersja. */
bool   parseNetHeader(CByteSpan in, u32& length, u64& pts, u8& flags);

/**
 * Wysyłanie bloków do zdalnego odbiorcy.
 *
 * Połączenie nawiązuje i utrzymuje wołający — element tylko pisze. Rozdzielenie
 * jest celowe: to samo gniazdo bywa dzielone z innym protokołem, a ponowne
 * łączenie ma własny backoff w `net::ConnectionManager` i nie ma powodu
 * powtarzać go tutaj.
 */
class NetSink : public Element {
public:
    struct Config {
        /**
         * Co zrobić, gdy gniazdo nie przyjmuje danych.
         *
         * Zapis do zapchanego TCP potrafi zwrócić mniej, niż podano — dosyłanie
         * reszty w pętli zablokowałoby domenę na czas, którego nikt nie
         * ogranicza. Blok jest wtedy porzucany w całości, a nie w połowie:
         * pół bloku po drugiej stronie to trzask, cały brakujący — przerwa.
         */
        bool dropOnPartialWrite = true;
        u8   blocksPerStep = 2;
    };

    explicit NetSink(net::IClient& client) : Element("net-out"), client_(client) {}

    void configure(const Config& cfg) { cfg_ = cfg; }

    u8 inputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

    u64 bytesSent() const { return sent_; }
    u32 dropped() const { return dropped_; }

private:
    net::IClient& client_;
    Config     cfg_{};
    Pipeline*  pipeline_ = nullptr;
    u64        sent_ = 0;
    u32        dropped_ = 0;
};

/**
 * Odbiór bloków ze zdalnego nadawcy.
 *
 * Format podaje konfiguracja, a nie strumień: nagłówek bloku niesie długość
 * i czas, ale nie częstotliwość próbkowania. Wysyłanie jej w każdym bloku
 * byłoby szesnastoma bajtami na okrągło; wysyłanie raz, na początku, wymagałoby
 * stanu, który po zerwaniu połączenia trzeba by odtwarzać. Obie strony
 * konfiguruje się więc tak samo — i rozjazd widać od razu jako dźwięk o złej
 * wysokości, a nie jako cichy błąd.
 */
class NetSource : public Element {
public:
    struct Config {
        MediaFormat format{};
        u16 framesPerBlock = 256;
        u8  blocksPerStep = 2;
    };

    explicit NetSource(net::IClient& client) : Element("net-in"), client_(client) {}

    Status configure(const Config& cfg);

    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

    u64 bytesReceived() const { return received_; }
    /** Ile razy odbiornik gubił się w strumieniu i szukał magii od nowa. */
    u32 resyncs() const { return resyncs_; }

private:
    /** Szuka magii bajt po bajcie. Zwraca `true`, gdy nagłówek jest w buforze. */
    bool findHeader();

    net::IClient& client_;
    Config     cfg_{};
    Pipeline*  pipeline_ = nullptr;
    BlockPool* pool_ = nullptr;

    u8     header_[kNetHeaderSize] = {};
    size_t headerLen_ = 0;
    u64    received_ = 0;
    u32    resyncs_ = 0;
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA && HYDRA_ENABLE_NET
