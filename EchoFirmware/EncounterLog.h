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

bool hasDockUploadPayload();