#pragma once

#include "Utils.h"

// =====================================================
// AUDIO / SYNTH
// =====================================================

void setupI2S();

void triggerPersonality(
  String type,
  float closeness
);

void renderAudio();

// =====================================================
// PERSONALITY / MELODY
// =====================================================

float personalityRootMidi();

void initEchoMelodyState();