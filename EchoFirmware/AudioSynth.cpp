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

int gArpTriggerIndex = 0;

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
    return 67.0f;
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
// PLANTASIA-STYLE ARPEGGIO MODEL
// =====================================================

struct SoundPersonality {
  float env;
  float envDecay;
  float baseCutoff;
  float closenessCutoff;
  float delayWet;
  float delayFeedback;
  unsigned long rateMs;

  int steps[10];
  int stepCount;

  float rhythm[10];
  int rhythmCount;

  int scale[9];
  int scaleCount;

  int progression[5];
  int progressionCount;
};

const SoundPersonality BOUNCE_SOUND = {
  0.58f,
  0.9962f,
  1350.0f,
  1650.0f,
  0.18f,
  0.34f,
  320,
  {0, 2, 1, 3, 4, 2, 5, 3, 0, 0},
  8,
  {0.72f, 0.56f, 1.12f, 0.64f, 0.82f, 0.52f, 1.28f, 0.58f, 1.0f, 1.0f},
  8,
  {0, 2, 4, 7, 9, 12, 14, 16, 0},
  8,
  {0, 7, 9, 4, 0},
  4
};

const SoundPersonality SHY_SOUND = {
  0.36f,
  0.9988f,
  560.0f,
  620.0f,
  0.36f,
  0.34f,
  840,
  {0, 1, 2, 1, 0, 3, 2, 1, 0, 0},
  8,
  {1.25f, 0.90f, 1.55f, 1.05f, 1.40f, 0.95f, 1.70f, 1.10f, 1.0f, 1.0f},
  8,
  {0, 2, 4, 7, 9, 12, 14, 0, 0},
  7,
  {0, 4, 9, 7, 0},
  4
};

const SoundPersonality MESSY_SOUND = {
  0.50f,
  0.9958f,
  1150.0f,
  1550.0f,
  0.28f,
  0.34f,
  260,
  {0, 3, 1, 5, 2, 6, 4, 7, 1, 4},
  10,
  {0.62f, 0.38f, 0.94f, 0.44f, 0.70f, 0.36f, 1.08f, 0.48f, 0.82f, 0.40f},
  10,
  {0, 2, 3, 5, 7, 9, 10, 12, 14},
  9,
  {0, 5, 10, 7, 3},
  5
};

const SoundPersonality& soundForType(String type) {
  if (type == "BOUNCE") return BOUNCE_SOUND;
  if (type == "SHY") return SHY_SOUND;
  return MESSY_SOUND;
}

uint32_t hashArp(
  int triggerIndex,
  int salt
) {
  uint32_t h = 2166136261UL;
  const char* name = MY_NAME;

  while (*name) {
    h ^= (uint8_t)(*name);
    h *= 16777619UL;
    name++;
  }

  h ^= (uint32_t)triggerIndex * 374761393UL;
  h *= 16777619UL;
  h ^= (uint32_t)salt * 668265263UL;
  h *= 16777619UL;

  return h;
}

float unitArp(
  int triggerIndex,
  int salt
) {
  return (hashArp(triggerIndex, salt) & 0xFFFFFF) / 16777215.0f;
}

int nearestScaleSemi(
  int semi,
  const SoundPersonality& p
) {
  int octave = (semi / 12) * 12;
  int normalized = semi % 12;
  if (normalized < 0) normalized += 12;

  int best = octave + p.scale[0];
  int bestDistance = abs(best - (octave + normalized));

  for (int i = 1; i < p.scaleCount; i++) {
    int candidate = octave + p.scale[i];
    int dist = abs(candidate - (octave + normalized));
    if (dist < bestDistance) {
      best = candidate;
      bestDistance = dist;
    }
  }

  return best;
}

float richnessFromCloseness(
  float closeness
) {
  float c =
    clampf(
      (closeness - 0.08f) / 0.82f,
      0.0f,
      1.0f
    );

  return c * c * (3.0f - 2.0f * c);
}

int richnessLiftSemi(
  const SoundPersonality& p,
  String type,
  int semi,
  int triggerIndex,
  float richness
) {
  if (richness < 0.48f) {
    return semi;
  }

  float chance =
    unitArp(triggerIndex, 53);

  if (chance > richness) {
    return semi;
  }

  int lift = 7;

  if (type == "BOUNCE") {
    lift = 12;
  }
  else if (type == "MESSY") {
    lift = chance > 0.5f ? 4 : 7;
  }

  return nearestScaleSemi(
    semi + lift,
    p
  );
}

int harmonySemi(
  const SoundPersonality& p,
  String type,
  int semi,
  int triggerIndex,
  float richness
) {
  if (richness < 0.38f) {
    return semi;
  }

  int interval = 7;

  if (type == "BOUNCE") {
    interval = 12;
  }
  else if (type == "MESSY") {
    interval =
      unitArp(triggerIndex, 59) > 0.5f ? 4 : 7;
  }

  return nearestScaleSemi(
    semi + interval,
    p
  );
}

