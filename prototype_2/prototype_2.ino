#include <Arduino.h>
#include <driver/i2s.h>
#include <NimBLEDevice.h>
#include <math.h>

// ======================================================
// DEVICE NAME - 보드마다 다르게 설정
// ======================================================
// #define MY_NAME "ECHO_SHY_001"
// #define MY_NAME "ECHO_BOUNCE_001"
#define MY_NAME "ECHO_MESSY_001"

// ======================================================
// I2S PINS
// ======================================================
#define I2S_BCLK  4
#define I2S_LRCLK 5
#define I2S_DOUT  6

#define SAMPLE_RATE 16000
#define BUFFER_SIZE 128

// ======================================================
// SYSTEM SETTINGS
// ======================================================
#define MAX_DEVICES 10
#define MAX_VOICES 3

const unsigned long DEVICE_TIMEOUT = 5000;
const unsigned long SIGNAL_LOST_OFF = 2500;
const unsigned long SIGNAL_DECAY_START = 900;

// ======================================================
// TYPES
// ======================================================
#define TYPE_UNKNOWN 0
#define TYPE_BOUNCE  1
#define TYPE_SHY     2
#define TYPE_MESSY   3

NimBLEAdvertising* pAdvertising = nullptr;
NimBLEScan* pScan = nullptr;

// ======================================================
// DEVICE TRACKING
// ======================================================
struct TrackedDevice {
  String name;
  uint8_t type;
  int rssi;
  float smoothRSSI;
  float closeness;
  unsigned long lastSeen;
  bool active;
};

TrackedDevice devices[MAX_DEVICES];

// ======================================================
// VOICE
// ======================================================
struct EchoVoice {
  bool active;
  String name;
  uint8_t type;
  float closeness;

  float phase1;
  float phase2;
  float phase3;

  float freq1;
  float freq2;
  float freq3;

  float env;
  float envTarget;
  float envDecay;
  float attackSpeed;

  float cutoff;
  float targetCutoff;
  float filterState;

  int melodyStep;
  unsigned long nextNoteTime;
};

EchoVoice voices[MAX_VOICES];

// ======================================================
// GLOBAL DELAY
// ======================================================
#define DELAY_SIZE 4096
float delayBuffer[DELAY_SIZE];
int delayIndex = 0;

float delayWet = 0.10f;
float delayFeedback = 0.12f;

unsigned long lastDebugPrint = 0;

// ======================================================
// UTILS
// ======================================================
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

void advancePhase(float &phase, float freq) {
  phase += freq / SAMPLE_RATE;
  while (phase >= 1.0f) phase -= 1.0f;
}

int chooseFrom(const int* arr, int len) {
  return arr[random(0, len)];
}

uint8_t typeFromName(const String& name) {
  if (name.indexOf("BOUNCE") >= 0) return TYPE_BOUNCE;
  if (name.indexOf("SHY") >= 0) return TYPE_SHY;
  if (name.indexOf("MESSY") >= 0) return TYPE_MESSY;
  return TYPE_UNKNOWN;
}

const char* typeLabel(uint8_t t) {
  if (t == TYPE_BOUNCE) return "BOUNCE";
  if (t == TYPE_SHY) return "SHY";
  if (t == TYPE_MESSY) return "MESSY";
  return "UNKNOWN";
}

// 넓은 RSSI 범위: 약한 신호도 presence로 남김
float rssiToCloseness(float rssi) {
  float c = (rssi - (-95.0f)) / (-55.0f - (-95.0f));
  c = clampf(c, 0.0f, 1.0f);
  return powf(c, 1.25f);
}

// ======================================================
// DEVICE MANAGEMENT
// ======================================================
void clearDevice(int i) {
  devices[i].active = false;
  devices[i].name = "";
  devices[i].type = TYPE_UNKNOWN;
  devices[i].rssi = -100;
  devices[i].smoothRSSI = -100;
  devices[i].closeness = 0.0f;
  devices[i].lastSeen = 0;
}

int findDeviceIndexByName(const String& name) {
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (devices[i].active && devices[i].name == name) return i;
  }
  return -1;
}

