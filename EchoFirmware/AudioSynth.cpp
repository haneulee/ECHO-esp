#include "AudioSynth.h"

// =====================================================
// AUDIO GLOBALS
// =====================================================

float phase1 = 0.0f;
float phase2 = 0.0f;

float env = 0.0f;
float envDecay = 0.9992f;

float freq1 = 440.0f;
float freq2 = 441.0f;

float lowpassState = 0.0f;
float cutoff = 1400.0f;

// Second LP stage + Plantasia-style smoothing (file-local)
static float gLowpass2 = 0.0f;
static float gFcSmoothed = 1400.0f;
static float gEnvVis = 0.0f;
static float gVibratoPhase = 0.0f;

unsigned long nextNoteTime = 0;

float delayBuffer[DELAY_SIZE];
int delayIndex = 0;

float delayWet = 0.15f;
float delayFeedback = 0.22f;

// =====================================================
// Peer timbre (Tone: sprout→BOUNCE, moss→SHY, fern→MESSY)
// 0 = MY_NAME timbre; 1 sprout; 2 moss; 3 fern
// =====================================================

static uint8_t gPeerRenderPreset = 0;

static bool sSwingLong = false;

static uint8_t gStepSprout = 0;
static uint8_t gStepMoss = 0;
static uint8_t gStepFern = 0;

static const float kPlantasiaBpm = 76.0f;
static const float kSecPerBeat = 60.0f / kPlantasiaBpm;

static unsigned long schedulePlantIntervalMs(
  uint8_t preset,
  float c
) {

  float swing =
    sSwingLong
      ? 1.16f
      : 0.84f;

  sSwingLong = !sSwingLong;

  float beats = 1.0f;

  if (preset == 1) {

    beats = 0.5f;
  }

  // Far: long gaps; close: short gaps (dominates over swing for clear distance feel)
  float tempoFromClose =
    1.50f * (1.0f - c) +
    0.62f * c;

  float baseMs =
    kSecPerBeat *
    beats *
    swing *
    1.08f *
    1000.0f;

  float ms = baseMs * tempoFromClose;

  // Less jitter when close — rhythm tightens like Tone "locking in"
  float jitterScale =
    1.0f - c * 0.55f;

  ms += (float)random(-55, 56) * jitterScale;

  if (ms < 140.0f) {
    ms = 140.0f;
  }

  return (unsigned long)ms;
}

// =====================================================
// ECHO STATE GLOBALS
// =====================================================

int gMelodySemi[MELODY_SLOTS];

float gBrightness = 0.62f;
float gCalmness = 0.84f;
float gDensityBias = 0.38f;

// =====================================================
// I2S
// =====================================================

void setupI2S() {

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(
    I2S_NUM_0,
    &i2s_config,
    0,
    NULL
  );

  i2s_set_pin(
    I2S_NUM_0,
    &pin_config
  );

  i2s_zero_dma_buffer(I2S_NUM_0);

  gFcSmoothed = cutoff;
  lowpassState = 0.0f;
  gLowpass2 = 0.0f;
  gEnvVis = 0.0f;
  gVibratoPhase = 0.0f;
}

// =====================================================
// PERSONALITY ROOT
// =====================================================

float personalityRootMidi() {

  if (String(MY_NAME).indexOf("BOUNCE") >= 0) {
    return 72.0f;
  }

  if (String(MY_NAME).indexOf("SHY") >= 0) {
    return 60.0f;
  }

  if (String(MY_NAME).indexOf("MESSY") >= 0) {
    return 55.0f;
  }

  return 60.0f;
}

// =====================================================
// INIT MELODY
// =====================================================

void initEchoMelodyState() {

  if (String(MY_NAME).indexOf("BOUNCE") >= 0) {

    int m[] = {0, 4, 7, 9, 7, 4, 2, 0};

    for (int k = 0; k < MELODY_SLOTS; k++) {
      gMelodySemi[k] = m[k];
    }
  }

  else if (String(MY_NAME).indexOf("SHY") >= 0) {

    int m[] = {0, 2, 4, 7, 4, 2, 0, 0};

    for (int k = 0; k < MELODY_SLOTS; k++) {
      gMelodySemi[k] = m[k];
    }
  }

  else {

    int m[] = {0, 1, 5, 7, 10, 3, 8, 2};

    for (int k = 0; k < MELODY_SLOTS; k++) {
      gMelodySemi[k] = m[k];
    }
  }
}

