#pragma once
/**
 * Hydra — źródło i ujście plikowe potoku (etap 3).
 *
 * Dwa elementy i jedna decyzja, która przesądza o obu: **plik jest wolny
 * i nieprzewidywalny**. Karta SD potrafi zamilknąć na sto milisekund, gdy
 * kontroler przenosi blok albo kasuje sektor. Element, który czyta w pętli
 * aż do końca pliku, zabiera wtedy cały czas domeny i wywraca strumień audio,
 * który akurat płynie obok.
 *
 * Dlatego oba elementy robią w jednym `process()` **najwyżej tyle, ile mieści
 * się w budżecie** — domyślnie jeden blok. Nadrabianie zaległości jest wtedy
 * rozłożone w czasie, a nie wykonane naraz. Z tego samego powodu naturalnym
 * miejscem obu jest domena o niskim priorytecie, oddzielona kolejką od tej,
 * w której chodzi przetwornik.
 *
 * **Nagłówek WAV jest łatany na bieżąco.** Rozmiary w RIFF znane są dopiero
 * po zamknięciu pliku, a nagranie przerwane zanikiem zasilania miałoby tam
 * zera — czyli plik nie do odtworzenia mimo poprawnych danych. Ujście
 * uzupełnia je więc co kilka bloków, żeby awaria kosztowała ogon, a nie
 * całość.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/hal/IFileSystem.hpp"
#include "hydra/media/Pipeline.hpp"

namespace hydra {
namespace media {

/** Nagłówek WAV rozłożony na to, co z niego potrzebne. */
struct WavInfo {
    MediaFormat format{};
    /** Przesunięcie pierwszej próbki od początku pliku. */
    u32 dataOffset = 0;
    u32 dataBytes  = 0;

    bool valid() const { return format.valid() && dataBytes > 0; }
};

/**
 * Rozbiera nagłówek WAV.
 *
 * Przechodzi listę fragmentów zamiast zakładać, że `fmt ` i `data` leżą
 * bezpośrednio po sobie. Nie leżą: edytory wstawiają między nie `LIST` z nazwą
 * programu, a rekordery — `fact`. Czytnik zakładający stałe przesunięcie 44
 * bajtów działa z plikami, które sam wyprodukował, i z niczym więcej.
 */
Result<WavInfo> parseWavHeader(CByteSpan header);

/** Składa 44-bajtowy nagłówek WAV. `dataBytes` = 0 na czas nagrywania. */
size_t buildWavHeader(ByteSpan out, const MediaFormat& format, u32 dataBytes);

// ---------------------------------------------------------------------------

/**
 * Odtwarzanie pliku WAV albo surowych próbek.
 *
 * Format bierze się z nagłówka pliku, a nie z konfiguracji — inaczej plik
 * 44,1 kHz odtworzony jako 16 kHz brzmiałby jak nagranie zwolnione i wyglądało
 * to na usterkę sprzętu.
 */
class FileSource : public Element {
public:
    struct Config {
        const char* path = nullptr;
        /**
         * Format surowych próbek. Używany wyłącznie, gdy plik nie jest WAV-em;
         * wtedy nie ma skąd wziąć tej informacji.
         */
        MediaFormat rawFormat{};
        u16  framesPerBlock = 256;
        /** Czy zaczynać od początku po dojściu do końca. */
        bool loop = false;
        /** Ile bloków najwyżej wczytać w jednym kroku. */
        u8   blocksPerStep = 1;
    };

    explicit FileSource(hal::IFileSystem& fs) : Element("file-in"), fs_(fs) {}

    Status configure(const Config& cfg);

    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    Status onStart() override;
    void   onStop() override;
    void   process(u64 nowUs) override;

    /** Czy plik doszedł do końca (i nie zapętla się). */
    bool   finished() const { return finished_; }
    u64    framesRead() const { return frames_; }
    const WavInfo& info() const { return info_; }

private:
    Status openAndParse();

    hal::IFileSystem& fs_;
    Config      cfg_{};
    hal::IFile* file_ = nullptr;
    WavInfo     info_{};
    Pipeline*   pipeline_ = nullptr;
    BlockPool*  pool_ = nullptr;
    u32         consumed_ = 0;   ///< bajty danych już wczytane
    u64         frames_ = 0;
    bool        finished_ = false;
};

/**
 * Zapis do pliku WAV albo surowego.
 *
 * Ujście, więc kończy potok: bloki są zapisywane i zwalniane. Nagłówek jest
 * uzupełniany co `patchEvery` bloków i przy zamknięciu.
 */
class FileSink : public Element {
public:
    struct Config {
        const char* path = nullptr;
        /** `false` = surowe próbki bez nagłówka. */
        bool writeWavHeader = true;
        /**
         * Co ile bloków uzupełnić rozmiary w nagłówku i wypchnąć bufor.
         * Zero wyłącza — plik będzie poprawny wyłącznie po czystym zamknięciu.
         */
        u16 patchEvery = 32;
        u8  blocksPerStep = 2;
    };

    explicit FileSink(hal::IFileSystem& fs) : Element("file-out"), fs_(fs) {}

    Status configure(const Config& cfg);

    u8 inputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    Status onPrepare(Pipeline& pipeline) override;
    Status onStart() override;
    void   onStop() override;
    void   process(u64 nowUs) override;

    u64 bytesWritten() const { return written_; }
    u32 writeErrors() const { return errors_; }

private:
    void patchHeader();

    hal::IFileSystem& fs_;
    Config      cfg_{};
    hal::IFile* file_ = nullptr;
    MediaFormat format_{};
    Pipeline*   pipeline_ = nullptr;
    u64         written_ = 0;
    u32         errors_ = 0;
    u16         sincePatch_ = 0;
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
