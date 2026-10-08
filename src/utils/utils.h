/*
 * SmallOLED-PCMonitor - Utility Functions
 *
 * String manipulation, validation, and helper functions.
 */

#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include "../config/user_config.h"

// String manipulation
void trimTrailingSpaces(char* str);
void convertCaretToSpaces(char* str);

// Validation helpers
bool validateIP(const char* ip);
bool safeCopyString(char* dest, const char* src, size_t maxLen);
void assertBounds(int value, int minVal, int maxVal, const char* name);

#if TOUCH_BUTTON_ENABLED
// ========== Touch Button Functions ==========
// Highest GPIO number on the chip.
#if CONFIG_IDF_TARGET_ESP32S3
#define TOUCH_PIN_MAX 48
#else
#define TOUCH_PIN_MAX 21
#endif

void initTouchButton();
bool checkTouchButtonPressed();
void resetTouchButtonState();

// True when `pin` can physically host the touch input on this build: rejects
// the SPI flash pins, the display bus and any GPIO another peripheral already
// owns. Everything that can reach settings.touchButtonPin - the web form, an
// imported config, a stale NVS value - goes through this first.
bool isValidTouchPin(int pin);

// Short note for a pin that works but costs something (strapping, UART0,
// USB). Empty string for an unencumbered pin.
const char* touchPinNote(int pin);

// Move the touch input to `pin`, releasing the previous one. Ignores an
// invalid pin. Takes effect immediately, no restart needed.
void applyTouchButtonPin(uint8_t pin);
#endif

#if LED_PWM_ENABLED
// ========== LED PWM Night Light Functions ==========
void initLEDPWM();
void setLEDBrightness(uint8_t brightness);
void enableLED(bool enable);
void handleTouchLED();
#endif

#endif // UTILS_H
