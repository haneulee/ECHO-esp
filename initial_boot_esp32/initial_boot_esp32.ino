void setup() {
  // Initialize Serial at 115200
  Serial.begin(115200);
  
  // Wait for Serial port to connect. 
  // On some Super Minis, you need to open the monitor AFTER upload.
  delay(2000); 
  
  Serial.println("--------------------------------");
  Serial.println("ESP32-C3 Super Mini TEST SUCCESS");
  Serial.println("--------------------------------");
}

void loop() {
  Serial.print("Uptime: ");
  Serial.print(millis());
  Serial.println(" ms");
  delay(1000);
}