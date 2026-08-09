/** Hydra — implementacja elementów audio na sprzęcie. */

#include "hydra/media/elements/Audio.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("media.audio")

namespace hydra {
namespace media {
namespace {

/** Format audio, jaki wynika z ustawień sprzętu I2S. */
MediaFormat formatOf(const hal::I2sConfig& cfg) {
    SampleFormat sample = SampleFormat::None;
    switch (cfg.bitsPerSample) {
        case 16: sample = SampleFormat::S16; break;
        case 24: sample = SampleFormat::S24; break;
        case 32: sample = SampleFormat::S32; break;
        default: break;
    }
    return MediaFormat::audio(cfg.sampleRate, sample, cfg.channels);
}

}  // namespace

// ---------------------------------------------------------------------------
// InFlightTable
// ---------------------------------------------------------------------------

bool InFlightTable::add(const Block& block, size_t bytes) {
    HYDRA_UNUSED(bytes);
    for (auto& entry : entries_) {
        if (entry.used) continue;
        entry.block = block;
        entry.data  = block.data;
        entry.used  = true;
        ++count_;
        return true;
    }
    return false;
}

bool InFlightTable::take(const u8* data, Block& out) {
    for (auto& entry : entries_) {
        if (!entry.used || entry.data != data) continue;
        out = entry.block;
        entry.used = false;
        --count_;
        return true;
    }
    return false;
}

bool InFlightTable::drain(Block& out) {
    for (auto& entry : entries_) {
        if (!entry.used) continue;
        out = entry.block;
        entry.used = false;
        --count_;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// I2sSink
// ---------------------------------------------------------------------------

Result<MediaFormat> I2sSink::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    return in;   // ujście nie ma wyjścia; wywoływane tylko dla porządku
}

Status I2sSink::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;

    // Rozjazd formatu z ustawieniami sprzętu daje dźwięk o złej wysokości —
    // objaw, który wygląda jak zepsuty przetwornik, a jest literówką
    // w konfiguracji. Sprawdzamy przy przygotowaniu, nie przy pierwszej próbce.
    const MediaFormat& want = input(0).format();
    const MediaFormat have = formatOf(cfg_);
    if (want.valid() && !want.equals(have)) {
        HYDRA_LOGE("i2s-out: potok podaje %lu Hz / %u kan. / %s, a sprzęt jest "
                   "ustawiony na %lu Hz / %u kan. / %s",
                   static_cast<unsigned long>(want.sampleRate),
                   static_cast<unsigned>(want.channels), toString(want.sampleFormat),
                   static_cast<unsigned long>(have.sampleRate),
                   static_cast<unsigned>(have.channels), toString(have.sampleFormat));
        return fail(Err::Protocol);
    }
    return ok();
}

Status I2sSink::onStart() {
    cfg_.direction = hal::I2sDirection::Tx;
    return i2s_.begin(cfg_);
}

void I2sSink::onStop() {
    i2s_.end();
    // Bufory, które zostały u sterownika, wracają do puli. Bez tego pula
    // chudnie o nie przy każdym cyklu start/stop.
    Block block;
    while (inFlight_.drain(block)) {
        if (BlockPool* p = pipeline_->pool(block.pool); p != nullptr) p->release(block);
    }
}

void I2sSink::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);

    // Najpierw odbiór, potem podanie. Odwrotna kolejność zaczynałaby od pełnej
    // kolejki sprzętu i oddawała miejsce dopiero w następnym kroku — czyli
    // marnowała jeden okres na każdym obiegu.
    ByteSpan done;
    u32      bytes = 0;
    while (i2s_.reclaim(done, bytes)) {
        Block block;
        if (!inFlight_.take(done.data(), block)) continue;
        if (BlockPool* p = pipeline_->pool(block.pool); p != nullptr) p->release(block);
    }

