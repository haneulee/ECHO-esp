#include "EncounterLog.h"

// =====================================================
// FILESYSTEM GLOBAL
// =====================================================

File logFile;

// =====================================================
// ENCOUNTER CSV
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
// ENCOUNTER EXISTS
// =====================================================

bool hasEncounterData() {

  if (!LittleFS.exists("/encounter.csv")) {
    return false;
  }

  File f = LittleFS.open("/encounter.csv", "r");

  if (!f) {
    return false;
  }

  size_t size = f.size();

  f.close();

  return size > 0;
}

// =====================================================
// EVOLUTION EXISTS
// =====================================================

bool hasEvolutionData() {

  if (!LittleFS.exists("/evolution.jsonl")) {
    return false;
  }

  File f = LittleFS.open("/evolution.jsonl", "r");

  if (!f) {
    return false;
  }

  size_t size = f.size();

  f.close();

  return size > 0;
}

// =====================================================
// EVOLUTION JSONL APPEND
// =====================================================

void appendEvolutionJsonl(
  const String& jsonLine
) {

  File f =
    LittleFS.open("/evolution.jsonl", "a");

  if (!f) return;

  f.print(jsonLine);
  f.print("\n");

  f.close();
}

// =====================================================
// DOCK PAYLOAD EXISTS
// =====================================================

bool hasDockUploadPayload() {

  return
    hasEncounterData() ||
    hasEvolutionData();
}