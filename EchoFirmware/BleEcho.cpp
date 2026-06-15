#include "BleEcho.h"
#include "AudioSynth.h"
#include "EncounterLog.h"
#include "SonicAdvert.h"

// =====================================================
// BLE GLOBALS
// =====================================================

NimBLEAdvertising* pAdvertising = nullptr;
NimBLEScan* pScan = nullptr;

TrackedDevice devices[MAX_DEVICES];

// Used by station upload blocking scan.
// Defined here because BLE scan callback needs to read it.
volatile bool sBlockingStationScan = false;

// =====================================================
// DEBUG GLOBAL
// =====================================================

unsigned long lastDebugPrint = 0;

// =====================================================
// DEVICE HELPERS
// =====================================================

int findDevice(String name) {

  for (int i = 0; i < MAX_DEVICES; i++) {

    if (
      devices[i].active &&
      devices[i].name == name
    ) {
      return i;
    }
  }

  return -1;
}

int freeSlot() {

  for (int i = 0; i < MAX_DEVICES; i++) {

    if (!devices[i].active) {
      return i;
    }
  }

  return -1;
}

void clearDevice(int i) {

  devices[i].active = false;
  devices[i].name = "";
  devices[i].type = "";
  devices[i].lastSeen = 0;
  devices[i].veryCloseStartMs = 0;
  devices[i].evolutionDoneForSession = false;
  devices[i].nextNoteMs = 0;
  devices[i].arpTriggerIndex = 0;
  devices[i].seenAtMs = 0;
  devices[i].peerSonic.valid = false;
  devices[i].peerSonic.fromBle = false;

  resetPeerVoice(i);
}

void clearAllTrackedEchoPeers() {

  for (int i = 0; i < MAX_DEVICES; i++) {

    if (devices[i].active) {
      clearDevice(i);
    }
  }
}

// =====================================================
// BLE SCAN RESTORE
// =====================================================

void resumeEchoAdvertising() {

  if (pAdvertising == nullptr) {
    return;
  }

  refreshSonicAdvertising();

  delay(30);

  pAdvertising->start();
}

void restoreEchoBlePeerScan(
  const char* reason
) {

  if (pScan == nullptr) {
    return;
  }

  const bool undocking =
    (reason != nullptr) &&
    (strcmp(reason, "undock") == 0);

  if (dockLatched && !undocking) {

    if (reason != nullptr) {

      Serial.print("BLE peer scan paused while docked: ");
      Serial.println(reason);
    }

    return;
  }

  pScan->stop();

  if (pAdvertising != nullptr) {
    pAdvertising->stop();
  }

  delay(undocking ? 220 : 120);

  pScan->clearResults();

  refreshSonicAdvertising();

  if (pAdvertising != nullptr) {
    pAdvertising->start();
  }

  pScan->setActiveScan(false);

  pScan->setInterval(45);
  pScan->setWindow(45);

  pScan->setDuplicateFilter(0);

  pScan->start(0, false, true);

  if (reason != nullptr) {

    Serial.print("BLE peer scan restored: ");
    Serial.println(reason);
  }
}

// =====================================================
// BLE SCAN CALLBACK
// =====================================================

class MyScanCallbacks : public NimBLEScanCallbacks {

