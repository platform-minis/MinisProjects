/**
 * CYDDashboard.ino — ESP-DASH real-time web dashboard on CYD
 *
 * Hardware: ESP32-2432S028 (Cheap Yellow Display)
 *   Display : ILI9341  2.8" 320×240, SPI1
 *   Touch   : XPT2046  resistive (unused in this sketch)
 *   LED     : RGB on GPIO 17/4/16, active-low
 *
 * What it does:
 *   1. Connects to WiFi
 *   2. Starts ESP-DASH on port 80
 *   3. Shows the dashboard URL on the TFT
 *   4. Updates four live cards every 2 s (simulated temp/humidity + real RSSI/uptime)
 *
 * Open http://<IP shown on display> in any browser on the same network.
 *
 * Library: ESP-DASH (free)  https://github.com/ayushsharma82/ESP-DASH
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPDash.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ── CYD pin definitions ───────────────────────────────────────────────────────
#define PIN_TFT_CS    15
#define PIN_TFT_DC     2
#define PIN_TFT_MOSI  13
#define PIN_TFT_CLK   14
#define PIN_TFT_MISO  12
#define PIN_TFT_BL    21   // backlight (HIGH = on)

#define PIN_LED_R     17   // RGB LED, active-low
#define PIN_LED_G      4
#define PIN_LED_B     16

// ── WiFi credentials ─────────────────────────────────────────────────────────
// MyCastle injects MinisConfig.h with MINIS_WIFI_SSID / MINIS_WIFI_PASSWORD.
// Fill in defaults for manual flashing.
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

AsyncWebServer  server(80);
ESPDash         dashboard(&server);

// ── Dashboard cards ──────────────────────────────────────────────────────────
Card temperature(&dashboard, TEMPERATURE_CARD, "Temperature", "\xB0""C");
Card humidity   (&dashboard, HUMIDITY_CARD,    "Humidity",    "%");
Card rssiCard   (&dashboard, GENERIC_CARD,     "WiFi Signal", "dBm");
Card uptimeCard (&dashboard, GENERIC_CARD,     "Uptime",      "s");

// ── TFT helpers ───────────────────────────────────────────────────────────────
static void tftSplash(const char* msg) {
    tft.fillScreen(ILI9341_BLACK);

    // Title
    tft.setTextColor(ILI9341_CYAN);
    tft.setTextSize(3);
    tft.setCursor(16, 30);
    tft.print("ESP-DASH");

    // Sub-title
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(16, 80);
    tft.print("CYD Dashboard");

    // Status line
    tft.setTextColor(ILI9341_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(16, 120);
    tft.print(msg);
}

static void tftShowReady(IPAddress ip) {
    // Clear lower half
    tft.fillRect(0, 110, 320, 130, ILI9341_BLACK);

    tft.setTextColor(ILI9341_GREEN);
    tft.setTextSize(2);
    tft.setCursor(16, 120);
    tft.print("Open in browser:");

    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(16, 150);
    tft.print("http://");
    tft.print(ip);

    tft.setTextColor(ILI9341_DARKGREY);
    tft.setTextSize(1);
    tft.setCursor(16, 195);
    tft.print("Updates every 2 s");
    tft.setCursor(16, 210);
    tft.print("Temp/Humidity are simulated");
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // RGB LED off (active-low)
    pinMode(PIN_LED_R, OUTPUT); digitalWrite(PIN_LED_R, HIGH);
    pinMode(PIN_LED_G, OUTPUT); digitalWrite(PIN_LED_G, HIGH);
    pinMode(PIN_LED_B, OUTPUT); digitalWrite(PIN_LED_B, HIGH);

    // Backlight + display
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
    tft.begin();
    tft.setRotation(1);   // landscape, USB connector on the right

    tftSplash("Connecting to WiFi...");

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD);
    Serial.print("WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());

    // Start dashboard
    server.begin();

    tftShowReady(WiFi.localIP());
    digitalWrite(PIN_LED_G, LOW);   // green LED = ready
    Serial.println("ESP-DASH ready");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
static unsigned long lastUpdate = 0;
static float simTemp = 22.5f;
static float simHum  = 55.0f;

void loop() {
    if (millis() - lastUpdate < 2000) return;
    lastUpdate = millis();

    // Simulate slow sensor drift
    simTemp += (float)(random(-10, 11)) * 0.1f;
    simHum  += (float)(random(-5,  6))  * 0.1f;
    simTemp = constrain(simTemp, 15.0f, 35.0f);
    simHum  = constrain(simHum,  30.0f, 80.0f);

    temperature.update((int)simTemp);
    humidity.update((int)simHum);
    rssiCard.update(WiFi.RSSI());
    uptimeCard.update((int)(millis() / 1000));

    dashboard.sendUpdates();
}
