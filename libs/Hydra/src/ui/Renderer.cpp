/** Hydra — implementacja pętli renderowania (rozdz. 6). */

#include "hydra/ui/Renderer.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"

HYDRA_LOG_MODULE("ui.render")

namespace hydra {
namespace ui {

Status Renderer::init(IDisplayBackend& backend) { return init(backend, Config{}); }

Status Renderer::init(IDisplayBackend& backend, const Config& cfg) {
    backend_ = &backend;
    cfg_     = cfg;

    HYDRA_CHECK(backend_->begin());

    // Pierwsza klatka zawsze przerysowuje wszystko — zawartość bufora
    // po starcie jest nieokreślona.
    invalidate();
    queue_.reset();
    stats_ = Stats{};

    HYDRA_LOGI("panel '%s' %dx%d, buforów: %u, odświeżanie częściowe: %s",
               backend_->name(), backend_->size().w, backend_->size().h,
               static_cast<unsigned>(backend_->bufferCount()),
               backend_->supportsPartial() ? "tak" : "nie");
    return ok();
}

void Renderer::invalidate() {
    if (backend_) pending_ = backend_->surface().bounds();
}

void Renderer::invalidate(Rect area) {
    if (!backend_) return;
    const Rect clipped = area.intersect(backend_->surface().bounds());
    if (clipped.empty()) return;
    pending_ = pending_.unite(clipped);
}

Status Renderer::renderFrame(Millis now) {
    HYDRA_UNUSED(now);
    if (!backend_) return fail(Err::NotInitialized);

    // 1. Polecenia z innych tasków. Wolno im unieważniać obszary, więc muszą
    //    wykonać się przed wyznaczeniem obszaru do przerysowania.
    stats_.commands += queue_.drain();

    // 2. Obszar do odświeżenia. Przy dwóch buforach dokładamy obszar
    //    z poprzedniej klatki — bufor, do którego teraz rysujemy, pamięta
    //    stan sprzed dwóch klatek.
    Rect region = pending_;
    if (backend_->bufferCount() > 1) region = region.unite(previous_);

    if (region.empty() && cfg_.skipIdleFrames) {
        ++stats_.skipped;
        return ok();
    }
    if (region.empty()) region = backend_->surface().bounds();

    gfx::ISurface& surface = backend_->surface();

    // 3. Rysowanie w obszarze przycinania — nawet funkcja rysująca cały ekran
    //    nie wyjdzie poza to, co naprawdę wymaga odświeżenia.
    const Micros renderStart = rtos::nowUs();
    surface.clearDirty();
    surface.setClip(region);
    surface.beginBatch();
    if (draw_) draw_(surface, region);
    surface.endBatch();
    surface.resetClip();
    const u32 renderUs = static_cast<u32>(rtos::nowUs() - renderStart);

    // 4. Transfer. Bierzemy sumę obszaru zamierzonego i faktycznie naruszonego:
    //    funkcja rysująca mogła narysować mniej, ale nigdy nie powinna wysłać
    //    mniej, niż sama zabrudziła.
    Rect area = region.unite(surface.dirty());
    if (!backend_->supportsPartial()) area = surface.bounds();

    const Micros presentStart = rtos::nowUs();
    const Status presented    = backend_->present(area);
    const u32    presentUs    = static_cast<u32>(rtos::nowUs() - presentStart);

    if (!presented) {
        HYDRA_LOGW("transfer na panel nieudany: %s", toString(presented.error()));
        // Obszar zostaje nieaktualny — kolejna klatka spróbuje ponownie,
        // zamiast zostawić na ekranie połowicznie przerysowaną treść.
        return presented;
    }

    // Zapamiętujemy unieważnienie, a nie narysowany obszar. Różnica jest
    // zasadnicza: bufor, do którego będziemy rysować w następnej klatce, jest
    // nieaktualny dokładnie tam, gdzie zmieniła się treść od czasu, gdy go
    // ostatnio malowano — czyli w obszarze pending_, nie w całej narysowanej
    // sumie. Zapisanie tu `region` sprawiłoby, że po jednej pełnej klatce
    // każda następna też byłaby pełna i odświeżanie częściowe przestałoby
    // cokolwiek dawać.
    previous_ = pending_;
    pending_  = Rect();
    surface.clearDirty();

    ++stats_.frames;
    ++frameNumber_;
    stats_.lastRenderUs  = renderUs;
    stats_.lastPresentUs = presentUs;
    if (renderUs > stats_.maxRenderUs) stats_.maxRenderUs = renderUs;
    if (presentUs > stats_.maxPresentUs) stats_.maxPresentUs = presentUs;

    if (cfg_.publishFrameEvents) {
        EventBus::publish(FrameRendered{
            frameNumber_, static_cast<u16>(area.w), static_cast<u16>(area.h),
            static_cast<u16>(renderUs > 0xFFFF ? 0xFFFF : renderUs),
            static_cast<u16>(presentUs > 0xFFFF ? 0xFFFF : presentUs)});
    }
    return ok();
}

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
