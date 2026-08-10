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
// How often the schedule is re-evaluated. Ten seconds, not a minute: a single
// wrong hour reading used to light the panel for a whole minute before the next
// check could take it back, which is what #59 looked like from the room.
const unsigned long BRIGHTNESS_CHECK_INTERVAL = 10000;
// Short retry when the schedule could not be evaluated (NTP not synced yet).
// Without this a failed check costs a full BRIGHTNESS_CHECK_INTERVAL, which is
// how a reboot inside a lights-out window left the panel lit for a whole minute.
const unsigned long BRIGHTNESS_RETRY_INTERVAL = 2000;
// How long the boot hold below waits for a clock before giving up. Without a
// time source the schedule cannot be honoured at all, and a panel that stays
// dark forever reads as a dead device.
const unsigned long SCHEDULE_WAIT_TIMEOUT_MS = 120000;
// Floor for the provisioning screens, which have to be readable whatever the
// schedule wants.
const uint8_t PROVISIONING_MIN_BRIGHTNESS = 64;

// Runtime override: when true, the panel is held off (e.g. via HTTP /api/display/off).
// Scheduled dimming and brightness re-applies are suppressed so they don't turn it back on.
static bool displayForcedOff = false;

// Set at boot when the schedule could not be read yet, so the panel is sitting
// at the dim level on the assumption it is night. Cleared by the first check
// that gets a real answer.
static bool scheduleHoldActive = false;
// Brightening candidate awaiting a second agreeing check (see
// checkScheduledBrightness).
static uint8_t pendingBrighten = 0;
static bool pendingBrightenValid = false;
static uint16_t suppressedWakeCount = 0;
// Provisioning is on screen - hold the panel readable until the reboot that
// ends provisioning.
static bool provisioningOverride = false;

#if TOUCH_BUTTON_ENABLED
static bool temporaryWakeActive = false;
static unsigned long temporaryWakeExpiry = 0;
static uint8_t brightnessBeforeTemporaryWake = 255;
const unsigned long TEMPORARY_WAKE_DURATION_MS = 10000;
const uint8_t TEMPORARY_WAKE_BRIGHTNESS = 20;
#endif

static void setDisplayPower(bool on) {
#if DISPLAY_TYPE == 1 || DISPLAY_TYPE == 2
  display.oled_command(on ? 0xAF : 0xAE);
#else
  display.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
#endif
}

