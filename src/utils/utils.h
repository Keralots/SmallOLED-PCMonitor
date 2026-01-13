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
void initTouchButton();
bool checkTouchButtonPressed();
void resetTouchButtonState();
#endif

#endif // UTILS_H
