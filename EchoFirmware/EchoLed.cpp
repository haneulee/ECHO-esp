#include "EchoLed.h"
#include "EchoSleep.h"
#include "EchoState.h"
#include "Globals.h"
#include "Utils.h"

#include <Adafruit_NeoPixel.h>

// =====================================================
// LED RING
// =====================================================

static Adafruit_NeoPixel sRing(
  LED_RING_COUNT,
  LED_RING_PIN,
  NEO_GRB + NEO_KHZ800
);

// =====================================================
// COLOR HELPERS (type hardcoded: bounce=yellow, messy=pink, shy=blue)
// =====================================================

struct EchoRgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static EchoRgb selfEchoColor() {

  const String type = echoModelTypeKey();

  if (type == "bounce") {
    return {255, 200, 0};
  }

  if (type == "messy") {
    return {255, 80, 140};
  }

  if (type == "shy") {
    return {60, 120, 255};
  }

  return {255, 200, 0};
}

static void setPixelScaled(
  int index,
  const EchoRgb &color,
  float level
) {

  if (level < 0.0f) {
    level = 0.0f;
  }

  if (level > 1.0f) {
    level = 1.0f;
  }

  const uint8_t scale = (uint8_t)(level * 255.0f);

  sRing.setPixelColor(
    index,
    (uint16_t)color.r * scale / 255,
    (uint16_t)color.g * scale / 255,
    (uint16_t)color.b * scale / 255
  );
}

static float maxEchoCloseness() {

  float best = 0.0f;

  for (int i = 0; i < MAX_DEVICES; i++) {

    if (!devices[i].active) {
      continue;
    }

    const float c =
      rssiToCloseness(devices[i].smoothRSSI);

    if (c > best) {
      best = c;
    }
  }

  return best;
}

// =====================================================
// ANIMATIONS (sequential chase: 0 → 1 → 2 → … around ring)
// =====================================================

static void renderChaseWave(
  unsigned long now,
  unsigned long stepMs,
  int trailLen,
  float peakLevel
) {

  const EchoRgb color = selfEchoColor();

  for (int i = 0; i < LED_RING_COUNT; i++) {
    sRing.setPixelColor(i, 0, 0, 0);
  }

  if (stepMs < 40UL) {
    stepMs = 40UL;
  }

  if (trailLen < 1) {
    trailLen = 1;
  }

  if (trailLen > LED_RING_COUNT) {
    trailLen = LED_RING_COUNT;
  }

  const int head =
    (int)((now / stepMs) % (unsigned long)LED_RING_COUNT);

  for (int t = 0; t < trailLen; t++) {

    const int idx =
      (head - t + LED_RING_COUNT) % LED_RING_COUNT;

    float level = peakLevel;

    if (t == 0) {
      level = peakLevel;
    }
    else if (t == 1) {
      level = peakLevel * 0.42f;
    }
    else {
      level = peakLevel * 0.18f;
    }

    setPixelScaled(idx, color, level);
  }
}

static void renderDockAnimation(unsigned long now) {

  const EchoRgb color = selfEchoColor();

  const float breath =
    sinf((now % 4800UL) / 4800.0f * TWO_PI) * 0.5f + 0.5f;

  const float level =
    0.10f + breath * 0.90f;

  for (int i = 0; i < LED_RING_COUNT; i++) {
    setPixelScaled(i, color, level);
  }
}

static void renderEchoAnimation(
  unsigned long now,
  float closeness
) {

  unsigned long stepMs =
    (unsigned long)(300.0f - closeness * 210.0f);

  const int trail =
    closeness > 0.55f ? 3 : 2;

  const float peak =
    0.55f + closeness * 0.45f;

  renderChaseWave(
    now,
    stepMs,
    trail,
    peak
  );
}

// =====================================================
// PUBLIC API
// =====================================================

void setupEchoLed() {

  sRing.begin();
  sRing.setBrightness(LED_RING_BRIGHTNESS);
  echoLedAllOff();
}

void echoLedAllOff() {

  for (int i = 0; i < LED_RING_COUNT; i++) {
    sRing.setPixelColor(i, 0, 0, 0);
  }

  sRing.show();
}

void updateEchoLed(unsigned long now) {

  if (isSleepModeActive()) {
    echoLedAllOff();
    return;
  }

  if (dockLatched) {
    renderDockAnimation(now);
    sRing.show();
    return;
  }

  const float closeness = maxEchoCloseness();

  if (closeness > 0.02f) {
    renderEchoAnimation(now, closeness);
    sRing.show();
    return;
  }

  echoLedAllOff();
}
