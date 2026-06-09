#include "EchoSleep.h"
#include "BleEcho.h"
#include "EchoLed.h"

#include <driver/i2s.h>
#include <esp_sleep.h>

// =====================================================
// SLEEP STATE
// =====================================================

static bool sSleepModeActive = false;

// =====================================================
// BUTTON HELPERS
// =====================================================

static bool buttonRawPressed() {

#if BUTTON_ACTIVE_LOW
  return digitalRead(BUTTON_PIN) == LOW;
#else
  return digitalRead(BUTTON_PIN) == HIGH;
#endif
}

static bool debouncedButtonPressed() {

  static bool stablePressed = false;
  static bool lastRaw = false;
  static unsigned long lastChangeMs = 0;

  const bool raw = buttonRawPressed();
  const unsigned long now = millis();

  if (raw != lastRaw) {
    lastRaw = raw;
    lastChangeMs = now;
  }

  if ((now - lastChangeMs) < BUTTON_DEBOUNCE_MS) {
    return false;
  }

  if (raw == stablePressed) {
    return false;
  }

  stablePressed = raw;
  return stablePressed;
}

static void waitForButtonRelease() {

  while (buttonRawPressed()) {
    delay(5);
  }

  delay(BUTTON_DEBOUNCE_MS);
}

// =====================================================
// PERIPHERAL SUSPEND / RESUME
// =====================================================

static void suspendForSleep() {

  echoLedAllOff();

  stopBLEForSleep();

  i2s_stop(I2S_NUM_0);

  Serial.println("Sleep mode ON — BLE/audio paused");
  Serial.flush();
}

static void resumeAfterSleep() {

  i2s_start(I2S_NUM_0);

  resumeBLEAfterSleep();

  Serial.println("Sleep mode OFF — BLE/audio resumed");
}

static void enterLightSleepUntilWake() {

  gpio_wakeup_enable(
    (gpio_num_t)BUTTON_PIN,
    GPIO_INTR_LOW_LEVEL
  );

  esp_sleep_enable_gpio_wakeup();

  while (true) {

    esp_light_sleep_start();

    waitForButtonRelease();
    break;
  }

  esp_sleep_disable_wakeup_source(
    ESP_SLEEP_WAKEUP_GPIO
  );
}

// =====================================================
// PUBLIC API
// =====================================================

void setupSleepButton() {

  pinMode(
    BUTTON_PIN,
#if BUTTON_ACTIVE_LOW
    INPUT_PULLUP
#else
    INPUT_PULLDOWN
#endif
  );
}

bool isSleepModeActive() {
  return sSleepModeActive;
}

bool processSleepMode() {

  if (!debouncedButtonPressed()) {
    return false;
  }

  if (!sSleepModeActive) {

    sSleepModeActive = true;

    waitForButtonRelease();

    suspendForSleep();

    enterLightSleepUntilWake();

    sSleepModeActive = false;

    resumeAfterSleep();

    return true;
  }

  return false;
}
