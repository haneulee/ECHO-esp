#pragma once

#include "Globals.h"

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
);

bool hasEncounterData();

bool hasEvolutionData();

void appendEvolutionJsonl(const String& jsonLine);

bool hasEncounterSonicData();

void logEncounterSonicSnapshot(
  String target,
  String type,
  unsigned long seenAtMs,
  unsigned long lostAtMs,
  const PeerSonicSnapshot &sonic
);

bool hasDockUploadPayload();