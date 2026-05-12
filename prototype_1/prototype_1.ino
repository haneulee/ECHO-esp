#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

// =========================
// I2S PIN SETTINGS
// 네 wiring에 맞게 바꿔도 됨
// MAX98357A 기준:
// BCLK -> BCLK
// LRCLK -> LRC / WS
// DOUT -> DIN
// =========================
#define I2S_BCLK  4
#define I2S_LRCLK 5
#define I2S_DOUT  6

#define SAMPLE_RATE 22050
#define BUFFER_SIZE 128

// =========================
// Echo Type
// =========================
#define TYPE_BOUNCE 0
#define TYPE_SHY    1
#define TYPE_MESSY  2

// 여기만 바꿔서 테스트
#define ECHO_TYPE TYPE_BOUNCE
// #define ECHO_TYPE TYPE_SHY
// #define ECHO_TYPE TYPE_MESSY

// =========================
// Synth State
// =========================
float phase1 = 0.0f;
float phase2 = 0.0f;

float freq1 = 440.0f;
float freq2 = 441.5f;

float env = 0.0f;
float envDecay = 0.9995f;

float lowpassState = 0.0f;
float cutoff = 1200.0f;

unsigned long nextNoteTime = 0;
int melodyStep = 0;

// simple delay
#define DELAY_SIZE 4096
float delayBuffer[DELAY_SIZE];
int delayIndex = 0;
float delayFeedback = 0.18f;
float delayWet = 0.16f;

// =========================
// Utils
// =========================
float clampf(float x, float a, float b) {
  if (x < a) return a;
  if (x > b) return b;
  return x;
}

float midiToFreq(float midi) {
  return 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f);
}

float waveSaw(float phase) {
  return phase * 2.0f - 1.0f;
}

float waveTriangle(float phase) {
  if (phase < 0.5f) {
    return -1.0f + phase * 4.0f;
  } else {
    return 3.0f - phase * 4.0f;
  }
}

float waveSine(float phase) {
  return sinf(phase * TWO_PI);
}

float waveNoise() {
  return random(-1000, 1000) / 1000.0f;
}

void advancePhase(float &phase, float freq) {
  phase += freq / SAMPLE_RATE;
  if (phase >= 1.0f) phase -= 1.0f;
}

// =========================
// I2S Setup
// =========================
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

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// =========================
// Echo Type Behavior
// =========================
void triggerNote() {
  // closeness는 Prototype 1에서는 가짜로 자동 변화시킴
  // 나중에 BLE RSSI 값으로 교체할 예정
  float closeness = 0.5f + 0.5f * sinf(millis() * 0.00045f);

#if ECHO_TYPE == TYPE_BOUNCE
  // Mr. Bounce style
  // flexible / anti-gravity / energetic
  int melody[] = {0, 4, 7, 9, 7, 4, 2, 0};
  int len = 8;

  float rootMidi = 72; // C5
  int semi = melody[melodyStep % len];

  freq1 = midiToFreq(rootMidi + semi);
  freq2 = freq1 * 1.003f;

  env = 0.75f + closeness * 0.25f;
  envDecay = 0.992f - closeness * 0.003f;

  cutoff = 900.0f + closeness * 2600.0f;
  delayWet = 0.12f;
  delayFeedback = 0.16f;

  unsigned long interval = 140 + (unsigned long)((1.0f - closeness) * 260.0f);
  nextNoteTime = millis() + interval;

  melodyStep++;

#elif ECHO_TYPE == TYPE_SHY
  // Little Miss Shy style
  // bashful / modest / timid
  int melody[] = {0, 2, 4, 7, 4, 2};
  int len = 6;

  float rootMidi = 60; // C4
  int semi = melody[melodyStep % len];

  freq1 = midiToFreq(rootMidi + semi);
  freq2 = freq1 * 1.001f;

  env = 0.28f + closeness * 0.18f;
  envDecay = 0.9985f;

  cutoff = 450.0f + closeness * 900.0f;
  delayWet = 0.22f;
  delayFeedback = 0.28f;

  unsigned long interval = 850 + random(0, 900);
  nextNoteTime = millis() + interval;

  // Shy는 가끔 말을 안 함
  if (random(0, 100) < 35) {
    env = 0.0f;
  }

  melodyStep++;

#elif ECHO_TYPE == TYPE_MESSY
  // Mr. Messy style
  // careless / chaotic / sloppy
  int melody[] = {0, 1, 5, 7, 10, 3, 8, 2};
  int len = 8;

  float rootMidi = 55; // G3
  int semi = melody[random(0, len)];

  freq1 = midiToFreq(rootMidi + semi);
  freq2 = freq1 * (0.985f + random(0, 30) * 0.001f);

  env = 0.45f + closeness * 0.35f;
  envDecay = 0.989f + random(0, 50) * 0.0001f;

  cutoff = 500.0f + random(0, 2500);
  delayWet = 0.18f;
  delayFeedback = 0.22f + random(0, 20) * 0.01f;

  unsigned long interval = random(80, 520);
  nextNoteTime = millis() + interval;

  melodyStep++;
#endif
}

