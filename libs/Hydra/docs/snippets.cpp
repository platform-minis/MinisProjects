/**
 * Fragmenty kodu z docs/api.md, w postaci kompilowalnej.
 *
 * Dokumentacja opisująca nieistniejące funkcje jest gorsza niż jej brak:
 * wygląda wiarygodnie i kosztuje godzinę, zanim czytelnik zorientuje się,
 * że problem jest w tekście, a nie w jego kodzie. Ten plik jest budowany
 * przez `make docs` i pilnuje, żeby każdy przykład z api.md odpowiadał
 * rzeczywistym sygnaturom.
 *
 * Zmieniasz przykład w dokumentacji — zmień go też tutaj.
 */

#include <string.h>

#include "Hydra.h"

#include "hydra/core/LogSinks.hpp"
#include "hydra/drivers/sense/Bme280.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/net/NetModule.hpp"
#include "hydra/net/TelemetryBridge.hpp"
#include "hydra/net/TlsClient.hpp"
#include "hydra/sense/SensorHub.hpp"
#include "hydra/ui/Binding.hpp"
#include "hydra/ui/RenderQueue.hpp"
#include "hydra/ui/Widgets.hpp"

HYDRA_LOG_MODULE("docs")

using namespace hydra;

// --- Błędy -----------------------------------------------------------------

Result<u16> readAdc(hal::PinNum pin);

Status snippetErrors(hal::PinNum pin) {
    Result<u16> raw = readAdc(pin);
    if (!raw) return fail(raw.error());
    HYDRA_UNUSED(*raw);

    HYDRA_TRY(u16 value, readAdc(pin));
    HYDRA_UNUSED(value);
    HYDRA_CHECK(hal::Hal::storage().commit());
    return ok();
}

// --- Moduł własny ----------------------------------------------------------

class MyModule : public ModuleBase {
public:
    MyModule() : ModuleBase("my") {}

protected:
    Status onInit() override { return ok(); }
    Status onStart() override {
        Task::Cfg cfg;
        cfg.name = "my.tick";
        cfg.prio = Prio::Normal;
        return task_.startPeriodic(cfg, 100, [this] { tick(); });
    }
    void onStop() override { task_.stopAndWait(); }

private:
    void tick() {}
    Task task_;
};

namespace {
MyModule    gMy;
UartLogSink gConsole;
}  // namespace

Status snippetStartup() {
    App::config()
        .name("czujnik-salon")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gMy);

    if (auto r = App::begin(); !r) return fail(r.error());
    HYDRA_LOGI("gotowe");
    return ok();
}

// --- Zdarzenia -------------------------------------------------------------

struct Reading {
    u8    sensorId;
    float value;
};
HYDRA_DECLARE_EVENT(Reading, "docs/reading")

void snippetEvents() {
    EventBus::subscribe<Reading>([](const Reading&) {});
    EventBus::publish(Reading{1, 21.5f});
    EventBus::publishFromIsr(Reading{1, 21.5f});
}

// --- HAL -------------------------------------------------------------------

Status snippetHal() {
    hal::Hal::gpio().configure(hal::board::led, hal::PinMode::Output);
    hal::Hal::gpio().write(hal::board::led, true);

    if (hal::Hal::hasI2c(0)) {
        u8 id = 0;
        HYDRA_CHECK(hal::Hal::i2c(0).transaction([&](hal::II2cBus::Session& s) {
            return s.readReg(drivers::Bme280::kDefaultAddress,
                             drivers::Bme280::RegChipId, ByteSpan{&id, 1});
        }));
    }
    return ok();
}

// --- Czujniki --------------------------------------------------------------

#if HYDRA_ENABLE_SENSE
namespace {
drivers::Bme280   gBme;
sense::SensorHub  gHub;
}  // namespace

