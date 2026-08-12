/**
 * Hydra — inferencja jako element potoku. Patrz nagłówek po decyzje projektowe.
 */

#include "hydra/media/elements/Infer.hpp"

#if HYDRA_ENABLE_MEDIA

#include <string.h>

#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/media/Pipeline.hpp"

HYDRA_LOG_MODULE("infer")

namespace hydra {
namespace media {
namespace {

/** Bajty jednej próbki wejścia modelu. */
u8 elementSize(const infer::TensorInfo& info) {
    return infer::bytesPerElement(info.type);
}

/**
 * Czy próbki ze strumienia da się podać modelowi bez przeliczania.
 *
 * Element świadomie **nie konwertuje** formatów. Przeliczenie S16 na F32
 * wymagałoby drugiego bufora wielkości okna, a decyzja o tym, gdzie on leży,
 * należy do aplikacji. Zgodność formatu jest za to sprawdzalna raz, przy
 * negocjacji — i wtedy błąd wychodzi przy składaniu potoku, a nie po godzinie
 * pracy jako sieczka na wejściu modelu.
 */
bool formatMatches(SampleFormat stream, infer::TensorType model) {
    switch (model) {
        case infer::TensorType::S16: return stream == SampleFormat::S16;
        case infer::TensorType::F32: return stream == SampleFormat::F32;
        case infer::TensorType::S8:  return stream == SampleFormat::U8;   // bajt to bajt
        case infer::TensorType::U8:  return stream == SampleFormat::U8;
        default: return false;
    }
}

}  // namespace

Result<MediaFormat> Inference::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);

    /*
     * Dwa rodzaje wejścia i oba są zwykłym ciągiem liczb dla modelu.
     *
     * `Audio` — próbki wprost z przetwornika: model uczony na surowym sygnale,
     * jak przy wykrywaniu anomalii wibracji.
     * `Features` — wektory cech z MFCC: model uczony na tym, co z sygnału
     * zostaje po odrzuceniu fazy i skali, jak przy rozpoznawaniu mowy.
     *
     * Element nie musi ich rozróżniać poza negocjacją: dalej jedno i drugie
     * jest oknem bajtów o rozmiarze wziętym z modelu. Rozdzielenie tego na dwa
     * elementy oznaczałoby dwie kopie składania okna i przesuwu.
     */
    if (in.kind != MediaKind::Audio && in.kind != MediaKind::Features) {
        return unexpected(Err::NotSupported);
    }
    if (engine_ == nullptr) return unexpected(Err::NotInitialized);

    const infer::TensorInfo input = engine_->input(0);
    if (!input.valid()) return unexpected(Err::NotInitialized);

    if (!formatMatches(in.sampleFormat, input.type)) {
        HYDRA_LOGE("model chce %s, a strumień daje %s — element nie przelicza formatów",
                   infer::toString(input.type), toString(in.sampleFormat));
        return unexpected(Err::NotSupported);
    }

    if (in.kind == MediaKind::Features) {
        /*
         * Okno modelu musi być wielokrotnością wektora cech.
         *
         * Model rozpoznający mowę patrzy na stos ramek — na przykład 49
         * wektorów po 13 współczynników. Gdyby okno wypadało w połowie wektora,
         * model dostawałby ramki poprzesuwane o kilka współczynników i uczyłby
         * się szumu zamiast treści. Sprawdzamy to raz, przy składaniu potoku.
         */
        const u32 vectorBytes = in.unitBytes();
        if (vectorBytes == 0 || (input.bytes() % vectorBytes) != 0) {
            HYDRA_LOGE("model chce okna %u B, a wektor cech ma %u B — nie dzieli się bez reszty",
                       static_cast<unsigned>(input.bytes()), static_cast<unsigned>(vectorBytes));
            return unexpected(Err::NotSupported);
        }
    }

    // Ujście: pad wyjściowy nie istnieje, więc format wyjścia jest pusty.
    return MediaFormat{};
}

Status Inference::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;

    if (engine_ == nullptr) {
        HYDRA_LOGE("brak silnika — ustaw setEngine() przed prepare()");
        return fail(Err::NotInitialized);
    }

    const infer::TensorInfo input = engine_->input(0);
    if (!input.valid()) {
        HYDRA_LOGE("silnik nie podaje kształtu wejścia — czy model jest wczytany?");
        return fail(Err::NotInitialized);
    }

    windowBytes_  = input.bytes();
    elementBytes_ = elementSize(input);
    if (windowBytes_ == 0 || elementBytes_ == 0) return fail(Err::NotSupported);

    if (window_ == nullptr || windowCapacity_ < windowBytes_) {
        // Rozmiar okna wynika z modelu, więc aplikacja nie ma jak go znać
        // przed wczytaniem — dlatego sprawdzamy tutaj, a nie w setWindowBuffer.
        HYDRA_LOGE("bufor okna ma %u B, a model potrzebuje %u B",
                   static_cast<unsigned>(windowCapacity_), static_cast<unsigned>(windowBytes_));
        return fail(Err::OutOfMemory);
    }

    // Domyślny przesuw: całe okno. Zakładka jest wyborem, nie stanem
    // domyślnym — kosztuje tyle inferencji, ile razy okna na siebie zachodzą.
    if (hop_ == 0) hop_ = windowBytes_ / elementBytes_;

    filled_ = 0;
    HYDRA_LOGI("okno %u próbek po %u B, przesuw %u, silnik '%s'",
               static_cast<unsigned>(windowBytes_ / elementBytes_),
               static_cast<unsigned>(elementBytes_),
               static_cast<unsigned>(hop_), engine_->name());
    return ok();
}