// =========================
// Audio Render
// =========================
void renderAudio() {
  static int16_t buffer[BUFFER_SIZE * 2]; // stereo

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float dry = 0.0f;

#if ECHO_TYPE == TYPE_BOUNCE
    float s1 = waveTriangle(phase1);
    float s2 = waveSaw(phase2) * 0.35f;
    dry = (s1 * 0.75f + s2 * 0.25f) * env;

#elif ECHO_TYPE == TYPE_SHY
    float s1 = waveSine(phase1);
    float s2 = waveTriangle(phase2) * 0.2f;
    dry = (s1 * 0.85f + s2 * 0.15f) * env;

#elif ECHO_TYPE == TYPE_MESSY
    float s1 = waveSaw(phase1);
    float s2 = waveTriangle(phase2);
    float n = waveNoise() * 0.08f;
    dry = (s1 * 0.55f + s2 * 0.35f + n) * env;
#endif

    advancePhase(phase1, freq1);
    advancePhase(phase2, freq2);

    // Envelope decay
    env *= envDecay;
    if (env < 0.0005f) env = 0.0f;

    // Simple lowpass filter
    float alpha = 1.0f - expf(-2.0f * PI * cutoff / SAMPLE_RATE);
    alpha = clampf(alpha, 0.001f, 0.95f);
    lowpassState += alpha * (dry - lowpassState);

    float filtered = lowpassState;

    // Simple delay
    float delayed = delayBuffer[delayIndex];
    float out = filtered + delayed * delayWet;

    delayBuffer[delayIndex] = filtered + delayed * delayFeedback;
    delayIndex++;
    if (delayIndex >= DELAY_SIZE) delayIndex = 0;

    // master volume
    // out *= 0.45f;
    // out = clampf(out, -1.0f, 1.0f);

    out = tanhf(out * 2.0f);
    out *= 1.0f;

    int16_t sample = (int16_t)(out * 32767.0f);

    buffer[i * 2] = sample;
    buffer[i * 2 + 1] = sample;
  }

  size_t bytesWritten;
  i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
}

// =========================
// Setup / Loop
// =========================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Echo Prototype 1 - Type Synth Test");

#if ECHO_TYPE == TYPE_BOUNCE
  Serial.println("Type: BOUNCE");
#elif ECHO_TYPE == TYPE_SHY
  Serial.println("Type: SHY");
#elif ECHO_TYPE == TYPE_MESSY
  Serial.println("Type: MESSY");
#endif

  randomSeed(esp_random());

  for (int i = 0; i < DELAY_SIZE; i++) {
    delayBuffer[i] = 0.0f;
  }

  setupI2S();

  nextNoteTime = millis() + 500;
}

void loop() {
  if (millis() >= nextNoteTime) {
    triggerNote();
  }

  renderAudio();
}