Status snippetSense() {
    sense::SensorHub::Registration weather;
    weather.sensor.periodMs     = 2000;
    weather.sensor.address      = drivers::Bme280::kDefaultAddress;
    weather.filter.kind         = sense::FilterKind::Ema;
    weather.filter.emaAlpha     = 0.3f;
    weather.anomaly.minValue    = -40.0f;
    weather.anomaly.maxValue    = 85.0f;
    weather.anomaly.frozenLimit = 10;

    HYDRA_CHECK(gHub.add(gBme, weather));
    return ok();
}
#endif

// --- Sieć ------------------------------------------------------------------

#if HYDRA_ENABLE_NET
// Dostarczane przez prawdziwą aplikację; tutaj tylko deklaracje, bo plik
// jest wyłącznie sprawdzany składniowo. Poza anonimową przestrzenią, żeby
// kompilator nie zgłaszał braku definicji dla symbolu wewnętrznego.
net::INetworkInterface& docsIface();
int formatSample(const sense::Sample& in, char* out, size_t cap);

namespace {
net::NetModule       gNet(docsIface());
net::TelemetryBridge gBridge(gNet.mqtt());
}  // namespace

Status snippetNet() {
    net::NetModule::Config cfg;
    cfg.mqtt.clientId    = "rover-01";
    cfg.mqtt.host        = "broker.local";
    cfg.mqtt.port        = 1883;
    cfg.mqtt.willTopic   = "hydra/rover-01/status";
    cfg.mqtt.willPayload = "offline";
    cfg.mqtt.willRetain  = true;
    cfg.mdnsHostname     = "rover-01";
    HYDRA_CHECK(gNet.configure(cfg));

    net::NetworkCredentials home;
    strncpy(home.ssid, "SSID", sizeof(home.ssid) - 1);
    home.psk.set("hasło");
    HYDRA_CHECK(gNet.connection().addNetwork(home));

    HYDRA_CHECK(gBridge.publishOn<sense::Sample>("dom/salon/temp", 0, false,
                                                 formatSample));
    return ok();
}

/** Atrapa o kształcie WiFiClientSecure — w prawdziwym kodzie typ z platformy. */
struct DocsSecureClient {
    void   setCACert(const char*) {}
    void   setInsecure() {}
    void   setCertificate(const char*) {}
    void   setPrivateKey(const char*) {}
    bool   connect(const char*, u16, i32) { return true; }
    void   stop() {}
    bool   connected() { return true; }
    size_t write(const u8*, size_t n) { return n; }
    int    read(u8*, size_t) { return 0; }
    int    available() { return 0; }
};

extern const char* kIsrgRootX1;

Status snippetTls() {
    DocsSecureClient                secure;
    net::TlsClient<DocsSecureClient> tls(secure);

    net::TlsClient<DocsSecureClient>::Config cfg;
    cfg.caCertificate = kIsrgRootX1;
    HYDRA_CHECK(tls.configure(cfg));

    net::IClient& asClient = tls;
    HYDRA_UNUSED(asClient);
    return ok();
}
#endif

// --- Interfejs -------------------------------------------------------------

#if HYDRA_ENABLE_UI
ui::RenderQueue& docsQueue();   // w prawdziwym kodzie: gUi.queue()

namespace {
ui::Screen     gHome("home");
ui::Label      gTemperature("--");
ui::BindingHub gBindings(docsQueue());
}  // namespace

Status snippetUi() {
    gTemperature.setBounds(gfx::Rect(4, 20, 120, 16));
    HYDRA_CHECK(gHome.add(gTemperature));

    // Przecinek w liście argumentów szablonu zmyliłby preprocesor, więc
    // wynik trafia najpierw do zmiennej.
    auto bound = gBindings.bind<ui::Label, sense::Sample>(
        gTemperature, [](ui::Label& label, const sense::Sample& e) {
            label.setValue(e.first(), 1, "°C");
        });
    HYDRA_CHECK(bound);
    return ok();
}
#endif
