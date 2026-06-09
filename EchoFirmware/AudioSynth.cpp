#include "AudioSynth.h"
#include "Globals.h"
#include "ReedDock.h"

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
    return 60.0f;
  }

  if (String(MY_NAME).indexOf("SHY") >= 0) {
    return 56.0f;
  }

  if (String(MY_NAME).indexOf("MESSY") >= 0) {
    return 88.0f;
  }

  return 60.0f;
}

struct PeerVoiceState {
  float phase1;
  float phase2;
  float freq1;
  float freq2;
  float env;
  float envDecay;
  float cutoff;
  float lowpassState;
  float mixGain;
  String voiceType;
};

static PeerVoiceState gPeerVoice[MAX_DEVICES];

static int gFocusSlot = -1;
static unsigned long gFocusTurnStartMs = 0;

static int gAudibleSlots[MAX_DEVICES];
static int gAudibleCount = 0;

static float rootMidiForType(String type) {

  if (type == "BOUNCE") {
    return 60.0f;
  }

  if (type == "SHY") {
    return 56.0f;
  }

  if (type == "MESSY") {
    return 88.0f;
  }

  return 60.0f;
}

static int factoryMelodySemi(
  String type,
  int slot
) {

  slot %= MELODY_SLOTS;

  if (type == "BOUNCE") {

    static const int m[] =
      {0, 4, 7, 4, 9, 7, 4, 0};

    return m[slot];
  }

  if (type == "SHY") {

    static const int m[] =
      {0, 3, 5, 7, 5, 3, 0, 0};

    return m[slot];
  }

  static const int m[] =
    {0, 2, 4, 7, 9, 11, 7, 4};

  return m[slot];
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
  0.78f,
  0.9860f,
  780.0f,
  620.0f,
  0.10f,
  0.22f,
  240,
  {0, 2, 0, 4, 2, 5, 3, 1, 0, 0},
  8,
  {0.32f, 0.28f, 0.72f, 0.30f, 0.34f, 0.78f, 0.32f, 0.36f, 1.0f, 1.0f},
  8,
  {0, 2, 4, 7, 9, 12, 0, 0},
  6,
  {0, 4, 7, 5, 0},
  4
};

const SoundPersonality SHY_SOUND = {
  0.36f,
  0.9986f,
  820.0f,
  480.0f,
  0.34f,
  0.28f,
  880,
  {0, 1, 0, 2, 1, 0, 3, 2, 0, 0},
  8,
  {1.35f, 1.15f, 1.65f, 1.25f, 1.50f, 1.10f, 1.80f, 1.30f, 1.0f, 1.0f},
  8,
  {0, 3, 5, 7, 10, 12, 0, 0},
  6,
  {0, 5, 10, 7, 0},
  4
};

const SoundPersonality MESSY_SOUND = {
  0.44f,
  0.9938f,
  3600.0f,
  1100.0f,
  0.06f,
  0.14f,
  78,
  {0, 2, 1, 4, 2, 5, 3, 6, 1, 4},
  10,
  {0.20f, 0.17f, 0.34f, 0.18f, 0.24f, 0.15f, 0.38f, 0.19f, 0.28f, 0.16f},
  10,
  {0, 2, 4, 7, 9, 11, 12, 14, 16},
  9,
  {0, 4, 7, 11, 9},
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
  float richness,
  bool useSelfMelody
) {
  int phraseIndex = triggerIndex / 8;
  int phraseStep = triggerIndex % 8;

  int progressionRoot =
    p.progression[phraseIndex % p.progressionCount];

  int stepIndex =
    p.steps[phraseStep % p.stepCount];

  int motifSemi = useSelfMelody
    ? gMelodySemi[stepIndex % MELODY_SLOTS]
    : factoryMelodySemi(
        type,
        stepIndex
      );

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
  else if (type == "MESSY" && phraseStep > 5 && richness > 0.58f) {
    octave = 7;
  }
  else if (type == "SHY" && phraseStep == 7 && richness < 0.34f) {
    octave = 0;
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

    int m[] = {0, 4, 7, 4, 9, 7, 4, 0};

    for (int k = 0; k < MELODY_SLOTS; k++) {
      gMelodySemi[k] = m[k];
    }

    gBrightness = 0.58f;
    gCalmness = 0.42f;
    gDensityBias = 0.54f;
  }

  else if (String(MY_NAME).indexOf("SHY") >= 0) {

    int m[] = {0, 3, 5, 7, 5, 3, 0, 0};

    for (int k = 0; k < MELODY_SLOTS; k++) {
      gMelodySemi[k] = m[k];
    }

    gBrightness = 0.40f;
    gCalmness = 0.88f;
    gDensityBias = 0.26f;
  }

  else {

    int m[] = {0, 1, 3, 6, 10, 8, 5, 2};

    for (int k = 0; k < MELODY_SLOTS; k++) {
      gMelodySemi[k] = m[k];
    }

    gBrightness = 0.54f;
    gCalmness = 0.34f;
    gDensityBias = 0.66f;
  }

  for (int i = 0; i < MAX_DEVICES; i++) {
    resetPeerVoice(i);
  }
}