    Block block;
    while (!inFlight_.full() && take(0, block)) {
        if (!i2s_.submit(ByteSpan{block.data, block.length})) {
            // Sprzęt zajęty — blok wraca do puli, a nie czeka w elemencie.
            // Trzymanie go oznaczałoby drugą kolejkę obok tej w padzie,
            // z własną polityką przepełnienia i własnymi błędami.
            if (BlockPool* p = pipeline_->pool(block.pool); p != nullptr) p->release(block);
            pipeline_->raise(MediaFault::Overrun, *this, 0);
            return;
        }
        inFlight_.add(block, block.length);
        ++submitted_;
    }
}

// ---------------------------------------------------------------------------
// I2sSource
// ---------------------------------------------------------------------------

Result<MediaFormat> I2sSource::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    HYDRA_UNUSED(in);
    const MediaFormat format = formatOf(cfg_);
    if (!format.valid() || format.sampleFormat == SampleFormat::None) {
        return unexpected(Err::NotSupported);
    }
    return format;
}

MemReq I2sSource::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = static_cast<u32>(framesPerBlock_) * cfg_.frameBytes();
    // Cztery bufory: dwa u sterownika, jeden w drodze, jeden wolny. Przy
    // dwóch kontroler zostaje bez bufora w chwili, gdy oddaje poprzedni —
    // i to jest dokładnie moment, w którym powstaje xrun.
    req.count = 4;
    req.dmaCapable = true;
    // Kontroler DMA zapisuje z pominięciem pamięci podręcznej; niewyrównany
    // początek oznacza uszkodzenie sąsiednich danych przy jej czyszczeniu.
    req.alignment = 32;
    return req;
}

Status I2sSource::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    pool_ = pipeline.poolFor(*this, 0);
    return pool_ != nullptr ? ok() : fail(Err::OutOfMemory);
}

Status I2sSource::onStart() {
    cfg_.direction = hal::I2sDirection::Rx;
    return i2s_.begin(cfg_);
}

void I2sSource::onStop() {
    i2s_.end();
    Block block;
    while (inFlight_.drain(block)) pool_->release(block);
}

void I2sSource::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);

    ByteSpan done;
    u32      bytes = 0;
    while (i2s_.reclaim(done, bytes)) {
        Block block;
        if (!inFlight_.take(done.data(), block)) continue;

        block.length = bytes;
        // Czas z licznika ramek, nie z zegara systemowego: przetwornik ma
        // własny kwarc i to on wyznacza tempo. Branie czasu z millis()
        // dawałoby dryf rzędu sekundy na godzinę.
        block.pts = frames_ * 1000000ull / cfg_.sampleRate;
        frames_ += bytes / cfg_.frameBytes();

        Block evicted;
        if (!emit(0, block, evicted)) {
            pool_->release(block);
            pipeline_->raise(MediaFault::Overrun, *this, 0);
        } else if (evicted.valid()) {
            pool_->release(evicted);
        }
    }

    // Sprzęt musi mieć w co pisać, zanim skończy bieżący bufor.
    while (!inFlight_.full()) {
        Block block = pool_->acquire();
        if (!block.valid()) {
            pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
            return;
        }
        if (!i2s_.submit(ByteSpan{block.data, block.capacity})) {
            pool_->release(block);
            return;   // kolejka sprzętu pełna — normalny stan, nie błąd
        }
        inFlight_.add(block, block.capacity);
    }
}

// ---------------------------------------------------------------------------
// SampleSink
// ---------------------------------------------------------------------------

Result<MediaFormat> SampleSink::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    return in;
}

Status SampleSink::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;

    const MediaFormat& format = input(0).format();
    if (format.kind != MediaKind::Audio) return fail(Err::NotSupported);
    if (format.sampleFormat != SampleFormat::S16) {
        HYDRA_LOGE("%s: obsługuje wyłącznie S16, a dostaje %s", name(),
                   toString(format.sampleFormat));
        return fail(Err::NotSupported);
    }
    sampleRate_ = format.sampleRate;
    channels_   = format.channels;
    return ok();
}