int findFreeSlot() {
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].active) return i;
  }
  return -1;
}

void cleanupDevices() {
  unsigned long now = millis();

  for (int i = 0; i < MAX_DEVICES; i++) {
    if (devices[i].active && now - devices[i].lastSeen > DEVICE_TIMEOUT) {
      Serial.print("Timeout: ");
      Serial.println(devices[i].name);
      clearDevice(i);
    }
  }
}

// ======================================================
// VOICE MANAGEMENT
// ======================================================
void clearVoice(int i) {
  voices[i].active = false;
  voices[i].name = "";
  voices[i].type = TYPE_UNKNOWN;
  voices[i].closeness = 0.0f;

  voices[i].phase1 = 0.0f;
  voices[i].phase2 = 0.0f;
  voices[i].phase3 = 0.0f;

  voices[i].freq1 = 440.0f;
  voices[i].freq2 = 441.0f;
  voices[i].freq3 = 880.0f;

  voices[i].env = 0.0f;
  voices[i].envTarget = 0.0f;
  voices[i].envDecay = 0.9992f;
  voices[i].attackSpeed = 0.01f;

  voices[i].cutoff = 900.0f;
  voices[i].targetCutoff = 900.0f;
  voices[i].filterState = 0.0f;

  voices[i].melodyStep = 0;
  voices[i].nextNoteTime = millis() + random(100, 700);
}

int findVoiceByName(const String& name) {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].active && voices[i].name == name) return i;
  }
  return -1;
}

int findFreeVoice() {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active) return i;
  }
  return -1;
}

void assignVoice(int voiceIndex, TrackedDevice &d) {
  clearVoice(voiceIndex);

  voices[voiceIndex].active = true;
  voices[voiceIndex].name = d.name;
  voices[voiceIndex].type = d.type;
  voices[voiceIndex].closeness = d.closeness;

  voices[voiceIndex].phase1 = random(0, 1000) / 1000.0f;
  voices[voiceIndex].phase2 = random(0, 1000) / 1000.0f;
  voices[voiceIndex].phase3 = random(0, 1000) / 1000.0f;

  Serial.print("Voice assigned: ");
  Serial.print(d.name);
  Serial.print(" / ");
  Serial.println(typeLabel(d.type));
}

// ======================================================
// SELECT NEARBY DEVICES FOR VOICES
// ======================================================
void updateVoicesFromDevices() {
  cleanupDevices();

  unsigned long now = millis();

  bool selected[MAX_DEVICES];
  for (int i = 0; i < MAX_DEVICES; i++) selected[i] = false;

  int chosen[MAX_VOICES];
  for (int i = 0; i < MAX_VOICES; i++) chosen[i] = -1;

  for (int v = 0; v < MAX_VOICES; v++) {
    int best = -1;
    float bestRSSI = -999.0f;

    for (int i = 0; i < MAX_DEVICES; i++) {
      if (!devices[i].active) continue;
      if (selected[i]) continue;

      unsigned long age = now - devices[i].lastSeen;
      if (age > SIGNAL_LOST_OFF) continue;

      float effectiveRSSI = devices[i].smoothRSSI;

      if (age > SIGNAL_DECAY_START) {
        float decay = (age - SIGNAL_DECAY_START) * 0.015f;
        effectiveRSSI -= decay;
      }

      if (effectiveRSSI < -98.0f) continue;

      if (effectiveRSSI > bestRSSI) {
        bestRSSI = effectiveRSSI;
        best = i;
      }
    }

    if (best >= 0) {
      selected[best] = true;
      chosen[v] = best;

      unsigned long age = now - devices[best].lastSeen;
      float effectiveRSSI = devices[best].smoothRSSI;

      if (age > SIGNAL_DECAY_START) {
        float decay = (age - SIGNAL_DECAY_START) * 0.015f;
        effectiveRSSI -= decay;
      }

      devices[best].closeness = rssiToCloseness(effectiveRSSI);
    }
  }

  for (int v = 0; v < MAX_VOICES; v++) {
    if (!voices[v].active) continue;

    bool stillChosen = false;

    for (int c = 0; c < MAX_VOICES; c++) {
      int dIdx = chosen[c];

      if (dIdx >= 0 && voices[v].name == devices[dIdx].name) {
        stillChosen = true;
        voices[v].closeness = devices[dIdx].closeness;
        voices[v].type = devices[dIdx].type;
      }
    }

    if (!stillChosen) {
      Serial.print("Voice removed: ");
      Serial.println(voices[v].name);
      clearVoice(v);
    }
  }

  for (int c = 0; c < MAX_VOICES; c++) {
    int dIdx = chosen[c];
    if (dIdx < 0) continue;

    int existing = findVoiceByName(devices[dIdx].name);

    if (existing >= 0) {
      voices[existing].closeness = devices[dIdx].closeness;
      voices[existing].type = devices[dIdx].type;
      continue;
    }

    int freeVoice = findFreeVoice();

    if (freeVoice >= 0) {
      assignVoice(freeVoice, devices[dIdx]);
    }
  }
}

