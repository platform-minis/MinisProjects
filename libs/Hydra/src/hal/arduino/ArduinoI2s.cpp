/**
 * Hydra — backend I2S dla ESP32 (rdzeń Arduino / ESP-IDF).
 *
 * Backend **kopiujący**, i to jest tu najważniejsze do zrozumienia. ESP-IDF
 * nie oddaje własności buforów DMA — daje `i2s_write()`/`i2s_read()`, które
 * przepisują dane do własnego pierścienia. Interfejs `II2s` na to pozwala:
 * `submit()` przyjmuje bufor, a `reclaim()` oddaje go, gdy przepisanie się
 * skończy. Warstwa wyżej nie zakłada, *kiedy* bufor wróci — zakłada tylko,
 * że wróci.
 *
 * Przepisanie bywa częściowe: przy zerowym czasie oczekiwania kontroler
 * przyjmuje tyle, ile ma miejsca w pierścieniu. Dlatego każdy wpis pamięta,
 * ile z niego już poszło, a resztę dosyłamy przy kolejnych wywołaniach
 * `reclaim()`. Alternatywą byłoby czekanie w `submit()` — czyli blokada taska
 * audio na czas transferu, dokładnie to, czego całe to API unika.
 *
 * Tylko ESP32. Na RP2040 I2S robi się przez PIO, a na STM32 przez SAI —
 * oba wymagają innego sterownika, nie innej gałęzi w tym pliku.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST && HYDRA_PLAT_ESP32

#include "hydra/hal/II2s.hpp"

#include <driver/i2s.h>

#include <string.h>

namespace hydra {
namespace hal {
namespace arduino {

class ArduinoI2s : public II2s {
public:
    static constexpr u8 kMaxQueued = 6;

    Status begin(const I2sConfig& cfg) override {
        if (running_) end();

        i2s_config_t driver = {};
        driver.mode = static_cast<i2s_mode_t>(
            I2S_MODE_MASTER | (cfg.direction == I2sDirection::Rx ? I2S_MODE_RX : I2S_MODE_TX));
        if (cfg.direction == I2sDirection::Duplex) {
            driver.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
        }
        if (cfg.standard == I2sStandard::Pdm) {
            driver.mode = static_cast<i2s_mode_t>(driver.mode | I2S_MODE_PDM);
        }

        driver.sample_rate = static_cast<int>(cfg.sampleRate);
        driver.bits_per_sample = static_cast<i2s_bits_per_sample_t>(cfg.bitsPerSample);
        driver.channel_format = cfg.channels == 1 ? I2S_CHANNEL_FMT_ONLY_LEFT
                                                  : I2S_CHANNEL_FMT_RIGHT_LEFT;
        driver.communication_format = cfg.standard == I2sStandard::LeftJustified
                                          ? I2S_COMM_FORMAT_STAND_MSB
                                          : I2S_COMM_FORMAT_STAND_I2S;
        driver.intr_alloc_flags = 0;
        // Osiem buforów po 256 ramek to około 128 ms zapasu przy 16 kHz.
        // Mniej oznacza xrun przy pierwszym dłuższym przerwaniu; więcej —
        // opóźnienie, którego nie da się nadrobić.
        driver.dma_buf_count = 8;
        driver.dma_buf_len = 256;
        driver.use_apll = false;
        driver.tx_desc_auto_clear = true;

        // Bez logowania: HAL nie sięga po rdzeń aplikacyjny (reguła 1
        // z check_includes.sh). Powód niepowodzenia niesie kod błędu,
        // a komunikat wypisuje ten, kto go odbiera — element potoku zna
        // przy tym kontekst, którego sterownik nie ma.
        if (i2s_driver_install(port_, &driver, 0, nullptr) != ESP_OK) {
            return fail(Err::IoError);
        }

        i2s_pin_config_t pins = {};
        pins.mck_io_num   = cfg.mclk == kNoPin ? I2S_PIN_NO_CHANGE : cfg.mclk;
        pins.bck_io_num   = cfg.bclk == kNoPin ? I2S_PIN_NO_CHANGE : cfg.bclk;
        pins.ws_io_num    = cfg.ws   == kNoPin ? I2S_PIN_NO_CHANGE : cfg.ws;
        pins.data_out_num = cfg.dout == kNoPin ? I2S_PIN_NO_CHANGE : cfg.dout;
        pins.data_in_num  = cfg.din  == kNoPin ? I2S_PIN_NO_CHANGE : cfg.din;

        if (i2s_set_pin(port_, &pins) != ESP_OK) {
            i2s_driver_uninstall(port_);
            return fail(Err::BadArgument);
        }

        cfg_ = cfg;
        running_ = true;
        for (auto& slot : slots_) slot.used = false;
        return ok();
    }

    void end() override {
        if (!running_) return;
        i2s_driver_uninstall(port_);
        running_ = false;
        for (auto& slot : slots_) slot.used = false;
    }

    bool running() const override { return running_; }

    Status submit(ByteSpan buffer) override {
        if (!running_) return fail(Err::NotInitialized);
        for (auto& slot : slots_) {
            if (slot.used) continue;
            slot.buffer = buffer;
            slot.done   = 0;
            slot.used   = true;
            pump(slot);
            return ok();
        }
        return fail(Err::Busy);
    }

    bool reclaim(ByteSpan& buffer, u32& bytes) override {
        for (auto& slot : slots_) {
            if (!slot.used) continue;
            pump(slot);
            if (slot.done < slot.buffer.size()) continue;

            buffer = slot.buffer;
            bytes  = static_cast<u32>(slot.done);
            slot.used = false;
            return true;
        }
        return false;
    }

    u8  queueDepth() const override { return kMaxQueued; }
    u32 xruns() const override { return xruns_; }
    u32 actualSampleRate() const override {
        // Dzielnik zegara rzadko trafia dokładnie; sterownik zna wartość
        // faktyczną i warto ją pokazać, bo z niej wynika dryf względem
        // drugiego urządzenia na tej samej magistrali.
        return running_ ? static_cast<u32>(i2s_get_clk(port_)) : 0;
    }

private:
    struct Slot {
        ByteSpan buffer{};
        size_t   done = 0;
        bool     used = false;
    };

    /** Dosyła to, co zostało z bufora. Bez czekania. */
    void pump(Slot& slot) {
        if (slot.done >= slot.buffer.size()) return;

        size_t moved = 0;
        u8* at = slot.buffer.data() + slot.done;
        const size_t left = slot.buffer.size() - slot.done;

        const esp_err_t r = (cfg_.direction == I2sDirection::Rx)
                                ? i2s_read(port_, at, left, &moved, 0)
                                : i2s_write(port_, at, left, &moved, 0);
        if (r != ESP_OK) return;

        // Zero przeniesionych bajtów przy niepustym buforze oznacza pierścień
        // pełny (Tx) albo pusty (Rx) — czyli że nie nadążamy. To jest xrun
        // i tak trzeba go liczyć, bo sterownik ESP-IDF sam go nie zgłasza.
        if (moved == 0) ++xruns_;
        slot.done += moved;
    }

    i2s_port_t port_ = I2S_NUM_0;
    I2sConfig  cfg_{};
    bool       running_ = false;
    u32        xruns_ = 0;
    Slot       slots_[kMaxQueued];
};

/** Jedyna instancja — kontroler jest jeden i współdzielenie go nie ma sensu. */
II2s& i2sBackend() {
    static ArduinoI2s instance;
    return instance;
}

}  // namespace arduino
}  // namespace hal
}  // namespace hydra

#endif  // !HYDRA_PLAT_HOST && HYDRA_PLAT_ESP32
