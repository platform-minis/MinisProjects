/**
 * Hydra — przykład: smart-display.
 *
 * Kryterium ukończenia etapu M4 (rozdz. 14): ekran statusu na ESP32-S3 z panelem
 * IPS i na RP2350 z monochromatycznym OLED-em. Ten sam plik obsługuje oba —
 * różni je wyłącznie motyw i rozmiar, wybierane raz, przy starcie.
 *
 * W kodzie nie ma pętli odświeżania ani ani jednego wywołania „przerysuj".
 * Widżety są związane ze zdarzeniami magistrali i aktualizują się same;
 * renderer przerysowuje wyłącznie to, co naprawdę się zmieniło.
 *
 * Panel podłącza się przez dowolny adapter z gfx/adapters/ — poniżej wariant
 * z buforem w pamięci, który działa także w buildzie hostowym. Dla prawdziwego
 * wyświetlacza wystarczy podmienić te kilka linii:
 *
 *     #include <TFT_eSPI.h>
 *     #include <hydra/gfx/adapters/TftEspiSurface.hpp>
 *     TFT_eSPI tft;
 *     hydra::gfx::TftEspiSurface<TFT_eSPI> surface(tft);
 *     hydra::ui::SurfaceDisplay display(surface, "st7789");
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
// Deklaracje setup() i loop(). Potrzebne, bo STM32duino umieszcza je w bloku
// extern "C" — bez tej deklaracji definicje poniżej dostają wiązanie C++
// i konsolidator ich nie znajduje. Na ESP32 i RP2040 deklaracje są zwykłe,
// więc włączenie niczego nie zmienia.
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include <stdio.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/gfx/Framebuffer.hpp"
#include "hydra/ui/Binding.hpp"
#include "hydra/ui/UiModule.hpp"
#include "hydra/ui/Widgets.hpp"

#if HYDRA_ENABLE_NET
#  include "hydra/net/NetTypes.hpp"
#endif

HYDRA_LOG_MODULE("display");

using namespace hydra;
using namespace hydra::ui;

namespace {

// --- panel -----------------------------------------------------------------

constexpr i16 kWidth  = 128;
constexpr i16 kHeight = 64;

u8               gVram[gfx::Framebuffer::bytesNeeded(kWidth, kHeight,
                                                     gfx::PixelFormat::Mono1)];
gfx::Framebuffer gSurface;
SurfaceDisplay   gDisplay(gSurface, "oled");

// --- interfejs -------------------------------------------------------------

UiModule    gUi(gDisplay);
BindingHub  gBindings(gUi.queue());
UartLogSink gConsole;

Screen gHome("home");

Label            gTitle("rover-01");
Label            gTemperature("--");
BatteryIndicator gBattery;
SignalBars       gSignal;
Sparkline        gChart;

/**
 * Motyw dobrany do panelu. Na wyświetlaczu jednobitowym kolorowa paleta
 * nic nie znaczy — widżety odróżniają stany kształtem.
 */
Theme themeFor(const gfx::ISurface& surface) {
    if (surface.pixelFormat() == gfx::PixelFormat::Mono1) return Theme::mono(1);
    // Duży panel IPS: skala 2× daje czytelny tekst z odległości ramienia.
    return surface.width() >= 240 ? Theme::dark(2) : Theme::dark(1);
}

Theme gTheme;

void layout() {
    const i16 line = gTheme.lineHeight();
    const i16 pad  = gTheme.padding;

    // Pasek górny: nazwa urządzenia, siła sygnału, bateria.
    gTitle.setBounds(Rect(pad, 0, static_cast<i16>(kWidth / 2), line));
    gSignal.setBounds(Rect(static_cast<i16>(kWidth - 44), 1, 16, static_cast<i16>(line - 2)));
    gBattery.setBounds(Rect(static_cast<i16>(kWidth - 24), 1, 22, static_cast<i16>(line - 2)));

    // Wartość bieżąca i przebieg pod nią.
    gTemperature.setBounds(Rect(pad, static_cast<i16>(line + pad),
                                static_cast<i16>(kWidth - 2 * pad), line));
    gTemperature.setAlign(Align::Center);

    gChart.setBounds(Rect(pad, static_cast<i16>(2 * line + 2 * pad),
                          static_cast<i16>(kWidth - 2 * pad),
                          static_cast<i16>(kHeight - 2 * line - 3 * pad)));
}

}  // namespace

void setup() {
    gSurface.attach(ByteSpan{gVram, sizeof(gVram)}, kWidth, kHeight,
                    gfx::PixelFormat::Mono1);
    gTheme = themeFor(gSurface);
    layout();

    gHome.add(gTitle);
    gHome.add(gSignal);
    gHome.add(gBattery);
    gHome.add(gTemperature);
    gHome.add(gChart);

    UiModule::Config uiCfg;
    uiCfg.framePeriodMs = 33;  // około 30 klatek na sekundę
    gUi.configure(uiCfg);

    // Renderer rysuje bieżący ekran; poza tym nie ma tu żadnej logiki.
    gUi.renderer().setDraw([](gfx::ISurface& surface, Rect area) {
        gHome.draw(surface, gTheme, area);
    });

    App::config()
        .name("smart-display")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gUi);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return;
    }

    // --- wiązania: od tej chwili ekran aktualizuje się sam -------------------

    // Puls systemu zasila wykres i wskaźnik baterii. W prawdziwym urządzeniu
    // byłyby to zdarzenia z modułu czujników i z pomiaru napięcia.
    gBindings.bind<Sparkline, SysHeartbeat>(
        gChart, [](Sparkline& chart, const SysHeartbeat& e) {
            chart.push(static_cast<float>(e.freeHeapBytes) / 1024.0f);
        });

    gBindings.bind<Label, SysHeartbeat>(
        gTemperature, [](Label& label, const SysHeartbeat& e) {
            label.setValue(static_cast<float>(e.uptimeMs) / 1000.0f, 0, "s");
        });

    gBindings.bind<BatteryIndicator, SysHeartbeat>(
        gBattery, [](BatteryIndicator& battery, const SysHeartbeat& e) {
            // Zastępczo: procent z czasu pracy. Docelowo z pomiaru napięcia
            // przez IAdc z uwzględnieniem dzielnika.
            battery.setPercent(static_cast<u8>(100 - (e.uptimeMs / 1000) % 100));
        });

    // Widżety sieciowe reagują na zdarzenia modułu net, o ile jest włączony.
#if HYDRA_ENABLE_NET
    gBindings.bind<SignalBars, net::NetGotAddress>(
        gSignal, [](SignalBars& bars, const net::NetGotAddress& e) {
            bars.setConnected(true);
            bars.setRssi(e.rssiDbm);
        });
    gBindings.bind<SignalBars, net::NetLost>(
        gSignal, [](SignalBars& bars, const net::NetLost&) { bars.setConnected(false); });
#endif

    // Zmiana ekranu i pierwsze rysowanie.
    gUi.renderer().invalidate();
    HYDRA_LOGI("ekran %dx%d, motyw %s", kWidth, kHeight,
               gTheme.monochrome ? "monochromatyczny" : "kolorowy");
}

void loop() {}
