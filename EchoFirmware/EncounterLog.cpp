#include "EncounterLog.h"
#include "SonicAdvert.h"

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

bool hasEncounterSonicData() {

  if (!LittleFS.exists("/encounter_sonic.jsonl")) {
    return false;
  }

  File f = LittleFS.open("/encounter_sonic.jsonl", "r");

  if (!f) {
    return false;
  }

  size_t size = f.size();

  f.close();

  return size > 0;
}

void logEncounterSonicSnapshot(
  String target,
  String type,
  unsigned long seenAtMs,
  unsigned long lostAtMs,
  const PeerSonicSnapshot &sonic
) {

  File f =
    LittleFS.open("/encounter_sonic.jsonl", "a");

  if (!f) {
    return;
  }

  String j = "{";

  j += "\"v\":1,";
  j += "\"target\":\"";
  j += target;
  String typeKey = type;
  typeKey.toLowerCase();

  j += "\",\"otherEchoType\":\"";
  j += typeKey;
  j += "\",\"seenAtMs\":";
  j += String(seenAtMs);
  j += ",\"lostAtMs\":";
  j += String(lostAtMs);
  j += ",\"sonicSource\":\"";
  j += sonic.fromBle ? "ble_adv" : "factory_default";
  j += "\",\"profileSnapshot\":{\"melodySemi\":[";

  for (int k = 0; k < MELODY_SLOTS; k++) {

    j += String(sonic.melodySemi[k]);

    if (k < MELODY_SLOTS - 1) {
      j += ",";
    }
  }

  j += "],\"brightness\":";
  j += String(sonic.brightness, 2);
  j += ",\"calmness\":";
  j += String(sonic.calmness, 2);
  j += ",\"densityBias\":";
  j += String(sonic.densityBias, 2);
  j += "}}";

  f.print(j);
  f.print("\n");

  f.close();
}

bool hasDockUploadPayload() {

  return
    hasEncounterData() ||
    hasEvolutionData();
}