#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

#define LED_PIN 7
#define BUZZER_PIN 8

BLEScan* pBLEScan;
float smoothRSSI = -100;

void setup() {
  Serial.begin(115200);
  delay(1000);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_PIN, HIGH); // LED OFF
  noTone(BUZZER_PIN);
}

void loop() {
  BLEScanResults* results = pBLEScan->start(1, false);

  int rssi = -100;
  bool found = false;

  for (int i = 0; i < results->getCount(); i++) {
    BLEAdvertisedDevice device = results->getDevice(i);
    String name = device.getName().c_str();

    if (name == "Creature_A") {
      rssi = device.getRSSI();
      found = true;
      Serial.print("Raw RSSI: ");
      Serial.println(rssi);
    }
  }

  pBLEScan->clearResults();

  if (found) {
    // 평균화
    smoothRSSI = smoothRSSI * 0.8 + rssi * 0.2;
  }

  Serial.print("Smooth RSSI: ");
  Serial.println(smoothRSSI);

  // 너무 멀면 소리 끔
  if (!found || smoothRSSI < -72) {
    noTone(BUZZER_PIN);
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    return;
  }

  int rssiClamped = constrain((int)smoothRSSI, -70, -20);
  int frequency = map(rssiClamped, -70, -20, 500, 1600);

  Serial.print("Frequency: ");
  Serial.println(frequency);

  tone(BUZZER_PIN, frequency);
  digitalWrite(LED_PIN, LOW);

  delay(50);
}