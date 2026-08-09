/**
 * Hydra — backend SDL dla celu `native`.
 *
 * Jedyny plik w bibliotece, który włącza SDL. Reguła w tools/check_includes.sh
 * pilnuje, żeby tak zostało: nagłówek SdlDisplay.hpp operuje na `void*`,
 * więc aplikacja i reszta frameworka nie widzą ani jednego typu SDL-a.
 *
 * Wariant bez SDL (brak HYDRA_WITH_SDL) jest na końcu pliku. Nie jest atrapą
 * na potrzeby testów — to normalna ścieżka dla buildu hostowego bez ekranu:
 * CI, kontener, sesja ssh. `begin()` zwraca wtedy Err::NotSupported, a aplikacja
 * decyduje, czy to błąd, czy powód, żeby ruszyć bez interfejsu.
 */

#include "hydra/gfx/SdlDisplay.hpp"

#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("sdl")

#if defined(HYDRA_WITH_SDL)

#include <SDL.h>

namespace hydra {
namespace gfx {
namespace {

/**
 * Piksel tekstury: ARGB8888 w kolejności natywnej.
 *
 * Konwertujemy zawsze, także dla Rgb565, choć SDL zna ten format. Powód jest
 * konkretny: Hydra trzyma Rgb565 bajtami od najstarszego (kolejność paneli SPI),
 * a SDL_PIXELFORMAT_RGB565 czyta je w kolejności maszyny. Na x86 dawało to
 * obraz z zamienionymi kanałami, a błąd tego rodzaju łatwo wziąć za usterkę
 * sterownika panelu. Jedna ścieżka konwersji dla wszystkich formatów jest
 * wolniejsza o rzecz, która na hoście nie ma znaczenia, i nie ma tej pułapki.
 */
inline u32 pack(Color c) {
    return (static_cast<u32>(c.a) << 24) | (static_cast<u32>(c.r) << 16) |
           (static_cast<u32>(c.g) << 8) | static_cast<u32>(c.b);
}

void convertRow(const u8* src, u32* dst, i16 width, PixelFormat format,
                Color monoOn, Color monoOff) {
    switch (format) {
        case PixelFormat::Mono1: {
            const u32 on  = pack(monoOn);
            const u32 off = pack(monoOff);
            for (i16 x = 0; x < width; ++x) {
                const u8 byte = src[x >> 3];
                const bool lit = (byte & (0x80u >> (x & 7))) != 0;
                dst[x] = lit ? on : off;
            }
            return;
        }
        case PixelFormat::Rgb565:
            for (i16 x = 0; x < width; ++x) {
                // Bajt starszy pierwszy — układ, w którym Hydra trzyma ten format.
                const u16 value = static_cast<u16>((static_cast<u16>(src[x * 2]) << 8) |
                                                   src[x * 2 + 1]);
                dst[x] = pack(Color::fromRgb565(value));
            }
            return;
        case PixelFormat::Rgb888:
            for (i16 x = 0; x < width; ++x) {
                dst[x] = pack(Color(src[x * 3], src[x * 3 + 1], src[x * 3 + 2]));
            }
            return;
        case PixelFormat::Rgba8888:
            for (i16 x = 0; x < width; ++x) {
                dst[x] = pack(Color(src[x * 4], src[x * 4 + 1], src[x * 4 + 2],
                                    src[x * 4 + 3]));
            }
            return;
    }
}

}  // namespace

SdlDisplay::~SdlDisplay() { end(); }

Status SdlDisplay::begin(ByteSpan buffer, const Cfg& cfg) {
    if (window_ != nullptr) return fail(Err::AlreadyExists);
    if (cfg.width <= 0 || cfg.height <= 0) return fail(Err::BadArgument);

    cfg_ = cfg;
    if (cfg_.scale == 0) cfg_.scale = 1;

    HYDRA_CHECK(fb_.attach(buffer, cfg.width, cfg.height, cfg.format));
    fb_.setPresent([this](CByteSpan pixels, Size size, PixelFormat format) {
        return present(pixels, size, format);
    });

    // Inicjujemy tylko wideo. SDL_INIT_EVERYTHING wciąga dźwięk i joysticki,
    // a na maszynie bez karty dźwiękowej cała inicjalizacja kończy się błędem
    // — z komunikatem o dźwięku w programie, który chce narysować okno.
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        /*
         * Program wchodzi przez własne main(), nie przez SDL2main.
         *
         * Aplikacja Hydry dołącza Hydra.h, a nie <SDL.h>, więc `#define main
         * SDL_main` nigdy się nie wykonuje i SDL2main nie miałby czego wołać.
         * Budowanie ustawia SDL_MAIN_HANDLED, a wtedy SDL wymaga, żeby ktoś
         * zgłosił gotowość ręcznie — bez tego SDL_Init odmawia na Windows.
         * Poza Windows wywołanie jest nieszkodliwe.
         */
        SDL_SetMainReady();

        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            HYDRA_LOGE("SDL_Init: %s", SDL_GetError());
            return fail(Err::IoError);
        }
        ownsSdl_ = true;
    }

    // Skalowanie całkowitoliczbowe bez wygładzania: piksel bufora ma być
    // widocznym kwadratem, a nie rozmazaną plamą. Przy podglądzie panelu
    // 128×64 filtrowanie liniowe zjada dokładnie tę informację, dla której
    // ogląda się go na PC.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    const int windowW = static_cast<int>(cfg.width) * cfg_.scale;
    const int windowH = static_cast<int>(cfg.height) * cfg_.scale;

