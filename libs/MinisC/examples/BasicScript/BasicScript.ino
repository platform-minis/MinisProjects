#include <MinisC.h>
#include <PubSubClient.h>
#include <WiFi.h>

// ── Native function implementations ──────────────────────────────────────────

Value native_gpio_write(Value* a, uint8_t n) {
    pinMode(a[0].i, OUTPUT);
    digitalWrite(a[0].i, a[1].b || a[1].i ? HIGH : LOW);
    return Value::makeNull();
}
Value native_gpio_read(Value* a, uint8_t n) {
    return Value::makeInt(digitalRead(a[0].i));
}
Value native_analog_read(Value* a, uint8_t n) {
    return Value::makeInt(analogRead(a[0].i));
}
Value native_delay_ms(Value* a, uint8_t n) {
    delay((uint32_t)a[0].i);
    return Value::makeNull();
}
Value native_millis(Value* a, uint8_t n) {
    return Value::makeInt((int32_t)millis());
}
Value native_print(Value* a, uint8_t n) {
    switch (a[0].type) {
        case T_INT:    Serial.println(a[0].i);              break;
        case T_FLOAT:  Serial.println(a[0].f, 4);           break;
        case T_BOOL:   Serial.println(a[0].b ? "true":"false"); break;
        case T_STRING: Serial.println(/* get_str needs VM ref — use workaround */ "?"); break;
        default:       Serial.println("null");               break;
    }
    return Value::makeNull();
}
Value native_println_int(Value* a, uint8_t n) {
    Serial.println(a[0].i);
    return Value::makeNull();
}
Value native_println_float(Value* a, uint8_t n) {
    Serial.println(a[0].f, 4);
    return Value::makeNull();
}

// ── MQTT client (for OTA script upload) ──────────────────────────────────────

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
MinisVM      vm;

const char* WIFI_SSID   = "YourSSID";
const char* WIFI_PASS   = "YourPassword";
const char* MQTT_SERVER = "192.168.0.207";
const char* TOPIC_SCRIPT = "minis/user/device/script";
const char* TOPIC_ERROR  = "minis/user/device/script/error";

// Buffer for incoming bytecode (stored in PSRAM if available)
static uint8_t* script_buf = nullptr;
static size_t   script_len = 0;

void on_mqtt_message(char* topic, byte* payload, unsigned int len) {
    if (strcmp(topic, TOPIC_SCRIPT) != 0) return;

    Serial.printf("[MinisC] received bytecode: %u bytes\n", len);

    // Copy to heap buffer
    if (script_buf) free(script_buf);
    script_buf = (uint8_t*)malloc(len);
    if (!script_buf) { Serial.println("[MinisC] malloc failed"); return; }
    memcpy(script_buf, payload, len);
    script_len = len;

    vm.reset();
    if (!vm.load(script_buf, script_len)) {
        Serial.printf("[MinisC] load error: %s\n", vm.lastError());
        mqtt.publish(TOPIC_ERROR, vm.lastError());
    } else {
        Serial.println("[MinisC] script loaded OK");
    }
}

void setup_vm() {
    // Register native functions — indices must match compiler's natives.ts
    vm.registerNative("gpio_write",   native_gpio_write,   2, T_NULL);
    vm.registerNative("gpio_read",    native_gpio_read,    1, T_INT);
    vm.registerNative("analog_read",  native_analog_read,  1, T_INT);
    vm.registerNative("delay",        native_delay_ms,     1, T_NULL);
    vm.registerNative("millis",       native_millis,       0, T_INT);
    vm.registerNative("print_int",    native_println_int,  1, T_NULL);
    vm.registerNative("print_float",  native_println_float,1, T_NULL);
}

void connect_mqtt() {
    while (!mqtt.connected()) {
        Serial.print("[MQTT] connecting...");
        if (mqtt.connect("minisc-device")) {
            mqtt.subscribe(TOPIC_SCRIPT);
            Serial.println(" OK");
        } else {
            Serial.printf(" failed rc=%d, retry in 5s\n", mqtt.state());
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("\n[WiFi] connected: %s\n", WiFi.localIP().toString().c_str());

    mqtt.setServer(MQTT_SERVER, 1883);
    mqtt.setCallback(on_mqtt_message);
    mqtt.setBufferSize(4096);   // allow larger bytecode payloads

    setup_vm();
    connect_mqtt();
}

void loop() {
    if (!mqtt.connected()) connect_mqtt();
    mqtt.loop();

    // Run VM in slices — 50k cycles keeps loop() responsive
    if (vm.isLoaded()) {
        MinisVM::Result r = vm.run(50000);
        if (r == MinisVM::Result::ERROR) {
            Serial.printf("[MinisC] runtime error: %s\n", vm.lastError());
            mqtt.publish(TOPIC_ERROR, vm.lastError());
            vm.reset();
        }
    }
}
