#include "EchoState.h"
#include "EncounterLog.h"
#include "Globals.h"
#include "SonicAdvert.h"
#include "Utils.h"

// =====================================================
// MODEL TYPE
// =====================================================

String echoModelTypeKey() {

  if (String(ECHO_UNIQUE_MODEL_NAME).indexOf("BOUNCE") >= 0) {
    return "bounce";
  }

  if (String(ECHO_UNIQUE_MODEL_NAME).indexOf("SHY") >= 0) {
    return "shy";
  }

  if (String(ECHO_UNIQUE_MODEL_NAME).indexOf("MESSY") >= 0) {
    return "messy";
  }

  return "unknown";
}

// =====================================================
// INFLUENCES JSON
// =====================================================

void appendInfluencesJson(
  String& j,
  const String& mkey
) {

  float shy = 0.33f;
  float messy = 0.33f;
  float bounce = 0.34f;

  if (mkey == "shy") {

    shy += 0.25f;
    messy -= 0.12f;
    bounce -= 0.12f;
  }

  else if (mkey == "messy") {

    messy += 0.25f;
    shy -= 0.12f;
    bounce -= 0.12f;
  }

  else if (mkey == "bounce") {

    bounce += 0.25f;
    shy -= 0.12f;
    messy -= 0.12f;
  }

  shy = clampf(shy, 0.0f, 1.0f);
  messy = clampf(messy, 0.0f, 1.0f);
  bounce = clampf(bounce, 0.0f, 1.0f);

  j += ",\"influences\":{\"shy\":";
  j += String(shy, 2);
  j += ",\"messy\":";
  j += String(messy, 2);
  j += ",\"bounce\":";
  j += String(bounce, 2);
  j += "}";
}

// =====================================================
// BUILD STATE JSON
// =====================================================

String buildEchoStateJsonWire() {

  String mkey = echoModelTypeKey();

  String j = "{";

  j += "\"v\":1,";
  j += "\"soundProfileId\":\"" ECHO_SOUND_PROFILE_ID_FW "\",";
  j += "\"uniqueDeviceName\":\"" + String(ECHO_UNIQUE_MODEL_NAME) + "\",";
  j += "\"echoUnitCode\":\"" + String(ECHO_UNIT_CODE) + "\",";
  j += "\"echoModelType\":\"" + mkey + "\",";
  j += "\"profileSnapshot\":{\"melodySemi\":[";

  for (int k = 0; k < MELODY_SLOTS; k++) {

    j += String(gMelodySemi[k]);

    if (k < MELODY_SLOTS - 1) {
      j += ",";
    }
  }

  j += "],\"brightness\":";
  j += String(gBrightness, 2);
  j += ",\"calmness\":";
  j += String(gCalmness, 2);
  j += ",\"densityBias\":";
  j += String(gDensityBias, 2);

  appendInfluencesJson(j, mkey);

  j += "},\"updatedAtMs\":";
  j += String(millis());
  j += "}";

  return j;
}

// =====================================================
// SAVE STATE
// =====================================================

bool saveEchoStateToFs() {

  File f =
    LittleFS.open(ECHO_STATE_FILE, "w");

  if (!f) {
    return false;
  }

  f.println("ECHO_STATE_V2");

  f.println(MELODY_SLOTS);

  for (int k = 0; k < MELODY_SLOTS; k++) {

    f.print(gMelodySemi[k]);

    f.print(
      k < MELODY_SLOTS - 1
        ? ","
        : "\n"
    );
  }

  f.println(gBrightness, 3);
  f.println(gCalmness, 3);
  f.println(gDensityBias, 3);
  f.println(echoModelTypeKey());
  f.println(millis());

  f.close();

  refreshSonicAdvertising();

  return true;
}

// =====================================================
// LOAD STATE
// =====================================================

bool loadEchoStateFromFs() {

  if (!LittleFS.exists(ECHO_STATE_FILE)) {
    return false;
  }

  File f =
    LittleFS.open(ECHO_STATE_FILE, "r");

  if (!f) {
    return false;
  }

  String ver =
    f.readStringUntil('\n');

  ver.trim();

  if (ver != "ECHO_STATE_V2") {

    f.close();

    return false;
  }

  String nLine =
    f.readStringUntil('\n');

  int n =
    nLine.toInt();

  if (n != MELODY_SLOTS) {

    f.close();

    return false;
  }

  String semiLine =
    f.readStringUntil('\n');

  int pos = 0;

  for (int k = 0; k < MELODY_SLOTS; k++) {

    int comma =
      semiLine.indexOf(',', pos);

    String tok =
      (comma < 0)
        ? semiLine.substring(pos)
        : semiLine.substring(pos, comma);

    tok.trim();

    gMelodySemi[k] =
      tok.toInt();

    pos =
      (comma < 0)
        ? semiLine.length()
        : comma + 1;
  }

  gBrightness =
    f.readStringUntil('\n').toFloat();

  gCalmness =
    f.readStringUntil('\n').toFloat();

  gDensityBias =
    f.readStringUntil('\n').toFloat();

  f.readStringUntil('\n');
  f.readStringUntil('\n');

  f.close();

  Serial.println("Loaded echo_state from LittleFS");

  return true;
}

