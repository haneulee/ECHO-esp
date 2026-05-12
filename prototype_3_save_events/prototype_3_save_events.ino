/*
========================================================
ECHO PERSONALITY SYSTEM
ESP32-C3 SUPER MINI
--------------------------------------------------------

FEATURES
- BLE proximity encounter
- multi personality synth
- encounter logging
- LittleFS CSV storage
- reed switch docking
- BLE upload to Raspberry Pi station
- auto memory clear after upload

STATION NAME:
ECHO_station_001

PERSONALITY TYPES:
- BOUNCE
- SHY
- MESSY

--------------------------------------------------------
LIBRARIES
- NimBLE-Arduino
- LittleFS
========================================================
*/

#include <Arduino.h>
#include <driver/i2s.h>
#include <NimBLEDevice.h>
#include <LittleFS.h>
#include <math.h>

// =====================================================
// SELECT DEVICE NAME
// =====================================================

//#define MY_NAME "ECHO_BOUNCE_001"
//#define MY_NAME "ECHO_SHY_001"
#define MY_NAME "ECHO_MESSY_001"

#define STATION_NAME "ECHO_station_001"

// =====================================================
// HARDWARE
// =====================================================

#define REED_PIN 2

#define I2S_BCLK  4
#define I2S_LRCLK 5
#define I2S_DOUT  6

// =====================================================
// AUDIO
// =====================================================

#define SAMPLE_RATE 22050
#define BUFFER_SIZE 128

float phase1 = 0.0f;
float phase2 = 0.0f;

float env = 0.0f;
float envDecay = 0.9992f;

float freq1 = 440.0f;
float freq2 = 441.0f;

float lowpassState = 0.0f;
float cutoff = 1400.0f;

unsigned long nextNoteTime = 0;

#define DELAY_SIZE 4096
float delayBuffer[DELAY_SIZE];
int delayIndex = 0;

float delayWet = 0.15f;
float delayFeedback = 0.22f;

// =====================================================
// BLE
// =====================================================

NimBLEAdvertising* pAdvertising = nullptr;
NimBLEScan* pScan = nullptr;

#define MAX_DEVICES 10

struct TrackedDevice {
  String name;
  String type;

  int rssi;
  float smoothRSSI;

  unsigned long lastSeen;

  bool active;
};

TrackedDevice devices[MAX_DEVICES];

// =====================================================
// FILESYSTEM
// =====================================================

File logFile;

// =====================================================
// TIMING
// =====================================================

unsigned long lastDebugPrint = 0;

const unsigned long DEVICE_TIMEOUT = 5000;

// =====================================================
// UTILS
// =====================================================

float clampf(float x, float a, float b) {
  if (x < a) return a;
  if (x > b) return b;
  return x;
}

float midiToFreq(float midi) {
  return 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f);
}

float waveSine(float p) {
  return sinf(p * TWO_PI);
}

float waveTriangle(float p) {
  if (p < 0.5f) return -1.0f + p * 4.0f;
  return 3.0f - p * 4.0f;
}

float waveSaw(float p) {
  return p * 2.0f - 1.0f;
}

float waveNoise() {
  return random(-1000, 1000) / 1000.0f;
}

void advancePhase(float &p, float freq) {
  p += freq / SAMPLE_RATE;
  if (p >= 1.0f) p -= 1.0f;
}

// =====================================================
// I2S
// =====================================================

void setupI2S() {

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// =====================================================
// DEVICE HELPERS
// =====================================================

int findDevice(String name) {

  for (int i = 0; i < MAX_DEVICES; i++) {
    if (devices[i].active && devices[i].name == name) {
      return i;
    }
  }

  return -1;
}

int freeSlot() {

  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].active) return i;
  }

  return -1;
}

void clearDevice(int i) {

  devices[i].active = false;
  devices[i].name = "";
  devices[i].type = "";
}

// =====================================================
// CLOSENESS
// =====================================================

float rssiToCloseness(float rssi) {

  float c = (rssi - (-92.0f)) / (-55.0f - (-92.0f));

  c = clampf(c, 0.0f, 1.0f);

  c = powf(c, 0.75f);

  return c;
}

// =====================================================
// LOGGING
// =====================================================

void logEncounter(
  String target,
  String type,
  String event,
  float rssi,
  float smooth,
  float close
) {

  File f = LittleFS.open("/encounter.csv", "a");

  if (!f) return;

  String line = "";

  line += String(millis());
  line += ",";

  line += MY_NAME;
  line += ",";

  line += target;
  line += ",";

  line += type;
  line += ",";

  line += event;
  line += ",";

  line += String(rssi);
  line += ",";

  line += String(smooth, 2);
  line += ",";

  line += String(close, 3);

  line += "\n";

  f.print(line);
  f.close();
}

