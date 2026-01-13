/*
 * SmallOLED-PCMonitor - Utility Functions
 */

#include "utils.h"
#include <string.h>

// Helper function to trim trailing whitespace (only from Python names, not custom labels)
void trimTrailingSpaces(char* str) {
  int len = strlen(str);
  while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t')) {
    str[len - 1] = '\0';
    len--;
  }
}

// Helper function to convert '^' to spaces in labels for custom spacing
// Example: "CPU^^" becomes "CPU  " for display alignment
void convertCaretToSpaces(char* str) {
  int len = strlen(str);
  for (int i = 0; i < len; i++) {
    if (str[i] == '^') {
      str[i] = ' ';
    }
  }
}

// ========== Security & Validation Helpers ==========

// Validate IP address format (prevents invalid IPs from crashing the device)
bool validateIP(const char* ip) {
  if (!ip || strlen(ip) == 0 || strlen(ip) > 15) {
    return false;
  }

  int octets[4];
  int result = sscanf(ip, "%d.%d.%d.%d", &octets[0], &octets[1], &octets[2], &octets[3]);

  if (result != 4) {
    return false;
  }

  // Validate each octet is in range 0-255
  for (int i = 0; i < 4; i++) {
    if (octets[i] < 0 || octets[i] > 255) {
      return false;
    }
  }

  return true;
}

// Safe string copy with null terminator (prevents buffer overflows)
bool safeCopyString(char* dest, const char* src, size_t maxLen) {
  if (!dest || !src || maxLen == 0) {
    return false;
  }

  size_t srcLen = strlen(src);
  if (srcLen >= maxLen) {
    // Source is too long, truncate
    strncpy(dest, src, maxLen - 1);
    dest[maxLen - 1] = '\0';
    return false;  // Indicate truncation occurred
  }

  strcpy(dest, src);
  return true;  // Success, no truncation
}

// Bounds checking for settings (logs errors if out of range)
void assertBounds(int value, int minVal, int maxVal, const char* name) {
  if (value < minVal || value > maxVal) {
    Serial.printf("ERROR: %s out of bounds: %d not in [%d,%d]\n",
                  name ? name : "value", value, minVal, maxVal);
  }
}

#if TOUCH_BUTTON_ENABLED
// ========== Touch Button Implementation ==========

static int lastButtonState = !TOUCH_ACTIVE_LEVEL;  // Initial state (opposite of active)
static unsigned long lastDebounceTime = 0;
static bool buttonTriggered = false;  // One-shot flag

void initTouchButton() {
  pinMode(TOUCH_BUTTON_PIN, INPUT_PULLDOWN);
  lastButtonState = digitalRead(TOUCH_BUTTON_PIN);
  lastDebounceTime = millis();
  buttonTriggered = false;

  Serial.print("Touch button initialized on GPIO ");
  Serial.print(TOUCH_BUTTON_PIN);
  Serial.print(" (active ");
  Serial.print(TOUCH_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.println(")");
}

bool checkTouchButtonPressed() {
  int reading = digitalRead(TOUCH_BUTTON_PIN);
  bool pressed = false;

  // Check if button state changed (noise or actual press)
  if (reading != lastButtonState) {
    lastDebounceTime = millis();  // Reset debounce timer
  }

  // Check if button is stable for debounce period
  if ((millis() - lastDebounceTime) > TOUCH_DEBOUNCE_MS) {
    // Check if button is in active state and not yet triggered
    if (reading == TOUCH_ACTIVE_LEVEL && !buttonTriggered) {
      buttonTriggered = true;
      pressed = true;
      Serial.println("Touch button PRESSED");
    }
    // Check if button released - reset trigger
    else if (reading != TOUCH_ACTIVE_LEVEL) {
      buttonTriggered = false;
    }
  }

  lastButtonState = reading;
  return pressed;
}

void resetTouchButtonState() {
  buttonTriggered = false;
  lastButtonState = digitalRead(TOUCH_BUTTON_PIN);
}

#endif // TOUCH_BUTTON_ENABLED
