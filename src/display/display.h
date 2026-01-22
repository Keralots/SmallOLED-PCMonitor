/*
 * SmallOLED-PCMonitor - Display Module
 *
 * Display initialization and global display object.
 * Supports both SSD1306 and SH1106 displays via compile-time selection.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include "../config/user_config.h"

// Include appropriate display library based on DISPLAY_TYPE
#ifndef DISPLAY_TYPE
  #define DISPLAY_TYPE DEFAULT_DISPLAY_TYPE
#endif

// Display type 1: SH1106 (1.3") - has 132x64 RAM, driver applies 2-column offset
#if DISPLAY_TYPE == 1
  #include <Adafruit_SH110X.h>
  extern Adafruit_SH1106G display;
  #ifndef DISPLAY_WHITE
    #define DISPLAY_WHITE SH110X_WHITE
  #endif
  #ifndef DISPLAY_BLACK
    #define DISPLAY_BLACK SH110X_BLACK
  #endif
// Display types 0 & 2: SSD1306 (0.96") and SSD1309 (2.42") - both have 128x64 RAM, no offset
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

#endif // DISPLAY_H
