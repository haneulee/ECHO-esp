#pragma once

#include "Config.h"

struct PeerSonicSnapshot {
  int melodySemi[MELODY_SLOTS];
  float brightness;
  float calmness;
  float densityBias;
  bool valid;
  bool fromBle;
};

void factorySonicForType(
  const String &type,
  PeerSonicSnapshot &out
);

bool parseSonicManufacturerData(
  const uint8_t *data,
  size_t len,
  PeerSonicSnapshot &out
);

void packSonicManufacturerData(
  uint8_t *out,
  size_t *outLen
);

void refreshSonicAdvertising();
