/*
 * SmallOLED-PCMonitor - Display Module
 *
 * Display initialization and global display object.
 * Supports both SSD1306 and SH1106 displays via compile-time selection.
 */

#include "display.h"
#include "../config/config.h"
#include "../config/settings.h"
#include <time.h>

// Track last applied brightness to avoid unnecessary updates
static uint8_t lastAppliedBrightness = 255;
static unsigned long lastBrightnessCheck = 0;
const unsigned long BRIGHTNESS_CHECK_INTERVAL = 60000; // Check every minute

// Runtime override: when true, the panel is held off (e.g. via HTTP /api/display/off).
// Scheduled dimming and brightness re-applies are suppressed so they don't turn it back on.
static bool displayForcedOff = false;

#if TOUCH_BUTTON_ENABLED
static bool temporaryWakeActive = false;
static unsigned long temporaryWakeExpiry = 0;
static uint8_t brightnessBeforeTemporaryWake = 255;
const unsigned long TEMPORARY_WAKE_DURATION_MS = 10000;
const uint8_t TEMPORARY_WAKE_BRIGHTNESS = 20;
#endif

static void setDisplayPower(bool on) {
#if DISPLAY_TYPE == 1
  display.oled_command(on ? 0xAF : 0xAE);
#else
  display.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
#endif
}

static void setDisplayContrast(uint8_t brightness) {
#if DISPLAY_TYPE == 1
  display.setContrast(brightness);
#else
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(brightness);
#endif
}

static void applyBrightnessLevel(uint8_t brightness) {
  if (!displayAvailable) {
    return;
  }

  brightness = sanitizeBrightnessValue(brightness);

  if (brightness == 0) {
    setDisplayPower(false);
  } else {
    setDisplayPower(true);
    setDisplayContrast(brightness);
  }

  lastAppliedBrightness = brightness;
}

static bool resolveScheduledBrightnessTarget(uint8_t &targetBrightness) {
  targetBrightness = sanitizeBrightnessValue(settings.displayBrightness);

  if (!settings.enableScheduledDimming) {
    return true;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return false;
  }

  const uint8_t currentHour = timeinfo.tm_hour;
  bool isDimPeriod = false;

  if (settings.dimStartHour == settings.dimEndHour) {
    isDimPeriod = false;
  } else if (settings.dimStartHour < settings.dimEndHour) {
    isDimPeriod =
        (currentHour >= settings.dimStartHour && currentHour < settings.dimEndHour);
  } else {
    isDimPeriod =
        (currentHour >= settings.dimStartHour || currentHour < settings.dimEndHour);
  }

  targetBrightness = sanitizeBrightnessValue(
      isDimPeriod ? settings.dimBrightness : settings.displayBrightness);
  return true;
}

// Initialize display - returns true on success
bool initDisplay() {
#if DISPLAY_INTERFACE == 1
  // SPI mode - remap ESP32-C3 SPI bus to our chosen pins
  SPI.begin(SPI_SCK_PIN, -1, SPI_MOSI_PIN, SPI_CS_PIN);

  for (int attempt = 0; attempt < 3; attempt++) {
  #if DISPLAY_TYPE == 1
    if (display.begin(0, true)) {  // SH1106 SPI: address ignored, reset=true
      display.setContrast(255);
      return true;
    }
  #else
    if (display.begin(SSD1306_SWITCHCAPVCC)) {  // SSD1306 SPI: no address needed
      return true;
    }
  #endif
    delay(500);
  }
#else
  // I2C mode (default)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  for (int attempt = 0; attempt < 3; attempt++) {
  #if DISPLAY_TYPE == 1
    byte addrToTry = (attempt == 0) ? DISPLAY_I2C_ADDRESS : 0x3D;
    if (display.begin(addrToTry)) {
      display.setContrast(255);
      return true;
    }
  #else
    if (display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_I2C_ADDRESS)) {
      return true;
    }
  #endif
    delay(500);
  }
#endif

  return false;
}

