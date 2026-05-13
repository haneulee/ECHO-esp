#include "DockUpload.h"

// =====================================================
// BLOCKING STATION SCAN GUARD
// =====================================================

struct BlockingStationScanGuard {

  BlockingStationScanGuard() {
    sBlockingStationScan = true;
  }

  ~BlockingStationScanGuard() {
    sBlockingStationScan = false;
  }
};

// =====================================================
// BLE MEMORY UPLOAD
// =====================================================

bool tryUploadMemoryToStationOnce() {

  if (!hasDockUploadPayload()) {
    Serial.println("Station BLE skipped (no encounter/evolution to send)");
    return false;
  }

  Serial.println("Searching station...");

  BlockingStationScanGuard blockGuard;

  if (pAdvertising != nullptr) {
    pAdvertising->stop();
    delay(120);
  }

  if (pScan != nullptr) {
    pScan->stop();
    delay(200);
  }

  NimBLEScan* scan =
    NimBLEDevice::getScan();

  scan->clearResults();
  scan->setMaxResults(64);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->setDuplicateFilter(0);

  NimBLEScanResults results =
    scan->getResults(
      DOCK_STATION_SCAN_MS,
      false
    );

  Serial.print("Scan count: ");
  Serial.println(results.getCount());

  for (int i = 0; i < results.getCount(); i++) {

    const NimBLEAdvertisedDevice* dev =
      results.getDevice(i);

    if (dev == nullptr) {
      continue;
    }

    String name = "";

    std::string n =
      dev->getName();

    if (!n.empty()) {
      name = String(n.c_str());
    }

    if (
      name.length() == 0 &&
      dev->haveManufacturerData()
    ) {
      std::string m =
        dev->getManufacturerData();

      name =
        String(m.c_str());
    }

    NimBLEUUID echoSvc(
      "12345678-1234-1234-1234-1234567890ab"
    );

    bool isStationByName =
      (name == STATION_NAME);

    bool isStationByService =
      dev->isAdvertisingService(echoSvc);

    if (
      !isStationByName &&
      !isStationByService
    ) {
      continue;
    }

    if (name.length() > 0) {

      Serial.print("Found station candidate: [");
      Serial.print(name);
      Serial.print("] byService=");
      Serial.print(isStationByService ? "yes" : "no");
      Serial.print(" RSSI=");
      Serial.print(dev->getRSSI());
      Serial.print(" addr=");
      Serial.println(dev->getAddress().toString().c_str());
    }

    else {

      Serial.println(
        "Found station candidate: (no name) by 128-bit service UUID in adv"
      );
    }

    Serial.println("Station found — connecting...");

    NimBLEClient* client =
      NimBLEDevice::createClient();

    if (!client->connect(dev)) {

      Serial.println("Connect failed");

      NimBLEDevice::deleteClient(client);

      restoreEchoBlePeerScan("dock_connect_fail");

      return false;
    }

    Serial.println("Connected");

    NimBLERemoteService* service =
      client->getService(
        "12345678-1234-1234-1234-1234567890ab"
      );

    if (!service) {

      Serial.println("Service not found");

      client->disconnect();

      NimBLEDevice::deleteClient(client);

      restoreEchoBlePeerScan("dock_no_service");

      return false;
    }

    NimBLERemoteCharacteristic* ch =
      service->getCharacteristic(
        "abcd1234-5678-1234-5678-abcdef123456"
      );

    if (!ch) {

      Serial.println("Characteristic not found");

      client->disconnect();

      NimBLEDevice::deleteClient(client);

      restoreEchoBlePeerScan("dock_no_char");

      return false;
    }

    bool hasEnc =
      hasEncounterData();

    bool hasEvo =
      hasEvolutionData();

    ch->writeValue("BEGIN_UPLOAD");

    delay(100);

    {
      String model = "unknown";

      if (String(ECHO_UNIQUE_MODEL_NAME).indexOf("BOUNCE") >= 0) {
        model = "bounce";
      }

      else if (String(ECHO_UNIQUE_MODEL_NAME).indexOf("SHY") >= 0) {
        model = "shy";
      }

      else if (String(ECHO_UNIQUE_MODEL_NAME).indexOf("MESSY") >= 0) {
        model = "messy";
      }

      String meta =
        String("ECHO_JSON_META:{\"v\":1,\"uniqueDeviceName\":\"") +
        String(ECHO_UNIQUE_MODEL_NAME) +
        "\",\"echoUnitCode\":\"" +
        String(ECHO_UNIT_CODE) +
        "\",\"bleDeviceName\":\"" +
        String(MY_NAME) +
        "\",\"echoModelType\":\"" +
        model +
        "\"}";

      ch->writeValue(meta.c_str());

      delay(50);
    }

    {
      String st =
        String("ECHO_STATE_JSON:") +
        buildEchoStateJsonWire();

      ch->writeValue(st.c_str());

      delay(50);
    }

    if (hasEnc) {

      File f =
        LittleFS.open("/encounter.csv", "r");

      if (f) {

        while (f.available()) {

          String line =
            f.readStringUntil('\n');

          if (line.length() > 0) {

            ch->writeValue(line.c_str());

            delay(25);
          }
        }

        f.close();
      }
    }

    if (hasEvo) {

      File fe =
        LittleFS.open("/evolution.jsonl", "r");

      if (fe) {

        while (fe.available()) {

          String line =
            fe.readStringUntil('\n');

          line.trim();

          if (line.length() == 0) {
            continue;
          }

          String out =
            String("ECHO_EVOLUTION_JSON:") +
            line;

          if (out.length() > 500) {

            Serial.print("WARN evolution BLE payload len=");
            Serial.println(out.length());
          }

          ch->writeValue(out.c_str());

          delay(30);
        }

        fe.close();
      }
    }

    ch->writeValue("END_UPLOAD");

    client->disconnect();

    NimBLEDevice::deleteClient(client);

    Serial.println("Upload complete");

    if (hasEnc) {

      LittleFS.remove("/encounter.csv");

      Serial.println("CSV cleared");
    }

    if (hasEvo) {

      LittleFS.remove("/evolution.jsonl");

      Serial.println("Evolution log cleared");
    }

    restoreEchoBlePeerScan("upload_ok");

    return true;
  }

  Serial.println("Station not found (will retry while docked)");

  restoreEchoBlePeerScan("station_not_found");

  return false;
}