void resetPeerVoice(int slot) {

  if (slot < 0 || slot >= MAX_DEVICES) {
    return;
  }

  gPeerVoice[slot].env = 0.0f;
  gPeerVoice[slot].phase1 = 0.0f;
  gPeerVoice[slot].phase2 = 0.0f;
  gPeerVoice[slot].lowpassState = 0.0f;
  gPeerVoice[slot].voiceType = "";
  gPeerVoice[slot].mixGain = 0.0f;
}

static float samplePeerVoice(
  const String &voiceType,
  float p1,
  float p2
) {

  if (voiceType == "BOUNCE") {

    float tri = waveTriangle(p1);
    float pluck =
      (tri >= 0.0f ? 1.0f : -1.0f) *
      powf(fabsf(tri), 0.55f);

    float body = waveSine(p2) * 0.18f;

    return pluck * 0.82f + body;
  }

  if (voiceType == "SHY") {

    float s1 = waveSine(p1);
    float s2 = waveTriangle(p2);

    return s1 * 0.78f + s2 * 0.22f;
  }

  float flute = waveSine(p1);
  float h2 = waveSine(p1 * 2.0f) * 0.14f;
  float h3 = waveSine(p1 * 3.0f) * 0.05f;
  float wing =
    sinf(p1 * TWO_PI * 9.0f) * 0.08f;
  float shimmer = waveSine(p2) * 0.10f;

  return flute * 0.76f + h2 + h3 + wing + shimmer;
}

static void triggerPeerNote(
  int slot,
  String peerType,
  float closeness,
  unsigned long now
) {

  if (slot < 0 || slot >= MAX_DEVICES) {
    return;
  }

  String voiceType = peerType;

  if (
    voiceType != "BOUNCE" &&
    voiceType != "SHY" &&
    voiceType != "MESSY"
  ) {
    return;
  }

  const SoundPersonality& voice =
    soundForType(voiceType);

  const float root = rootMidiForType(voiceType);

  const float richness =
    richnessFromCloseness(closeness);

  const bool sparse = richness < 0.32f;
  const bool rich = richness > 0.50f;

  const int arpIdx = devices[slot].arpTriggerIndex;

  int semi =
    phraseSemi(
      voice,
      voiceType,
      arpIdx,
      richness,
      false
    );

  int harmony =
    harmonySemi(
      voice,
      voiceType,
      semi,
      arpIdx,
      richness
    );

  PeerVoiceState &pv = gPeerVoice[slot];

  pv.voiceType = voiceType;
  pv.freq1 = midiToFreq(root + (float)semi);

  if (sparse) {
    pv.freq2 = pv.freq1;
  }
  else if (rich) {
    pv.freq2 = midiToFreq(root + (float)harmony);
  }
  else if (voiceType == "BOUNCE") {
    pv.freq2 = pv.freq1 * 1.004f;
  }
  else if (voiceType == "SHY") {
    pv.freq2 = pv.freq1 * 1.002f;
  }
  else if (voiceType == "MESSY") {
    pv.freq2 =
      rich
        ? pv.freq1 * 2.004f
        : pv.freq1 * 1.010f;
  }
  else {
    pv.freq2 = pv.freq1 * 1.004f;
  }

  pv.env =
    voice.env *
    (sparse ? 0.38f : (0.42f + richness * 0.58f));

  pv.envDecay = voice.envDecay;
  pv.cutoff =
    voice.baseCutoff + closeness * voice.closenessCutoff;
  pv.mixGain =
    sparse ? (0.22f + richness * 0.35f) : (0.30f + richness * 0.55f);

  float rhythm =
    voice.rhythm[arpIdx % voice.rhythmCount];

  long humanize =
    (long)((unitArp(arpIdx, 41) - 0.5f) *
      (voiceType == "MESSY" ? 28.0f :
       voiceType == "SHY" ? 90.0f : 40.0f));

  float distanceRate = 1.40f - richness * 0.70f;

  if (voiceType == "SHY") {
    distanceRate = 1.55f - richness * 0.28f;
  }
  else if (voiceType == "BOUNCE") {
    distanceRate = 1.05f - richness * 0.45f;
  }
  else if (voiceType == "MESSY") {
    distanceRate = 0.75f - richness * 0.48f;
  }

  if (sparse) {
    distanceRate *= 1.55f;
  }
  else if (rich) {
    distanceRate *= 0.72f;
  }

  long interval =
    (long)((float)voice.rateMs * rhythm * distanceRate) + humanize;

  long minInterval = 120;

  if (voiceType == "SHY") {
    minInterval = sparse ? 520 : 300;
  }
  else if (voiceType == "BOUNCE") {
    minInterval = sparse ? 320 : 130;
  }
  else if (voiceType == "MESSY") {
    minInterval = sparse ? 150 : 36;
  }

  if (interval < minInterval) {
    interval = minInterval;
  }

  devices[slot].arpTriggerIndex++;
  devices[slot].nextNoteMs = now + (unsigned long)interval;
}

