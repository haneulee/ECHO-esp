
#include <Wire.h>
#define LED_PIN 8 

void setup() {
  // Initialize Serial at 115200
  Serial.begin(115200);
  
  // Wait for Serial port to connect. 
  // On some Super Minis, you need to open the monitor AFTER upload.
  delay(2000); 
  
  Serial.println("--------------------------------");
  Serial.println("ESP32-C3 Super Mini TEST SUCCESS");
  Serial.println("--------------------------------");
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  Serial.print("Uptime: ");
  Serial.print(millis());
  Serial.println(" ms");
  
  digitalWrite(LED_PIN, HIGH);  // change state of the LED by setting the pin to the HIGH voltage level
  delay(1000);                      // wait for a second
  digitalWrite(LED_PIN, LOW);   // change state of the LED by setting the pin to the LOW voltage level
  delay(1000);
}