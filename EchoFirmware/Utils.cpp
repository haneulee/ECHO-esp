#include "Utils.h"

float clampf(float x, float a, float b) {
  if (x < a) return a;
  if (x > b) return b;
  return x;
}

float midiToFreq(float midi) {
  return 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f);
}

float waveSine(float p) {
  return sinf(p * TWO_PI);
}

float waveTriangle(float p) {
  if (p < 0.5f) return -1.0f + p * 4.0f;
  return 3.0f - p * 4.0f;
}

float waveSaw(float p) {
  return p * 2.0f - 1.0f;
}

float waveNoise() {
  return random(-1000, 1000) / 1000.0f;
}

void advancePhase(float &p, float freq) {
  p += freq / SAMPLE_RATE;
  if (p >= 1.0f) p -= 1.0f;
}

float rssiToCloseness(float rssi) {
  float c = (rssi - (-92.0f)) / (-55.0f - (-92.0f));
  c = clampf(c, 0.0f, 1.0f);
  c = powf(c, 0.75f);
  return c;
}

float rssiToEvolutionCloseness(float rssi) {
  float span = EVOLUTION_RSSI_LINEAR_MAX - EVOLUTION_RSSI_LINEAR_MIN;
  float c = (rssi - EVOLUTION_RSSI_LINEAR_MIN) / span;
  c = clampf(c, 0.0f, 1.0f);
  c = powf(c, 0.75f);
  return c;
}
