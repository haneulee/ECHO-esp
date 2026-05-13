#pragma once

#include "EncounterLog.h"
#include "AudioSynth.h"

// =====================================================
// ECHO STATE
// =====================================================

String echoModelTypeKey();

void appendInfluencesJson(
  String& j,
  const String& mkey
);

String buildEchoStateJsonWire();

bool saveEchoStateToFs();

bool loadEchoStateFromFs();

// =====================================================
// EVOLUTION
// =====================================================

void tryEvolutionForDevice(int idx);