void SampleSink::releaseCurrent() {
    if (!current_.valid()) return;
    if (BlockPool* p = pipeline_->pool(current_.pool); p != nullptr) p->release(current_);
    current_ = Block{};
    offset_ = 0;
}

void SampleSink::onStop() { releaseCurrent(); }

void SampleSink::process(u64 nowUs) {
    if (sampleRate_ == 0) return;

    // Pierwsze wywołanie tylko ustawia punkt odniesienia: budżet liczony od
    // zera dałby na starcie tysiące próbek naraz.
    if (!primed_) { lastUs_ = nowUs; primed_ = true; return; }

    const u64 elapsed = nowUs - lastUs_ + carryUs_;
    lastUs_ = nowUs;

    const u64 budget = elapsed * sampleRate_ / 1000000ull;
    // Reszta poniżej jednej próbki przechodzi na następny krok. Bez tego przy
    // okresie 1 ms i 8 kHz gubimy ułamek co krok i wysokość dźwięku spada.
    carryUs_ = elapsed - budget * 1000000ull / sampleRate_;

    for (u64 i = 0; i < budget; ++i) {
        if (!current_.valid() || offset_ >= current_.length) {
            releaseCurrent();
            if (!take(0, current_)) {
                // Nie ma czego wystawić. Reszta budżetu przepada — nadrabianie
                // później oznaczałoby przyspieszenie dźwięku po każdej przerwie.
                starved_ += static_cast<u32>(budget - i);
                pipeline_->raise(MediaFault::Underrun, *this, 0);
                carryUs_ = 0;
                return;
            }
            offset_ = 0;
        }

        const i16* samples = reinterpret_cast<const i16*>(current_.data + offset_);
        // Tylko pierwszy kanał: PWM i DAC mają jedno wyjście, a mieszanie
        // kanałów jest zadaniem filtru, nie ujścia.
        const i32 value = samples[0];
        offset_ += static_cast<u32>(sizeof(i16)) * channels_;

        // Ze znakiem na bez znaku: −32768…32767 → 0…fullScale.
        const u32 scaled = static_cast<u32>((value + 32768) * fullScale() / 65535);
        (void)writeSample(static_cast<u16>(scaled));
        ++written_;
    }
}

// ---------------------------------------------------------------------------
// PwmAudioSink
// ---------------------------------------------------------------------------

Status PwmAudioSink::onStart() {
    if (sampleRate_ > 0 && carrierHz_ < sampleRate_ * 10) {
        // Filtr RC na wyjściu nie odróżni nośnej od sygnału, gdy leżą blisko
        // siebie. Ostrzegamy, bo to konfiguracja, która „prawie działa" —
        // dźwięk jest, tylko brzmi jak przez radio z zakłóceniami.
        HYDRA_LOGW("pwm-out: nośna %lu Hz przy próbkowaniu %lu Hz — daj co "
                   "najmniej dziesięciokrotność, inaczej nośna będzie słyszalna",
                   static_cast<unsigned long>(carrierHz_),
                   static_cast<unsigned long>(sampleRate_));
    }
    return pwm_.configure(pin_, carrierHz_, bits_);
}

void PwmAudioSink::onStop() {
    // Zatrzymanie na ciszy, czyli w połowie zakresu — nie na zerze. Skok do
    // zera to trzask w głośniku o pełnej amplitudzie.
    //
    // Kanału **nie** zwalniamy. Zwolnienie zatrzymuje nośną i pin idzie do
    // stanu niskiego, czyli robi dokładnie ten trzask, którego chwilę wcześniej
    // unikaliśmy. Pin oddaje aplikacja, gdy naprawdę potrzebuje go do czegoś
    // innego — i wtedy wie, czy głośnik jest jeszcze podłączony.
    (void)pwm_.setDutyPermille(pin_, 500);
    SampleSink::onStop();
}

Status PwmAudioSink::writeSample(u16 value) {
    return pwm_.setDutyPermille(pin_, value);
}

