// ESP32-S3 Zero uses native USB (no USB-UART bridge).
// Serial = USB CDC — wait for host to connect before printing.

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);  // wait up to 3s for USB CDC

  Serial.println("ESP32-S3 Zero - Hello!");
  Serial.printf("CPU freq:   %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Flash size: %d MB\n",  ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("Free heap:  %d bytes\n", ESP.getFreeHeap());

#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM)
  Serial.printf("PSRAM size: %d MB\n", ESP.getPsramSize() / (1024 * 1024));
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
#else
  Serial.println("PSRAM:      not detected");
#endif
}

void loop() {
  Serial.println("Running...");
  delay(1000);
}
