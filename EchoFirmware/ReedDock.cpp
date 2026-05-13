#include "ReedDock.h"

// =====================================================
// DOCK GLOBALS
// =====================================================

bool dockLatched = false;
bool uploadCompletedThisDock = false;

unsigned long gDockSenseReadyMs = 0;
unsigned long gLastDockUploadTryMs = 0;
unsigned long gPostUndockNoRedockUntilMs = 0;

// =====================================================
// REED RAW
// =====================================================

bool reedRawDockedSense() {

#if REED_DOCK_ACTIVE_LOW

  return digitalRead(REED_PIN) == LOW;

#else

  return digitalRead(REED_PIN) == HIGH;

#endif
}

// =====================================================
// REED FILTERED
// =====================================================

bool reedRawDockedSenseFiltered() {

  int dockVotes = 0;

  for (uint8_t k = 0; k < REED_DOCK_SAMPLES; k++) {

    if (reedRawDockedSense()) {
      dockVotes++;
    }

    delayMicroseconds(35);
  }

  return dockVotes * 2 > REED_DOCK_SAMPLES;
}

// =====================================================
// DOCK LOGIC
// =====================================================

void updateDockLogic() {

  unsigned long now =
    millis();

  const bool rawDocked =
    reedRawDockedSenseFiltered();

  const bool rawOpen =
    !rawDocked;

  static unsigned long reedDockedSince = 0;
  static unsigned long reedOpenSince = 0;

  static bool gReedAllowDock = false;

  static uint16_t reedDockConsecLoops = 0;

  bool pastArm =
    (gDockSenseReadyMs == 0) ||
    (now >= gDockSenseReadyMs);

  if (!pastArm) {

    reedDockedSince = 0;
    reedOpenSince = 0;
    gReedAllowDock = false;
    reedDockConsecLoops = 0;
  }

  else if (rawDocked) {

    const bool redockCooldown =
      (gPostUndockNoRedockUntilMs != 0) &&
      (now < gPostUndockNoRedockUntilMs);

    if (redockCooldown) {

      reedDockedSince = 0;
      reedDockConsecLoops = 0;
    }

    else {

      if (reedDockConsecLoops < 0xFFFF) {
        reedDockConsecLoops++;
      }

      if (reedDockConsecLoops < REED_DOCK_CONSEC_LOOPS) {

        reedDockedSince = 0;
      }

      else {

        if (reedDockedSince == 0) {
          reedDockedSince = now;
        }

        if (
          reedOpenSince != 0 &&
          (
            now - reedDockedSince >=
            REED_DOCK_DEBOUNCE_MS
          )
        ) {
          reedOpenSince = 0;
        }
      }
    }
  }

  else {

    reedDockConsecLoops = 0;

    if (reedOpenSince == 0) {
      reedOpenSince = now;
    }

    reedDockedSince = 0;
  }

  bool undockedStable =
    pastArm &&
    reedOpenSince != 0 &&
    (
      now - reedOpenSince >=
      REED_UNDOCK_DEBOUNCE_MS
    );

  if (
    undockedStable &&
    (
      gPostUndockNoRedockUntilMs == 0 ||
      now >= gPostUndockNoRedockUntilMs
    )
  ) {
    gReedAllowDock = true;
  }

  bool docked =
    pastArm &&
    gReedAllowDock &&
    reedDockedSince != 0 &&
    (
      now - reedDockedSince >=
      REED_DOCK_DEBOUNCE_MS
    );

  // =====================================================
  // UNDOCK
  // =====================================================

  if (undockedStable && dockLatched) {

    dockLatched = false;

    uploadCompletedThisDock = false;

    gLastDockUploadTryMs = 0;

    reedDockedSince = 0;

    gReedAllowDock = false;

    reedDockConsecLoops = 0;

    gPostUndockNoRedockUntilMs =
      now + REED_POST_UNDOCK_COOLDOWN_MS;

    Serial.println("Undocked");

    clearAllTrackedEchoPeers();

    restoreEchoBlePeerScan("undock");

    Serial.println(
      "Cleared peer cache; BLE scan restarted for other ECHOs"
    );
  }

  // =====================================================
  // DOCK LATCH
  // =====================================================

  if (docked && !dockLatched) {

    dockLatched = true;

    uploadCompletedThisDock = false;

    gLastDockUploadTryMs = 0;

    Serial.println("=== DOCKED ===");
    Serial.println("Dock detected");

    saveEchoStateToFs();

    if (!hasDockUploadPayload()) {

      uploadCompletedThisDock = true;

      Serial.println(
        "Dock: no encounter/evolution — skipping station BLE"
      );
    }
  }

  // =====================================================
  // UPLOAD LOOP
  // =====================================================

  if (
    docked &&
    dockLatched &&
    !uploadCompletedThisDock
  ) {

    bool ranStationPass = false;

    if (
      gLastDockUploadTryMs == 0 ||
      (
        now - gLastDockUploadTryMs >=
        DOCK_STATION_RETRY_MS
      )
    ) {

      gLastDockUploadTryMs = now;

      ranStationPass = true;

      if (tryUploadMemoryToStationOnce()) {

        uploadCompletedThisDock = true;
      }
    }

    if (ranStationPass) {

      renderAudio();

      delay(10);

      return;
    }
  }

  // =====================================================
  // DOCKED IDLE
  // =====================================================

  if (
    docked &&
    dockLatched &&
    uploadCompletedThisDock
  ) {

    renderAudio();

    delay(100);

    return;
  }
}