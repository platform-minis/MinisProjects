#pragma once
/**
 * Hydra — wejście i wyjście potoku na maszynie deweloperskiej (etap 4).
 *
 * Karta dźwiękowa i okno hosta jako zwykłe elementy potoku. Ten sam graf, który
 * na ESP32 gra przez I2S, na celu `native` gra przez głośniki laptopa —
 * podmienia się jedno ujście, reszta zostaje.
 *
 * **Kolejka SDL, nie funkcja zwrotna.** SDL daje oba warianty; wybraliśmy
 * `SDL_QueueAudio`, i to nie jest wybór estetyczny. Funkcja zwrotna jest
 * wołana z **wątku audio SDL-a**, którego moduł `media` nie tworzy i nie
 * kontroluje. Trzeba by więc dołożyć bezzamkowy pierścień między nią a potokiem
 * i nagle moduł, który dotąd nie miał ani jednego wątku, ma wątek obcy w środku.
 * Kolejka pasuje do modelu push bez żadnej dodatkowej maszynerii: element
 * dokłada, SDL wybiera.
 *
 * **Opóźnienie trzeba ograniczyć jawnie.** Kolejka SDL nie ma górnego rozmiaru.
 * Potok czytający plik szybciej, niż karta go odtwarza — a tak jest zawsze —
 * wepchnąłby w nią całe nagranie w ułamku sekundy i użytkownik usłyszałby
 * pauzę reagującą z opóźnieniem kilkunastu sekund. Element trzyma więc zapas
 * między dwoma progami i przestaje dokładać powyżej górnego.
 *
 * Gdy build powstał bez SDL, wszystkie trzy elementy istnieją i linkują się,
 * a `onStart()` zwraca `Err::NotSupported`. To normalna ścieżka dla CI i sesji
 * ssh, nie atrapa na potrzeby testów.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/gfx/SdlDisplay.hpp"
#include "hydra/media/Pipeline.hpp"

namespace hydra {
namespace media {

/**
 * Ile bajtów wolno jeszcze dołożyć do kolejki karty dźwiękowej.
 *
 * Wydzielone z elementu, bo to jedyna arytmetyka, która decyduje o opóźnieniu —
 * i jedyna rzecz w tym pliku, którą da się sprawdzić bez karty dźwiękowej.
 *
 * @param queued    ile bajtów czeka w kolejce sterownika
 * @param highBytes próg, powyżej którego przestajemy dokładać
 * @param blockBytes rozmiar jednego bloku
 * @return ile pełnych bloków wolno wysłać (0 = kolejka wystarczająco pełna)
 */
u8 queueBudget(u32 queued, u32 highBytes, u32 blockBytes);

/** Bajty odpowiadające zadanemu czasowi dla danego formatu. */
u32 bytesForMillis(const MediaFormat& format, u16 milliseconds);

/**
 * Wyjście audio na kartę dźwiękową hosta.
 *
 * Format S16 i F32; reszta jest odrzucana przy `prepare()`, bo przeliczanie
 * próbek to zadanie filtru, a nie ujścia.
 */
class SdlAudioSink : public Element {
public:
    struct Config {
        /**
         * Ile dźwięku trzymać w zapasie. Poniżej dolnego progu dokładamy
         * agresywnie, powyżej górnego — wcale.
         *
         * Sto milisekund to kompromis: mniej oznacza przerwy przy każdym
         * większym obciążeniu systemu, więcej — zauważalne opóźnienie reakcji
         * na pauzę i zmianę głośności.
         */
        u16 targetLatencyMs = 100;
        /** Nazwa urządzenia; `nullptr` = domyślne systemu. */
        const char* device = nullptr;
    };

    SdlAudioSink() : Element("sdl-out") {}
    ~SdlAudioSink() override;

    void configure(const Config& cfg) { cfg_ = cfg; }

    u8 inputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    Status onPrepare(Pipeline& pipeline) override;
    Status onStart() override;
    void   onStop() override;
    void   process(u64 nowUs) override;

