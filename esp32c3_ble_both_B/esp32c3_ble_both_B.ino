#include <Arduino.h>
#include <NimBLEDevice.h>
#include <math.h>

#define LED_PIN 7
#define BUZZER_PIN 8

const char* MY_NAME = "Creature_B";

NimBLEAdvertising* pAdvertising = nullptr;
NimBLEScan* pScan = nullptr;

const int MAX_DEVICES = 10;

// 장치를 완전히 지우는 timeout
const unsigned long DEVICE_TIMEOUT = 3000;

// 광고를 이 시간 이상 못 받으면 출력(LED/BUZZ) 먼저 OFF
const unsigned long SIGNAL_LOST_OFF = 500;

// 약하게 decay 시작
const unsigned long SIGNAL_DECAY_START = 250;

unsigned long lastBuzzerCycleStart = 0;
bool buzzerState = false;

struct TrackedDevice {
  String name;
  int rssi;
  float smoothRSSI;
  unsigned long lastSeen;
  bool active;
};

TrackedDevice devices[MAX_DEVICES];

unsigned long lastLedToggle = 0;
bool ledState = false;
unsigned long lastDebugPrint = 0;

// =========================================================
// LED polarity
// 현재는 active-high 기준:
// HIGH = ON / LOW = OFF
// 만약 반대로 동작하면 이 두 함수만 바꾸면 됨
// =========================================================
void setLedOn() {
  digitalWrite(LED_PIN, HIGH);
}

void setLedOff() {
  digitalWrite(LED_PIN, LOW);
}

