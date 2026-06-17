/**
 * CYDEspDash.ino — ESP-DASH real-time dashboard on Cheap Yellow Display
 *
 * Hardware: ESP32-2432S028 (CYD)
 *   TFT  : ILI9341  2.8" 320×240  SPI1  (SCK=14, MOSI=13, MISO=12, CS=15, DC=2, BL=21)
 *   Touch: XPT2046  resistive     SoftSPI (SCK=25, MOSI=32, MISO=39, CS=33, IRQ=36)
 *   LED  : RGB  GPIO 17/4/16  active-low
 *
 * Library: ESP-DASH (free) — https://github.com/ayushsharma82/ESP-DASH
 *   Dashboard cards: Temperature, Humidity, Status, Slider, Button, Chart (bar)
 *
 * Usage:
 *   1. Flash the sketch (WiFi creds from MinisConfig.h or hardcoded fallback)
 *   2. TFT shows "http://<IP>" — open it in any browser on the same network
 *   3. Tap the "LED" button on the dashboard to toggle the green LED
 *   4. Slider "Brightness" controls TFT backlight PWM (pin 21, 5 kHz, 8-bit)
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPDash.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ── CYD pins ──────────────────────────────────────────────────────────────────
#define PIN_TFT_CS   15
#define PIN_TFT_DC    2
#define PIN_TFT_MOSI 13
#define PIN_TFT_CLK  14
#define PIN_TFT_MISO 12
#define PIN_TFT_BL   21

#define PIN_LED_R    17   // active-low
#define PIN_LED_G     4
#define PIN_LED_B    16

// ── WiFi (MyCastle injects MinisConfig.h on deploy) ──────────────────────────
#if __has_include("MinisConfig.h")
  #include "MinisConfig.h"
#else
  #define MINIS_WIFI_SSID     "YourSSID"
  #define MINIS_WIFI_PASSWORD "YourPassword"
#endif

// ── Objects ───────────────────────────────────────────────────────────────────
Adafruit_ILI9341 tft(PIN_TFT_CS, PIN_TFT_DC,
                     PIN_TFT_MOSI, PIN_TFT_CLK,
                     /* rst */ -1, PIN_TFT_MISO);

AsyncWebServer server(80);
ESPDash        dashboard(&server);

// ── Dashboard cards ──────────────────────────────────────────────────────────
//   First arg  : pointer to dashboard
//   Second arg : card type constant
//   Third arg  : label shown in the browser
//   Fourth arg : unit (optional)
Card cardTemp    (&dashboard, TEMPERATURE_CARD, "Temperature",  "\xB0""C");
Card cardHum     (&dashboard, HUMIDITY_CARD,    "Humidity",     "%");
Card cardRssi    (&dashboard, GENERIC_CARD,     "WiFi Signal",  "dBm");
Card cardUptime  (&dashboard, GENERIC_CARD,     "Uptime",       "s");
Card cardStatus  (&dashboard, STATUS_CARD,      "Board");
Card cardLed     (&dashboard, BUTTON_CARD,      "Green LED");
Card cardBright  (&dashboard, SLIDER_CARD,      "Backlight",    "%", 10, 100);

// ── Bar chart — temperature history (last 10 readings) ───────────────────────
Chart chartTemp(&dashboard, BAR_CHART, "Temperature History");
static String chartLabels[10] = {"1","2","3","4","5","6","7","8","9","10"};
static int    chartData  [10] = {0};
static int    chartIdx = 0;

// ── State ─────────────────────────────────────────────────────────────────────
static bool    ledOn    = false;
static int     brightness = 80;   // %
static float   simTemp  = 22.5f;
static float   simHum   = 55.0f;