u32 Inference::fill(const u8* data, u32 bytes) {
    const u32 room = windowBytes_ - filled_;
    const u32 take = bytes < room ? bytes : room;
    memcpy(window_ + filled_, data, take);
    filled_ += take;
    return take;
}

void Inference::slide() {
    const u32 hopBytes = hop_ * elementBytes_;
    if (hopBytes >= filled_) {
        filled_ = 0;
        return;
    }
    // Zakładka: reszta okna wraca na początek. `memmove`, bo obszary zachodzą
    // na siebie z definicji — `memcpy` dawałby tu wynik zależny od implementacji.
    const u32 keep = filled_ - hopBytes;
    memmove(window_, window_ + hopBytes, keep);
    filled_ = keep;
}

void Inference::run() {
    const u64 startUs = rtos::nowUs();

    if (auto r = engine_->setInput(0, window_, windowBytes_); !r) {
        HYDRA_LOGE("setInput: %s", engine_->error());
        if (pipeline_ != nullptr) pipeline_->raise(MediaFault::Internal, *this, 0);
        return;
    }
    if (auto r = engine_->invoke(); !r) {
        HYDRA_LOGE("invoke: %s", engine_->error());
        if (pipeline_ != nullptr) pipeline_->raise(MediaFault::Internal, *this, 0);
        return;
    }

    const u32 elapsedUs = static_cast<u32>(rtos::nowUs() - startUs);
    if (elapsedUs > worstUs_) worstUs_ = elapsedUs;

    /*
     * Odczyt całego wyjścia naraz, nie po jednej wartości.
     *
     * Pierwsza wersja czytała po jednej — i działała, dopóki model miał jedno
     * wyjście. Przy klasyfikatorze dwuklasowym `readOutput()` odrzucał żądanie
     * o rozmiarze jednej liczby (wymaga całego tensora co do bajta), pętla
     * przerywała się na pierwszym obiegu, a werdykt zawsze wynosił zero.
     * Objaw był najgorszy z możliwych: łańcuch działał, liczniki rosły,
     * a wynik był stały.
     */
    const infer::TensorInfo out = engine_->output(0);
    const u32 count = out.elements();

    u8    top = 0;
    float best = 0.0f;

    if (count == 0 || count > HYDRA_INFER_MAX_OUTPUTS) {
        HYDRA_LOGE("model ma %u wyjść — element obsługuje do %u",
                   static_cast<unsigned>(count), static_cast<unsigned>(HYDRA_INFER_MAX_OUTPUTS));
        if (pipeline_ != nullptr) pipeline_->raise(MediaFault::Internal, *this, 0);
        return;
    }

    if (out.type == infer::TensorType::F32) {
        float values[HYDRA_INFER_MAX_OUTPUTS];
        if (auto r = engine_->readOutput(0, values, count * sizeof(float)); !r) {
            HYDRA_LOGE("readOutput: %s", engine_->error());
            if (pipeline_ != nullptr) pipeline_->raise(MediaFault::Internal, *this, 0);
            return;
        }
        best = values[0];
        for (u32 i = 1; i < count; ++i) {
            if (values[i] > best) { best = values[i]; top = static_cast<u8>(i); }
        }
    } else {
        // Model skwantyzowany: surowe bajty, a wielkość fizyczna dopiero po
        // przeliczeniu przez skalę tensora — inaczej „-42" nie znaczy nic.
        i8 raw[HYDRA_INFER_MAX_OUTPUTS];
        if (auto r = engine_->readOutput(0, raw, count * sizeof(i8)); !r) {
            HYDRA_LOGE("readOutput: %s", engine_->error());
            if (pipeline_ != nullptr) pipeline_->raise(MediaFault::Internal, *this, 0);
            return;
        }
        best = out.dequantize(raw[0]);
        for (u32 i = 1; i < count; ++i) {
            const float value = out.dequantize(raw[i]);
            if (value > best) { best = value; top = static_cast<u8>(i); }
        }
    }

    ++inferences_;
    last_ = infer::InferenceReady{name(), top, best, elapsedUs};

    if (budgetUs_ > 0 && elapsedUs > budgetUs_) {
        ++overruns_;
        // Przekroczenia nie da się cofnąć — inferencji nie przerywa się
        // w środku. Ale musi być widoczne: model, który raz na jakiś czas
        // przekracza budżet, objawia się inaczej niż model, który go łamie
        // za każdym razem, a bez licznika oba wyglądają jak „czasem zacina".
        // Bez `raise()`: usterki potoku opisują przepływ, a przekroczony
        // budżet nie jest problemem przepływu — dane dopłynęły i wynik
        // powstał, tylko za wolno. Zgłoszenie go jako usterki kolejki
        // kazałoby szukać przyczyny w pulach i połączeniach.
        HYDRA_LOGW("inferencja %u us przekracza budżet %u us",
                   static_cast<unsigned>(elapsedUs), static_cast<unsigned>(budgetUs_));
    }

    EventBus::publish(last_);
}

void Inference::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);
    if (engine_ == nullptr || window_ == nullptr) return;

    Block block;
    // Pętla z tego samego powodu, co w Gain: gdy element chodzi rzadziej niż
    // źródło, pojedyncze pobranie na krok nigdy nie nadgoni kolejki.
    while (take(0, block)) {
        u32 offset = 0;
        while (offset < block.length) {
            offset += fill(block.data + offset, block.length - offset);

            if (filled_ >= windowBytes_) {
                run();
                slide();
            }
        }

        if (BlockPool* p = pipeline_->pool(block.pool); p != nullptr) p->release(block);
    }
}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
