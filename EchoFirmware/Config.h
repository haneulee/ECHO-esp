#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <NimBLEDevice.h>
#include <LittleFS.h>
#include <math.h>

// =====================================================
// UNIQUE MODEL NAME
// =====================================================

#define ECHO_UNIQUE_MODEL_NAME "ECHO_BOUNCE_001"
// #define ECHO_UNIQUE_MODEL_NAME "ECHO_SHY_001"
// #define ECHO_UNIQUE_MODEL_NAME "ECHO_MESSY_001"

#ifndef ECHO_UNIQUE_MODEL_NAME
#define ECHO_UNIQUE_MODEL_NAME "ECHO_BOUNCE_001"
#endif

#define MY_NAME ECHO_UNIQUE_MODEL_NAME

#define STATION_NAME "ECHO_station_001"

#ifndef ECHO_UNIT_CODE
#define ECHO_UNIT_CODE MY_NAME
#endif

// =====================================================
// HARDWARE
// =====================================================

#ifndef REED_PIN
#define REED_PIN 10
#endif

#ifndef REED_DOCK_ACTIVE_LOW
#define REED_DOCK_ACTIVE_LOW 1
#endif

#ifndef REED_DOCK_DEBOUNCE_MS
#define REED_DOCK_DEBOUNCE_MS 280UL
#endif

#ifndef REED_UNDOCK_DEBOUNCE_MS
#define REED_UNDOCK_DEBOUNCE_MS 420UL
#endif

#ifndef REED_DOCK_SAMPLES
#define REED_DOCK_SAMPLES 7
#endif

#ifndef REED_DOCK_ARMING_MS
#define REED_DOCK_ARMING_MS 400UL
#endif

#ifndef REED_POST_UNDOCK_COOLDOWN_MS
#define REED_POST_UNDOCK_COOLDOWN_MS 2000UL
#endif

#ifndef REED_DOCK_CONSEC_LOOPS
#define REED_DOCK_CONSEC_LOOPS 48
#endif

#ifndef REED_UNDOCK_CONSEC_LOOPS
#define REED_UNDOCK_CONSEC_LOOPS 32
#endif

#ifndef DOCK_STATION_SCAN_MS
#define DOCK_STATION_SCAN_MS 6000UL
#endif

#ifndef DOCK_STATION_RETRY_MS
#define DOCK_STATION_RETRY_MS 2500UL
#endif

#define I2S_BCLK  4
#define I2S_LRCLK 5
#define I2S_DOUT  6

// =====================================================
// AUDIO
// =====================================================

#define AUDIO_GAIN 0.5f

#define SAMPLE_RATE 22050
#define BUFFER_SIZE 128

#define DELAY_SIZE 4096

// =====================================================
// EVOLUTION
// =====================================================

#ifndef EVOLUTION_RSSI_LINEAR_MIN
#define EVOLUTION_RSSI_LINEAR_MIN -93.0f
#endif

#ifndef EVOLUTION_RSSI_LINEAR_MAX
#define EVOLUTION_RSSI_LINEAR_MAX -63.0f
#endif

#ifndef EVOLUTION_CLOSE_THRESHOLD
#define EVOLUTION_CLOSE_THRESHOLD 0.35f
#endif

#ifndef EVOLUTION_MIN_MS
#define EVOLUTION_MIN_MS 60000UL
#endif

#define MELODY_SLOTS 8

// =====================================================
// BLE
// =====================================================

#define MAX_DEVICES 10

// =====================================================
// FILESYSTEM
// =====================================================

#define ECHO_STATE_FILE "/echo_state.txt"

#ifndef ECHO_SOUND_PROFILE_ID_FW
#define ECHO_SOUND_PROFILE_ID_FW "ambient3_meditation_v1"
#endif

// =====================================================
// TIMING
// =====================================================

const unsigned long DEVICE_TIMEOUT = 5000;