    SDL_Window* window = SDL_CreateWindow(cfg.title,
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          windowW, windowH, SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        HYDRA_LOGE("SDL_CreateWindow: %s", SDL_GetError());
        end();
        return fail(Err::IoError);
    }
    window_ = window;

    // Najpierw akcelerowany, potem programowy. Pulpit zdalny na Windows i maszyna
    // wirtualna bez sterownika GPU nie dają akceleracji, a rysowanie 320×240
    // procesorem jest w zupełności wystarczające — odmowa otwarcia okna nie.
    const Uint32 vsyncFlag = cfg.vsync ? SDL_RENDERER_PRESENTVSYNC : 0u;
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
                                                SDL_RENDERER_ACCELERATED | vsyncFlag);
    if (renderer == nullptr) {
        HYDRA_LOGW("brak akceleracji (%s) — rysowanie programowe", SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE | vsyncFlag);
    }
    if (renderer == nullptr) {
        HYDRA_LOGE("SDL_CreateRenderer: %s", SDL_GetError());
        end();
        return fail(Err::IoError);
    }
    renderer_ = renderer;

    // Rozmiar logiczny = rozmiar powierzchni. Dzięki temu współrzędne myszy
    // przeliczają się same i nie trzeba dzielić ich przez skalę w każdym miejscu,
    // w którym są czytane.
    SDL_RenderSetLogicalSize(renderer, cfg.width, cfg.height);

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             cfg.width, cfg.height);
    if (texture == nullptr) {
        HYDRA_LOGE("SDL_CreateTexture: %s", SDL_GetError());
        end();
        return fail(Err::IoError);
    }
    texture_ = texture;

    HYDRA_LOGI("okno '%s' %dx%d ×%u, format %s", cfg.title,
               static_cast<int>(cfg.width), static_cast<int>(cfg.height),
               static_cast<unsigned>(cfg_.scale), toString(cfg.format));
    return ok();
}

void SdlDisplay::end() {
    if (texture_ != nullptr) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(texture_));
        texture_ = nullptr;
    }
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer_));
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
        window_ = nullptr;
    }
    if (ownsSdl_) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        ownsSdl_ = false;
    }
}

bool SdlDisplay::pump() {
    if (window_ == nullptr) return false;

    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        switch (event.type) {
            case SDL_QUIT:
                quit_ = true;
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) quit_ = true;
                break;

            case SDL_MOUSEMOTION:
                pointer_.x = static_cast<i16>(event.motion.x);
                pointer_.y = static_cast<i16>(event.motion.y);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    pointer_.down = (event.type == SDL_MOUSEBUTTONDOWN);
                }
                pointer_.x = static_cast<i16>(event.button.x);
                pointer_.y = static_cast<i16>(event.button.y);
                break;

            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                const bool down = (event.type == SDL_KEYDOWN);
                // Esc zamyka okno tak samo jak krzyżyk. Program uruchamiany
                // z terminala setki razy dziennie ma mieć wyjście pod ręką.
                if (down && event.key.keysym.sym == SDLK_ESCAPE) quit_ = true;
                // `repeat` odfiltrowane: autopowtarzanie klawiatury systemu nie
                // jest naciśnięciem i nie ma dla niego odpowiadającego zwolnienia.
                if (key_ && event.key.repeat == 0) {
                    key_(static_cast<u32>(event.key.keysym.sym), down);
                }
                break;
            }

            default:
                break;
        }
    }
    return !quit_;
}

Status SdlDisplay::present(CByteSpan pixels, Size size, PixelFormat format) {
    if (texture_ == nullptr || renderer_ == nullptr) return fail(Err::NotInitialized);

    SDL_Texture* texture = static_cast<SDL_Texture*>(texture_);

    void* raw    = nullptr;
    int   pitch  = 0;
    if (SDL_LockTexture(texture, nullptr, &raw, &pitch) != 0) {
        HYDRA_LOGE("SDL_LockTexture: %s", SDL_GetError());
        return fail(Err::IoError);
    }

    const u32 srcStride = Framebuffer::rowStride(size.w, format);
    for (i16 y = 0; y < size.h; ++y) {
        const u8* src = pixels.data() + static_cast<size_t>(y) * srcStride;
        u32* dst = reinterpret_cast<u32*>(static_cast<u8*>(raw) +
                                          static_cast<size_t>(y) * static_cast<size_t>(pitch));
        convertRow(src, dst, size.w, format, cfg_.monoOn, cfg_.monoOff);
    }

    SDL_UnlockTexture(texture);

    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(renderer_);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    ++frames_;
    return ok();
}

}  // namespace gfx
}  // namespace hydra

#else  // !HYDRA_WITH_SDL

namespace hydra {
namespace gfx {

SdlDisplay::~SdlDisplay() = default;

Status SdlDisplay::begin(ByteSpan buffer, const Cfg& cfg) {
    HYDRA_UNUSED(buffer);
    HYDRA_UNUSED(cfg);
    // Bufor zostaje niepodpięty celowo: powierzchnia bez okna rysowałaby
    // w pamięć, której nikt nie ogląda, a aplikacja nie miałaby jak zauważyć,
    // że nie ma ekranu.
    return fail(Err::NotSupported);
}

void SdlDisplay::end() {}

bool SdlDisplay::pump() { return false; }

Status SdlDisplay::present(CByteSpan pixels, Size size, PixelFormat format) {
    HYDRA_UNUSED(pixels);
    HYDRA_UNUSED(size);
    HYDRA_UNUSED(format);
    return fail(Err::NotSupported);
}

}  // namespace gfx
}  // namespace hydra

#endif  // HYDRA_WITH_SDL
