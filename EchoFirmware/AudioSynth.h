#pragma once

#include "Utils.h"

// =====================================================
// AUDIO / SYNTH
// =====================================================

void setupI2S();

// Schedule + trigger notes for each nearby peer (never self).
void updatePeerAudio(unsigned long now);

bool hasAudiblePeers();

void resetPeerVoice(int slot);

// Clear peer-audio scheduler after dock/undock transitions.
void resetPeerAudioFocus();

void renderAudio();

// =====================================================
// PERSONALITY / MELODY
// =====================================================

float personalityRootMidi();

void initEchoMelodyState();
