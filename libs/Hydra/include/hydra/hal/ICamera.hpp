#pragma once
/**
 * Hydra — kamera.
 *
 * Drugi po I2S peryferiał operujący na buforach, ale własność biegnie
 * **w drugą stronę** — i to jest tu najważniejsze do zrozumienia.
 *
 *     I2S:     my dajemy bufor  → sterownik go wypełnia → oddaje nam
 *     kamera:  sterownik ma bufory → pożycza nam klatkę → my ją oddajemy
 *
 * Tak działa `esp_camera_fb_get()` / `esp_camera_fb_return()` w ESP32 i tak
 * samo v4l2 w trybie MMAP: sterownik alokuje pamięć DMA przy starcie, bo tylko
 * on wie, gdzie musi leżeć i jak być wyrównana. Interfejs, w którym to my
 * podajemy bufor, wymuszałby na backendzie kopiowanie — czyli dokładnie to,
 * czego przy klatce 1080p nie wolno robić.
 *
 * Konsekwencja dla wołającego: **klatkę trzeba oddać**. Kamera ma zwykle dwa
 * albo trzy bufory; zatrzymanie jednego na dłużej niż jedną klatkę zatrzymuje
 * strumień. Dlatego `CameraSource` kopiuje treść do bloku puli i zwalnia
 * klatkę od razu — patrz komentarz przy tej klasie.
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace hal {

/** Format wyjściowy sensora. Odpowiada temu, co potrafią typowe moduły. */
enum class CameraFormat : u8 {
    None = 0,
    Yuv422,   ///< YUYV — natywny format prawie każdego sensora
    Rgb565,
    Grayscale,
    Jpeg,     ///< kompresja w module kamery, nie w procesorze
};

/** Rozdzielczość. Nazwy jak w kartach katalogowych modułów. */
enum class CameraResolution : u8 {
    Qqvga = 0,  ///< 160×120
    Qvga,       ///< 320×240
    Vga,        ///< 640×480
    Svga,       ///< 800×600
    Hd,         ///< 1280×720
};

constexpr u16 widthOf(CameraResolution r) {
    switch (r) {
        case CameraResolution::Qqvga: return 160;
        case CameraResolution::Qvga:  return 320;
        case CameraResolution::Vga:   return 640;
        case CameraResolution::Svga:  return 800;
        case CameraResolution::Hd:    return 1280;
    }
    return 0;
}

constexpr u16 heightOf(CameraResolution r) {
    switch (r) {
        case CameraResolution::Qqvga: return 120;
        case CameraResolution::Qvga:  return 240;
        case CameraResolution::Vga:   return 480;
        case CameraResolution::Svga:  return 600;
        case CameraResolution::Hd:    return 720;
    }
    return 0;
}

struct CameraConfig {
    CameraFormat     format     = CameraFormat::Rgb565;
    CameraResolution resolution = CameraResolution::Qvga;
    /**
     * Jakość kompresji 0…63, mniej znaczy lepiej — skala z modułów OV2640.
     * Ignorowana poza formatem JPEG.
     */
    u8  jpegQuality = 12;
    /**
     * Ile buforów alokuje sterownik. Dwa pozwalają wypełniać jeden, gdy drugi
     * jest w użyciu; przy jednym strumień staje na czas przetwarzania.
     */
    u8  bufferCount = 2;
    u8  framesPerSecondLimit = 0;   ///< 0 = tyle, ile sensor da
};

/**
 * Klatka pożyczona ze sterownika.
 *
 * `data` wskazuje pamięć **należącą do sterownika**. Wolno ją czytać do chwili
 * `release()`, i ani chwili dłużej.
 */
struct CameraFrame {
    const u8* data   = nullptr;
    u32       length = 0;
    u16       width  = 0;
    u16       height = 0;
    CameraFormat format = CameraFormat::None;
    /** Czas pobrania w mikrosekundach zegara sterownika. */
    u64       timestampUs = 0;
    /** Uchwyt sterownika — dla nas nieprzezroczysty. */
    void*     handle = nullptr;

    bool valid() const { return data != nullptr && length > 0; }
};

class ICamera {
public:
    virtual ~ICamera() = default;

    virtual Status begin(const CameraConfig& cfg) = 0;
    virtual void   end() = 0;
    virtual bool   running() const = 0;

    /**
     * Pożycza najświeższą klatkę. `NotFound` = nic jeszcze nie ma gotowe.
     *
     * Nie blokuje: czekanie na klatkę zatrzymałoby domenę na kilkadziesiąt
     * milisekund, a przy 30 klatkach na sekundę to trzy czwarte budżetu.
     */
    virtual Result<CameraFrame> capture() = 0;

    /** Oddaje klatkę sterownikowi. Obowiązkowe — bez tego strumień staje. */
    virtual void release(CameraFrame& frame) = 0;

    virtual CameraConfig config() const = 0;
    /** Klatki, które sensor wyprodukował, a nikt nie odebrał. */
    virtual u32 dropped() const = 0;
};

}  // namespace hal
}  // namespace hydra