// ======================================================
// BLE CALLBACK
// ======================================================
class MyScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    String name = "";

    std::string devNameStd = advertisedDevice->getName();

    if (!devNameStd.empty()) {
      name = String(devNameStd.c_str());
    }

    if (name.length() == 0 && advertisedDevice->haveManufacturerData()) {
      std::string m = advertisedDevice->getManufacturerData();
      name = String(m.c_str());
    }

    if (name.length() == 0) return;
    if (!name.startsWith("ECHO_")) return;
    if (name == String(MY_NAME)) return;

    int rssi = advertisedDevice->getRSSI();
    unsigned long now = millis();

    int idx = findDeviceIndexByName(name);

    if (idx >= 0) {
      devices[idx].rssi = rssi;
      devices[idx].smoothRSSI = devices[idx].smoothRSSI * 0.75f + rssi * 0.25f;
      devices[idx].type = typeFromName(name);
      devices[idx].lastSeen = now;
      return;
    }

    int freeIdx = findFreeSlot();

    if (freeIdx >= 0) {
      devices[freeIdx].active = true;
      devices[freeIdx].name = name;
      devices[freeIdx].type = typeFromName(name);
      devices[freeIdx].rssi = rssi;
      devices[freeIdx].smoothRSSI = rssi;
      devices[freeIdx].closeness = rssiToCloseness(rssi);
      devices[freeIdx].lastSeen = now;

      Serial.print("NEW ECHO: ");
      Serial.print(name);
      Serial.print(" | Type=");
      Serial.print(typeLabel(devices[freeIdx].type));
      Serial.print(" | RSSI=");
      Serial.println(rssi);
    }
  }

  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    if (pScan != nullptr) {
      pScan->start(0, false, true);
    }
  }
};

MyScanCallbacks scanCallbacks;

// ======================================================
// I2S
// ======================================================
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

// ======================================================
// BLE SETUP
// ======================================================
void setupBLE() {
  NimBLEDevice::init(MY_NAME);
  NimBLEDevice::setPower(3);

  pAdvertising = NimBLEDevice::getAdvertising();

  NimBLEAdvertisementData advData;
  advData.setName(MY_NAME);
  advData.setManufacturerData(MY_NAME);

  pAdvertising->setAdvertisementData(advData);

  pAdvertising->setMinInterval(32);
  pAdvertising->setMaxInterval(64);
  pAdvertising->start();

  Serial.println("Advertising started");

  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks, true);

  pScan->setActiveScan(false);
  pScan->setInterval(45);
  pScan->setWindow(45);

  pScan->start(0, false, true);

  Serial.println("Async NimBLE scan started");
}