  void onResult(
    const NimBLEAdvertisedDevice* advertisedDevice
  ) override {

    if (dockLatched) {
      return;
    }

    std::string devNameStd =
      advertisedDevice->getName();

    if (devNameStd.empty()) {
      return;
    }

    String name =
      String(devNameStd.c_str());

    if (!name.startsWith("ECHO_")) {
      return;
    }

    if (name == MY_NAME) {
      return;
    }

    if (name == STATION_NAME) {
      return;
    }

    int rssi =
      advertisedDevice->getRSSI();

    String type = "UNKNOWN";

    if (name.indexOf("BOUNCE") >= 0) {
      type = "BOUNCE";
    }

    if (name.indexOf("SHY") >= 0) {
      type = "SHY";
    }

    if (name.indexOf("MESSY") >= 0) {
      type = "MESSY";
    }

    int idx =
      findDevice(name);

    PeerSonicSnapshot parsed;
    bool hasParsed = false;

    if (advertisedDevice->haveManufacturerData()) {

      std::string mfg =
        advertisedDevice->getManufacturerData();

      hasParsed = parseSonicManufacturerData(
        reinterpret_cast<const uint8_t *>(mfg.data()),
        mfg.length(),
        parsed
      );
    }

    if (idx >= 0) {

      devices[idx].rssi = rssi;

      devices[idx].smoothRSSI =
        devices[idx].smoothRSSI * 0.82f +
        rssi * 0.18f;

      devices[idx].lastSeen =
        millis();

      if (hasParsed) {
        devices[idx].peerSonic = parsed;
      }

      return;
    }

    int freeIdx =
      freeSlot();

    if (freeIdx < 0) {
      return;
    }

    devices[freeIdx].active = true;
    devices[freeIdx].name = name;
    devices[freeIdx].type = type;
    devices[freeIdx].rssi = rssi;
    devices[freeIdx].smoothRSSI = rssi;
    devices[freeIdx].lastSeen = millis();

    devices[freeIdx].veryCloseStartMs = 0;
    devices[freeIdx].evolutionDoneForSession = false;
    devices[freeIdx].nextNoteMs = 0;
    devices[freeIdx].arpTriggerIndex = 0;
    devices[freeIdx].seenAtMs = millis();

    if (hasParsed) {
      devices[freeIdx].peerSonic = parsed;
    }
    else {
      factorySonicForType(type, devices[freeIdx].peerSonic);
    }

    Serial.print("NEW ECHO: ");
    Serial.print(name);
    Serial.print(" | Type=");
    Serial.print(type);
    Serial.print(" | RSSI=");
    Serial.println(rssi);

    logEncounter(
      name,
      type,
      "seen",
      rssi,
      rssi,
      rssiToCloseness(rssi)
    );
  }

  void onScanEnd(
    const NimBLEScanResults& results,
    int reason
  ) override {

    if (sBlockingStationScan || dockLatched) {
      return;
    }

    if (pScan != nullptr) {
      pScan->start(0, false, true);
    }
  }
};

MyScanCallbacks scanCallbacks;

// =====================================================
// CLEANUP
// =====================================================

void cleanupDevices() {

  unsigned long now =
    millis();

  for (int i = 0; i < MAX_DEVICES; i++) {

    if (!devices[i].active) {
      continue;
    }

    if (now - devices[i].lastSeen > DEVICE_TIMEOUT) {

      Serial.print("Timeout: ");
      Serial.println(devices[i].name);

      logEncounter(
        devices[i].name,
        devices[i].type,
        "lost",
        devices[i].rssi,
        devices[i].smoothRSSI,
        0.0f
      );

      PeerSonicSnapshot sonic = devices[i].peerSonic;

      if (!sonic.valid) {
        factorySonicForType(devices[i].type, sonic);
      }

      logEncounterSonicSnapshot(
        devices[i].name,
        devices[i].type,
        devices[i].seenAtMs,
        now,
        sonic
      );

      clearDevice(i);
    }
  }
}

// =====================================================
// SETUP BLE
// =====================================================

void setupBLE() {

  NimBLEDevice::init(MY_NAME);

  NimBLEDevice::setPower(3);

  pAdvertising =
    NimBLEDevice::getAdvertising();

  NimBLEAdvertisementData advData;

  advData.setName(MY_NAME);

  refreshSonicAdvertising();

  pAdvertising->start();

  Serial.println("Advertising started");

  pScan =
    NimBLEDevice::getScan();

  pScan->setScanCallbacks(
    &scanCallbacks,
    true
  );

  pScan->setActiveScan(false);

  pScan->setInterval(45);
  pScan->setWindow(45);

  pScan->setDuplicateFilter(0);

  pScan->start(0, false, true);

  Serial.println("Async NimBLE scan started");
}

// =====================================================
// SLEEP SUSPEND / RESUME
// =====================================================

void stopBLEForSleep() {

  if (pScan != nullptr) {
    pScan->stop();
    pScan->clearResults();
  }

  if (pAdvertising != nullptr) {
    pAdvertising->stop();
  }

  NimBLEDevice::deinit(true);

  pScan = nullptr;
  pAdvertising = nullptr;
}

void resumeBLEAfterSleep() {

  setupBLE();

  if (dockLatched) {

    if (pScan != nullptr) {
      pScan->stop();
      pScan->clearResults();
    }
  }

  else {
    restoreEchoBlePeerScan("wake");
  }
}