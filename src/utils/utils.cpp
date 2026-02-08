/*
 * SmallOLED-PCMonitor - Utility Functions
 */

#include "utils.h"
#include "../config/config.h"
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

  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
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
static unsigned long buttonPressStartTime = 0;  // Track when button was pressed
static bool buttonIsPressed = false;  // Button is currently pressed (after debounce)
static bool buttonHandled = false;  // Button action already handled

void initTouchButton() {
  pinMode(TOUCH_BUTTON_PIN, INPUT_PULLDOWN);
  lastButtonState = digitalRead(TOUCH_BUTTON_PIN);
  lastDebounceTime = millis();
  buttonIsPressed = false;
  buttonHandled = false;

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
    // Button just pressed (after debounce)
    if (reading == TOUCH_ACTIVE_LEVEL && !buttonIsPressed) {
      buttonIsPressed = true;
      buttonPressStartTime = millis();
      buttonHandled = false;
      // Don't return true yet - wait to see if it's a short or long press
    }
    // Button just released (after debounce)
    else if (reading != TOUCH_ACTIVE_LEVEL && buttonIsPressed) {
      buttonIsPressed = false;
      // Only trigger if not already handled by long press
      if (!buttonHandled) {
        pressed = true;
        Serial.println("Touch button PRESSED (short press)");
      }
      buttonHandled = false;  // Reset for next press
    }
  }

  lastButtonState = reading;
  return pressed;
}

void resetTouchButtonState() {
  buttonIsPressed = false;
  buttonHandled = false;
  lastButtonState = digitalRead(TOUCH_BUTTON_PIN);
}

#endif

#if LED_PWM_ENABLED
// ========== LED PWM Night Light Control ==========

void initLEDPWM() {
  ledcSetup(LED_PWM_CHANNEL, LED_PWM_FREQ, LED_PWM_RESOLUTION);
  ledcAttachPin(LED_PWM_PIN, LED_PWM_CHANNEL);
  Serial.print("LED PWM initialized on GPIO ");
  Serial.println(LED_PWM_PIN);
}

void setLEDBrightness(uint8_t brightness) {
  if (settings.ledEnabled) {
    ledcWrite(LED_PWM_CHANNEL, brightness);
  } else {
    ledcWrite(LED_PWM_CHANNEL, 0);  // Off if disabled
  }
}

void enableLED(bool enable) {
  settings.ledEnabled = enable;
  if (enable) {
    setLEDBrightness(settings.ledBrightness);
    Serial.print("LED enabled, brightness: ");
    Serial.println(settings.ledBrightness);
  } else {
    ledcWrite(LED_PWM_CHANNEL, 0);
    Serial.println("LED disabled");
  }
}

// ========== Long Press Gesture Detection ==========
// Shares state with checkTouchButtonPressed() to avoid conflicts

static const unsigned long LONG_PRESS_THRESHOLD = 1000; // 1000ms = 1 second

extern bool buttonIsPressed;  // Referenced from touch button code above
extern unsigned long buttonPressStartTime;
extern bool buttonHandled;

bool checkTouchButtonLongPress() {
  int reading = digitalRead(TOUCH_BUTTON_PIN);

  // Only check if button is currently pressed and not yet handled
  if (buttonIsPressed && !buttonHandled) {
    unsigned long pressDuration = millis() - buttonPressStartTime;

    // Check if long press threshold reached
    if (pressDuration >= LONG_PRESS_THRESHOLD) {
      buttonHandled = true;  // Mark as handled so short press won't fire
      Serial.println("Long press detected - toggling LED!");
      return true;  // Long press detected!
    }
  }

  return false;
}

#endif // TOUCH_BUTTON_ENABLED