// =====================================================
// PERSONALITY SYNTH (neighbor BLE type → Tone plant voices)
// =====================================================

void triggerPersonality(
  String type,
  float closeness
) {

  float c = clampf(closeness, 0.0f, 1.0f);

  uint8_t preset = 0;

  if (type == "BOUNCE") {

    preset = 1;
  }

  else if (type == "SHY") {

    preset = 2;
  }

  else if (type == "MESSY") {

    preset = 3;
  }

  if (preset == 0) {

    gPeerRenderPreset = 0;

    nextNoteTime =
      millis() +
      600UL;

    return;
  }

  float prob = 0.0f;

  if (preset == 1) {

    prob = 0.06f + c * 0.90f;
  }

  else if (preset == 2) {

    prob = 0.10f + c * 0.82f;
  }

  else {

    prob = 0.05f + c * 0.88f;
  }

  nextNoteTime =
    millis() +
    schedulePlantIntervalMs(preset, c);

  if ((float)random(0, 10000) / 10000.0f >= prob) {

    return;
  }

  gPeerRenderPreset = preset;

  if (preset == 1) {

    // Sprout (BOUNCE): 8n-ish grid, saw lead, C4 contour
    static const int mel[] = {
      60, 64, 67, 69, 67, 64, 62, 60
    };

    static const int var[] = {
      62, 64, 67, 69
    };

    int midi =
      random(0, 1000) < 780
        ? mel[gStepSprout % 8]
        : var[random(0, 4)];

    gStepSprout++;

    float detCents =
      ((float)random(0, 1000) / 1000.0f - 0.5f) *
      5.0f;

    float f0 =
      midiToFreq((float)midi) *
      powf(2.0f, detCents / 1200.0f);

    freq1 = f0;

    freq2 =
      f0 *
      powf(2.0f, 1.2f / 1200.0f);

    env = 0.82f;

    envDecay =
      c > 0.68f
        ? 0.9922f
        : 0.9948f - (c * 0.00055f);

    cutoff =
      800.0f + c * 1350.0f +
      (float)random(-85, 86);

    cutoff = clampf(cutoff, 500.0f, 11000.0f);

    delayWet = 0.12f;
    delayFeedback = 0.16f;
  }

  else if (preset == 2) {

    // Moss (SHY): same contour as Tone C2 line, transposed to C6 (very high pipe / recorder)
    static const int mel[] = {
      84, 91, 93, 91, 88, 93, 86, 93
    };

    static const int varHi[] = {
      84, 88, 91, 93, 96
    };

    int midi =
      random(0, 1000) < 860
        ? mel[gStepMoss % 8]
        : varHi[random(0, 5)];

    gStepMoss++;

    freq1 = midiToFreq((float)midi);

    freq2 =
      freq1 *
      1.0015f;

    env = 0.74f;

    envDecay =
      0.9975f - c * 0.00085f;

    cutoff =
      5200.0f + c * 4200.0f +
      (float)random(-120, 121);

    cutoff = clampf(cutoff, 2000.0f, 14000.0f);

    delayWet = 0.06f;
    delayFeedback = 0.10f;
  }

  else {

    // Fern (MESSY): 4n grid, detuned saws + noise
    static const int mel[] = {
      55, 57, 60, 62, 60, 57, 55, 52
    };

    static const int mid[] = {
      48, 50, 52, 55, 57, 60
    };

    int midi =
      random(0, 1000) < 740
        ? mel[gStepFern % 8]
        : mid[random(0, 6)];

    gStepFern++;

    float detCents =
      ((float)random(0, 1000) / 1000.0f - 0.5f) *
      6.0f;

    float f0 =
      midiToFreq((float)midi) *
      powf(2.0f, detCents / 1200.0f);

    freq1 = f0;

    freq2 =
      f0 *
      powf(2.0f, 6.5f / 1200.0f);

    env = 0.80f;

    envDecay =
      (c > 0.65f)
        ? 0.9932f - (c - 0.65f) * 0.004f
        : 0.9948f - c * 0.0006f;

    cutoff =
      650.0f + c * 1100.0f +
      (float)random(-90, 91);

    cutoff = clampf(cutoff, 400.0f, 9500.0f);

    delayWet = 0.14f;
    delayFeedback = 0.15f;
  }
}