// =====================================================
// BLE SCAN CALLBACK
// =====================================================

class MyScanCallbacks : public NimBLEScanCallbacks {

  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {

    std::string devNameStd = advertisedDevice->getName();

    if (devNameStd.empty()) return;

    String name = String(devNameStd.c_str());

    if (!name.startsWith("ECHO_")) return;
    if (name == MY_NAME) return;
    if (name == STATION_NAME) return;

    int rssi = advertisedDevice->getRSSI();

    String type = "UNKNOWN";

    if (name.indexOf("BOUNCE") >= 0) type = "BOUNCE";
    if (name.indexOf("SHY") >= 0) type = "SHY";
    if (name.indexOf("MESSY") >= 0) type = "MESSY";

    int idx = findDevice(name);

    if (idx >= 0) {

      devices[idx].rssi = rssi;

      devices[idx].smoothRSSI =
        devices[idx].smoothRSSI * 0.82f +
        rssi * 0.18f;

      devices[idx].lastSeen = millis();

      return;
    }

    int freeIdx = freeSlot();

    if (freeIdx < 0) return;

    devices[freeIdx].active = true;
    devices[freeIdx].name = name;
    devices[freeIdx].type = type;
    devices[freeIdx].rssi = rssi;
    devices[freeIdx].smoothRSSI = rssi;
    devices[freeIdx].lastSeen = millis();

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

  void onScanEnd(const NimBLEScanResults& results, int reason) override {

    if (pScan != nullptr) {
      pScan->start(0, false, true);
    }
  }
};

MyScanCallbacks scanCallbacks;

// =====================================================
// PERSONALITY SYNTH
// =====================================================

void triggerPersonality(String type, float closeness) {

  if (type == "BOUNCE") {

    int melody[] = {0,4,7,9,7,4,2,0};

    float root = 72;

    int semi = melody[random(0,8)];

    freq1 = midiToFreq(root + semi);
    freq2 = freq1 * 1.004f;

    env = 0.85f;
    envDecay = 0.995f;

    cutoff = 1500 + closeness * 2200;

    delayWet = 0.12f;
  }

  else if (type == "SHY") {

    int melody[] = {0,2,4,7,4,2};

    float root = 60;

    int semi = melody[random(0,6)];

    freq1 = midiToFreq(root + semi);
    freq2 = freq1 * 1.001f;

    env = 0.70f;
    envDecay = 0.998f;

    cutoff = 800 + closeness * 1200;

    delayWet = 0.20f;
  }

  else if (type == "MESSY") {

    int melody[] = {0,1,5,7,10,3,8,2};

    float root = 55;

    int semi = melody[random(0,8)];

    freq1 = midiToFreq(root + semi);

    freq2 =
      freq1 *
      (0.98f + random(0,20) * 0.001f);

    env = 0.78f;
    envDecay = 0.992f;

    cutoff = 1000 + random(0,2000);

    delayWet = 0.25f;
  }

  unsigned long interval =
    300 - closeness * 220;

  nextNoteTime = millis() + interval;
}

// =====================================================
// AUDIO RENDER
// =====================================================

void renderAudio() {

  static int16_t buffer[BUFFER_SIZE * 2];

  for (int i = 0; i < BUFFER_SIZE; i++) {

    float dry = 0.0f;

    if (String(MY_NAME).indexOf("BOUNCE") >= 0) {

      float s1 = waveTriangle(phase1);
      float s2 = waveSine(phase2) * 0.4f;

      dry = s1 * 0.75f + s2 * 0.25f;
    }

    else if (String(MY_NAME).indexOf("SHY") >= 0) {

      float s1 = waveSine(phase1);
      float s2 = waveTriangle(phase2) * 0.2f;

      dry = s1 * 0.8f + s2 * 0.2f;
    }

    else {

      float s1 = waveSaw(phase1);
      float s2 = waveTriangle(phase2);

      float n = waveNoise() * 0.04f;

      dry = s1 * 0.5f + s2 * 0.35f + n;
    }

    dry *= env;

    advancePhase(phase1, freq1);
    advancePhase(phase2, freq2);

    env *= envDecay;

    float alpha =
      1.0f -
      expf(-2.0f * PI * cutoff / SAMPLE_RATE);

    alpha = clampf(alpha, 0.001f, 0.95f);

    lowpassState += alpha * (dry - lowpassState);

    float filtered = lowpassState;

    float delayed = delayBuffer[delayIndex];

    float out = filtered + delayed * delayWet;

    delayBuffer[delayIndex] =
      filtered +
      delayed * delayFeedback;

    delayIndex++;

    if (delayIndex >= DELAY_SIZE) {
      delayIndex = 0;
    }

    out = tanhf(out * 2.3f);

    int16_t sample =
      (int16_t)(out * 32767.0f);

    buffer[i*2] = sample;
    buffer[i*2+1] = sample;
  }

  size_t bytesWritten;

  i2s_write(
    I2S_NUM_0,
    buffer,
    sizeof(buffer),
    &bytesWritten,
    portMAX_DELAY
  );
}

// =====================================================
// CLEANUP
// =====================================================

void cleanupDevices() {

  unsigned long now = millis();

  for (int i = 0; i < MAX_DEVICES; i++) {

    if (!devices[i].active) continue;

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

      clearDevice(i);
    }
  }
}

