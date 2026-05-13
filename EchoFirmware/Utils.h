#pragma once

#include "Globals.h"

// =====================================================
// UTILS
// =====================================================

float clampf(float x, float a, float b);

float midiToFreq(float midi);

float waveSine(float p);

float waveTriangle(float p);

float waveSaw(float p);

float waveRecorder(float p, float breath);

float waveSprout(float p, float breath);

float waveFern(float p, float breath);

float waveNoise();

void advancePhase(float &p, float freq);

float rssiToCloseness(float rssi);

float rssiToEvolutionCloseness(float rssi);