static void rebuildAudiblePeerList() {

  gAudibleCount = 0;

  for (int i = 0; i < MAX_DEVICES; i++) {

    if (!devices[i].active) {
      continue;
    }

    if (
      rssiToCloseness(devices[i].smoothRSSI) > 0.02f
    ) {
      gAudibleSlots[gAudibleCount] = i;
      gAudibleCount++;
    }
  }
}

static int audibleListIndexOf(int slot) {

  for (int k = 0; k < gAudibleCount; k++) {

    if (gAudibleSlots[k] == slot) {
      return k;
    }
  }

  return -1;
}

static void advancePeerTurn(unsigned long now) {

  rebuildAudiblePeerList();

  if (gAudibleCount == 0) {

    if (gFocusSlot >= 0) {
      resetPeerVoice(gFocusSlot);
    }

    gFocusSlot = -1;
    return;
  }

  const int focusIdx =
    audibleListIndexOf(gFocusSlot);

  const bool focusValid = focusIdx >= 0;

  const bool turnExpired =
    focusValid &&
    (now - gFocusTurnStartMs >= PEER_AUDIO_TURN_MS);

  if (focusValid && !turnExpired) {
    return;
  }

  if (focusValid && turnExpired && gAudibleCount == 1) {
    gFocusTurnStartMs = now;
    return;
  }

  int nextListIdx = 0;

  if (focusValid) {
    nextListIdx = (focusIdx + 1) % gAudibleCount;
  }

  const int oldFocus = gFocusSlot;

  gFocusSlot = gAudibleSlots[nextListIdx];
  gFocusTurnStartMs = now;

  if (oldFocus >= 0 && oldFocus != gFocusSlot) {

    resetPeerVoice(oldFocus);
    devices[oldFocus].nextNoteMs = 0;
  }

  for (int k = 0; k < gAudibleCount; k++) {

    const int slot = gAudibleSlots[k];

    if (slot != gFocusSlot) {
      resetPeerVoice(slot);
      devices[slot].nextNoteMs = 0;
    }
  }

  devices[gFocusSlot].nextNoteMs = 0;
}

bool hasAudiblePeers() {

  rebuildAudiblePeerList();

  if (gAudibleCount > 0) {
    return true;
  }

  if (
    gFocusSlot >= 0 &&
    gPeerVoice[gFocusSlot].env > 0.002f
  ) {
    return true;
  }

  return false;
}

void updatePeerAudio(unsigned long now) {

  if (dockLatched) {
    gFocusSlot = -1;
    return;
  }

  advancePeerTurn(now);

  if (gFocusSlot < 0) {
    return;
  }

  const int i = gFocusSlot;

  if (!devices[i].active) {
    return;
  }

  const float closeness =
    rssiToCloseness(devices[i].smoothRSSI);

  if (closeness <= 0.02f) {
    return;
  }

  if (
    devices[i].nextNoteMs == 0 ||
    now >= devices[i].nextNoteMs
  ) {
    triggerPeerNote(
      i,
      devices[i].type,
      closeness,
      now
    );
  }
}

// =====================================================
// AUDIO RENDER
// =====================================================

static void renderSilence() {

  static int16_t silence[BUFFER_SIZE * 2];

  size_t bytesWritten;

  i2s_write(
    I2S_NUM_0,
    silence,
    sizeof(silence),
    &bytesWritten,
    portMAX_DELAY
  );
}

void renderAudio() {

  static int16_t buffer[BUFFER_SIZE * 2];

  if (dockLatched || !hasAudiblePeers()) {

    gFocusSlot = -1;

    for (int i = 0; i < MAX_DEVICES; i++) {
      gPeerVoice[i].env = 0.0f;
    }

    env = 0.0f;
    renderSilence();
    return;
  }

  const int focus =
    (gFocusSlot >= 0) ? gFocusSlot : -1;

  for (int i = 0; i < BUFFER_SIZE; i++) {

    float mix = 0.0f;

    if (focus >= 0) {

      PeerVoiceState &pv = gPeerVoice[focus];

      if (pv.env >= 0.001f) {

        float dry =
          samplePeerVoice(
            pv.voiceType,
            pv.phase1,
            pv.phase2
          );

        dry *= pv.env * pv.mixGain;

        float alpha =
          1.0f -
          expf(-2.0f * PI * pv.cutoff / SAMPLE_RATE);

        alpha = clampf(alpha, 0.001f, 0.95f);

        pv.lowpassState +=
          alpha * (dry - pv.lowpassState);

        mix = pv.lowpassState;

        advancePhase(pv.phase1, pv.freq1);
        advancePhase(pv.phase2, pv.freq2);

        pv.env *= pv.envDecay;
      }
    }

    float delayed =
      delayBuffer[delayIndex];

    float out =
      mix + delayed * delayWet;

    delayBuffer[delayIndex] =
      mix + delayed * delayFeedback;

    delayIndex++;

    if (delayIndex >= DELAY_SIZE) {
      delayIndex = 0;
    }

    out = tanhf(out * 1.25f);

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