// =====================================================
// BLE MEMORY UPLOAD
// =====================================================

void uploadMemoryToStation() {

  Serial.println("=== DOCKED ===");
  Serial.println("Searching station...");

  pScan->stop();

  NimBLEScan* scan = NimBLEDevice::getScan();

  NimBLEScanResults results = scan->start(5);

  for (int i = 0; i < results.getCount(); i++) {

    NimBLEAdvertisedDevice dev =
      results.getDevice(i);

    String name = dev.getName().c_str();

    if (name != STATION_NAME) continue;

    Serial.println("Station found");

    NimBLEClient* client =
      NimBLEDevice::createClient();

    if (!client->connect(&dev)) {

      Serial.println("Connect failed");
      return;
    }

    Serial.println("Connected");

    NimBLERemoteService* service =
      client->getService(
        "12345678-1234-1234-1234-1234567890ab"
      );

    if (!service) {
      client->disconnect();
      return;
    }

    NimBLERemoteCharacteristic* ch =
      service->getCharacteristic(
        "abcd1234-5678-1234-5678-abcdef123456"
      );

    if (!ch) {
      client->disconnect();
      return;
    }

    File f =
      LittleFS.open("/encounter.csv", "r");

    if (!f) {
      client->disconnect();
      return;
    }

    ch->writeValue("BEGIN_UPLOAD");

    delay(100);

    while (f.available()) {

      String line =
        f.readStringUntil('\n');

      ch->writeValue(line.c_str());

      delay(25);
    }

    ch->writeValue("END_UPLOAD");

    f.close();

    client->disconnect();

    Serial.println("Upload complete");

    // CLEAR MEMORY
    LittleFS.remove("/encounter.csv");

    Serial.println("CSV cleared");

    ESP.restart();
  }

  Serial.println("Station not found");

  pScan->start(0, false, true);
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

  pAdvertising->setAdvertisementData(
    advData
  );

  pAdvertising->start();

  Serial.println("Advertising started");

  pScan = NimBLEDevice::getScan();

  pScan->setScanCallbacks(
    &scanCallbacks,
    true
  );

  pScan->setActiveScan(false);

  pScan->setInterval(45);
  pScan->setWindow(45);

  pScan->start(0, false, true);

  Serial.println("Async NimBLE scan started");
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("========================");
  Serial.println("ECHO PERSONALITY SYSTEM");
  Serial.println("========================");

  Serial.print("MY_NAME: ");
  Serial.println(MY_NAME);

  pinMode(REED_PIN, INPUT_PULLUP);

  randomSeed(esp_random());

  if (!LittleFS.begin(true)) {

    Serial.println("LittleFS mount failed");

    while (1);
  }

  setupI2S();
  setupBLE();
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // DOCK DETECT
  if (digitalRead(REED_PIN) == LOW) {

    uploadMemoryToStation();

    delay(1000);
  }

  cleanupDevices();

  unsigned long now = millis();

  for (int i = 0; i < MAX_DEVICES; i++) {

    if (!devices[i].active) continue;

    float closeness =
      rssiToCloseness(
        devices[i].smoothRSSI
      );

    if (closeness <= 0.02f) continue;

    if (now >= nextNoteTime) {

      triggerPersonality(
        devices[i].type,
        closeness
      );
    }

    if (now - lastDebugPrint > 1000) {

      Serial.print(devices[i].name);

      Serial.print(" | ");

      Serial.print(devices[i].type);

      Serial.print(" | RSSI=");

      Serial.print(devices[i].rssi);

      Serial.print(" | Smooth=");

      Serial.print(
        devices[i].smoothRSSI,
        1
      );

      Serial.print(" | Close=");

      Serial.print(closeness, 2);

      Serial.print(" | age=");

      Serial.println(
        now - devices[i].lastSeen
      );
    }
  }

  if (now - lastDebugPrint > 1000) {
    lastDebugPrint = now;
    Serial.println();
  }

  renderAudio();
}