// =====================================================
// EVOLUTION
// =====================================================

void tryEvolutionForDevice(int idx) {

  if (
    idx < 0 ||
    idx >= MAX_DEVICES ||
    !devices[idx].active
  ) {
    return;
  }

  int beforeSemi[MELODY_SLOTS];

  for (int k = 0; k < MELODY_SLOTS; k++) {
    beforeSemi[k] = gMelodySemi[k];
  }

  float beforeBr = gBrightness;
  float beforeCa = gCalmness;
  float beforeDe = gDensityBias;

  const int* pool = nullptr;
  int plen = 0;

  if (devices[idx].type == "BOUNCE") {

    static const int p[] =
      {0, 4, 7, 4, 9, 7, 4, 0};

    pool = p;
    plen = 8;
  }

  else if (devices[idx].type == "SHY") {

    static const int p[] =
      {0, 3, 5, 7, 5, 3, 0, 0};

    pool = p;
    plen = 8;
  }

  else if (devices[idx].type == "MESSY") {

    static const int p[] =
      {0, 1, 3, 6, 10, 8, 5, 2};

    pool = p;
    plen = 8;
  }

  else {

    static const int p[] =
      {0, 4, 7, 11};

    pool = p;
    plen = 4;
  }

  int pi = random(0, plen);

  int orig0 = pool[pi];
  int orig1 = pool[(pi + 1) % plen];

  int ins = random(0, MELODY_SLOTS);

  int transpose =
    random(0, 2) == 0 ? 1 : -1;

  int trans0 = orig0 + transpose;

  if (trans0 < 0) {
    trans0 += 12;
  }

  if (trans0 > 12) {
    trans0 -= 12;
  }

  int trans1 = trans0;

  gMelodySemi[ins] = trans0;

  if (devices[idx].type == "SHY") {

    gCalmness =
      clampf(gCalmness + 0.018f, 0.0f, 1.0f);

    gBrightness =
      clampf(gBrightness - 0.008f, 0.0f, 1.0f);
  }

  else if (devices[idx].type == "BOUNCE") {

    gBrightness =
      clampf(gBrightness + 0.015f, 0.0f, 1.0f);

    gDensityBias =
      clampf(gDensityBias + 0.012f, 0.0f, 1.0f);
  }

  else if (devices[idx].type == "MESSY") {

    gDensityBias =
      clampf(gDensityBias + 0.018f, 0.0f, 1.0f);

    gCalmness =
      clampf(gCalmness - 0.012f, 0.0f, 1.0f);
  }

  else {

    gBrightness =
      clampf(gBrightness + 0.010f, 0.0f, 1.0f);
  }

  unsigned long durSec =
    (
      millis() -
      devices[idx].veryCloseStartMs
    ) / 1000UL;

  float closeAvg =
    rssiToEvolutionCloseness(
      devices[idx].smoothRSSI
    );

  String evoHexId =
    String((uint32_t)esp_random(), HEX);

  String j = "{";

  j += "\"v\":1,\"id\":\"" + evoHexId + "\",";
  j += "\"sourceTarget\":\"" + devices[idx].name + "\",";
  j += "\"trigger\":{\"durationSec\":";
  j += String((unsigned long)durSec);
  j += ",\"closenessAvg\":";
  j += String(closeAvg, 2);
  j += "},\"beforeState\":{\"melodySemi\":[";

  for (int k = 0; k < MELODY_SLOTS; k++) {

    j += String(beforeSemi[k]);

    if (k < MELODY_SLOTS - 1) {
      j += ",";
    }
  }

  j += "],\"brightness\":";
  j += String(beforeBr, 1);
  j += ",\"calmness\":";
  j += String(beforeCa, 1);
  j += ",\"densityBias\":";
  j += String(beforeDe, 1);
  j += "},\"afterState\":{\"melodySemi\":[";

  for (int k = 0; k < MELODY_SLOTS; k++) {

    j += String(gMelodySemi[k]);

    if (k < MELODY_SLOTS - 1) {
      j += ",";
    }
  }

  j += "],\"brightness\":";
  j += String(gBrightness, 1);
  j += ",\"calmness\":";
  j += String(gCalmness, 1);
  j += ",\"densityBias\":";
  j += String(gDensityBias, 1);
  j += "},\"borrowedFragment\":{\"original\":[";

  j += String(orig0);
  j += ",";
  j += String(orig1);

  j += "],\"transposed\":[";

  j += String(trans0);
  j += ",";
  j += String(trans1);

  j += "],\"insertedAt\":";
  j += String(ins);
  j += "},\"createdAtMs\":";
  j += String(millis());
  j += "}";

  appendEvolutionJsonl(j);

  Serial.println("EVOLUTION logged to LittleFS");

  saveEchoStateToFs();
}