#pragma once
/**
 * Hydra — elementy obrazu (etap 5).
 *
 * Zakres celowo kończy się tam, gdzie zaczyna się dekodowanie. Potok potrafi
 * wziąć klatkę z kamery, przeskalować ją, zmienić przestrzeń barw i wystawić
 * na panel albo do pliku. **Nie ma dekodera JPEG** i nie będzie go tu, bo
 * dekompresja 640×480 na Cortex-M4 zajmuje kilkaset milisekund — czyli tyle,
 * ile trwa kilkanaście klatek.
 *
 * Zamiast tego JPEG jedzie **bez rekompresji**: moduł kamery kompresuje
 * sprzętowo, a potok przepuszcza bajty do pliku albo do sieci, nie zaglądając
 * do środka. To jest najczęstszy i najtańszy przypadek na tej klasie sprzętu,
 * a nie obejście braku dekodera. Potrzebne są z niego tylko wymiary — i te
 * czyta `jpegInfo()` z nagłówka SOF.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/hal/ICamera.hpp"
#include "hydra/media/Pipeline.hpp"

namespace hydra {
namespace media {

/** Wymiary odczytane z nagłówka strumienia JPEG. */
struct JpegInfo {
    u16 width  = 0;
    u16 height = 0;
    /** Liczba składowych: 1 = odcienie szarości, 3 = kolor. */
    u8  components = 0;

    bool valid() const { return width > 0 && height > 0; }
};

/**
 * Czyta wymiary z JPEG bez dekodowania.
 *
 * Przechodzi znaczniki do pierwszego SOF (0xC0…0xCF poza 0xC4, 0xC8 i 0xCC —
 * te trzy to tablice Huffmana i rozszerzenia, nie ramka). Potrzebne, bo moduł
 * kamery potrafi zmienić rozdzielczość między klatkami przy zmianie
 * oświetlenia, a potok musi wiedzieć, co przepuszcza.
 */
Result<JpegInfo> jpegInfo(CByteSpan data);

// ---------------------------------------------------------------------------

/**
 * Klatki z kamery jako źródło potoku.
 *
 * **Kopiuje klatkę do bloku puli** i natychmiast oddaje ją sterownikowi.
 * Kamera ma zwykle dwa bufory; zatrzymanie jednego na czas przejścia przez
 * cały potok zatrzymałoby strumień na tyle klatek, ile trwa przetwarzanie.
 *
 * Kopia jest tu świadomym kosztem, nie przeoczeniem. Dla QVGA w RGB565 to
 * 150 kB na klatkę — przy 30 klatkach na sekundę 4,5 MB/s, czyli zauważalnie,
 * ale znośnie. Dla JPEG-a to zwykle 15 kB i koszt znika. Przy 1080p trzeba by
 * puli, której bloki leżą w pamięci sterownika — a to zmiana w rdzeniu
 * (`BlockPool` alokuje z jednego ciągłego obszaru), nie w tym elemencie.
 */
class CameraSource : public Element {
public:
    struct Config {
        hal::CameraConfig camera{};
        /**
         * Ile klatek najwyżej pobrać w jednym kroku. Jedna wystarcza: przy
         * 30 klatkach na sekundę i kroku co 10 ms i tak czekamy na sensor.
         */
        u8 framesPerStep = 1;
    };

    explicit CameraSource(hal::ICamera& camera) : Element("camera"), camera_(camera) {}

    Status configure(const Config& cfg);

    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    Status onStart() override;
    void   onStop() override;
    void   process(u64 nowUs) override;

    u64 framesCaptured() const { return frames_; }
    /** Klatki większe niż blok puli — objaw źle dobranego rozmiaru. */
    u32 oversized() const { return oversized_; }

private:
    hal::ICamera& camera_;
    Config        cfg_{};
    MediaFormat   format_{};
    Pipeline*     pipeline_ = nullptr;
    BlockPool*    pool_ = nullptr;
    u64           frames_ = 0;
    u32           oversized_ = 0;
};

/**
 * Skalowanie najbliższym sąsiadem.
 *
 * Bez interpolacji i bez filtru — celowo. Interpolacja dwuliniowa kosztuje
 * cztery odczyty i trzy mnożenia na piksel; przy QVGA i 30 klatkach na sekundę
 * to dwa i pół miliona mnożeń na sekundę na układzie bez jednostki
 * zmiennoprzecinkowej. Najbliższy sąsiad to jeden odczyt i jedno przesunięcie,
 * a przy zmniejszaniu obrazu do podglądu różnicy i tak nie widać.
 *
 * Współczynniki są liczone raz, przy `prepare()`, w Q16 — dzielenie na piksel
 * byłoby najdroższą operacją w całym elemencie.
 */
class Scaler : public Element {
public:
    Scaler() : Element("scale") {}

    /** Rozmiar docelowy. Zero = bez zmiany w tej osi. */
    void setOutputSize(u16 width, u16 height) { outW_ = width; outH_ = height; }

    u8 inputCount() const override { return 1; }
    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

    u64 framesScaled() const { return frames_; }

private:
    Pipeline*   pipeline_ = nullptr;
    BlockPool*  pool_ = nullptr;
    MediaFormat in_{};
    MediaFormat out_{};
    u16         outW_ = 0;
    u16         outH_ = 0;
    u32         stepX_ = 0;    ///< Q16 pikseli wejścia na piksel wyjścia
    u32         stepY_ = 0;
    u8          bytesPerPixel_ = 0;
    u64         frames_ = 0;
};

/**
 * Zmiana przestrzeni barw.
 *
 * Trzy przejścia, wszystkie na liczbach całkowitych i wszystkie potrzebne
 * w praktyce:
 *
 *   YUV422 → RGB565   sensor daje YUYV, panel chce RGB565
 *   RGB565 → GRAY8    wykrywanie ruchu i kody kreskowe nie potrzebują barw
 *   YUV422 → GRAY8    to samo, ale bez przechodzenia przez kolor
 *
 * Przejście na szarość z YUV jest darmowe — składowa Y **jest** jasnością.
 * To jedyny powód, dla którego warto trzymać sensor w YUV, gdy i tak liczy się
 * tylko luminancja.
 */
class ColorConvert : public Element {
public:
    ColorConvert() : Element("convert") {}

    void setOutputFormat(FrameFormat format) { target_ = format; }

    u8 inputCount() const override { return 1; }
    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

    u64 framesConverted() const { return frames_; }

private:
    Pipeline*   pipeline_ = nullptr;
    BlockPool*  pool_ = nullptr;
    MediaFormat in_{};
    MediaFormat out_{};
    FrameFormat target_ = FrameFormat::Rgb565;
    u64         frames_ = 0;
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