static void setDisplayContrast(uint8_t brightness) {
#if DISPLAY_TYPE == 1 || DISPLAY_TYPE == 2
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

// Resolve the brightness the schedule currently calls for. Reports the window
// verdict separately from the resolved value: the two brightness levels may be
// equal, so the value alone cannot tell a caller whether dimming is in effect.
// Returns false when there is no valid time to evaluate against.
static bool resolveScheduledBrightness(uint8_t &targetBrightness,
                                       bool &isDimPeriod) {
  targetBrightness = sanitizeBrightnessValue(settings.displayBrightness);
  isDimPeriod = false;

  if (!settings.enableScheduledDimming) {
    return true;
  }

  struct tm timeinfo;
  // Timeout 0: read the clock once and report. The default is a 5-second
  // blocking wait for time to become valid, which would stall the loop on
  // every 2s retry while NTP is still unsynced.
  if (!getLocalTime(&timeinfo, 0)) {
    return false;
  }

  const uint8_t currentHour = timeinfo.tm_hour;

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

static bool resolveScheduledBrightnessTarget(uint8_t &targetBrightness) {
  bool isDimPeriod = false;
  return resolveScheduledBrightness(targetBrightness, isDimPeriod);
}

// Initialize display - returns true on success
bool initDisplay() {
#if DISPLAY_INTERFACE == 1
  // SPI mode - remap ESP32-C3 SPI bus to our chosen pins
  SPI.begin(SPI_SCK_PIN, -1, SPI_MOSI_PIN, SPI_CS_PIN);

  for (int attempt = 0; attempt < 3; attempt++) {
  #if DISPLAY_TYPE == 1 || DISPLAY_TYPE == 2
    if (display.begin(0, true)) {  // SH1106/CH1116 SPI: address ignored, reset=true
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
  #if DISPLAY_TYPE == 1 || DISPLAY_TYPE == 2
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

// Apply display brightness from settings. Called from setup(), before WiFi and
// NTP, so most of the time there is no clock to consult yet.
void applyDisplayBrightness() {
#if TOUCH_BUTTON_ENABLED
  if (temporaryWakeActive) {
    return;
  }
#endif

  if (displayForcedOff) {
    return;
  }

  uint8_t targetBrightness = sanitizeBrightnessValue(settings.displayBrightness);
  if (resolveScheduledBrightnessTarget(targetBrightness)) {
    scheduleHoldActive = false;
    applyBrightnessLevel(targetBrightness);
    return;
  }

  // No clock: a schedule is configured but cannot be evaluated. Assume the dim
  // level rather than full brightness - booting into the middle of the night
  // must not light the room for however long WiFi and NTP take. Released by the
  // first check that resolves, or after SCHEDULE_WAIT_TIMEOUT_MS.
  scheduleHoldActive = true;
  applyBrightnessLevel(settings.dimBrightness);
}

bool refreshDisplayBrightnessNow() {
#if TOUCH_BUTTON_ENABLED
  if (temporaryWakeActive) {
    return true;
  }
#endif

  if (displayForcedOff || provisioningOverride) {
    return true;
  }

  uint8_t targetBrightness = sanitizeBrightnessValue(settings.displayBrightness);
  if (!resolveScheduledBrightnessTarget(targetBrightness)) {
    return false;
  }

  // On-demand callers - settings saved, panel switched back on, NTP just became
  // valid - are asking for the schedule to be applied now, so no confirmation
  // step here.
  scheduleHoldActive = false;
  pendingBrightenValid = false;

  if (lastAppliedBrightness != targetBrightness) {
    applyBrightnessLevel(targetBrightness);
  }
  return true;
}

// Check and apply time-based brightness (scheduled dimming)
void checkScheduledBrightness() {
#if TOUCH_BUTTON_ENABLED
  if (temporaryWakeActive) {
    return;
  }
#endif

  if (displayForcedOff || provisioningOverride) {
    return;
  }

  // The first check runs immediately - waiting a full interval after boot is
  // what left the panel lit for a minute when the device restarted inside a
  // scheduled-off window. Afterwards check on the interval, or retry quickly
  // while the time is still unavailable.
  static bool firstCheckDone = false;
  static unsigned long nextBrightnessCheck = 0;

  if (firstCheckDone && (long)(millis() - nextBrightnessCheck) < 0) {
    return;
  }
  firstCheckDone = true;

  uint8_t targetBrightness = sanitizeBrightnessValue(settings.displayBrightness);
  if (!resolveScheduledBrightnessTarget(targetBrightness)) {
    if (scheduleHoldActive && millis() > SCHEDULE_WAIT_TIMEOUT_MS) {
      scheduleHoldActive = false;
      applyBrightnessLevel(settings.displayBrightness);
      Serial.println("Dim schedule: no time source, using normal brightness");
    }
    nextBrightnessCheck = millis() + BRIGHTNESS_RETRY_INTERVAL;
    return;
  }

  scheduleHoldActive = false;
  nextBrightnessCheck = millis() + BRIGHTNESS_CHECK_INTERVAL;

  if (targetBrightness == lastAppliedBrightness) {
    if (pendingBrightenValid) {
      // A brighten was proposed one check ago and this check disagrees, so the
      // reading behind it was wrong. This is #59: something hands the schedule
      // an hour outside the dim window for a moment, and the panel used to act
      // on it immediately.
      pendingBrightenValid = false;
      suppressedWakeCount++;
      Serial.println("Dim schedule: unconfirmed wake-up ignored");
    }
    return;
  }

  if (targetBrightness < lastAppliedBrightness) {
    // Going darker is always safe to do at once.
    pendingBrightenValid = false;
    applyBrightnessLevel(targetBrightness);
    return;
  }

  // Brightening means leaving the dim window - the only transition a single bad
  // hour reading can fake. Require two agreeing checks, which costs at most one
  // BRIGHTNESS_CHECK_INTERVAL at a real hour boundary.
  if (pendingBrightenValid && pendingBrighten == targetBrightness) {
    pendingBrightenValid = false;
    applyBrightnessLevel(targetBrightness);
    return;
  }

  pendingBrighten = targetBrightness;
  pendingBrightenValid = true;
}

// True when the schedule calls for a dark panel. With no clock to evaluate
// against this reports the boot hold instead, so the extra boot screens stay
// off while the panel is being held dark on the assumption it is night.
bool scheduledDisplayIsOff() {
  uint8_t targetBrightness = 0;
  bool isDimPeriod = false;
  if (!resolveScheduledBrightness(targetBrightness, isDimPeriod)) {
    return scheduleHoldActive &&
           sanitizeBrightnessValue(settings.dimBrightness) == 0;
  }
  return targetBrightness == 0;
}

// Provisioning screens (AP portal, setup instructions, QR code) have to be
// readable whatever the schedule wants - a device being set up at night would
// otherwise look dead. Holds off scheduled dimming until the reboot that ends
// provisioning anyway.
void ensureDisplayVisible() {
  provisioningOverride = true;
  scheduleHoldActive = false;
  pendingBrightenValid = false;

  uint8_t targetBrightness = sanitizeBrightnessValue(settings.displayBrightness);
  if (targetBrightness < PROVISIONING_MIN_BRIGHTNESS) {
    targetBrightness = PROVISIONING_MIN_BRIGHTNESS;
  }
  applyBrightnessLevel(targetBrightness);
}

// Number of wake-ups the confirmation step above has thrown away. Stays 0 on a
// healthy device; anything else means the clock is being read wrong.
uint16_t getSuppressedScheduleWakeCount() {
  return suppressedWakeCount;
}

uint8_t getLastAppliedBrightness() {
  return lastAppliedBrightness;
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
