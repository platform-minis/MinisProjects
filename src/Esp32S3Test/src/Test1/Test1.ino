void setup() {
  Serial.begin(115200);

  // HWCDC (native USB) needs time to enumerate on the host
  // Without this delay, early Serial.print() is lost
  delay(2000);

  Serial.println("ESP32-S3 Test - Hello!");
  Serial.printf("CPU freq: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Flash size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
}

void loop() {
  Serial.println("Running...");
  delay(1000);
}