float clampf(float x, float a, float b) {
  if (x < a) return a;
  if (x > b) return b;
  return x;
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

int getNearestDeviceIndex() {
  int bestIndex = -1;
  float bestRSSI = -999.0f;

  for (int i = 0; i < MAX_DEVICES; i++) {
    if (devices[i].active && devices[i].smoothRSSI > bestRSSI) {
      bestRSSI = devices[i].smoothRSSI;
      bestIndex = i;
    }
  }
  return bestIndex;
}

void clearDevice(int i) {
  devices[i].active = false;
  devices[i].name = "";
  devices[i].rssi = -100;
  devices[i].smoothRSSI = -100;
  devices[i].lastSeen = 0;
}

void stopOutputs() {
  noTone(BUZZER_PIN);
  buzzerState = false;

  ledState = false;
  lastLedToggle = millis();

  setLedOff();
}

void cleanupDevices() {
  unsigned long now = millis();

  for (int i = 0; i < MAX_DEVICES; i++) {
    if (devices[i].active && (now - devices[i].lastSeen > DEVICE_TIMEOUT)) {
      Serial.print("Timeout: ");
      Serial.println(devices[i].name);

      clearDevice(i);
      stopOutputs();
    }
  }
}

// =========================================================
// Scan callbacks
// =========================================================
class MyScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    std::string devNameStd = advertisedDevice->getName();
    if (devNameStd.empty()) return;

    String name = String(devNameStd.c_str());

    if (!name.startsWith("Creature")) return;
    if (name == MY_NAME) return;

    int rssi = advertisedDevice->getRSSI();
    unsigned long now = millis();

    int idx = findDeviceIndexByName(name);

    if (idx >= 0) {
      devices[idx].rssi = rssi;
      devices[idx].smoothRSSI = devices[idx].smoothRSSI * 0.8f + rssi * 0.2f;
      devices[idx].lastSeen = now;
      return;
    }

    int freeIdx = findFreeSlot();
    if (freeIdx >= 0) {
      devices[freeIdx].active = true;
      devices[freeIdx].name = name;
      devices[freeIdx].rssi = rssi;
      devices[freeIdx].smoothRSSI = rssi;
      devices[freeIdx].lastSeen = now;

      // 새 장치 처음 감지 시 OFF 상태에서 시작 후 blink
      ledState = false;
      setLedOff();
      lastLedToggle = now;

      Serial.print("New: ");
      Serial.print(name);
      Serial.print(" | RSSI: ");
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

// =========================================================
// Main output update
// =========================================================
void updateOutput() {
  cleanupDevices();

  int nearest = getNearestDeviceIndex();
  if (nearest < 0) {
    stopOutputs();
    return;
  }

  float smoothRSSI = devices[nearest].smoothRSSI;
  unsigned long now = millis();
  unsigned long age = now - devices[nearest].lastSeen;

  // 광고를 일정 시간 못 받으면 출력부터 OFF
  if (age > SIGNAL_LOST_OFF) {
    stopOutputs();
    return;
  }

  // 신호가 잠깐 끊기기 시작하면 점점 약하게 처리
  if (age > SIGNAL_DECAY_START) {
    float decay = (age - SIGNAL_DECAY_START) * 0.03f;
    smoothRSSI -= decay;
  }

  if (smoothRSSI < -95.0f) {
    stopOutputs();
    return;
  }

  // =========================================================
  // BUZZER
  // 멀면: 뚜--뚜
  // 가까우면: 뚜뚜뚜뚜
  // =========================================================
  float freq;
  float buzzerNorm = 0.0f;
  float buzzerEased = 0.0f;
  unsigned long beepInterval;
  unsigned long beepOnTime;

  if (smoothRSSI < -90.0f) {
    // 아주 멀면 느리고 낮게
    freq = 220.0f;
    beepInterval = 1400;
    beepOnTime   = 180;
    buzzerNorm = 0.0f;
    buzzerEased = 0.0f;

  } else if (smoothRSSI < -60.0f) {
    // 중간 거리: 점점 빨라짐
    float normMid = (smoothRSSI - (-90.0f)) / (-60.0f - (-90.0f));
    normMid = clampf(normMid, 0.0f, 1.0f);

    float easedMid = powf(normMid, 1.5f);

    // pitch
    freq = 220.0f + easedMid * (700.0f - 220.0f);

    // interval: 1400ms -> 500ms
    beepInterval = (unsigned long)(1400.0f - easedMid * (1400.0f - 500.0f));

    // on-time: 180ms -> 120ms
    beepOnTime = (unsigned long)(180.0f - easedMid * (180.0f - 120.0f));

    buzzerNorm = normMid;
    buzzerEased = easedMid;

  } else {
    // 가까운 거리: 훨씬 빠르게
    float normNear = (smoothRSSI - (-60.0f)) / (-30.0f - (-60.0f));
    normNear = clampf(normNear, 0.0f, 1.0f);

    float easedNear = powf(normNear, 0.3f);

    // pitch
    freq = 700.0f + easedNear * (1600.0f - 700.0f);

    // interval: 500ms -> 90ms
    beepInterval = (unsigned long)(500.0f - easedNear * (500.0f - 90.0f));

    // on-time: 120ms -> 45ms
    beepOnTime = (unsigned long)(120.0f - easedNear * (120.0f - 45.0f));

    buzzerNorm = normNear;
    buzzerEased = easedNear;
  }

  // 버저 on/off를 LED처럼 토글
  if (now - lastBuzzerCycleStart >= beepInterval) {
    lastBuzzerCycleStart = now;

    if (!buzzerState) {
      tone(BUZZER_PIN, (int)freq);
      buzzerState = true;
    }
  }

  // on-time 지나면 끔
  if (buzzerState && (now - lastBuzzerCycleStart >= beepOnTime)) {
    noTone(BUZZER_PIN);
    buzzerState = false;
  }

  // =========================================================
  // LED blink
  // 감지 전에는 OFF
  // 감지 후에만 blink
  // =========================================================
  float blinkIntervalF;
  float debugNorm = 0.0f;
  float debugEased = 0.0f;

  if (smoothRSSI < -90.0f) {
    blinkIntervalF = 800.0f;
    debugNorm = 0.0f;
    debugEased = 0.0f;

  } else if (smoothRSSI < -60.0f) {
    float normMid = (smoothRSSI - (-90.0f)) / (-60.0f - (-90.0f));
    normMid = clampf(normMid, 0.0f, 1.0f);

    float easedMid = powf(normMid, 1.5f);

    // 800ms -> 250ms
    blinkIntervalF = 800.0f - easedMid * (800.0f - 250.0f);

    debugNorm = normMid;
    debugEased = easedMid;

  } else {
    float normNear = (smoothRSSI - (-60.0f)) / (-30.0f - (-60.0f));
    normNear = clampf(normNear, 0.0f, 1.0f);

    float easedNear = powf(normNear, 0.3f);

    // 250ms -> 30ms
    blinkIntervalF = 250.0f - easedNear * (250.0f - 30.0f);

    debugNorm = normNear;
    debugEased = easedNear;
  }

  unsigned long blinkInterval = (unsigned long)blinkIntervalF;

  if (now - lastLedToggle >= blinkInterval) {
    lastLedToggle = now;
    ledState = !ledState;

    if (ledState) {
      setLedOn();
    } else {
      setLedOff();
    }
  }

  if (now - lastDebugPrint > 200) {
    lastDebugPrint = now;

    Serial.print("Nearest: ");
    Serial.print(devices[nearest].name);
    Serial.print(" | Smooth RSSI: ");
    Serial.print(smoothRSSI);
    Serial.print(" | age=");
    Serial.println(age);

    Serial.print("LED norm=");
    Serial.print(debugNorm, 3);
    Serial.print(" | LED eased=");
    Serial.print(debugEased, 3);
    Serial.print(" | blinkInterval=");
    Serial.print(blinkInterval);

    Serial.print(" | BUZZ norm=");
    Serial.print(buzzerNorm, 3);
    Serial.print(" | BUZZ eased=");
    Serial.print(buzzerEased, 3);
    Serial.print(" | freq=");
    Serial.print((int)freq);
    Serial.print(" | beepInterval=");
    Serial.print(beepInterval);
    Serial.print(" | beepOnTime=");
    Serial.println(beepOnTime);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Boot OK");
  Serial.print("My name: ");
  Serial.println(MY_NAME);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // 부팅 직후 무조건 OFF
  setLedOff();
  noTone(BUZZER_PIN);

  ledState = false;
  buzzerState = false;
  lastLedToggle = millis();
  lastBuzzerCycleStart = millis();

  for (int i = 0; i < MAX_DEVICES; i++) {
    clearDevice(i);
  }

  NimBLEDevice::init(MY_NAME);
  NimBLEDevice::setPower(3);

  // =========================================================
  // Advertising
  // =========================================================
  pAdvertising = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setName(MY_NAME);
  advData.setManufacturerData(MY_NAME);
  pAdvertising->setAdvertisementData(advData);

  // 광고 간격 조금 빠르게
  pAdvertising->setMinInterval(32); // 20ms
  pAdvertising->setMaxInterval(64); // 40ms

  pAdvertising->start();
  Serial.println("Advertising started");

  // =========================================================
  // Scan
  // =========================================================
  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks, true);

  // passive scan
  pScan->setActiveScan(false);

  // scan duty 높임
  pScan->setInterval(45);
  pScan->setWindow(45);

  pScan->start(0, false, true);
  Serial.println("Async NimBLE scan started");

  // setup 끝에서도 OFF 유지
  stopOutputs();
}

void loop() {
  updateOutput();
  delay(10);
}