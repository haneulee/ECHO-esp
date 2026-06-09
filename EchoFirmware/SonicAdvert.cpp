#include "SonicAdvert.h"
#include "Globals.h"
#include "Utils.h"

static const uint8_t ECHO_SONIC_MAGIC_0 = 0xEC;
static const uint8_t ECHO_SONIC_MAGIC_1 = 0xE0;
static const uint8_t ECHO_SONIC_VERSION = 0x01;
static const size_t ECHO_SONIC_PACKET_LEN = 14;

void factorySonicForType(
  const String &type,
  PeerSonicSnapshot &out
) {

  for (int k = 0; k < MELODY_SLOTS; k++) {
    out.melodySemi[k] = 0;
  }

  out.brightness = 0.50f;
  out.calmness = 0.50f;
  out.densityBias = 0.50f;
  out.valid = true;
  out.fromBle = false;

  if (type == "BOUNCE") {

    static const int m[] = {0, 4, 7, 4, 9, 7, 4, 0};

    for (int k = 0; k < MELODY_SLOTS; k++) {
      out.melodySemi[k] = m[k];
    }

    out.brightness = 0.58f;
    out.calmness = 0.42f;
    out.densityBias = 0.54f;
    return;
  }

  if (type == "SHY") {

    static const int m[] = {0, 3, 5, 7, 5, 3, 0, 0};

    for (int k = 0; k < MELODY_SLOTS; k++) {
      out.melodySemi[k] = m[k];
    }

    out.brightness = 0.40f;
    out.calmness = 0.88f;
    out.densityBias = 0.26f;
    return;
  }

  static const int m[] = {0, 2, 4, 7, 9, 11, 7, 4};

  for (int k = 0; k < MELODY_SLOTS; k++) {
    out.melodySemi[k] = m[k];
  }

  out.brightness = 0.54f;
  out.calmness = 0.34f;
  out.densityBias = 0.66f;
}

bool parseSonicManufacturerData(
  const uint8_t *data,
  size_t len,
  PeerSonicSnapshot &out
) {

  if (data == nullptr || len < ECHO_SONIC_PACKET_LEN) {
    out.valid = false;
    return false;
  }

  if (
    data[0] != ECHO_SONIC_MAGIC_0 ||
    data[1] != ECHO_SONIC_MAGIC_1 ||
    data[2] != ECHO_SONIC_VERSION
  ) {
    out.valid = false;
    return false;
  }

  for (int k = 0; k < MELODY_SLOTS; k++) {
    out.melodySemi[k] = (int)data[3 + k];
  }

  out.brightness = data[11] / 255.0f;
  out.calmness = data[12] / 255.0f;
  out.densityBias = data[13] / 255.0f;
  out.valid = true;
  out.fromBle = true;

  return true;
}

void packSonicManufacturerData(
  uint8_t *out,
  size_t *outLen
) {

  if (out == nullptr || outLen == nullptr) {
    return;
  }

  out[0] = ECHO_SONIC_MAGIC_0;
  out[1] = ECHO_SONIC_MAGIC_1;
  out[2] = ECHO_SONIC_VERSION;

  for (int k = 0; k < MELODY_SLOTS; k++) {

    int semi = gMelodySemi[k];

    if (semi < 0) {
      semi = 0;
    }

    if (semi > 24) {
      semi = 24;
    }

    out[3 + k] = (uint8_t)semi;
  }

  out[11] = (uint8_t)(clampf(gBrightness, 0.0f, 1.0f) * 255.0f);
  out[12] = (uint8_t)(clampf(gCalmness, 0.0f, 1.0f) * 255.0f);
  out[13] = (uint8_t)(clampf(gDensityBias, 0.0f, 1.0f) * 255.0f);

  *outLen = ECHO_SONIC_PACKET_LEN;
}

void refreshSonicAdvertising() {

  if (pAdvertising == nullptr) {
    return;
  }

  uint8_t packet[ECHO_SONIC_PACKET_LEN];
  size_t packetLen = 0;

  packSonicManufacturerData(packet, &packetLen);

  std::string mfg(
    reinterpret_cast<const char *>(packet),
    packetLen
  );

  NimBLEAdvertisementData advData;

  advData.setName(MY_NAME);
  advData.setManufacturerData(mfg);

  pAdvertising->setAdvertisementData(advData);
}
