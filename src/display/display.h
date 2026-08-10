/*
 * SmallOLED-PCMonitor - Display Module
 *
 * Display initialization and global display object.
 * Supports both SSD1306 and SH1106 displays via compile-time selection.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#if DISPLAY_INTERFACE == 1
  #include <SPI.h>
#else
  #include <Wire.h>
#endif
#include <Adafruit_GFX.h>
#include "../config/user_config.h"

// Include appropriate display library based on DISPLAY_TYPE
#ifndef DISPLAY_TYPE
  #define DISPLAY_TYPE DEFAULT_DISPLAY_TYPE
#endif

// Display type 1: SH1106 (1.3") - has 132x64 RAM, driver applies 2-column offset
// Display type 2: CH1116 (1.54") - SH1106-compatible, uses a 1-column offset
#if DISPLAY_TYPE == 1 || DISPLAY_TYPE == 2
  #if DISPLAY_TYPE == 2
    #include "ch1116.h"
    extern Adafruit_CH1116 display;
  #else
    #include <Adafruit_SH110X.h>
    extern Adafruit_SH1106G display;
  #endif
  #ifndef DISPLAY_WHITE
    #define DISPLAY_WHITE SH110X_WHITE
  #endif
  #ifndef DISPLAY_BLACK
    #define DISPLAY_BLACK SH110X_BLACK
  #endif
// Display type 0: SSD1306 (0.96") - 128x64 RAM, no offset. Also drives 2.42"
// SSD1309 panels, which use the same SSD1306 driver.
#else
  #include <Adafruit_SSD1306.h>
  extern Adafruit_SSD1306 display;
  #ifndef DISPLAY_WHITE
    #define DISPLAY_WHITE SSD1306_WHITE
  #endif
  #ifndef DISPLAY_BLACK
    #define DISPLAY_BLACK SSD1306_BLACK
  #endif
#endif

// Initialize display - returns true on success
bool initDisplay();
void applyDisplayBrightness();
// Returns false when the scheduled target could not be resolved (no valid time).
bool refreshDisplayBrightnessNow();
void checkScheduledBrightness();

// True when the schedule resolves to a dark panel, or when the boot hold is
// keeping it dark because there is no clock yet to evaluate the schedule with.
bool scheduledDisplayIsOff();

// Force the panel readable for provisioning screens and keep scheduled dimming
// off its back until the next reboot.
void ensureDisplayVisible();

// Brightness actually pushed to the panel (0 = powered off), for status reporting.
uint8_t getLastAppliedBrightness();

// Wake-ups discarded by the schedule confirmation step, for status reporting.
uint16_t getSuppressedScheduleWakeCount();

// Runtime display control (HTTP API) - not persisted to flash
void setDisplayForcedOff(bool off);
bool isDisplayForcedOff();
void setDisplayBrightnessPercent(uint8_t percent);

#if TOUCH_BUTTON_ENABLED
bool handleTemporaryDisplayWake();
void updateTemporaryDisplayWake();
#endif

#endif // DISPLAY_H
