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

// Soft recorder / flute: sine ladder + breath (noise scaled by breath 0..1)
float waveRecorder(float p, float breath) {

  float w = TWO_PI * p;

  float out =
    sinf(w) +
    0.14f * sinf(2.0f * w) +
    0.055f * sinf(3.0f * w) +
    0.024f * sinf(4.0f * w);

  breath = clampf(breath, 0.0f, 1.0f);

  out += breath * waveNoise() * 0.088f;

  return clampf(out, -1.0f, 1.0f);
}

// BOUNCE / sprout: bright pipe, forward 5th partials + airy breath (playful)
float waveSprout(float p, float breath) {

  float w = TWO_PI * p;

  float out =
    sinf(w) +
    0.11f * sinf(2.0f * w) +
    0.036f * sinf(3.0f * w) +
    0.062f * sinf(5.0f * w) +
    0.022f * sinf(6.0f * w);

  breath = clampf(breath, 0.0f, 1.0f);

  out += breath * waveNoise() * 0.10f;

  return clampf(out, -1.0f, 1.0f);
}

// MESSY / fern: warm, wide body + slight detuned burr, softer hiss
float waveFern(float p, float breath) {

  float w = TWO_PI * p;

  float out =
    sinf(w) +
    0.20f * sinf(2.0f * w) +
    0.088f * sinf(3.0f * w) +
    0.034f * sinf(4.0f * w);

  out += 0.058f * sinf(w * 2.07f);

  breath = clampf(breath, 0.0f, 1.0f);

  out += breath * waveNoise() * 0.064f;

  return clampf(out, -1.0f, 1.0f);
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
