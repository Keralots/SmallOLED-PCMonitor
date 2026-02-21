/*
 * SmallOLED-PCMonitor - Utility Functions
 */

#include "utils.h"
#include "../config/config.h"
#include "../config/settings.h"
#include <string.h>
#include <math.h>

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
      if (!buttonHandled) {
        unsigned long pressDuration = millis() - buttonPressStartTime;
#if LED_PWM_ENABLED
        // Only fire short press for quick taps (< 500ms)
        // Medium press (500-1000ms) is handled by handleTouchLED()
        if (pressDuration < 500) {
          pressed = true;
          Serial.println("Touch button PRESSED (short press)");
        }
#else
        pressed = true;
        Serial.println("Touch button PRESSED (short press)");
#endif
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
    if (settings.ledBrightness == 0) {
      settings.ledBrightness = 128;  // Restore to 50% if dimmed to zero
    }
    setLEDBrightness(settings.ledBrightness);
    Serial.print("LED enabled, brightness: ");
    Serial.println(settings.ledBrightness);
  } else {
    ledcWrite(LED_PWM_CHANNEL, 0);
    Serial.println("LED disabled");
  }
}

// ========== LED Gesture Control ==========
// Quick tap (< 500ms): clock action (handled by checkTouchButtonPressed)
// Medium press (500ms-1s, release): toggle LED on/off
// Long hold (> 1s, keep holding): ramp brightness up/down
// Gamma-corrected ramp for natural feel.

extern bool buttonIsPressed;  // Referenced from touch button code above
extern unsigned long buttonPressStartTime;
extern bool buttonHandled;

// Gamma correction: maps linear position (0-255) to perceived brightness
// Quadratic approximation of gamma ~2.0 — no floats in hot path
static uint8_t gammaCorrect(uint8_t pos) {
  return (uint16_t(pos) * pos + pos) >> 8;
}

static const unsigned long MEDIUM_PRESS_THRESHOLD = 500;
static const unsigned long LONG_PRESS_THRESHOLD = 1000;
static const unsigned long LED_RAMP_INTERVAL_MS = 10; // 10ms per step → ~2.5s full range

void handleTouchLED() {
  static bool rampActive = false;
  static bool rampUp = true;
  static uint8_t rampPosition = 0;
  static unsigned long lastRampUpdate = 0;
  static bool prevPressed = false;

  bool held = buttonIsPressed && !buttonHandled;
  unsigned long pressDuration = millis() - buttonPressStartTime;

  // Detect long press threshold crossing → start ramp
  if (held && pressDuration >= LONG_PRESS_THRESHOLD) {
    buttonHandled = true; // Block short press

    if (!rampActive) {
      // First frame: determine direction
      if (!settings.ledEnabled || settings.ledBrightness == 0) {
        rampUp = true;
        rampPosition = 0;
        settings.ledEnabled = true;
      } else {
        rampUp = false;
        // Inverse gamma to find ramp position from current brightness
        rampPosition = (uint8_t)sqrtf(float(settings.ledBrightness) * 255.0f);
        if (rampPosition > 255) rampPosition = 255;
      }
      rampActive = true;
      lastRampUpdate = millis();
    }
  }

  // Continuous ramp while held
  if (rampActive && buttonIsPressed) {
    if (millis() - lastRampUpdate >= LED_RAMP_INTERVAL_MS) {
      lastRampUpdate = millis();
      if (rampUp) {
        if (rampPosition < 255) rampPosition++;
      } else {
        if (rampPosition > 0) rampPosition--;
      }

      settings.ledBrightness = gammaCorrect(rampPosition);
      if (settings.ledBrightness == 0 && !rampUp) {
        settings.ledEnabled = false;
        ledcWrite(LED_PWM_CHANNEL, 0);
      } else {
        ledcWrite(LED_PWM_CHANNEL, settings.ledBrightness);
      }
    }
    prevPressed = buttonIsPressed;
    return;
  }

  // Detect release moment
  if (prevPressed && !buttonIsPressed) {
    if (rampActive) {
      // Was ramping → save brightness
      rampActive = false;
      saveSettings();
    } else {
      // Check for medium press (500ms-1000ms) → toggle LED
      if (pressDuration >= MEDIUM_PRESS_THRESHOLD && pressDuration < LONG_PRESS_THRESHOLD) {
        enableLED(!settings.ledEnabled);
        saveSettings();
        Serial.println(settings.ledEnabled ? "Medium press: LED ON" : "Medium press: LED OFF");
      }
    }
  }

  prevPressed = buttonIsPressed;
}

#endif // TOUCH_BUTTON_ENABLED