// =====================================================
// AUDIO RENDER
// =====================================================

void renderAudio() {

  static int16_t buffer[BUFFER_SIZE * 2];

  for (int i = 0; i < BUFFER_SIZE; i++) {

    gVibratoPhase += 2.05f / (float)SAMPLE_RATE;

    if (gVibratoPhase >= 1.0f) {
      gVibratoPhase -= 1.0f;
    }

    float vb =
      sinf(gVibratoPhase * TWO_PI) *
      0.0042f;

    if (gPeerRenderPreset == 2) {

      vb *= 1.38f;
    }

    else if (gPeerRenderPreset == 1) {

      vb *= 1.20f;
    }

    else if (gPeerRenderPreset == 3) {

      vb *= 1.06f;
    }

    float f1 = freq1 * (1.0f + vb);
    float f2 = freq2 * (1.0f - 0.62f * vb);

    float dry = 0.0f;

    if (gPeerRenderPreset == 1) {

      float a = waveSprout(phase1, 0.58f);
      float b = waveSprout(phase2, 0.30f) * 0.34f;
      float edge = waveSaw(phase1) * 0.075f;

      dry = a * 0.70f + b + edge;
    }

    else if (gPeerRenderPreset == 2) {

      float r1 = waveRecorder(phase1, 0.64f);
      float r2 = waveRecorder(phase2, 0.36f) * 0.30f;

      dry = r1 * 0.78f + r2;
    }

    else if (gPeerRenderPreset == 3) {

      float a = waveFern(phase1, 0.56f);
      float b = waveFern(phase2, 0.38f) * 0.36f;

      dry = a * 0.74f + b;
    }

    else {

      if (String(MY_NAME).indexOf("BOUNCE") >= 0) {

        float a = waveSprout(phase1, 0.52f);
        float b = waveSprout(phase2, 0.28f) * 0.32f;

        dry = a * 0.72f + b + waveSaw(phase1) * 0.065f;
      }

      else if (String(MY_NAME).indexOf("SHY") >= 0) {

        float r1 = waveRecorder(phase1, 0.58f);
        float r2 = waveRecorder(phase2, 0.34f) * 0.28f;

        dry = r1 * 0.76f + r2;
      }

      else {

        float a = waveFern(phase1, 0.52f);
        float b = waveFern(phase2, 0.35f) * 0.34f;

        dry = a * 0.73f + b;
      }
    }

    gEnvVis +=
      (env - gEnvVis) *
      0.092f;

    dry *= gEnvVis;

    advancePhase(phase1, f1);
    advancePhase(phase2, f2);

    env *= envDecay;

    gFcSmoothed +=
      0.040f *
      (cutoff - gFcSmoothed);

    float alpha =
      1.0f -
      expf(-2.0f * PI * gFcSmoothed / SAMPLE_RATE);

    alpha = clampf(alpha, 0.001f, 0.95f);

    lowpassState +=
      alpha * (dry - lowpassState);

    gLowpass2 +=
      alpha * (lowpassState - gLowpass2);

    float filtered = gLowpass2;

    float delayed =
      delayBuffer[delayIndex];

    float out =
      filtered + delayed * delayWet;

    delayBuffer[delayIndex] =
      filtered +
      delayed * delayFeedback;

    delayIndex++;

    if (delayIndex >= DELAY_SIZE) {
      delayIndex = 0;
    }

    out = tanhf(out * 1.62f);

    int16_t sample =
      (int16_t)(
        out *
        32767.0f *
        AUDIO_GAIN
      );

    buffer[i * 2] = sample;
    buffer[i * 2 + 1] = sample;
  }

  size_t bytesWritten;

  i2s_write(
    I2S_NUM_0,
    buffer,
    sizeof(buffer),
    &bytesWritten,
    portMAX_DELAY
  );
}