// ======================================================
// PERSONALITY NOTE TRIGGER
// ======================================================
void triggerVoiceNote(EchoVoice &v) {
  float c = v.closeness;

  // distance affects density / brightness only, not volume
  v.envTarget = 0.95f;

  if (v.type == TYPE_BOUNCE) {
    // Energetic, flexible, anti-gravity
    // High, jumpy melodic contour
    int melody[] = {72, 79, 76, 81, 79, 76, 74, 72};       // C5 G5 E5 A5...
    int variations[] = {74, 76, 79, 81, 84};               // D5 E5 G5 A5 C6

    float probability = 0.25f + c * 0.65f;

    if (random(0, 1000) > probability * 1000.0f) {
      v.nextNoteTime = millis() + 180;
      return;
    }

    int base = melody[v.melodyStep % 8];
    int note = random(0, 100) < 76 ? base : chooseFrom(variations, 5);

    v.freq1 = midiToFreq(note);
    v.freq2 = v.freq1 * (1.0f + random(-4, 5) / 1200.0f);
    v.freq3 = v.freq1 * 2.0f;

    v.attackSpeed = 0.035f;
    v.envDecay = 0.99915f;

    v.targetCutoff = 850.0f + c * 1700.0f;

    int interval = 360 - (int)(c * 230.0f);
    if (interval < 120) interval = 120;

    v.nextNoteTime = millis() + interval + random(0, 80);
    v.melodyStep++;
  }

  else if (v.type == TYPE_SHY) {
    // Bashful, modest, timid
    // Small motion, lots of hesitation
    int melody[] = {60, 62, 64, 62, 60, 67, 64, 62};       // C4 D4 E4 D4 C4 G4...
    int variations[] = {60, 62, 64, 67, 69};               // C4 D4 E4 G4 A4

    float probability = 0.06f + c * 0.36f;

    if (random(0, 1000) > probability * 1000.0f) {
      v.nextNoteTime = millis() + 700 + random(0, 500);
      return;
    }

    int base = melody[v.melodyStep % 8];
    int note = random(0, 100) < 88 ? base : chooseFrom(variations, 5);

    v.freq1 = midiToFreq(note);
    v.freq2 = v.freq1 * 1.0008f;
    v.freq3 = v.freq1 * 2.0f;

    v.attackSpeed = 0.012f;
    v.envDecay = 0.99955f;

    v.targetCutoff = 420.0f + c * 850.0f;

    int interval = 1300 - (int)(c * 500.0f);
    if (interval < 650) interval = 650;

    v.nextNoteTime = millis() + interval + random(0, 900);
    v.melodyStep++;
  }

  else if (v.type == TYPE_MESSY) {
    // Careless, chaotic, sloppy
    // Still melodic, but unstable and chromatic
    int melody[] = {67, 72, 73, 70, 76, 69, 74, 71};       // G4 C5 C#5 A#4 E5 A4 D5 B4
    int variations[] = {65, 67, 69, 70, 72, 73, 76};       // F4 G4 A4 A#4 C5 C#5 E5

    float probability = 0.18f + c * 0.60f;

    if (random(0, 1000) > probability * 1000.0f) {
      v.nextNoteTime = millis() + 300 + random(0, 250);
      return;
    }

    int note = random(0, 100) < 62
                 ? melody[random(0, 8)]
                 : chooseFrom(variations, 7);

    v.freq1 = midiToFreq(note);
    v.freq2 = v.freq1 * (0.993f + random(0, 16) * 0.001f);
    v.freq3 = v.freq1 * 2.01f;

    v.attackSpeed = 0.025f;
    v.envDecay = 0.99925f;

    v.targetCutoff = 650.0f + c * 1600.0f + random(0, 350);

    int interval = 620 - (int)(c * 380.0f);
    if (interval < 150) interval = 150;

    v.nextNoteTime = millis() + interval + random(0, 220);
    v.melodyStep++;
  }

  else {
    v.nextNoteTime = millis() + 500;
  }
}