// ---------------------------------------------------------------------------
// DacAudioSink
// ---------------------------------------------------------------------------

Status DacAudioSink::onStart() {
    HYDRA_CHECK(dac_.enable(channel_));
    const u8 bits = dac_.resolutionBits();
    scale_ = bits >= 16 ? 0xFFFF : static_cast<u16>((1u << bits) - 1u);
    return ok();
}

void DacAudioSink::onStop() {
    (void)dac_.write(channel_, static_cast<u16>(scale_ / 2));   // cisza
    dac_.disable(channel_);
    SampleSink::onStop();
}

Status DacAudioSink::writeSample(u16 value) {
    return dac_.write(channel_, value);
}

// ---------------------------------------------------------------------------
// AdcAudioSource
// ---------------------------------------------------------------------------

Result<MediaFormat> AdcAudioSource::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    HYDRA_UNUSED(in);
    if (cfg_.sampleRate == 0) return unexpected(Err::NotInitialized);
    return MediaFormat::audio(cfg_.sampleRate, SampleFormat::S16, 1);
}

MemReq AdcAudioSource::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = static_cast<u32>(cfg_.framesPerBlock) * sizeof(i16);
    req.count = 3;
    return req;
}

Status AdcAudioSource::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    pool_ = pipeline.poolFor(*this, 0);
    return pool_ != nullptr ? ok() : fail(Err::OutOfMemory);
}

Status AdcAudioSource::onStart() {
    hal::AdcConfig adc;
    return adc_.configure(pin_, adc);
}

void AdcAudioSource::onStop() {
    if (current_.valid()) { pool_->release(current_); current_ = Block{}; }
    offset_ = 0;
}

void AdcAudioSource::flush() {
    if (!current_.valid() || offset_ == 0) return;

    current_.length = offset_;
    current_.pts = frames_ * 1000000ull / cfg_.sampleRate;
    frames_ += offset_ / sizeof(i16);

    Block evicted;
    if (!emit(0, current_, evicted)) {
        pool_->release(current_);
        pipeline_->raise(MediaFault::Overrun, *this, 0);
    } else if (evicted.valid()) {
        pool_->release(evicted);
    }
    current_ = Block{};
    offset_ = 0;
}

void AdcAudioSource::process(u64 nowUs) {
    if (!primed_) { lastUs_ = nowUs; primed_ = true; return; }

    const u64 elapsed = nowUs - lastUs_ + carryUs_;
    lastUs_ = nowUs;
    const u64 budget = elapsed * cfg_.sampleRate / 1000000ull;
    carryUs_ = elapsed - budget * 1000000ull / cfg_.sampleRate;

    for (u64 i = 0; i < budget; ++i) {
        if (!current_.valid()) {
            current_ = pool_->acquire();
            if (!current_.valid()) {
                pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
                carryUs_ = 0;
                return;
            }
            offset_ = 0;
        }

        auto raw = adc_.readRaw(pin_);
        if (!raw) continue;

        i32 sample = static_cast<i32>(*raw);
        if (cfg_.removeDc) {
            // Filtr górnoprzepustowy pierwszego rzędu na średniej bieżącej.
            // Bez tego cały sygnał siedzi przy połowie zakresu i po wzmocnieniu
            // natychmiast wychodzi poza skalę.
            if (!dcPrimed_) { dc_ = sample << 16; dcPrimed_ = true; }
            dc_ += ((sample << 16) - dc_) >> 10;
            sample -= (dc_ >> 16);
        } else {
            sample -= 2048;   // środek typowego 12-bitowego zakresu
        }

        // 12 bitów ADC → 16 bitów próbki.
        const i32 scaled = sample << 4;
        i16* out = reinterpret_cast<i16*>(current_.data + offset_);
        *out = static_cast<i16>(scaled > 32767 ? 32767 : (scaled < -32768 ? -32768 : scaled));
        offset_ += sizeof(i16);

        if (offset_ >= current_.capacity) flush();
    }
}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
