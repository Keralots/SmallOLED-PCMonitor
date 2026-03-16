/*
 * SmallOLED-PCMonitor - Display Module
 *
 * Display initialization and global display object.
 * Supports both SSD1306 and SH1106 displays via compile-time selection.
 */

#include "display.h"
#include "../config/config.h"
#include <time.h>

// Track last applied brightness to avoid unnecessary updates
uint8_t lastAppliedBrightness = 255;
unsigned long lastBrightnessCheck = 0;
const unsigned long BRIGHTNESS_CHECK_INTERVAL = 60000; // Check every minute
extern bool isTemporaryWake; // Tells the compiler this exists in the main file

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
  if (!displayAvailable) return;
  
  // If the screen is currently in its 5-second "Temporary Wake" state, 
  // don't let the background settings override it.
  if (isTemporaryWake) return; 

  uint8_t val = settings.displayBrightness;
#if DISPLAY_TYPE == 1
  // --- SH1106 Logic ---
  if (val == 0) {
    display.oled_command(0xAE); // [SH110X_DISPLAYOFF](https://adafruit.github.io)
    Serial.println("SH1106: Display Power OFF");
  } else {
    display.oled_command(0xAF); // [SH110X_DISPLAYON](https://adafruit.github.io)
    // Map user 1-255 to your hardware visible floor 20-255
    uint8_t hardwareContrast = map(val, 1, 255, 20, 255);
    display.setContrast(hardwareContrast);
    Serial.printf("SH1106: Contrast Set to %d\n", hardwareContrast);
  }
#else
  // SSD1306/SSD1309: Use library's internal command method
  // Send 0x81 (contrast command) followed by value
  display.ssd1306_command(0x81);
  display.ssd1306_command(settings.displayBrightness);
#endif
}

// Check and apply time-based brightness (scheduled dimming)
void checkScheduledBrightness() {
  // Only check every minute to avoid unnecessary updates
  if (isTemporaryWake) return; // Don't let the schedule fight the touch button
  unsigned long currentTime = millis();
  if (currentTime - lastBrightnessCheck < BRIGHTNESS_CHECK_INTERVAL) {
    return;
  }
  lastBrightnessCheck = currentTime;

  // If scheduled dimming is disabled, ensure normal brightness is applied
  if (!settings.enableScheduledDimming) {
    if (lastAppliedBrightness != settings.displayBrightness) {
      applyDisplayBrightness();
      lastAppliedBrightness = settings.displayBrightness;
    }
    return;
  }

  // Get current time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return; // Can't get time, skip this check
  }

  uint8_t currentHour = timeinfo.tm_hour;
  uint8_t targetBrightness;

  // Check if current time is within dim period
  // Handle wrap-around case (e.g., 22:00 to 07:00)
  if (settings.dimStartHour == settings.dimEndHour) {
    // Same hour means no dim period — use normal brightness
    if (lastAppliedBrightness != settings.displayBrightness) {
      applyDisplayBrightness();
      lastAppliedBrightness = settings.displayBrightness;
    }
    return;
  }
  if (settings.dimStartHour < settings.dimEndHour) {
    // Normal case: start and end are in same day
    // e.g., 01:00 to 07:00
    if (currentHour >= settings.dimStartHour && currentHour < settings.dimEndHour) {
      targetBrightness = settings.dimBrightness;
    } else {
      targetBrightness = settings.displayBrightness;
    }
  } else {
    // Wrap-around case: spans midnight
    // e.g., 22:00 to 07:00
    if (currentHour >= settings.dimStartHour || currentHour < settings.dimEndHour) {
      targetBrightness = settings.dimBrightness;
    } else {
      targetBrightness = settings.displayBrightness;
    }
  }

// Apply brightness only if it changed
  if (lastAppliedBrightness != targetBrightness) {
    // 1. Backup the user's permanent setting
    uint8_t userSettingBackup = settings.displayBrightness;
    
    // 2. Temporarily swap the setting to the scheduled target (0-255)
    settings.displayBrightness = targetBrightness;
    
    // 3. This function handles the #if DISPLAY_TYPE, 0xAE/0xAF, and Mapping 20-255
    applyDisplayBrightness();
    
    // 4. Restore the user's setting so the Web UI doesn't break
    settings.displayBrightness = userSettingBackup;
    
    // 5. Track the change to prevent looping
    lastAppliedBrightness = targetBrightness;
    
    Serial.printf("Scheduled Dimming: %d applied\n", targetBrightness);
  }
}
