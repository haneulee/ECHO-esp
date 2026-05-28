#include "Config.h"
#include "Globals.h"
#include "Utils.h"
#include "AudioSynth.h"
#include "BleEcho.h"
#include "DockUpload.h"
#include "EchoState.h"
#include "EncounterLog.h"
#include "ReedDock.h"

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

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

  initEchoMelodyState();

  if (!loadEchoStateFromFs()) {
    // defaults already set by initEchoMelodyState
  }

  saveEchoStateToFs();

  setupI2S();

  setupBLE();

  gDockSenseReadyMs =
    millis() + REED_DOCK_ARMING_MS;
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  updateDockLogic();

  cleanupDevices();

  unsigned long now =
    millis();

  for (int i = 0; i < MAX_DEVICES; i++) {

    if (!devices[i].active) {
      continue;
    }

    float closeness =
      rssiToCloseness(
        devices[i].smoothRSSI
      );

    float evoClose =
      rssiToEvolutionCloseness(
        devices[i].smoothRSSI
      );

    if (evoClose < EVOLUTION_CLOSE_THRESHOLD) {

      devices[i].veryCloseStartMs = 0;
    }

    else {

      if (devices[i].veryCloseStartMs == 0) {

        devices[i].veryCloseStartMs = now;
      }

      if (
        !devices[i].evolutionDoneForSession &&
        (
          now - devices[i].veryCloseStartMs
        ) >= EVOLUTION_MIN_MS
      ) {

        tryEvolutionForDevice(i);

        devices[i].evolutionDoneForSession = true;
      }
    }

    if (closeness <= 0.02f) {
      continue;
    }

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

      Serial.print(" | Evo=");

      Serial.print(evoClose, 2);

      Serial.print(" | age=");

      {
        unsigned long t = millis();
        unsigned long ls = devices[i].lastSeen;
        unsigned long age = (t >= ls) ? (t - ls) : 0;
        Serial.println(age);
      }
    }
  }

  if (now - lastDebugPrint > 1000) {
    lastDebugPrint = now;
  }

  renderAudio();
}