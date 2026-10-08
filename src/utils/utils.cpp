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
// Sampled once at release so every consumer classifies the same number. Both
// checkTouchButtonPressed() and handleTouchLED() act on one press, but they run
// in different loop iterations, and the work the short press kicks off
// (saveSettings() is ~200 ms of NVS) used to inflate a re-read of the elapsed
// time past the medium-press threshold - one tap switched the clock AND
// toggled the LED.
static unsigned long lastPressDuration = 0;

static const unsigned long MEDIUM_PRESS_THRESHOLD = 500;
static const unsigned long LONG_PRESS_THRESHOLD = 1000;

// The touch pin is configurable at runtime because the GPIO the TTP223 lands
// on is a board decision, not a firmware one - the reference wiring uses GPIO 7
// but the sponsored carrier PCB routes it to GPIO 21. TOUCH_BUTTON_PIN stays
// the default for a device that has never been configured.
bool isValidTouchPin(int pin) {
  if (pin < 0 || pin > TOUCH_PIN_MAX) return false;
#if CONFIG_IDF_TARGET_ESP32S3
  if (pin >= 22 && pin <= 25) return false; // not bonded out
  if (pin >= 26 && pin <= 37) return false; // SPI flash/PSRAM (33-37 on octal modules)
#if ARDUINO_USB_CDC_ON_BOOT
  if (pin == 19 || pin == 20) return false; // USB D-/D+ carries Serial and OTA-less reflashing
#endif
#else
  if (pin >= 11 && pin <= 17) return false; // SPI flash bus + VDD_SPI
#if ARDUINO_USB_CDC_ON_BOOT
  if (pin == 18 || pin == 19) return false; // USB D-/D+ carries Serial and OTA-less reflashing
#endif
#endif
#if DISPLAY_INTERFACE == 1
  if (pin == SPI_MOSI_PIN || pin == SPI_SCK_PIN || pin == SPI_CS_PIN ||
      pin == SPI_DC_PIN) return false;
  if (SPI_RST_PIN >= 0 && pin == SPI_RST_PIN) return false;
#else
  if (pin == I2C_SDA_PIN || pin == I2C_SCL_PIN) return false;
#endif
#if LED_PWM_ENABLED
  if (pin == LED_PWM_PIN) return false;
#endif
  return true;
}

const char* touchPinNote(int pin) {
  switch (pin) {
#if CONFIG_IDF_TARGET_ESP32S3
    case 0: case 3: case 45: case 46: return "strapping";
    case 19: case 20: return "USB";
    case 43: return "UART0 TX";
    case 44: return "UART0 RX";
#else
    case 2: case 8: case 9: return "strapping";
    case 18: case 19: return "USB";
    case 20: return "UART0 RX";
    case 21: return "UART0 TX";
#endif
    default: return "";
  }
}

void applyTouchButtonPin(uint8_t pin) {
  if (!isValidTouchPin(pin)) return;
  if (pin != settings.touchButtonPin) {
    pinMode(settings.touchButtonPin, INPUT); // drop the pulldown on the old pin
    settings.touchButtonPin = pin;
  }
  initTouchButton();
}

void initTouchButton() {
  if (!isValidTouchPin(settings.touchButtonPin)) {
    settings.touchButtonPin = TOUCH_BUTTON_PIN;
  }
  pinMode(settings.touchButtonPin, INPUT_PULLDOWN);
  lastButtonState = digitalRead(settings.touchButtonPin);
  lastDebounceTime = millis();
  buttonIsPressed = false;
  buttonHandled = false;

  Serial.print("Touch button initialized on GPIO ");
  Serial.print(settings.touchButtonPin);
  Serial.print(" (active ");
  Serial.print(TOUCH_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.println(")");
}

bool checkTouchButtonPressed() {
  int reading = digitalRead(settings.touchButtonPin);
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
      lastPressDuration = millis() - buttonPressStartTime;
      if (!buttonHandled) {
#if LED_PWM_ENABLED
        // Only fire short press for quick taps (< 500ms)
        // Medium press (500-1000ms) is handled by handleTouchLED()
        if (lastPressDuration < MEDIUM_PRESS_THRESHOLD) {
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
  lastPressDuration = 0;
  lastButtonState = digitalRead(settings.touchButtonPin);
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
extern unsigned long lastPressDuration;

// Gamma correction: maps linear position (0-255) to perceived brightness
// Quadratic approximation of gamma ~2.0 — no floats in hot path
// Capped at 254 to avoid PWM→DC transition jump at 100% duty cycle
static uint8_t gammaCorrect(uint8_t pos) {
  uint8_t val = (uint16_t(pos) * pos + pos) >> 8;
  return val >= 255 ? 254 : val;
}

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
      if (lastPressDuration >= MEDIUM_PRESS_THRESHOLD && lastPressDuration < LONG_PRESS_THRESHOLD) {
        enableLED(!settings.ledEnabled);
        saveSettings();
        Serial.println(settings.ledEnabled ? "Medium press: LED ON" : "Medium press: LED OFF");
      }
    }
  }

  prevPressed = buttonIsPressed;
}

#endif // TOUCH_BUTTON_ENABLED