    u64 bytesQueued() const { return queued_; }
    /** Ile razy kolejka opadła do zera — czyli ile było słyszalnych przerw. */
    u32 underruns() const { return underruns_; }
    /** Ile bloków wstrzymano, żeby nie rozdąć opóźnienia. */
    u32 throttled() const { return throttled_; }

private:
    Config      cfg_{};
    MediaFormat format_{};
    Pipeline*   pipeline_ = nullptr;
    u32         device_ = 0;      ///< SDL_AudioDeviceID; 0 = zamknięte
    u32         highBytes_ = 0;
    u64         queued_ = 0;
    u32         underruns_ = 0;
    u32         throttled_ = 0;
    bool        primed_ = false;
};

/**
 * Wejście audio z mikrofonu hosta.
 *
 * Odpowiednik `I2sSource` na PC. Znaczniki czasu liczy z licznika ramek, tak
 * samo jak on i z tego samego powodu: karta ma własny kwarc, a branie czasu
 * z zegara systemowego dawałoby dryf.
 */
class SdlAudioSource : public Element {
public:
    struct Config {
        MediaFormat format = MediaFormat::audio(16000, SampleFormat::S16, 1);
        u16 framesPerBlock = 256;
        const char* device = nullptr;
    };

    SdlAudioSource() : Element("sdl-in") {}
    ~SdlAudioSource() override;

    Status configure(const Config& cfg);

    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    Status onStart() override;
    void   onStop() override;
    void   process(u64 nowUs) override;

    u64 framesCaptured() const { return frames_; }

private:
    Config      cfg_{};
    Pipeline*   pipeline_ = nullptr;
    BlockPool*  pool_ = nullptr;
    u32         device_ = 0;
    u64         frames_ = 0;
};

/**
 * Podgląd klatek w oknie.
 *
 * Opakowuje `gfx::SdlDisplay`, więc dziedziczy po nim wszystko: skalowanie
 * całkowitoliczbowe, barwy dla obrazu jednobitowego i obsługę zamknięcia okna.
 *
 * **Kopiuje klatkę.** `SdlDisplay` rysuje z bufora, który sam dostał przy
 * `begin()`, a klatka przychodzi w bloku z puli potoku. Zero-copy wymagałoby
 * przepinania bufora powierzchni pod każdą klatkę, czyli wystawienia w `gfx`
 * API, którego nikt poza tym miejscem by nie użył. Kopia 320×240 na PC jest
 * darmowa; gdyby kiedyś chodziło o 1080p na docelowym sprzęcie, ta decyzja
 * wymaga rewizji.
 */
class SdlVideoSink : public Element {
public:
    struct Config {
        const char* title = "Hydra — podgląd";
        u8   scale = 2;
        bool vsync = true;
    };

    SdlVideoSink() : Element("sdl-video") {}

    /**
     * Bufor okna dostarcza wołający — tak samo jak w `gfx::Framebuffer`
     * i z tego samego powodu (rozdz. 11: nic nie alokujemy po starcie).
     * Musi pomieścić klatkę w formacie negocjowanym przez potok.
     */
    void configure(ByteSpan windowBuffer, const Config& cfg) {
        buffer_ = windowBuffer;
        cfg_ = cfg;
    }

    u8 inputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    Status onPrepare(Pipeline& pipeline) override;
    Status onStart() override;
    void   onStop() override;
    void   process(u64 nowUs) override;

    /** `false` = użytkownik zamknął okno. Pętla główna ma wtedy skończyć. */
    bool open();

    gfx::SdlDisplay& display() { return display_; }
    u64 framesShown() const { return frames_; }

private:
    ByteSpan        buffer_{};
    Config          cfg_{};
    MediaFormat     format_{};
    Pipeline*       pipeline_ = nullptr;
    gfx::SdlDisplay display_;
    u64             frames_ = 0;
    bool            started_ = false;
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
