#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>

BLEAdvertising *pAdvertising;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting Module A...");

  // BLE device name
  BLEDevice::init("Creature_A");

  // Get advertising object
  pAdvertising = BLEDevice::getAdvertising();

  // Advertising payload
  BLEAdvertisementData advData;
  advData.setName("Creature_A");
  advData.setManufacturerData("CREA:A001");

  pAdvertising->setAdvertisementData(advData);

  // Optional: advertise a bit faster for easier demo detection
  pAdvertising->setMinInterval(0xA0);
  pAdvertising->setMaxInterval(0xA0);

  // Start advertising
  BLEDevice::startAdvertising();

  Serial.println("Module A advertising started");
}

void loop() {
  // nothing else needed
  delay(1000);
  Serial.println("Advertising...");
}