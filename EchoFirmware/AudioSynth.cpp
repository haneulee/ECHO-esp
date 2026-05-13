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

unsigned long nextNoteTime = 0;

float delayBuffer[DELAY_SIZE];
int delayIndex = 0;

float delayWet = 0.15f;
float delayFeedback = 0.22f;

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
// PERSONALITY SYNTH
// =====================================================

void triggerPersonality(
  String type,
  float closeness
) {

  float root = personalityRootMidi();

  int semi =
    gMelodySemi[random(0, MELODY_SLOTS)];

  if (type == "BOUNCE") {

    freq1 =
      midiToFreq(root + (float)semi);

    freq2 =
      freq1 * 1.004f;

    env = 0.85f;

    envDecay = 0.995f;

    cutoff =
      1500 + closeness * 2200;

    delayWet = 0.12f;
  }

  else if (type == "SHY") {

    freq1 =
      midiToFreq(root + (float)semi);

    freq2 =
      freq1 * 1.001f;

    env = 0.70f;

    envDecay = 0.998f;

    cutoff =
      800 + closeness * 1200;

    delayWet = 0.20f;
  }

  else if (type == "MESSY") {

    freq1 =
      midiToFreq(root + (float)semi);

    freq2 =
      freq1 *
      (0.98f + random(0, 20) * 0.001f);

    env = 0.78f;

    envDecay = 0.992f;

    cutoff =
      1000 + random(0, 2000);

    delayWet = 0.25f;
  }

  unsigned long interval =
    300 - closeness * 220;

  nextNoteTime = millis() + interval;
}

// =====================================================
// AUDIO RENDER
// =====================================================

void renderAudio() {

  static int16_t buffer[BUFFER_SIZE * 2];

  for (int i = 0; i < BUFFER_SIZE; i++) {

    float dry = 0.0f;

    if (String(MY_NAME).indexOf("BOUNCE") >= 0) {

      float s1 = waveTriangle(phase1);
      float s2 = waveSine(phase2) * 0.4f;

      dry = s1 * 0.75f + s2 * 0.25f;
    }

    else if (String(MY_NAME).indexOf("SHY") >= 0) {

      float s1 = waveSine(phase1);
      float s2 = waveTriangle(phase2) * 0.2f;

      dry = s1 * 0.8f + s2 * 0.2f;
    }

    else {

      float s1 = waveSaw(phase1);
      float s2 = waveTriangle(phase2);

      float n = waveNoise() * 0.04f;

      dry = s1 * 0.5f + s2 * 0.35f + n;
    }

    dry *= env;

    advancePhase(phase1, freq1);
    advancePhase(phase2, freq2);

    env *= envDecay;

    float alpha =
      1.0f -
      expf(-2.0f * PI * cutoff / SAMPLE_RATE);

    alpha = clampf(alpha, 0.001f, 0.95f);

    lowpassState +=
      alpha * (dry - lowpassState);

    float filtered = lowpassState;

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

    out = tanhf(out * 2.3f);

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