// Apply display brightness from settings
void applyDisplayBrightness() {
#if TOUCH_BUTTON_ENABLED
  if (temporaryWakeActive) {
    return;
  }
#endif

  if (displayForcedOff) {
    return;
  }

  applyBrightnessLevel(settings.displayBrightness);
}

void refreshDisplayBrightnessNow() {
#if TOUCH_BUTTON_ENABLED
  if (temporaryWakeActive) {
    return;
  }
#endif

  if (displayForcedOff) {
    return;
  }

  uint8_t targetBrightness = settings.displayBrightness;
  if (settings.enableScheduledDimming &&
      !resolveScheduledBrightnessTarget(targetBrightness)) {
    return;
  }

  if (lastAppliedBrightness != targetBrightness) {
    applyBrightnessLevel(targetBrightness);
  }
}

// Check and apply time-based brightness (scheduled dimming)
void checkScheduledBrightness() {
#if TOUCH_BUTTON_ENABLED
  if (temporaryWakeActive) {
    return;
  }
#endif

  if (displayForcedOff) {
    return;
  }

  // Only check every minute to avoid unnecessary updates
  unsigned long currentTime = millis();
  if (currentTime - lastBrightnessCheck < BRIGHTNESS_CHECK_INTERVAL) {
    return;
  }
  lastBrightnessCheck = currentTime;

  refreshDisplayBrightnessNow();
}

// ---- Runtime display power / brightness control (HTTP API) ----

bool isDisplayForcedOff() {
  return displayForcedOff;
}

// Force the panel off (off=true) or restore normal/scheduled brightness (off=false).
void setDisplayForcedOff(bool off) {
  displayForcedOff = off;
  if (off) {
    applyBrightnessLevel(0); // sends panel-off command
  } else {
    refreshDisplayBrightnessNow(); // re-applies normal or scheduled brightness
  }
}

// Set display brightness from a 0-100 percentage and apply immediately.
// Updates the in-RAM "normal" brightness so scheduled dimming still layers on top.
// Not persisted to flash (runtime-only, like the on/off override).
void setDisplayBrightnessPercent(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  uint8_t brightness = (uint16_t)percent * 255 / 100;
  settings.displayBrightness = brightness;
  displayForcedOff = (brightness == 0);
  applyBrightnessLevel(brightness);
}

#if TOUCH_BUTTON_ENABLED
bool handleTemporaryDisplayWake() {
  if (!displayAvailable) {
    return false;
  }

  // Don't wake into a blank lit panel while the display is held off via HTTP.
  if (displayForcedOff) {
    return false;
  }

  if (temporaryWakeActive) {
    temporaryWakeExpiry = millis() + TEMPORARY_WAKE_DURATION_MS;
    return true;
  }

  if (lastAppliedBrightness != 0) {
    return false;
  }

  brightnessBeforeTemporaryWake = lastAppliedBrightness;
  temporaryWakeActive = true;
  temporaryWakeExpiry = millis() + TEMPORARY_WAKE_DURATION_MS;

  uint8_t wakeBrightness = settings.displayBrightness;
  if (wakeBrightness == 0) {
    wakeBrightness = settings.dimBrightness;
  }
  if (wakeBrightness < TEMPORARY_WAKE_BRIGHTNESS) {
    wakeBrightness = TEMPORARY_WAKE_BRIGHTNESS;
  }

  applyBrightnessLevel(wakeBrightness);
  Serial.println("Touch button: temporary display wake active");
  return true;
}

void updateTemporaryDisplayWake() {
  if (!temporaryWakeActive || millis() < temporaryWakeExpiry) {
    return;
  }

  temporaryWakeActive = false;

  uint8_t targetBrightness = brightnessBeforeTemporaryWake;
  if (resolveScheduledBrightnessTarget(targetBrightness)) {
    applyBrightnessLevel(targetBrightness);
  } else {
    applyBrightnessLevel(brightnessBeforeTemporaryWake);
  }

  Serial.println("Touch button: temporary display wake expired");
}
#endif