// ======================================================
// RENDER VOICE
// ======================================================
float renderVoice(EchoVoice &v) {
  if (!v.active) return 0.0f;

  v.cutoff += (v.targetCutoff - v.cutoff) * 0.0035f;

  // soft attack
  if (v.env < v.envTarget) {
    v.env += (v.envTarget - v.env) * v.attackSpeed;
  } else {
    v.env *= v.envDecay;
  }

  v.envTarget *= v.envDecay;

  if (v.env < 0.0005f) {
    v.env = 0.0f;
  }

  float dry = 0.0f;

  if (v.type == TYPE_BOUNCE) {
    float s1 = waveSine(v.phase1) * 0.62f;
    float s2 = waveTriangle(v.phase2) * 0.26f;
    float s3 = waveSine(v.phase3) * 0.12f;
    dry = (s1 + s2 + s3) * v.env;
  }

  else if (v.type == TYPE_SHY) {
    float s1 = waveSine(v.phase1) * 0.86f;
    float s2 = waveSine(v.phase2) * 0.10f;
    float s3 = waveTriangle(v.phase3) * 0.04f;
    dry = (s1 + s2 + s3) * v.env;
  }

  else if (v.type == TYPE_MESSY) {
    float s1 = waveTriangle(v.phase1) * 0.50f;
    float s2 = waveSine(v.phase2) * 0.30f;
    float s3 = waveSine(v.phase3) * 0.13f;
    float n = waveNoise() * 0.025f;
    dry = (s1 + s2 + s3 + n) * v.env;
  }

  advancePhase(v.phase1, v.freq1);
  advancePhase(v.phase2, v.freq2);
  advancePhase(v.phase3, v.freq3);

  float alpha = 1.0f - expf(-2.0f * PI * v.cutoff / SAMPLE_RATE);
  alpha = clampf(alpha, 0.001f, 0.95f);

  v.filterState += alpha * (dry - v.filterState);

  return v.filterState;
}

// ======================================================
// AUDIO
// ======================================================
void renderAudio() {
  static int16_t buffer[BUFFER_SIZE * 2];

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float mix = 0.0f;
    int activeCount = 0;

    for (int v = 0; v < MAX_VOICES; v++) {
      if (voices[v].active) {
        mix += renderVoice(voices[v]);
        activeCount++;
      }
    }

    if (activeCount > 1) {
      mix /= sqrtf((float)activeCount);
    }

    float delayed = delayBuffer[delayIndex];

    float out = mix + delayed * delayWet;

    delayBuffer[delayIndex] = mix + delayed * delayFeedback;

    delayIndex++;
    if (delayIndex >= DELAY_SIZE) delayIndex = 0;

    // softer saturation
    out = tanhf(out * 1.15f);
    out *= 1.0f;

    out = clampf(out, -1.0f, 1.0f);

    int16_t sample = (int16_t)(out * 32767.0f);

    buffer[i * 2] = sample;
    buffer[i * 2 + 1] = sample;
  }

  size_t bytesWritten;

  i2s_write(
    I2S_NUM_0,
    buffer,
    sizeof(buffer),
    &bytesWritten,
    0
  );
}

// ======================================================
// DEBUG
// ======================================================
void printDebug() {
  if (millis() - lastDebugPrint < 1000) return;
  lastDebugPrint = millis();

  bool any = false;

  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].active) continue;

    any = true;

    Serial.print(devices[i].name);
    Serial.print(" | ");
    Serial.print(typeLabel(devices[i].type));
    Serial.print(" | RSSI=");
    Serial.print(devices[i].rssi);
    Serial.print(" | Smooth=");
    Serial.print(devices[i].smoothRSSI, 1);
    Serial.print(" | Close=");
    Serial.print(devices[i].closeness, 2);
    Serial.print(" | age=");
    Serial.println(millis() - devices[i].lastSeen);
  }

  if (any) Serial.println();
}

// ======================================================
// SETUP / LOOP
// ======================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("========================");
  Serial.println("ECHO PERSONALITY MULTI-VOICE");
  Serial.println("========================");
  Serial.print("MY_NAME: ");
  Serial.println(MY_NAME);

  randomSeed(esp_random());

  for (int i = 0; i < MAX_DEVICES; i++) {
    clearDevice(i);
  }

  for (int v = 0; v < MAX_VOICES; v++) {
    clearVoice(v);
  }

  for (int i = 0; i < DELAY_SIZE; i++) {
    delayBuffer[i] = 0.0f;
  }

  setupI2S();
  setupBLE();
}

void loop() {
  updateVoicesFromDevices();

  for (int v = 0; v < MAX_VOICES; v++) {
    if (voices[v].active && millis() >= voices[v].nextNoteTime) {
      triggerVoiceNote(voices[v]);
    }
  }

  renderAudio();

  printDebug();

  delay(3);
}