int phraseSemi(
  const SoundPersonality& p,
  String type,
  int triggerIndex,
  float richness
) {
  int phraseIndex = triggerIndex / 8;
  int phraseStep = triggerIndex % 8;

  int progressionRoot =
    p.progression[phraseIndex % p.progressionCount];

  int stepIndex =
    p.steps[phraseStep % p.stepCount];

  int motifSemi =
    gMelodySemi[stepIndex % MELODY_SLOTS];

  if (richness < 0.22f) {
    return nearestScaleSemi(
      progressionRoot + motifSemi,
      p
    );
  }

  int randomDegree =
    (int)(unitArp(triggerIndex, 11) * p.scaleCount);
  if (randomDegree >= p.scaleCount) randomDegree = p.scaleCount - 1;

  int contour = 0;
  if (phraseStep == 3 || phraseStep == 6) {
    contour = 1;
  }
  else if (phraseStep != 0 && unitArp(triggerIndex, 17) > 0.68f) {
    contour = -1;
  }

  int scaleSemi =
    p.scale[(randomDegree + phraseStep + contour + p.scaleCount) % p.scaleCount];

  int blended =
    unitArp(triggerIndex, 23) > 0.42f
      ? scaleSemi
      : nearestScaleSemi(motifSemi, p);

  int octave = 0;
  if (type == "BOUNCE" && phraseStep > 4 && richness > 0.44f) {
    octave = 12;
  }
  else if (type == "MESSY" && phraseStep > 4 && richness > 0.38f) {
    octave = 12;
  }
  else if (type == "SHY" && phraseStep == 7 && richness < 0.34f) {
    octave = -12;
  }

  int semi =
    nearestScaleSemi(
      progressionRoot + blended + octave,
      p
    );

  return richnessLiftSemi(
    p,
    type,
    semi,
    triggerIndex,
    richness
  );
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

  const SoundPersonality& p =
    soundForType(type);

  float root = personalityRootMidi();
  float richness =
    richnessFromCloseness(closeness);

  int semi =
    phraseSemi(
      p,
      type,
      gArpTriggerIndex,
      richness
    );

  int harmony =
    harmonySemi(
      p,
      type,
      semi,
      gArpTriggerIndex,
      richness
    );

  if (type == "BOUNCE") {

    freq1 =
      midiToFreq(root + (float)semi);

    freq2 =
      richness > 0.38f
        ? midiToFreq(root + (float)harmony)
        : freq1 * 1.004f;

    env =
      p.env * (0.58f + richness * 0.42f);

    envDecay = p.envDecay;

    cutoff =
      p.baseCutoff + closeness * p.closenessCutoff;

    delayWet =
      clampf(p.delayWet * (0.55f + richness * 0.72f), 0.06f, 0.34f);
  }

  else if (type == "SHY") {

    freq1 =
      midiToFreq(root + (float)semi);

    freq2 =
      richness > 0.42f
        ? midiToFreq(root + (float)harmony)
        : freq1 * 1.001f;

    env =
      p.env * (0.54f + richness * 0.46f);

    envDecay = p.envDecay;

    cutoff =
      p.baseCutoff + closeness * p.closenessCutoff;

    delayWet =
      clampf(p.delayWet * (0.62f + richness * 0.55f), 0.10f, 0.42f);
  }

  else if (type == "MESSY") {

    freq1 =
      midiToFreq(root + (float)semi);

    freq2 =
      richness > 0.36f
        ? midiToFreq(root + (float)harmony)
        : freq1 *
          (0.98f + (int)(unitArp(gArpTriggerIndex, 31) * 20.0f) * 0.001f);

    env =
      p.env * (0.56f + richness * 0.44f);

    envDecay = p.envDecay;

    cutoff =
      p.baseCutoff +
      (closeness * 0.72f + unitArp(gArpTriggerIndex, 37) * 0.28f) *
      p.closenessCutoff;

    delayWet =
      clampf(p.delayWet * (0.58f + richness * 0.68f), 0.08f, 0.42f);
  }

  delayFeedback =
    clampf(p.delayFeedback * (0.62f + richness * 0.58f), 0.16f, 0.46f);

  float rhythm =
    p.rhythm[gArpTriggerIndex % p.rhythmCount];

  long humanize =
    (long)((unitArp(gArpTriggerIndex, 41) - 0.5f) *
      (type == "MESSY" ? 80.0f : 35.0f));

  float distanceRate =
    1.62f - richness * 0.72f;

  long interval =
    (long)(p.rateMs * rhythm * distanceRate) + humanize;

  if (interval < 120) {
    interval = 120;
  }

  nextNoteTime =
    millis() + (unsigned long)interval;

  gArpTriggerIndex++;
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
      float s2 = waveSine(phase2);

      dry = s1 * 0.62f + s2 * 0.38f;
    }

    else if (String(MY_NAME).indexOf("SHY") >= 0) {

      float s1 = waveSine(phase1);
      float s2 = waveTriangle(phase2);

      dry = s1 * 0.88f + s2 * 0.12f;
    }

    else {

      float s1 = waveSaw(phase1);
      float s2 = waveTriangle(phase2);
      float s3 = waveSine((phase1 + phase2) * 0.5f);

      float n = waveNoise() * 0.035f;

      dry = s1 * 0.26f + s2 * 0.36f + s3 * 0.28f + n;
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

    out = tanhf(out * 1.35f);

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