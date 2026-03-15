void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);  // wait for USB CDC to connect
  Serial.println("ESP32-S3 Test - Hello!");
  Serial.printf("CPU freq: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Flash size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
}

void loop() {
  Serial.println("Running...");
  delay(1000);
}