// ── TFT helpers ───────────────────────────────────────────────────────────────
static void tftSplash(const char* line) {
    tft.fillScreen(ILI9341_BLACK);

    tft.setTextColor(ILI9341_CYAN);
    tft.setTextSize(3);
    tft.setCursor(14, 28);
    tft.print("ESP-DASH");

    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(14, 76);
    tft.print("CYD Dashboard");

    tft.setTextColor(ILI9341_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(14, 116);
    tft.print(line);
}

static void tftShowReady(IPAddress ip) {
    tft.fillRect(0, 108, 320, 132, ILI9341_BLACK);

    tft.setTextColor(ILI9341_GREEN);
    tft.setTextSize(2);
    tft.setCursor(14, 116);
    tft.print("Open in browser:");

    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(14, 146);
    tft.print("http://");
    tft.print(ip);

    tft.setTextColor(ILI9341_DARKGREY);
    tft.setTextSize(1);
    tft.setCursor(14, 194);
    tft.print("Cards: Temp  Hum  RSSI  Uptime  LED  Backlight");
}

static void tftUpdateValues(int temp, int hum, int rssi) {
    // small status bar at the bottom
    tft.fillRect(0, 210, 320, 30, ILI9341_NAVY);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(8, 220);
    tft.printf("T:%d\xB0""C  H:%d%%  RSSI:%d dBm  LED:%s",
               temp, hum, rssi, ledOn ? "ON " : "OFF");
}

static void applyBrightness(int pct) {
    ledcWrite(PIN_TFT_BL, map(pct, 0, 100, 0, 255));
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // RGB LED — all off (active-low)
    const uint8_t ledPins[] = {PIN_LED_R, PIN_LED_G, PIN_LED_B};
    for (uint8_t p : ledPins) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); }

    // Backlight via PWM so the slider card works (ESP32 Arduino 3.x API)
    ledcAttach(PIN_TFT_BL, 5000, 8);
    applyBrightness(brightness);

    // Display
    tft.begin();
    tft.setRotation(1);   // landscape, USB on the right
    tftSplash("Connecting to WiFi...");

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD);
    Serial.print("WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());

    // Initial card values
    cardStatus.update("Online", "green");
    cardBright.update(brightness);

    // Button callback — toggle green LED
    cardLed.attachCallback([](int val) {
        ledOn = (val == 1);
        digitalWrite(PIN_LED_G, ledOn ? LOW : HIGH);
        cardLed.update(val);
        dashboard.sendUpdates();
        Serial.printf("LED %s\n", ledOn ? "ON" : "OFF");
    });

    // Slider callback — adjust backlight
    cardBright.attachCallback([](int val) {
        brightness = constrain(val, 10, 100);
        applyBrightness(brightness);
        cardBright.update(brightness);
        dashboard.sendUpdates();
        Serial.printf("Brightness %d%%\n", brightness);
    });

    // Init chart labels
    chartTemp.updateX(chartLabels, 10);

    server.begin();
    tftShowReady(WiFi.localIP());
    digitalWrite(PIN_LED_B, LOW);   // blue = WiFi connected
    Serial.println("ESP-DASH ready");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
static unsigned long lastUpdate = 0;

void loop() {
    if (millis() - lastUpdate < 2000) return;
    lastUpdate = millis();

    // Simulate slow sensor drift
    simTemp += (float)random(-8, 9) * 0.1f;
    simHum  += (float)random(-4, 5) * 0.1f;
    simTemp  = constrain(simTemp, 15.0f, 35.0f);
    simHum   = constrain(simHum,  30.0f, 80.0f);

    int t    = (int)simTemp;
    int h    = (int)simHum;
    int rssi = WiFi.RSSI();
    int up   = (int)(millis() / 1000);

    // Update scalar cards
    cardTemp.update(t);
    cardHum.update(h);
    cardRssi.update(rssi);
    cardUptime.update(up);

    // Rolling bar chart
    chartData[chartIdx] = t;
    chartIdx = (chartIdx + 1) % 10;
    chartTemp.updateY(chartData, 10);

    dashboard.sendUpdates();
    tftUpdateValues(t, h, rssi);
}
