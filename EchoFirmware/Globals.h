#pragma once

#include "Config.h"

// =====================================================
// AUDIO GLOBALS
// =====================================================

extern float phase1;
extern float phase2;

extern float env;
extern float envDecay;

extern float freq1;
extern float freq2;

extern float lowpassState;
extern float cutoff;

extern unsigned long nextNoteTime;

extern float delayBuffer[DELAY_SIZE];
extern int delayIndex;

extern float delayWet;
extern float delayFeedback;

// =====================================================
// ECHO SOUND STATE
// =====================================================

extern int gMelodySemi[MELODY_SLOTS];
extern float gBrightness;
extern float gCalmness;
extern float gDensityBias;

// =====================================================
// BLE GLOBALS
// =====================================================

extern NimBLEAdvertising* pAdvertising;
extern NimBLEScan* pScan;

struct TrackedDevice {
  String name;
  String type;

  int rssi;
  float smoothRSSI;

  unsigned long lastSeen;

  bool active;

  unsigned long veryCloseStartMs;
  bool evolutionDoneForSession;
};

extern TrackedDevice devices[MAX_DEVICES];
extern volatile bool sBlockingStationScan;

// =====================================================
// FILESYSTEM
// =====================================================

extern File logFile;

// =====================================================
// TIMING / DEBUG
// =====================================================

extern unsigned long lastDebugPrint;

// =====================================================
// DOCK STATE
// =====================================================

extern bool dockLatched;
extern bool uploadCompletedThisDock;

extern unsigned long gDockSenseReadyMs;
extern unsigned long gLastDockUploadTryMs;
extern unsigned long gPostUndockNoRedockUntilMs;